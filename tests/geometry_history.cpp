#include "geometry_history.h"
#include <cassert>
#include <iostream>
using namespace gh2::temporal;
int main(){
    GeometryHistory history;GeometryKey key{123,1,4096,30,{1,2,3}};GeometryState old,current;
    old.constants[96]=7;old.geometry_revision=9;old.previous_vertices_preserved=true;
    current=old;current.constants[96]=11;
    history.Record(key,old);assert(!history.Previous(key));history.EndFrame();
    history.Record(key,current);assert(history.Previous(key)&&history.Previous(key)->constants[96]==7);
    history.Record(key,current);assert(!history.Previous(key));assert(history.Stats().ambiguous==1);history.EndFrame();
    history.Record(key,current);assert(!history.Previous(key));history.EndFrame();
    current.previous_vertices_preserved=false;history.Record(key,current);assert(!history.Previous(key));history.EndFrame();
    current.previous_vertices_preserved=true;current.geometry_revision=10;history.Record(key,current);assert(!history.Previous(key));history.EndFrame();
    auto other=key;other.layer=2;history.Record(other,current);assert(!history.Previous(other));history.EndFrame(true);
    history.Record(other,current);assert(!history.Previous(other));history.EndFrame();
    for(size_t i=0;i<GeometryHistory::kMaximumDraws+1;++i){auto entry=key;entry.index_base=uint32_t(i);history.Record(entry,current);}
    assert(history.Stats().overflow);assert(!history.Previous(other));history.EndFrame();
    history.Record(other,current);assert(!history.Previous(other));
    history.Reset();current.tile_identity=0;history.Record(key,current);
    current.tile_identity=uint64_t(uint32_t(-640))<<32;history.Record(key,current);
    assert(history.Stats().unique==1&&history.Stats().ambiguous==0&&history.Stats().tile_replays==1);
    current.constants[96]++;current.tile_identity=uint64_t(uint32_t(-1280))<<32;history.Record(key,current);
    assert(history.Stats().ambiguous==1);
    history.Reset();auto lighting=key;lighting.pixel_shader=99;
    history.Record(key,current);history.Record(lighting,current);assert(history.Stats().unique==2);
    history.Reset();current.linear.available=true;history.Record(key,current);history.EndFrame();
    current.previous_vertices_preserved=false;history.Record(key,current);unsigned candidates=0;
    history.ForEachPositionCandidate([&](const auto&,const auto&,const auto&){++candidates;});
    assert(candidates==1&&!history.Previous(key));
    history.Record(key,current);candidates=0;history.ForEachPositionCandidate([&](const auto&,const auto&,const auto&){++candidates;});assert(candidates==0);
    std::cout<<"PASS: previous constants, conflicting duplicate rejection, identical cross-tile replay association, pass/layer isolation, missing previous vertices, content revision, cuts and bounded overflow.\n";
}
