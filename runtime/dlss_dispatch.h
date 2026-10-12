#pragma once
// Optional official-SDK bridge. The game must not call this until its temporal
// resources have been generated and verified. Initialization/device ownership
// belongs to the host, before D3D12 initialization and after GPU drain at exit.
#include <windows.h>
#include <d3d12.h>
#include <sl.h>
#include <sl_dlss.h>
#include <array>
#include <stdexcept>

namespace gh2::dlss {
struct Inputs {
    ID3D12Resource *color=nullptr,*depth=nullptr,*motion=nullptr,*output=nullptr;
    uint32_t width=0,height=0,output_width=0,output_height=0;
    uint32_t color_state=UINT_MAX,depth_state=UINT_MAX,motion_state=UINT_MAX,output_state=UINT_MAX;
    // Explicit producer contract, not inferred from resource existence.
    bool matching_frame=false,object_motion_valid=false,depth_valid=false;
};
inline bool Validate(const Inputs& in) {
    if(!in.matching_frame||!in.object_motion_valid||!in.depth_valid ||
       !in.color||!in.depth||!in.motion||!in.output||!in.width||!in.height||
       !in.output_width||!in.output_height)return false;
    if(in.color==in.output||in.depth==in.output||in.motion==in.output)return false;
    if(in.color_state==UINT_MAX||in.depth_state==UINT_MAX||in.motion_state==UINT_MAX||in.output_state==UINT_MAX)return false;
    const auto c=in.color->GetDesc(),d=in.depth->GetDesc(),m=in.motion->GetDesc(),o=in.output->GetDesc();
    auto fits=[](const D3D12_RESOURCE_DESC& r,uint32_t w,uint32_t h){return r.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && r.Width>=w&&r.Height>=h&&r.SampleDesc.Count==1&&r.DepthOrArraySize==1;};
    return fits(c,in.width,in.height)&&fits(d,in.width,in.height)&&fits(m,in.width,in.height)&&fits(o,in.output_width,in.output_height)&&
        d.Format==DXGI_FORMAT_R32_FLOAT && (m.Format==DXGI_FORMAT_R16G16_FLOAT||m.Format==DXGI_FORMAT_R32G32_FLOAT)&&
        (o.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
}
class Dispatch {
    PFun_slGetNewFrameToken* frame_;
    PFun_slSetConstants* constants_;
    PFun_slSetTagForFrame* tag_;
    PFun_slEvaluateFeature* evaluate_;
    PFun_slFreeResources* free_;
    PFun_slDLSSSetOptions* options_;
    PFun_slDLSSGetOptimalSettings* optimal_;
    sl::ViewportHandle viewport_;
    uint32_t previous_width_=0,previous_height_=0,previous_output_width_=0,previous_output_height_=0;
    sl::DLSSMode previous_mode_=sl::DLSSMode::eOff;
    template<class T>static T* Load(HMODULE dll,const char* name){auto p=reinterpret_cast<T*>(GetProcAddress(dll,name));if(!p)throw std::runtime_error(name);return p;}
public:
    Dispatch(HMODULE dll,uint32_t viewport):viewport_(viewport) {
        frame_=Load<PFun_slGetNewFrameToken>(dll,"slGetNewFrameToken");constants_=Load<PFun_slSetConstants>(dll,"slSetConstants");
        tag_=Load<PFun_slSetTagForFrame>(dll,"slSetTagForFrame");evaluate_=Load<PFun_slEvaluateFeature>(dll,"slEvaluateFeature");
        free_=Load<PFun_slFreeResources>(dll,"slFreeResources");
        auto get=Load<PFun_slGetFeatureFunction>(dll,"slGetFeatureFunction");void* function=nullptr;
        if(get(sl::kFeatureDLSS,"slDLSSSetOptions",function)!=sl::Result::eOk)throw std::runtime_error("DLSS options unavailable");
        options_=reinterpret_cast<PFun_slDLSSSetOptions*>(function);
        if(get(sl::kFeatureDLSS,"slDLSSGetOptimalSettings",function)!=sl::Result::eOk)throw std::runtime_error("DLSS dimensions unavailable");
        optimal_=reinterpret_cast<PFun_slDLSSGetOptimalSettings*>(function);
    }
    static sl::DLSSOptions Options(sl::DLSSMode mode,uint32_t width,uint32_t height) {
        sl::DLSSOptions options;options.mode=mode;options.outputWidth=width;options.outputHeight=height;
        options.colorBuffersHDR=sl::Boolean::eFalse;options.useAutoExposure=sl::Boolean::eTrue;
        options.dlaaPreset=options.qualityPreset=options.balancedPreset=sl::DLSSPreset::ePresetK;
        options.performancePreset=sl::DLSSPreset::ePresetM;return options;
    }
    sl::Result Optimal(sl::DLSSMode mode,uint32_t width,uint32_t height,sl::DLSSOptimalSettings& result) {
        return optimal_(Options(mode,width,height),result);
    }
    sl::Result Evaluate(const Inputs& in,sl::Constants constants,sl::DLSSMode mode,uint32_t frame_index,ID3D12GraphicsCommandList* commands) {
        if(!commands||!Validate(in))return sl::Result::eErrorInvalidIntegration;
        if(mode==sl::DLSSMode::eOff||mode>=sl::DLSSMode::eCount)return sl::Result::eErrorInvalidParameter;
        if(mode==sl::DLSSMode::eDLAA&&(in.width!=in.output_width||in.height!=in.output_height))return sl::Result::eErrorInvalidParameter;
        if(in.width!=previous_width_||in.height!=previous_height_||in.output_width!=previous_output_width_||in.output_height!=previous_output_height_||mode!=previous_mode_)constants.reset=sl::Boolean::eTrue;
        sl::FrameToken* token=nullptr;
        auto result=frame_(token,&frame_index);if(result!=sl::Result::eOk)return result;
        result=options_(viewport_,Options(mode,in.output_width,in.output_height));if(result!=sl::Result::eOk)return result;
        result=constants_(constants,*token,viewport_);if(result!=sl::Result::eOk)return result;
        sl::Resource color(sl::ResourceType::eTex2d,in.color,in.color_state),depth(sl::ResourceType::eTex2d,in.depth,in.depth_state),
                     motion(sl::ResourceType::eTex2d,in.motion,in.motion_state),output(sl::ResourceType::eTex2d,in.output,in.output_state);
        sl::Extent input_extent{0,0,in.width,in.height},output_extent{0,0,in.output_width,in.output_height};
        std::array<sl::ResourceTag,4> tags={{{&color,sl::kBufferTypeScalingInputColor,sl::eValidUntilEvaluate,&input_extent},
            {&depth,sl::kBufferTypeDepth,sl::eValidUntilEvaluate,&input_extent},
            {&motion,sl::kBufferTypeMotionVectors,sl::eValidUntilEvaluate,&input_extent},
            {&output,sl::kBufferTypeScalingOutputColor,sl::eValidUntilEvaluate,&output_extent}}};
        result=tag_(*token,viewport_,tags.data(),uint32_t(tags.size()),commands);if(result!=sl::Result::eOk)return result;
        const sl::BaseStructure* inputs[]={&viewport_};
        result=evaluate_(sl::kFeatureDLSS,*token,inputs,1,commands);
        if(result==sl::Result::eOk){previous_width_=in.width;previous_height_=in.height;previous_output_width_=in.output_width;previous_output_height_=in.output_height;previous_mode_=mode;}
        // The host must restore command-list state and keep resources alive
        // through GPU completion; no wait or CPU readback occurs here.
        return result;
    }
    sl::Result ReleaseAfterGpuDrain(){previous_mode_=sl::DLSSMode::eOff;return free_(sl::kFeatureDLSS,viewport_);}
};
}
