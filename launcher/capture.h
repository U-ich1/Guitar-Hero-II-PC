#pragma once
#include "../runtime/controller_mapping.h"
#include <dinput.h>
#include <map>
#include <string>

namespace gh2::launcher {
inline gh2::controls::State capture_state(gh2::controls::State state,bool guitar){
    // Native guitars use these analog fields for non-button signals. In
    // particular an idle LT of 229 must never become a captured binding.
    if(guitar){state.lt=0;state.rt=0;}return state;
}
struct EdgeCapture {
    std::array<bool,gh2::controls::SourceCount> previous{};
    void baseline(const gh2::controls::State& state){for(int i=1;i<gh2::controls::SourceCount;++i)previous[i]=gh2::controls::pressed(state,i);}
    int poll(const gh2::controls::State& state){
        int digital=0,analog=0;
        for(int i=1;i<gh2::controls::SourceCount;++i){const bool down=gh2::controls::pressed(state,i);
            if(down&&!previous[i]){if(i==gh2::controls::LT||i==gh2::controls::RT||i>=gh2::controls::LSUp){if(!analog)analog=i;}else if(!digital)digital=i;}
            previous[i]=down;
        }
        return digital?digital:analog;
    }
};
struct RawEdgeCapture {
    std::array<bool,128> previous{};
    void baseline(const DIJOYSTATE2& state){for(unsigned i=0;i<128;++i)previous[i]=(state.rgbButtons[i]&0x80)!=0;}
    int poll(const DIJOYSTATE2& state){int result=0;for(unsigned i=0;i<128;++i){bool down=(state.rgbButtons[i]&0x80)!=0;if(down&&!previous[i]&&!result)result=int(i)+1;previous[i]=down;}return result;}
};
inline gh2::controls::State convert_directinput(const DIJOYSTATE2& raw,const std::map<std::string,int>& settings){
    auto get=[&](const std::string& key,int fallback){auto found=settings.find(key);return found==settings.end()?fallback:found->second;};
    gh2::controls::State state{};const uint16_t targets[]={0x1000,0x2000,0x4000,0x8000,0x100,0x200,0x20,0x10,0x40,0x80,0,0,1,2,4,8};
    const int defaults[]={1,2,3,4,5,6,9,10,11,12,7,8,0,0,0,0};
    for(int i=0;i<16;++i){int source=get("dinput_button_"+std::to_string(i),defaults[i]);if(source<1||source>128||!(raw.rgbButtons[source-1]&0x80))continue;
        if(i==10)state.lt=255;else if(i==11)state.rt=255;else state.buttons|=targets[i];}
    if((raw.rgdwPOV[0]&0xFFFF)!=0xFFFF){constexpr uint16_t directions[]={1,1|8,8,2|8,2,2|4,4,1|4};state.buttons|=directions[((raw.rgdwPOV[0]+2250)%36000)/4500];}
    const LONG axes[]={raw.lX,raw.lY,raw.lZ,raw.lRx,raw.lRy,raw.lRz,raw.rglSlider[0],raw.rglSlider[1]};const int defaults_axes[]={0,1,3,4};
    for(int i=0;i<4;++i){int axis=get("dinput_axis_"+std::to_string(i),defaults_axes[i]);if(axis<0||axis>=8)continue;int value=int(axes[axis]);if(i==1||i==3)value=-value;state.axes[i]=int16_t(std::clamp(value,-32768,32767));}
    for(int i=0;i<2;++i){int axis=get("dinput_trigger_axis_"+std::to_string(i),0);if(axis<1||axis>8)continue;int value=std::clamp(int(axes[axis-1]),-32768,32767),mode=get("dinput_trigger_mode_"+std::to_string(i),0);
        if(mode==1)value=-value-1;int amount=mode<2?int((int64_t(value)+32768)*255/65535):int(std::clamp(int64_t(mode==2?value:-value)*255/32767,int64_t(0),int64_t(255)));
        if(i==0)state.lt=std::max(state.lt,uint8_t(amount));else state.rt=std::max(state.rt,uint8_t(amount));}
    return state;
}
}
