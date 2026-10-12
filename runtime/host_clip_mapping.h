#pragma once
#include "temporal_geometry.h"
namespace gh2::temporal {
struct HostClipMapping {
    std::array<double,3> ndc_scale{1,1,1},ndc_offset{};
    Viewport viewport{};
    double tile_x=0,tile_y=0;
    uint32_t image_width=0,image_height=0;
    bool w_not_reciprocal=true,xy_divided=false,z_divided=false;
    // The current depth converter reverses a D3D [0,1] guest depth range.
    // Other depth conventions need their own conversion before acceptance.
    bool canonical_depth=false;
};
inline std::optional<Matrix4> BakeLinearHostMapping(const Matrix4& projection,const HostClipMapping& mapping) {
    if(!mapping.w_not_reciprocal||mapping.xy_divided||mapping.z_divided||!mapping.canonical_depth||!mapping.image_width||!mapping.image_height)return {};
    const double ax=2*mapping.ndc_scale[0]*mapping.viewport.x_scale/mapping.image_width;
    const double bx=2*(mapping.ndc_offset[0]*mapping.viewport.x_scale+mapping.viewport.x_offset+mapping.tile_x)/mapping.image_width-1;
    const double ay=-2*mapping.ndc_scale[1]*mapping.viewport.y_scale/mapping.image_height;
    const double by=1-2*(mapping.ndc_offset[1]*mapping.viewport.y_scale+mapping.viewport.y_offset+mapping.tile_y)/mapping.image_height;
    if(!std::isfinite(ax)||!std::isfinite(bx)||!std::isfinite(ay)||!std::isfinite(by))return {};
    auto result=projection;
    for(unsigned i=0;i<4;++i){result[0][i]=ax*projection[0][i]+bx*projection[3][i];result[1][i]=ay*projection[1][i]+by*projection[3][i];}
    return result;
}
inline std::optional<Vector4> MapGuestClipToImage(Vector4 clip,const HostClipMapping& mapping) {
    if(!mapping.image_width||!mapping.image_height||!mapping.canonical_depth)return {};
    for(auto value:clip)if(!std::isfinite(value))return {};
    if(!mapping.w_not_reciprocal){if(std::abs(clip[3])<1e-12)return {};clip[3]=1/clip[3];}
    if(clip[3]<=1e-9)return {};
    if(mapping.xy_divided){clip[0]*=clip[3];clip[1]*=clip[3];}
    if(mapping.z_divided)clip[2]*=clip[3];
    auto host=clip;
    for(unsigned i=0;i<3;++i){if(!std::isfinite(mapping.ndc_scale[i])||!std::isfinite(mapping.ndc_offset[i]))return {};host[i]=clip[i]*mapping.ndc_scale[i]+clip[3]*mapping.ndc_offset[i];}
    auto pixel=ProjectPixel(host,mapping.viewport);if(!pixel)return {};
    const double x=(*pixel)[0]+mapping.tile_x,y=(*pixel)[1]+mapping.tile_y;
    if(!std::isfinite(x)||!std::isfinite(y))return {};
    // Depth is restored guest canonical projection depth, matching the atlas
    // converter. XY removes native tile cropping and returns full-image clip.
    return Vector4{(2*x/mapping.image_width-1)*clip[3],(1-2*y/mapping.image_height)*clip[3],clip[2],clip[3]};
}
}
