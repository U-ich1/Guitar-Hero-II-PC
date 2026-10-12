#include "../runtime/controls_dialog.h"
#include <iostream>
#include <stdexcept>
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv) {
    try {
        if(argc>1&&std::string(argv[1])=="--probe") {
            CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
            {
                gh2::dinput::Driver direct;require(direct.Setup()==0,"DirectInput setup failed");
                gh2::dinput::NamedXInput native;require(native.Setup()==0,"XInput setup failed");
                std::vector<rex::input::DeviceInfo> devices;native.EnumerateDevices(devices);direct.EnumerateDevices(devices);
                for(const auto& device:devices)std::cout<<device.name<<" | "<<device.guid<<'\n';
                std::cout<<"Connected physical entries: "<<devices.size()<<"\nPASS: native driver enumeration; no game launched.\n";
                const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(2400);
                double worst_ms=0;unsigned polls=0;
                while(std::chrono::steady_clock::now()<until){
                    devices.clear();const auto start=std::chrono::steady_clock::now();
                    native.EnumerateDevices(devices);direct.EnumerateDevices(devices);
                    worst_ms=std::max(worst_ms,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
                    ++polls;std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                require(worst_ms<20,"Cached discovery blocked polling for over 20 ms");
                std::cout<<"PASS: cached discovery during background rescans: "<<polls<<" polls; worst "<<worst_ms<<" ms.\n";
            }
            CoUninitialize();return 0;
        }
        if(argc>1) {
            rex::cvar::LoadConfig(argv[1]);
            require(gh2::controls::selected_key()=="dinput:test-guid"&&gh2::dinput::button_sources[0].load()==128,"DirectInput profile did not reload");
            std::cout<<"PASS: persistent DirectInput identity and raw button assignment reload.\n";return 0;
        }
        DIJOYSTATE2 raw{};for(auto& pov:raw.rgdwPOV)pov=0xFFFFFFFF;
        for(unsigned i=0;i<128;++i)require(gh2::dinput::convert(raw).gamepad.buttons==0,"Neutral DirectInput state stuck a button");
        raw.rgbButtons[0]=0x80;raw.rgbButtons[2]=0x80;raw.rgbButtons[6]=0x80;
        auto output=gh2::dinput::convert(raw);
        require((uint16_t(output.gamepad.buttons)&0x5000)==0x5000&&output.gamepad.left_trigger==255,"DirectInput face/trigger conversion failed");
        require(gh2::dinput::pov_buttons(4500)==9&&gh2::dinput::pov_buttons(22500)==6&&gh2::dinput::pov_buttons(0xFFFFFFFF)==0,"POV diagonal or neutral failed");
        require(gh2::dinput::trigger_value(-32768,0)==0&&gh2::dinput::trigger_value(32767,0)==255&&gh2::dinput::trigger_value(32767,1)==0&&gh2::dinput::trigger_value(-32768,1)==255,"Full-range trigger normalization failed");
        require(gh2::dinput::trigger_value(0,2)==0&&gh2::dinput::trigger_value(32767,2)==255&&gh2::dinput::trigger_value(-32768,3)==255&&gh2::dinput::trigger_value(32767,3)==0,"Shared centered trigger axis normalization failed");
        raw.lX=-32768;raw.lY=-32768;raw.lRx=32767;raw.lRy=12345;
        output=gh2::dinput::convert(raw);
        require(output.gamepad.thumb_lx==-32768&&output.gamepad.thumb_ly==32767&&output.gamepad.thumb_rx==32767&&output.gamepad.thumb_ry==-12345,"Axis range/Y inversion failed");
        require(gh2::dinput::xinput_path(L"hid\\vid_045e&pid_028e&ig_00")&&!gh2::dinput::xinput_path(L"hid\\vid_1234&pid_5678"),"XInput duplicate filter failed");
        GUID first{1,2,3,{4,5,6,7,8,9,10,11}},second=first;second.Data1=2;
        require(gh2::dinput::device_id(first)==gh2::dinput::device_id(first)&&gh2::dinput::device_id(first)!=gh2::dinput::device_id(second),"Persistent device IDs collided");
        gh2::controls::ControllerAssignment assignment;
        rex::input::DeviceInfo device;device.id=gh2::dinput::device_id(first);device.guid="dinput:test-guid";device.name="Test USB controller";
        assignment.OnDevicesChanged({device});
        rex::cvar::SetFlagByName("player_one_device_key",device.guid);
        std::vector<rex::input::DeviceId> selected;assignment.DevicesForUser(0,selected);
        require(selected==std::vector<rex::input::DeviceId>{device.id},"Named DirectInput selection failed");
        assignment.OnDevicesChanged({});assignment.DevicesForUser(0,selected);require(selected.empty(),"Disconnected selected device remained active");
        assignment.OnDevicesChanged({device});assignment.DevicesForUser(0,selected);require(selected.size()==1,"Stable GUID reconnect failed");
        require(gh2::controls::catalog().size()==1&&gh2::controls::catalog()[0].name==device.name,"Device names not published");
        rex::input::DeviceInfo xbox=device;xbox.id=rex::input::DeviceId(0x58490000ULL);xbox.guid="xinput:0";
        rex::input::DeviceInfo keyboard=device;keyboard.id=rex::input::DeviceId(123);keyboard.guid="keyboard";keyboard.synthetic=true;
        assignment.OnDevicesChanged({xbox,device,keyboard});rex::cvar::SetFlagByName("player_one_device_key","");
        for(int mode:{1,2}){require(rex::cvar::SetFlagByName("gh2_controller_api",std::to_string(mode)),"Controller API setting rejected");assignment.DevicesForUser(0,selected);
            const auto wanted=mode==1?xbox.id:device.id,excluded=mode==1?device.id:xbox.id;
            require(std::find(selected.begin(),selected.end(),wanted)!=selected.end()&&std::find(selected.begin(),selected.end(),excluded)==selected.end(),"API mode mixed device types");
            require(std::find(selected.begin(),selected.end(),keyboard.id)!=selected.end(),"API selection removed keyboard");
            std::vector<rex::input::DeviceId> player2;assignment.DevicesForUser(1,player2);require(std::find(player2.begin(),player2.end(),wanted)==player2.end(),"API mode assigned one controller twice");}
        rex::cvar::SetFlagByName("gh2_controller_api","0");rex::cvar::SetFlagByName("player_one_device_key",device.guid);
        require(!rex::cvar::SetFlagByName("dinput_button_0","129"),"Invalid raw button accepted");
        rex::cvar::SetFlagByName("dinput_button_0","128");raw={};for(auto& pov:raw.rgdwPOV)pov=0xFFFFFFFF;raw.rgbButtons[127]=0x80;
        require(uint16_t(gh2::dinput::convert(raw).gamepad.buttons)&0x1000,"Button 128 mapping failed");
        rex::cvar::SaveConfig("build/directinput_profile_test.toml");
        std::cout<<"PASS: DirectInput buttons, triggers, POV, axes, duplicate filtering, named selection, stable reconnect and validation.\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
