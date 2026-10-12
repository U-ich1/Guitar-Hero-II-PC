#include "../runtime/controls_dialog.h"
#include <iostream>
#include <stdexcept>
#include <random>
using namespace gh2::controls;
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv) {
    try {
        if(argc>1) {
            rex::cvar::LoadConfig(argv[1]);
            require(gamepad_settings.mode.load()==2&&gamepad_settings.sources[Green].load()==A&&player_one_device.load()==2,"Profile did not reload");
            std::cout<<"PASS: controller profile and device selection reload in a fresh process.\n";return 0;
        }
        std::mt19937 random(0x415607E7);
        for(bool guitar:{false,true})for(unsigned i=0;i<1000;++i) {
            State state{uint16_t(random()),uint8_t(random()),uint8_t(random()),{int16_t(random()),int16_t(random()),int16_t(random()),int16_t(random())}};
            auto output=apply(state,defaults(guitar),guitar);
            require(output.buttons==state.buttons&&output.lt==state.lt&&output.rt==state.rt&&output.axes==state.axes,"Native profile changed working controls");
        }
        auto profile=defaults(false);profile.mode=2;profile.sources={A,B,Y,X,LB,Up,Down,Back,Start};
        State chord{uint16_t(bits[A]|bits[Y]|bits[Up])};
        auto output=apply(chord,profile,false);
        require(output.buttons==(bits[A]|bits[Y]|bits[Up])&&output.lt==0&&output.rt==0,"Strummed guitar chord mapping failed");
        profile.sources[Green]=None;output=apply(chord,profile,false);
        require(!(output.buttons&bits[A]),"Unbound fret leaked through");
        profile=defaults(false);profile.mode=1;
        for(int source=1;source<SourceCount;++source) {
            profile.sources.fill(None);profile.sources[Green]=source;
            State input{}; input.buttons=bits[source];
            if(source==LT)input.lt=128;if(source==RT)input.rt=128;
            if(source==LSUp)input.axes[1]=16000;if(source==LSDown)input.axes[1]=-16000;
            if(source==RSUp)input.axes[3]=16000;if(source==RSDown)input.axes[3]=-16000;
            require(apply(input,profile,false).lt==255,"Source did not activate mapped gamepad fret");
        }
        State trigger{};trigger.lt=127;require(!pressed(trigger,LT),"Trigger threshold too low");trigger.lt=128;require(pressed(trigger,LT),"Trigger threshold too high");
        profile=defaults(false);profile.mode=2;profile.sources[StarPower]=RSUp;profile.whammy=3;
        State tilt{};tilt.axes={0,0,12000,17000};output=apply(tilt,profile,false);
        require((output.buttons&bits[Back])&&output.axes[0]==12000,"Tilt/whammy remap failed");
        std::array<uint8_t,16> wire{0x12,0x34,0x56,0x78};write16(wire.data()+4,bits[A]);
        gamepad_settings.mode.store(2);gamepad_settings.sources[Green].store(A);gamepad_settings.sources[Orange].store(None);
        detected_type[0].store(1);remap_guest_state(wire.data(),0,0);
        require(wire[0]==0x12&&wire[3]==0x78&&(read16(wire.data()+4)&bits[A]),"Guest endian state or packet corrupted");
        std::array<uint8_t,20> caps{1,1};adapt_capabilities(caps.data(),0,0);
        require(caps[1]==7&&detected_type[0].load()==1,"Virtual guitar overwrote physical type");
        auto previous=wire;remap_guest_state(wire.data(),0,1);require(wire==previous,"Failed input query was modified");
        ControllerAssignment assignment;
        rex::input::DeviceInfo guitar,gamepad,keyboard;
        guitar.id=static_cast<rex::input::DeviceId>(0x58490000);guitar.ordinal=0;
        gamepad.id=static_cast<rex::input::DeviceId>(0x58490001);gamepad.ordinal=1;
        keyboard.id=static_cast<rex::input::DeviceId>(123);keyboard.synthetic=true;
        assignment.OnDevicesChanged({guitar,gamepad,keyboard});player_one_device.store(2);
        std::vector<rex::input::DeviceId> ids;assignment.DevicesForUser(0,ids);
        require(ids==std::vector<rex::input::DeviceId>{gamepad.id,keyboard.id},"Selected gamepad not routed to player one");
        assignment.DevicesForUser(1,ids);require(ids.empty(),"Selected controller was duplicated on player two");
        assignment.OnDevicesChanged({keyboard});assignment.DevicesForUser(0,ids);require(ids==std::vector<rex::input::DeviceId>{keyboard.id},"Disconnected controller stayed active");
        assignment.OnDevicesChanged({guitar,gamepad,keyboard});assignment.DevicesForUser(0,ids);require(ids.size()==2,"Reconnect failed");
        require(!rex::cvar::SetFlagByName("gamepad_mode","9"),"Invalid profile mode accepted");
        rex::cvar::SetFlagByName("gamepad_mode","2");rex::cvar::SetFlagByName("gamepad_bind_0",std::to_string(A));rex::cvar::SetFlagByName("player_one_device","2");
        rex::cvar::SaveConfig("build/controller_profile_test.toml");
        std::cout<<"PASS: 2000 native states unchanged; sources, chords, triggers, tilt, whammy, guest endian state, failed queries, slot selection and reconnect.\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
