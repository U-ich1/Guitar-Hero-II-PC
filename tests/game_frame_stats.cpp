#include "../runtime/game_frame_stats.h"
#include <cfenv>
#include <iostream>
int main(){
    std::fesetround(FE_DOWNWARD);std::feraiseexcept(FE_INVALID);
    const int before=std::fetestexcept(FE_ALL_EXCEPT);
    for(int i=0;i<1000;++i)gh2::performance::record_output();
    gh2::performance::record_swap_duration(100);gh2::performance::record_swap_duration(1000);gh2::performance::record_swap_duration(250);
    if(gh2::performance::swap_calls.load()!=3||gh2::performance::swap_total_ns.load()!=1350||gh2::performance::swap_max_ns.load()!=1000)return 2;
    if(std::fegetround()!=FE_DOWNWARD||std::fetestexcept(FE_ALL_EXCEPT)!=before)return 1;
    std::cout<<"PASS: output sampling preserves floating-point rounding and exception flags.\n";
}
