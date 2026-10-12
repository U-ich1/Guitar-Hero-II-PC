#include "depth_tiles.h"
#include <cassert>
#include <iostream>
using namespace gh2::temporal;
int main(){
    gh2::temporal::Rectangle viewport{0,0,2560,1440},scissor{0,0,1280,1440};
    auto left=PlanDepthTile(viewport,scissor,1280,4096,0,0,2,2,2560,1440,-0.9f,0.9f);
    auto right=PlanDepthTile(viewport,scissor,1280,4096,-640,0,2,2,2560,1440,-0.9f,0.9f);
    assert(left&&right);assert(left->width==1280&&right->width==1280&&left->destination_left==0&&right->destination_left==1280);
    assert(!CompleteDepthTiles({*left},2560,1440));assert(CompleteDepthTiles({*left,*right},2560,1440));
    assert(!CompleteDepthTiles({*left,*left},2560,1440));
    assert(!PlanDepthTile(viewport,{0,0,0,1440},1280,4096,0,0,2,2,2560,1440,-.9f,.9f));
    assert(!PlanDepthTile(viewport,scissor,1280,4096,-1280,0,2,2,2560,1440,-.9f,.9f));
    assert(!PlanDepthTile(viewport,scissor,1280,4096,640,0,2,2,2560,1440,-.9f,.9f));
    std::cout<<"PASS: observed two-tile dimensions, signed window offsets, padded allocation exclusion, full coverage, missing/overlapping tiles and invalid placement.\n";
}
