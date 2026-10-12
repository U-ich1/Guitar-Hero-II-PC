#pragma once
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <bit>
#include <cstring>
#include <cmath>
#include <string>
#include <stdexcept>
#include "geometry_history.h"

namespace gh2::temporal {
// Exact title position families: rigid c24-26 transforms and four-bone
// c24-38 palettes, followed by c4-7 projection. Unknown programs are rejected.
inline constexpr uint64_t kLinearPositionShader=0x2eba09b0974055ebULL;
// Same position dataflow and stride; independently compared against each
// original title program, including their temporary-register permutations.
inline constexpr std::array<uint64_t,3> kLinearPositionShaders{
    kLinearPositionShader,0xcb9d92c934e26a03ULL,0x8f72b1ddc2edc8e1ULL};
inline bool SupportsLinearPositionShader(uint64_t hash) {
    for(auto supported:kLinearPositionShaders)if(hash==supported)return true;
    return false;
}
inline constexpr std::array<uint64_t,5> kSkinnedPositionShaders{
    0xb6d10de26caf4b00ULL,0x47b0406889d62288ULL,0x17c6d38225de0653ULL,
    0x72180349f1ac86eeULL,0x86495a03257c4f49ULL};
inline bool SupportsSkinnedPositionShader(uint64_t hash) {
    for(auto supported:kSkinnedPositionShaders)if(hash==supported)return true;
    return false;
}
inline bool SupportsPositionShader(uint64_t hash) {
    return SupportsLinearPositionShader(hash)||SupportsSkinnedPositionShader(hash);
}
inline uint32_t PositionVertexStride(uint64_t hash) {return SupportsSkinnedPositionShader(hash)?48:36;}
struct alignas(16) LinearClipParameters {
    std::array<std::array<float,4>,3> current_model{},previous_model{};
    std::array<std::array<float,4>,4> current_projection{},previous_projection{};
    // g0: current/previous vertex base, current/previous index base.
    // g1: stride bytes, count, index bytes (2/4), vertex endian (0-3).
    // g2: index endian, current/previous vertex bytes, indexed (0/1).
    // g3: current/previous index bytes, position offset, vertex index offset.
    // g4: current min/max, previous base, previous min; g5: previous max.
    std::array<std::array<uint32_t,4>,6> geometry{{{},{},{},{},{0,0x00ffffff,0,0},{0x00ffffff,0,0,0}}};
    std::array<std::array<float,4>,12> current_bones{},previous_bones{};
    std::array<float,4> current_weight_constant{},previous_weight_constant{};
};
static_assert(sizeof(LinearClipParameters)==736);
inline constexpr uint32_t kPositionConstantBytes=(sizeof(LinearClipParameters)+255)&~255u;
inline void SetLinearTransforms(LinearClipParameters& parameters,const GeometryState& current,const GeometryState& previous) {
    for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col){parameters.current_model[row][col]=std::bit_cast<float>(current.constants[(24+row)*4+col]);parameters.previous_model[row][col]=std::bit_cast<float>(previous.constants[(24+row)*4+col]);}
    for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col){parameters.current_projection[row][col]=std::bit_cast<float>(current.constants[(4+row)*4+col]);parameters.previous_projection[row][col]=std::bit_cast<float>(previous.constants[(4+row)*4+col]);}
    for(unsigned bone=0;bone<4;++bone)for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col) {
        parameters.current_bones[bone*3+row][col]=std::bit_cast<float>(current.constants[(24+bone*4+row)*4+col]);
        parameters.previous_bones[bone*3+row][col]=std::bit_cast<float>(previous.constants[(24+bone*4+row)*4+col]);
    }
    for(unsigned col=0;col<4;++col){parameters.current_weight_constant[col]=std::bit_cast<float>(current.constants[255*4+col]);parameters.previous_weight_constant[col]=std::bit_cast<float>(previous.constants[255*4+col]);}
}
inline bool SetMappedLinearTransforms(LinearClipParameters& parameters,const GeometryState& current,const GeometryState& previous) {
    if(current.clip_mapping.image_width!=previous.clip_mapping.image_width||current.clip_mapping.image_height!=previous.clip_mapping.image_height)return false;
    SetLinearTransforms(parameters,current,previous);
    Matrix4 c{},p{};
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j){c[i][j]=parameters.current_projection[i][j];p[i][j]=parameters.previous_projection[i][j];}
    auto mapped_c=BakeLinearHostMapping(c,current.clip_mapping),mapped_p=BakeLinearHostMapping(p,previous.clip_mapping);
    if(!mapped_c||!mapped_p)return false;
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j){parameters.current_projection[i][j]=float((*mapped_c)[i][j]);parameters.previous_projection[i][j]=float((*mapped_p)[i][j]);}
    return true;
}
class LinearMotionClips {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
    static void Require(HRESULT result){if(FAILED(result))throw std::runtime_error("Linear position program D3D12 failure");}
public:
    void Initialize(ID3D12Device* device) {
        D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,2,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,2}};
        D3D12_ROOT_PARAMETER parameters[2]{};parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[0].DescriptorTable={2,ranges};
        parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;parameters[1].Descriptor={0,0};
        D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=2;desc.pParameters=parameters;
        Microsoft::WRL::ComPtr<ID3DBlob> blob,error;
        Require(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));Require(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root_)));
        const char* source=R"(
ByteAddressBuffer currentData:register(t0);
ByteAddressBuffer previousData:register(t1);
RWByteAddressBuffer clipData:register(u0);
cbuffer Parameters:register(b0) {
 float4 currentModel[3];float4 previousModel[3];float4 currentProjection[4];float4 previousProjection[4];
 uint4 g0;uint4 g1;uint4 g2;uint4 g3;uint4 g4;uint4 g5;
 float4 currentBones[12];float4 previousBones[12];
 float4 currentWeightConstant;float4 previousWeightConstant;
};
uint endianWord(uint word,uint mode){
 if(mode==1)return ((word&0x00ff00ff)<<8)|((word&0xff00ff00)>>8);
 if(mode==2)return (word<<24)|((word&0x0000ff00)<<8)|((word&0x00ff0000)>>8)|(word>>24);
 if(mode==3)return (word<<16)|(word>>16);
 return word;
}
uint indexValue(ByteAddressBuffer data,uint base,uint index){
 uint address=base+index*g1.z;
 uint word=endianWord(data.Load(address&~3u),g2.x);
 return g1.z==2?(word>>((address&2u)*8))&65535:word;
}
float3 position(ByteAddressBuffer data,uint base,uint index){
 uint3 bits=data.Load3(base+index*g1.x+g3.z);
 return asfloat(uint3(endianWord(bits.x,g1.w),endianWord(bits.y,g1.w),endianWord(bits.z,g1.w)));
}
float4 project(float3 positionValue,float4 model[3],float4 projection[4]){
 float4 local=float4(positionValue,1);
 float4 world=float4(dot(model[0].zxyw,local.zxyw),dot(model[1].zxyw,local.zxyw),dot(model[2].zxyw,local.zxyw),1);
 return float4(dot(projection[0].zxyw,world.zxyw),dot(projection[1].zxyw,world.zxyw),dot(projection[2].zxyw,world.zxyw),dot(projection[3].zxyw,world.zxyw));
}
float4 projectSkinned(ByteAddressBuffer data,uint base,uint index,float4 bones[12],float weightConstant,float4 projection[4]) {
 uint3 raw=data.Load3(base+index*g1.x+12);
 float3 weights=asfloat(uint3(endianWord(raw.x,g1.w),endianWord(raw.y,g1.w),endianWord(raw.z,g1.w)));
 float fourth=weightConstant-dot(weights.zxy,weightConstant.xxx);
 float4 model[3];
 [unroll]for(uint row=0;row<3;++row) {
   // Match the title's accumulation order: bone 1, then 0, 2, 3.
   model[row]=bones[3+row]*weights.y;
   model[row]=mad(bones[row],weights.x,model[row]);
   model[row]=mad(bones[6+row],weights.z,model[row]);
   model[row]=mad(bones[9+row],fourth,model[row]);
 }
 return project(position(data,base,index),model,projection);
}
[numthreads(64,1,1)]void main(uint3 tid:SV_DispatchThreadID){
 uint i=tid.x;if(i>=g1.y)return;
 bool indexed=g2.w!=0;
 bool currentIndexValid=!indexed||i*g1.z+g1.z<=g3.x;
 bool previousIndexValid=!indexed||i*g1.z+g1.z<=g3.y;
 uint rawCurrent=currentIndexValid?(indexed?indexValue(currentData,g0.z,i):i):0;
 uint rawPrevious=previousIndexValid?(indexed?indexValue(previousData,g0.w,i):i):0;
 uint currentIndex=min(max((rawCurrent+g3.w)&0x00ffffffu,g4.x),g4.y);
 uint previousIndex=min(max((rawPrevious+g4.z)&0x00ffffffu,g4.w),g5.x);
 uint requiredBytes=g5.y!=0?24:12;
 bool cv=currentIndexValid&&g2.y>=g3.z+requiredBytes&&currentIndex<=(g2.y-g3.z-requiredBytes)/g1.x;
 bool pv=previousIndexValid&&g2.z>=g3.z+requiredBytes&&previousIndex<=(g2.z-g3.z-requiredBytes)/g1.x;
 float4 current=float4(0,0,0,1),previous=float4(0,0,0,1);
 if(cv)current=g5.y!=0?projectSkinned(currentData,g0.x,currentIndex,currentBones,currentWeightConstant.x,currentProjection):project(position(currentData,g0.x,currentIndex),currentModel,currentProjection);
 if(pv)previous=g5.y!=0?projectSkinned(previousData,g0.y,previousIndex,previousBones,previousWeightConstant.x,previousProjection):project(position(previousData,g0.y,previousIndex),previousModel,previousProjection);
 bool valid=cv&&pv&&currentIndex==previousIndex&&all(isfinite(current))&&all(isfinite(previous));
 uint address=i*36;clipData.Store4(address,asuint(current));clipData.Store4(address+16,asuint(previous));clipData.Store(address+32,asuint(valid?1.0:0.0));
})";
        blob.Reset();error.Reset();const auto compiled=D3DCompile(source,std::strlen(source),"gh2_linear_position",nullptr,nullptr,"main","cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&error);
        if(FAILED(compiled))throw std::runtime_error(error?std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()):"Position program compilation failed");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};pipeline.pRootSignature=root_.Get();pipeline.CS={blob->GetBufferPointer(),blob->GetBufferSize()};Require(device->CreateComputePipelineState(&pipeline,IID_PPV_ARGS(&pipeline_)));
    }
    template<class Commands>
    bool Record(Commands* commands,ID3D12Resource* current,ID3D12Resource* previous,ID3D12Resource* output,
                ID3D12DescriptorHeap* heap,D3D12_GPU_DESCRIPTOR_HANDLE table,D3D12_GPU_VIRTUAL_ADDRESS constants,
                const LinearClipParameters& parameters,uint64_t shader_hash,bool snapshots_coherent) {
        if(!commands||!current||!previous||!output||!heap||!pipeline_||!snapshots_coherent||!SupportsPositionShader(shader_hash)||!constants||constants%256)return false;
        const auto& g=parameters.geometry;const uint32_t count=g[1][1];
        if(!count||g[1][0]!=PositionVertexStride(shader_hash)||g[5][1]!=uint32_t(SupportsSkinnedPositionShader(shader_hash))||g[3][2]!=0||g[1][3]>3||g[2][0]>3||g[2][3]>1||
           (g[1][2]!=2&&g[1][2]!=4)||count>64*65535||count>UINT_MAX/36||
           g[4][0]>0x00ffffff||g[4][1]>0x00ffffff||g[4][3]>0x00ffffff||g[5][0]>0x00ffffff)return false;
        const auto c=current->GetDesc(),p=previous->GetDesc(),o=output->GetDesc();
        if(c.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||p.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||o.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||
           uint64_t(g[0][0])+g[2][1]>c.Width||uint64_t(g[0][1])+g[2][2]>p.Width||uint64_t(count)*36>o.Width||!(o.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)||
           (g[0][0]&3)||(g[0][1]&3)||g[2][1]<12||g[2][2]<12)return false;
        if(g[2][3]&&(uint64_t(g[0][2])+((uint64_t(g[3][0])+3)&~3ULL)>c.Width||uint64_t(g[0][3])+((uint64_t(g[3][1])+3)&~3ULL)>p.Width||
           g[0][2]%g[1][2]||g[0][3]%g[1][2]||uint64_t(count)*g[1][2]>g[3][0]||uint64_t(count)*g[1][2]>g[3][1]))return false;
        commands->SetDescriptorHeaps(1,&heap);commands->SetComputeRootSignature(root_.Get());commands->SetPipelineState(pipeline_.Get());
        commands->SetComputeRootDescriptorTable(0,table);commands->SetComputeRootConstantBufferView(1,constants);commands->Dispatch((count+63)/64,1,1);return true;
    }
};
}
