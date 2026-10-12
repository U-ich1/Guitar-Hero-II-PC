#include "host_clip_mapping.h"
#include <cassert>
#include <iostream>
using namespace gh2::temporal;
static void close(const Vector4& a,const Vector4& b){for(unsigned i=0;i<4;++i)assert(std::abs(a[i]-b[i])<1e-9);}
int main(){
    HostClipMapping left;
    left.viewport={1280,1280,-720,720,2560,1440};left.image_width=2560;left.image_height=1440;left.canonical_depth=true;
    HostClipMapping right=left;
    right.viewport={640,640,-720,720,1280,1440};right.ndc_scale[0]=2;right.ndc_offset[0]=-1;right.tile_x=1280;
    for(Vector4 raw:std::array<Vector4,3>{{{-.5,.25,.3,1},{.5,-.25,.7,1},{.3,.6,.8,2}}}){
        auto a=MapGuestClipToImage(raw,left),b=MapGuestClipToImage(raw,right);assert(a&&b);close(*a,raw);close(*b,raw);
    }
    auto reciprocal=left;reciprocal.w_not_reciprocal=false;reciprocal.xy_divided=true;reciprocal.z_divided=true;
    Matrix4 projection={{{2,0,0,.3},{0,3,0,-.4},{0,0,1,0},{0,0,0,1}}};
    auto baked=BakeLinearHostMapping(projection,right);assert(baked);
    const Vector4 local{.2,.3,.5,1};
    auto reference=MapGuestClipToImage(Transform(projection,local),right);assert(reference);
    close(Transform(*baked,local),*reference);
    assert(!BakeLinearHostMapping(projection,reciprocal));
    auto converted=MapGuestClipToImage({.25,-.125,.5,.5},reciprocal);assert(converted);close(*converted,{.5,-.25,1,2});
    assert(!MapGuestClipToImage({0,0,0,0},reciprocal));
    assert(!MapGuestClipToImage({0,0,0,-1},left));
    auto unknown=left;unknown.canonical_depth=false;assert(!MapGuestClipToImage({0,0,.5,1},unknown));
    auto changed=left;changed.image_width=0;assert(!MapGuestClipToImage({0,0,.5,1},changed));
    std::cout<<"PASS: full-image clip mapping across both observed tile layouts, Xbox reciprocal/divided coordinates, canonical depth and invalid convention rejection.\n";
}
