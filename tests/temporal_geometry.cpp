#include "temporal_geometry.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace gh2::temporal;
static void close(double a,double b){assert(std::abs(a-b)<1e-9);}
int main(){
    Matrix4 projection={{{2,0,0,0},{0,3,0,0},{0,0,1,0},{0,0,1,0}}};
    Matrix4 model={{{1,0,0,1},{0,1,0,2},{0,0,1,3},{0,0,0,1}}};
    Vector4 local={1,2,3,1};
    auto separate=Transform(projection,Transform(model,local));
    auto combined=Transform(Multiply(projection,model),local);
    for(unsigned i=0;i<4;++i)close(separate[i],combined[i]);
    Viewport viewport={640,640,-360,360,1280,720};
    auto pixel=ProjectPixel({0.25,0.5,0.1,1},viewport);assert(pixel);close((*pixel)[0],800);close((*pixel)[1],180);
    auto motion=BackwardMotion({0.25,0.5,0.1,1},{0,0,0.1,1},viewport,viewport);
    assert(motion);close((*motion)[0],-160);close((*motion)[1],180);
    auto jittered=JitterProjection(projection,viewport,0.25,-0.5);assert(jittered);
    auto unjittered_pixel=ProjectPixel(Transform(projection,local),viewport);
    auto jittered_pixel=ProjectPixel(Transform(*jittered,local),viewport);
    close((*jittered_pixel)[0]-(*unjittered_pixel)[0],0.25);
    close((*jittered_pixel)[1]-(*unjittered_pixel)[1],-0.5);
    close(*UndoDepthRange(0.45,-0.9,0.9),0.5);
    close(*UndoDepthRange(0.95,-0.1,1),0.5);
    close(*UndoDepthRange(0.99,-0.02,1),0.5);
    assert(!ProjectPixel({0,0,0,0},viewport));assert(!ProjectPixel({0,0,0,-1},viewport));
    assert(!ProjectPixel({std::numeric_limits<double>::infinity(),0,0,1},viewport));
    auto changed=viewport;changed.width=2560;assert(!BackwardMotion(local,local,viewport,changed));
    assert(!UndoDepthRange(0.5,0,1));
    std::cout<<"PASS: projection/model composition, negative-Y viewport, current-to-previous pixel motion, pixel jitter, stage/highway/HUD depth-range inversion and invalid history rejection.\n";
}
