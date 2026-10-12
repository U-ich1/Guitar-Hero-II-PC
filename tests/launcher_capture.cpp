#define DIRECTINPUT_VERSION 0x0800
#include "../launcher/capture.h"
#include <iostream>
#include <stdexcept>
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    using namespace gh2::controls;using namespace gh2::launcher;
    EdgeCapture capture;State idle{};idle.lt=229;capture.baseline(idle);
    require(capture.poll(idle)==0,"Idle guitar trigger was captured");
    EdgeCapture guitar_capture;State transient{};guitar_capture.baseline(capture_state(transient,true));transient.lt=229;require(guitar_capture.poll(capture_state(transient,true))==0,"Transient native guitar trigger captured");
    auto press=idle;press.buttons=bits[A];require(capture.poll(press)==A,"Green/A press missed");
    require(capture.poll(press)==0,"Held button recaptured");capture.poll(idle);require(capture.poll(press)==A,"Released button did not rearm");
    for(int source=1;source<SourceCount;++source){State neutral{};capture.baseline(neutral);State state{};
        if(source==LT)state.lt=255;else if(source==RT)state.rt=255;else if(source>=LSUp){state.axes[source>=RSUp?3:1]=(source==LSUp||source==RSUp)?20000:-20000;}else state.buttons=bits[source];
        require(capture.poll(state)==source,"Logical input capture mismatch");}
    DIJOYSTATE2 raw{};for(auto& pov:raw.rgdwPOV)pov=0xFFFFFFFF;RawEdgeCapture direct;raw.rgbButtons[3]=0x80;direct.baseline(raw);require(direct.poll(raw)==0,"Held raw button captured");raw.rgbButtons[3]=0;direct.poll(raw);raw.rgbButtons[127]=0x80;require(direct.poll(raw)==128,"Raw button 128 missed");
    auto converted=convert_directinput(raw,{{"dinput_button_0",128}});require(converted.buttons==bits[A],"Raw layout was not applied before logical capture");
    raw={};for(auto& pov:raw.rgdwPOV)pov=0xFFFFFFFF;raw.rgdwPOV[0]=18000;converted=convert_directinput(raw,{});require(converted.buttons==bits[Down],"DirectInput POV mapping mismatch");
    raw.rgdwPOV[0]=0xFFFFFFFF;raw.lZ=32767;converted=convert_directinput(raw,{{"dinput_trigger_axis_0",3}});require(converted.lt==255,"DirectInput trigger conversion failed");
    std::cout<<"PASS: all 20 logical sources, held-input suppression, release/repress, idle guitar trigger, DirectInput raw button 128, POV and trigger conversion.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
