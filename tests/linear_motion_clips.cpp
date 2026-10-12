#define XXH_INLINE_ALL
#include <xxhash.h>
#ifdef GH2_DEFERRED_LINEAR_TEST
#include <rex/ui/linear_motion_clips.h>
#include <rex/ui/clip_frame_pool.h>
#include <rex/ui/deferred_depth.h>
#include <rex/graphics/d3d12/command_processor.h>
#include <rex/graphics/d3d12/graphics_system.h>
#else
#include "../runtime/linear_motion_clips.h"
#include "../runtime/clip_frame_pool.h"
#endif
#ifdef GH2_DEFERRED_LINEAR_TEST
#include <rex/ui/motion_raster.h>
#else
#include "../runtime/motion_raster.h"
#endif
#include <rex/graphics/pipeline/shader/interpreter.h>
#include <rex/logging.h>
#include <dxgi1_4.h>
#include <fstream>
#include <iostream>
#include <vector>

using Microsoft::WRL::ComPtr;
static uint64_t oracle_shader=gh2::temporal::kLinearPositionShader;
static void Require(HRESULT hr){if(FAILED(hr))throw std::runtime_error("Linear fixture D3D12 operation failed");}
static std::vector<uint32_t> ReadProgram(const char* filename){
    std::ifstream input(filename,std::ios::binary);uint32_t magic=0,version=0;input.read(reinterpret_cast<char*>(&magic),4);input.read(reinterpret_cast<char*>(&version),4);
    if(!input||magic!=0x48534558||version!=0x19122020)throw std::runtime_error("Cannot read supported closed-game shader cache");
    for(;;){uint64_t hash;uint32_t packed;input.read(reinterpret_cast<char*>(&hash),8);if(!input)break;input.read(reinterpret_cast<char*>(&packed),4);const uint32_t count=packed&0x7fffffff;
        if(!input||!count||count>65535)throw std::runtime_error("Malformed cached shader");std::vector<uint32_t> code(count);input.read(reinterpret_cast<char*>(code.data()),count*4);
        if(!input||XXH3_64bits(code.data(),count*4)!=hash)throw std::runtime_error("Cached shader integrity failure");
        if(hash==oracle_shader&&!(packed>>31))return code;
    }
    throw std::runtime_error("Supported title position program is absent from cache");
}
struct PositionSink:rex::graphics::ShaderInterpreter::ExportSink{
    std::array<float,4> position{};
    void Export(rex::graphics::ucode::ExportRegister reg,const float* value,uint32_t mask)override {
        if(reg==rex::graphics::ucode::ExportRegister::kVSPosition)for(unsigned i=0;i<4;++i)if(mask&(1u<<i))position[i]=value[i];
    }
};
static std::array<float,4> Interpret(rex::graphics::Shader& shader,rex::memory::Memory& memory,uint32_t physical,
        const std::array<uint8_t,256>& bytes,const gh2::temporal::LinearClipParameters& parameters,bool previous,uint32_t index,unsigned endian) {
    rex::graphics::RegisterFile regs;std::memset(regs.values,0,sizeof(regs.values));
    regs[rex::graphics::XE_GPU_REG_SQ_VS_CONST]=0x000ff000;
    const auto& model=previous?parameters.previous_model:parameters.current_model;const auto& projection=previous?parameters.previous_projection:parameters.current_projection;
    for(unsigned i=0;i<3;++i)std::memcpy(&regs[rex::graphics::XE_GPU_REG_SHADER_CONSTANT_000_X+(24+i)*4],model[i].data(),16);
    for(unsigned i=0;i<4;++i)std::memcpy(&regs[rex::graphics::XE_GPU_REG_SHADER_CONSTANT_000_X+(4+i)*4],projection[i].data(),16);
    if(gh2::temporal::SupportsSkinnedPositionShader(oracle_shader)) {
        const auto& bones=previous?parameters.previous_bones:parameters.current_bones;
        for(unsigned bone=0;bone<4;++bone)for(unsigned row=0;row<3;++row)
            std::memcpy(&regs[rex::graphics::XE_GPU_REG_SHADER_CONSTANT_000_X+(24+bone*4+row)*4],bones[bone*3+row].data(),16);
        const auto& weight=previous?parameters.previous_weight_constant:parameters.current_weight_constant;
        std::memcpy(&regs[rex::graphics::XE_GPU_REG_SHADER_CONSTANT_000_X+255*4],weight.data(),16);
    }
    std::memcpy(memory.TranslatePhysical(physical),bytes.data(),bytes.size());
    rex::graphics::xenos::xe_gpu_vertex_fetch_t fetch{};fetch.type=rex::graphics::xenos::FetchConstantType::kVertex;fetch.address=physical>>2;fetch.size=64;fetch.endian=rex::graphics::xenos::Endian(endian);
    std::memcpy(&regs[rex::graphics::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0+95*2],&fetch,sizeof(fetch));
    rex::graphics::ShaderInterpreter interpreter(regs,memory);interpreter.SetShader(shader);
    std::memset(interpreter.temp_registers(),0,rex::graphics::xenos::kMaxShaderTempRegisters*4*sizeof(float));interpreter.temp_registers()[0]=float(index);
    PositionSink sink;interpreter.SetExportSink(&sink);interpreter.Execute();return sink.position;
}
int main(int argc,char** argv){try{
    if(argc!=3)throw std::runtime_error("Expected shader cache and oracle shader hash");
    oracle_shader=std::stoull(argv[2],nullptr,16);
    rex::InitLogging("build/linear_motion_clips_test.log",spdlog::level::info);struct LoggingGuard{~LoggingGuard(){rex::ShutdownLogging();}}logging_guard;
#ifdef GH2_DEFERRED_LINEAR_TEST
    rex::graphics::d3d12::D3D12GraphicsSystem graphics_system;
    rex::graphics::d3d12::D3D12CommandProcessor command_processor(&graphics_system,nullptr);
#endif
    const auto code=ReadProgram(argv[1]);rex::graphics::Shader shader(rex::graphics::xenos::ShaderType::kVertex,oracle_shader,code.data(),code.size());rex::string::StringBuffer disassembly;shader.AnalyzeUcode(disassembly);
    if(!rex::graphics::ShaderInterpreter::CanInterpretShader(shader))throw std::runtime_error("Cached shader cannot be interpreted");
    rex::memory::Memory memory;if(!memory.Initialize())throw std::runtime_error("Isolated guest memory initialization failed");
    const uint32_t allocation=memory.SystemHeapAlloc(4096,4096,rex::memory::kSystemHeapPhysical);if(!allocation)throw std::runtime_error("Physical fixture allocation failed");const uint32_t physical=memory.GetPhysicalAddress(allocation);
    ComPtr<IDXGIFactory1> factory;Require(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;ComPtr<ID3D12Device> device;
    for(UINT i=0;factory->EnumAdapters1(i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i){DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);if(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)&&SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device))))break;adapter.Reset();}
    if(!device)throw std::runtime_error("No hardware D3D12 device");gh2::temporal::LinearMotionClips producer;producer.Initialize(device.Get());
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};Require(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));ComPtr<ID3D12Fence> fence;Require(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("Fence event failed");struct Guard{HANDLE event;~Guard(){CloseHandle(event);}}guard{event};uint64_t submission=0;unsigned comparisons=0,cases=0;
    for(unsigned endian=0;endian<4;++endian)for(unsigned index_size:std::array<unsigned,2>{2,4})for(unsigned scenario=0;scenario<5;++scenario){
        gh2::temporal::LinearClipParameters parameters;
        parameters.current_model={{{1,.2f,0,.1f},{.1f,1,0,.05f},{0,0,1,.1f}}};parameters.previous_model=parameters.current_model;parameters.previous_model[0][3]=-.2f;
        parameters.current_projection={{{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,.2f,1}}};parameters.previous_projection=parameters.current_projection;parameters.previous_projection[1][1]=1.25f;
        parameters.geometry={{{0,0,128,128},{36,3,index_size,endian},{endian,108,108,scenario!=0},{3*index_size,3*index_size,0,0},{0,0x00ffffff,0,0},{0x00ffffff,0,0,0}}};
        if(scenario==3){parameters.geometry[3][3]=1;parameters.geometry[4]={0,2,1,0};parameters.geometry[5][0]=2;}
        const bool skinned=gh2::temporal::SupportsSkinnedPositionShader(oracle_shader);
        const uint32_t stride=gh2::temporal::PositionVertexStride(oracle_shader),index_base=192;
        parameters.geometry[0][2]=parameters.geometry[0][3]=index_base;
        parameters.geometry[1][0]=stride;parameters.geometry[2][1]=parameters.geometry[2][2]=3*stride;
        parameters.geometry[5][1]=skinned;
        for(unsigned bone=0;bone<4;++bone)for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col) {
            parameters.current_bones[bone*3+row][col]=parameters.current_model[row][col]+float(bone)*.07f*(col==row?1.f:-1.f);
            parameters.previous_bones[bone*3+row][col]=parameters.previous_model[row][col]-float(bone)*.03f;
        }
        parameters.current_weight_constant[0]=1;parameters.previous_weight_constant[0]=.9f;
        const auto oracle_parameters=parameters;
#ifdef GH2_MAPPED_LINEAR_TEST
        gh2::temporal::GeometryState mapped_current,mapped_previous;
        for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col){mapped_current.constants[(24+row)*4+col]=std::bit_cast<uint32_t>(parameters.current_model[row][col]);mapped_previous.constants[(24+row)*4+col]=std::bit_cast<uint32_t>(parameters.previous_model[row][col]);}
        for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col){mapped_current.constants[(4+row)*4+col]=std::bit_cast<uint32_t>(parameters.current_projection[row][col]);mapped_previous.constants[(4+row)*4+col]=std::bit_cast<uint32_t>(parameters.previous_projection[row][col]);}
        for(unsigned bone=0;bone<4;++bone)for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col) {
            mapped_current.constants[(24+bone*4+row)*4+col]=std::bit_cast<uint32_t>(parameters.current_bones[bone*3+row][col]);
            mapped_previous.constants[(24+bone*4+row)*4+col]=std::bit_cast<uint32_t>(parameters.previous_bones[bone*3+row][col]);
        }
        mapped_current.constants[255*4]=std::bit_cast<uint32_t>(parameters.current_weight_constant[0]);
        mapped_previous.constants[255*4]=std::bit_cast<uint32_t>(parameters.previous_weight_constant[0]);
        auto& cm=mapped_current.clip_mapping;cm.canonical_depth=true;cm.image_width=1280;cm.image_height=720;cm.viewport={320,320,-360,360,640,720};cm.ndc_scale[0]=1.8;cm.ndc_offset[0]=.2;
        auto& pm=mapped_previous.clip_mapping;pm=cm;pm.tile_x=640;pm.ndc_scale[0]=1.6;pm.ndc_offset[0]=-.4;
        if(!gh2::temporal::SetMappedLinearTransforms(parameters,mapped_current,mapped_previous))throw std::runtime_error("Mapped transforms rejected valid tiled fixture");
        auto incompatible=mapped_previous;incompatible.clip_mapping.image_width=2560;
        auto unused=parameters;if(gh2::temporal::SetMappedLinearTransforms(unused,mapped_current,incompatible))throw std::runtime_error("Resolution discontinuity accepted");
#endif
        std::array<uint8_t,256> current{},previous{};const std::array<std::array<float,3>,3> points={{{-.25f,-.2f,.3f},{.15f,.4f,.5f},{.35f,-.1f,.7f}}};
        for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j){const uint32_t a=rex::graphics::xenos::GpuSwap(std::bit_cast<uint32_t>(points[i][j]),rex::graphics::xenos::Endian(endian));const uint32_t b=rex::graphics::xenos::GpuSwap(std::bit_cast<uint32_t>(points[i][j]-(j==0?.05f:0)),rex::graphics::xenos::Endian(endian));std::memcpy(current.data()+i*stride+j*4,&a,4);std::memcpy(previous.data()+i*stride+j*4,&b,4);}
        if(skinned)for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j) {
            const float weight=scenario==0?(j==i?1.f:0.f):scenario==1?0.f:.1f*float(j+1)+.03f*float(i);
            const uint32_t a=rex::graphics::xenos::GpuSwap(std::bit_cast<uint32_t>(weight),rex::graphics::xenos::Endian(endian));
            const uint32_t b=rex::graphics::xenos::GpuSwap(std::bit_cast<uint32_t>(weight*.8f),rex::graphics::xenos::Endian(endian));
            std::memcpy(current.data()+i*stride+12+j*4,&a,4);std::memcpy(previous.data()+i*stride+12+j*4,&b,4);
        }
        std::array<uint32_t,3> current_indices{2,0,1},previous_indices{2,0,1};if(scenario==2)previous_indices={1,0,2};
        if(scenario==4&&index_size==4)current_indices={0xf0000002,0xe1000000,0xd2000001};
        auto encode=[&](auto& bytes,const auto& indices){for(unsigned i=0;i<3;++i)if(index_size==2){const uint16_t v=uint16_t(indices[i]);std::memcpy(bytes.data()+index_base+i*2,&v,2);}else std::memcpy(bytes.data()+index_base+i*4,&indices[i],4);
            for(unsigned i=0;i<(3*index_size+3)/4;++i){uint32_t value;std::memcpy(&value,bytes.data()+index_base+i*4,4);value=rex::graphics::xenos::GpuSwap(value,rex::graphics::xenos::Endian(endian));std::memcpy(bytes.data()+index_base+i*4,&value,4);}};
        encode(current,current_indices);encode(previous,previous_indices);
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=1024;buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;std::array<ComPtr<ID3D12Resource>,3> uploads;
        for(auto& upload:uploads)Require(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)));
        void* mapped=nullptr;D3D12_RANGE none{0,0};for(unsigned i=0;i<3;++i){Require(uploads[i]->Map(0,&none,&mapped));std::memset(mapped,0,1024);if(i==0)std::memcpy(mapped,current.data(),256);else if(i==1)std::memcpy(mapped,previous.data(),256);else std::memcpy(mapped,&parameters,sizeof(parameters));uploads[i]->Unmap(0,nullptr);}
        heap.Type=D3D12_HEAP_TYPE_DEFAULT;buffer.Width=sizeof(gh2::temporal::MotionVertex)*3;buffer.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;ComPtr<ID3D12Resource> output;Require(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&output)));
        heap.Type=D3D12_HEAP_TYPE_READBACK;buffer.Flags=D3D12_RESOURCE_FLAG_NONE;ComPtr<ID3D12Resource> readback;Require(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
        ComPtr<ID3D12DescriptorHeap> descriptors;D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=3;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;Require(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&descriptors)));
        auto cpu=descriptors->GetCPUDescriptorHandleForHeapStart();const auto increment=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;srv.Format=DXGI_FORMAT_R32_TYPELESS;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Buffer.NumElements=1024/4;srv.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;
        for(unsigned i=0;i<2;++i){device->CreateShaderResourceView(uploads[i].Get(),&srv,cpu);cpu.ptr+=increment;}D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uav.Format=DXGI_FORMAT_R32_TYPELESS;uav.Buffer.NumElements=108/4;uav.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;device->CreateUnorderedAccessView(output.Get(),nullptr,&uav,cpu);
        ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> commands;Require(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));Require(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&commands)));
        auto gpu=descriptors->GetGPUDescriptorHandleForHeapStart();const auto constants=uploads[2]->GetGPUVirtualAddress();
        if(producer.Record(commands.Get(),uploads[0].Get(),uploads[1].Get(),output.Get(),descriptors.Get(),gpu,constants,parameters,0,true))throw std::runtime_error("Unknown position shader accepted");
        if(producer.Record(commands.Get(),uploads[0].Get(),uploads[1].Get(),output.Get(),descriptors.Get(),gpu,constants,parameters,oracle_shader,false))throw std::runtime_error("Unverified snapshot pair accepted");
#ifdef GH2_DEFERRED_LINEAR_TEST
        rex::graphics::d3d12::DeferredCommandList deferred(command_processor);
        gh2::temporal::DeferredDepthCommands queued(deferred);
        if(!producer.Record(&queued,uploads[0].Get(),uploads[1].Get(),output.Get(),descriptors.Get(),gpu,constants,parameters,oracle_shader,true))throw std::runtime_error("Queued position program rejected");
        deferred.Execute(commands.Get(),nullptr);
#else
        if(!producer.Record(commands.Get(),uploads[0].Get(),uploads[1].Get(),output.Get(),descriptors.Get(),gpu,constants,parameters,oracle_shader,true))throw std::runtime_error("Supported position program rejected");
#endif
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={output.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};commands->ResourceBarrier(1,&barrier);commands->CopyBufferRegion(readback.Get(),0,output.Get(),0,108);Require(commands->Close());ID3D12CommandList* list=commands.Get();queue->ExecuteCommandLists(1,&list);Require(queue->Signal(fence.Get(),++submission));Require(fence->SetEventOnCompletion(submission,event));if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0)throw std::runtime_error("Position program GPU timeout");
        D3D12_RANGE range{0,108};Require(readback->Map(0,&range,&mapped));std::array<gh2::temporal::MotionVertex,3> result;std::memcpy(result.data(),mapped,108);readback->Unmap(0,&none);
        for(unsigned i=0;i<3;++i){const unsigned ci=std::min(std::max(((scenario?current_indices[i]:i)+parameters.geometry[3][3])&0x00ffffff,parameters.geometry[4][0]),parameters.geometry[4][1]);
            const unsigned pi=std::min(std::max(((scenario?previous_indices[i]:i)+parameters.geometry[4][2])&0x00ffffff,parameters.geometry[4][3]),parameters.geometry[5][0]);
            auto c=Interpret(shader,memory,physical,current,oracle_parameters,false,ci,endian),p=Interpret(shader,memory,physical,previous,oracle_parameters,true,pi,endian);
#ifdef GH2_MAPPED_LINEAR_TEST
            const gh2::temporal::Vector4 cv{c[0],c[1],c[2],c[3]},pv{p[0],p[1],p[2],p[3]};
            auto image_c=gh2::temporal::MapGuestClipToImage(cv,mapped_current.clip_mapping),image_p=gh2::temporal::MapGuestClipToImage(pv,mapped_previous.clip_mapping);
            if(!image_c||!image_p)throw std::runtime_error("Interpreter clip mapping invalid");
            for(unsigned j=0;j<4;++j){c[j]=float((*image_c)[j]);p[j]=float((*image_p)[j]);}
#endif
            for(unsigned j=0;j<4;++j){if(!std::isfinite(c[j])||!std::isfinite(p[j])||!std::isfinite(result[i].current[j])||!std::isfinite(result[i].previous[j])||std::abs(c[j]-result[i].current[j])>0.00002f||std::abs(p[j]-result[i].previous[j])>0.00002f){std::cerr<<"endian="<<endian<<" index_size="<<index_size<<" scenario="<<scenario<<" vertex="<<i<<" component="<<j<<" cpu_current="<<c[j]<<" gpu_current="<<result[i].current[j]<<" cpu_previous="<<p[j]<<" gpu_previous="<<result[i].previous[j]<<'\n';throw std::runtime_error("GPU clip output disagrees with actual title microcode interpreter");}comparisons+=2;}
            if(result[i].history_valid!=float(ci==pi))throw std::runtime_error("Changed index correspondence accepted");
        }
        ++cases;
        if(cases==1) {
            gh2::temporal::ClipFramePool pool;
            for(unsigned i=0;i<48;++i)if(!pool.Acquire(device.Get(),0,1,uploads[0].Get(),uploads[1].Get(),3))throw std::runtime_error("Clip pool allocation failed");
            if(pool.Acquire(device.Get(),0,2,uploads[0].Get(),uploads[1].Get(),3))throw std::runtime_error("In-flight clip slot overwritten");
            auto* reused=pool.Acquire(device.Get(),1,2,uploads[0].Get(),uploads[1].Get(),6);
            if(!reused||reused->capacity<6)throw std::runtime_error("Completed clip slot could not grow/reuse");
            if(pool.Acquire(device.Get(),2,3,uploads[0].Get(),uploads[1].Get(),gh2::temporal::ClipFramePool::kMaximumVertices+1))throw std::runtime_error("Clip allocation limit ignored");
            pool.ClearAfterGpuDrain();
        }
    }
    memory.SystemHeapFree(allocation);
    std::cout<<"PASS: "<<cases<<" GPU current/previous position cases and "<<comparisons<<" component comparisons against GH2's actual cached shader interpreter, all four byte orders, 16/32-bit and automatic indices, changed topology masking and provenance guards. No game launched.\n";
#ifdef GH2_DEFERRED_LINEAR_TEST
    std::cout<<"PASS: position generation executes through the SDK's actual deferred command stream.\n";
#endif
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
