#pragma once
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <rex/input/input_driver.h>
#include <rex/input/xinput/xinput_input_driver.h>
#include <windows.h>
#include <dinput.h>
#include <xinput.h>
#include <mutex>
#include <atomic>
#include <chrono>
#include <map>
#include <set>
#include <array>
#include <cwctype>
#include <algorithm>
#include <memory>
#include <thread>
#include <condition_variable>

namespace gh2::dinput {
using rex::X_STATUS;
using rex::X_RESULT;
inline std::mutex names_mutex;
inline std::map<uint32_t,std::set<std::string>> product_names;
inline std::map<std::string,std::string> display_names;
inline std::string device_label(const rex::input::DeviceInfo& device) {
    std::lock_guard lock(names_mutex);auto found=display_names.find(device.guid);
    return found==display_names.end()?device.name:found->second;
}
inline std::array<std::atomic<int>,16> button_sources{1,2,3,4,5,6,9,10,11,12,7,8,0,0,0,0};
inline std::array<std::atomic<int>,4> axis_sources{0,1,3,4};
inline std::array<std::atomic<int>,2> trigger_axes{0,0},trigger_modes{0,0};
inline std::array<std::atomic<bool>,128> live_buttons{};
inline std::array<std::atomic<int32_t>,8> live_axes{};
inline std::mutex live_mutex;
inline std::string live_name;
inline std::string utf8(const wchar_t* text) {
    const int length=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);
    if(length<=1)return {};
    std::string result(size_t(length),'\0');
    WideCharToMultiByte(CP_UTF8,0,text,-1,result.data(),length,nullptr,nullptr);
    result.pop_back();return result;
}
inline std::string guid_key(const GUID& guid) {
    wchar_t text[40]{};StringFromGUID2(guid,text,40);return "dinput:"+utf8(text);
}
inline rex::input::DeviceId device_id(const GUID& guid) {
    uint64_t hash=14695981039346656037ULL;
    const auto* bytes=reinterpret_cast<const uint8_t*>(&guid);
    for(unsigned i=0;i<sizeof(guid);++i){hash^=bytes[i];hash*=1099511628211ULL;}
    return static_cast<rex::input::DeviceId>(0x4449000000000000ULL|(hash&0x0000FFFFFFFFFFFFULL));
}
inline bool xinput_path(std::wstring path) {
    for(auto& value:path)value=wchar_t(std::towupper(value));
    return path.find(L"IG_")!=std::wstring::npos;
}
inline bool xinput_product(uint32_t product) {
    UINT count=0;
    if(GetRawInputDeviceList(nullptr,&count,sizeof(RAWINPUTDEVICELIST))!=0)return false;
    std::vector<RAWINPUTDEVICELIST> list(count);
    if(GetRawInputDeviceList(list.data(),&count,sizeof(RAWINPUTDEVICELIST))==UINT(-1))return false;
    for(const auto& item:list) {
        if(item.dwType!=RIM_TYPEHID)continue;
        RID_DEVICE_INFO info{};info.cbSize=sizeof(info);UINT size=sizeof(info);
        if(GetRawInputDeviceInfoW(item.hDevice,RIDI_DEVICEINFO,&info,&size)==UINT(-1))continue;
        if((info.hid.dwVendorId|(info.hid.dwProductId<<16))!=product)continue;
        UINT length=0;GetRawInputDeviceInfoW(item.hDevice,RIDI_DEVICENAME,nullptr,&length);
        if(!length)continue;
        std::wstring path(size_t(length)+1,L'\0');
        if(GetRawInputDeviceInfoW(item.hDevice,RIDI_DEVICENAME,path.data(),&length)!=UINT(-1)&&xinput_path(path))return true;
    }
    return false;
}
inline uint16_t pov_buttons(DWORD pov) {
    if((pov&0xFFFF)==0xFFFF)return 0;
    constexpr uint16_t directions[]={1,1|8,8,2|8,2,2|4,4,1|4};
    return directions[((pov+2250)%36000)/4500];
}
inline uint8_t trigger_value(int value,int mode) {
    value=std::clamp(value,-32768,32767);
    if(mode==1)value=-value-1;
    if(mode<2)return uint8_t((int64_t(value)+32768)*255/65535);
    const int magnitude=mode==2?value:-value;
    return uint8_t(std::clamp(int64_t(magnitude)*255/32767,int64_t(0),int64_t(255)));
}
inline rex::input::X_INPUT_STATE convert(const DIJOYSTATE2& raw) {
    rex::input::X_INPUT_STATE output{};
    constexpr uint16_t targets[]={0x1000,0x2000,0x4000,0x8000,0x100,0x200,0x20,0x10,0x40,0x80,0,0,1,2,4,8};
    uint16_t buttons=pov_buttons(raw.rgdwPOV[0]);
    for(unsigned i=0;i<16;++i) {
        const int source=button_sources[i].load();
        if(source<=0||source>128||!(raw.rgbButtons[source-1]&0x80))continue;
        if(i==10)output.gamepad.left_trigger=255;else if(i==11)output.gamepad.right_trigger=255;else buttons|=targets[i];
    }
    output.gamepad.buttons=buttons;
    const LONG axes[]={raw.lX,raw.lY,raw.lZ,raw.lRx,raw.lRy,raw.lRz,raw.rglSlider[0],raw.rglSlider[1]};
    for(unsigned i=0;i<2;++i){const int source=trigger_axes[i].load();
        if(source>0&&source<=8){const auto value=trigger_value(int(axes[source-1]),trigger_modes[i].load());
            if(i==0)output.gamepad.left_trigger=std::max(output.gamepad.left_trigger,value);else output.gamepad.right_trigger=std::max(output.gamepad.right_trigger,value);
        }
    }
    auto axis=[&](unsigned target) {
        int source=axis_sources[target].load();
        if(source<0||source>=8)return int16_t(0);
        int value=int(axes[source]);if(target==1||target==3)value=-value;
        return int16_t(std::clamp(value,-32768,32767));
    };
    output.gamepad.thumb_lx=axis(0);output.gamepad.thumb_ly=axis(1);
    output.gamepad.thumb_rx=axis(2);output.gamepad.thumb_ry=axis(3);
    return output;
}
class Driver final : public rex::input::InputDriver {
    struct Device {
        IDirectInputDevice8W* handle=nullptr;
        rex::input::DeviceInfo info;
        uint32_t packet=0;
        ~Device(){if(handle){handle->Unacquire();handle->Release();}}
    };
    IDirectInput8W* input_=nullptr;
    HWND window_=nullptr;
    std::mutex mutex_;
    std::map<std::string,std::shared_ptr<Device>> devices_;
    // Discovery owns working_ and never holds the polling mutex while Windows
    // enumerates HID devices or opens a newly connected controller.
    std::map<std::string,std::shared_ptr<Device>> working_;
    std::set<std::string> ignored_;
    std::set<std::string> seen_;
    std::map<uint32_t,std::set<std::string>> working_names_;
    std::jthread discovery_;
    std::condition_variable_any wake_;
    std::mutex wait_mutex_;
    static BOOL CALLBACK range_axis(const DIDEVICEOBJECTINSTANCEW* object,void* context) {
        DIPROPRANGE range{};range.diph.dwSize=sizeof(range);range.diph.dwHeaderSize=sizeof(range.diph);
        range.diph.dwHow=DIPH_BYID;range.diph.dwObj=object->dwType;range.lMin=-32768;range.lMax=32767;
        static_cast<IDirectInputDevice8W*>(context)->SetProperty(DIPROP_RANGE,&range.diph);return DIENUM_CONTINUE;
    }
    static BOOL CALLBACK found(const DIDEVICEINSTANCEW* instance,void* context) {
        auto* self=static_cast<Driver*>(context);
        const auto name=utf8(instance->tszProductName);
        self->working_names_[instance->guidProduct.Data1].insert(name);
        const auto key=guid_key(instance->guidInstance);self->seen_.insert(key);
        if(self->working_.contains(key)||self->ignored_.contains(key))return DIENUM_CONTINUE;
        IDirectInputDevice8W* handle=nullptr;
        if(FAILED(self->input_->CreateDevice(instance->guidInstance,&handle,nullptr)))return DIENUM_CONTINUE;
        DIPROPGUIDANDPATH path{};path.diph.dwSize=sizeof(path);path.diph.dwHeaderSize=sizeof(path.diph);path.diph.dwHow=DIPH_DEVICE;
        const bool duplicate=(SUCCEEDED(handle->GetProperty(DIPROP_GUIDANDPATH,&path.diph))&&xinput_path(path.wszPath))||xinput_product(instance->guidProduct.Data1);
        if(duplicate){self->ignored_.insert(key);handle->Release();return DIENUM_CONTINUE;}
        if(FAILED(handle->SetDataFormat(&c_dfDIJoystick2))||FAILED(handle->SetCooperativeLevel(self->window_,DISCL_NONEXCLUSIVE|DISCL_BACKGROUND))) {
            handle->Release();return DIENUM_CONTINUE;
        }
        handle->EnumObjects(range_axis,handle,DIDFT_AXIS);handle->Acquire();
        auto device=std::make_shared<Device>();device->handle=handle;
        device->info.id=device_id(instance->guidInstance);device->info.name=(name.empty()?utf8(instance->tszInstanceName):name)+" (DirectInput)";
        device->info.guid=key;device->info.synthetic=false;
        self->working_.emplace(key,std::move(device));return DIENUM_CONTINUE;
    }
    void refresh() {
        seen_.clear();
        working_names_.clear();
        input_->EnumDevices(DI8DEVCLASS_GAMECTRL,found,this,DIEDFL_ATTACHEDONLY);
        {std::lock_guard lock(names_mutex);product_names=working_names_;}
        std::erase_if(working_,[&](const auto& item){return !seen_.contains(item.first);});
        std::erase_if(ignored_,[&](const auto& key){return !seen_.contains(key);});
        // Keep removed handles alive until after the short publication lock.
        auto published=working_;
        {std::lock_guard lock(mutex_);devices_.swap(published);}
    }
    Device* find(rex::input::DeviceId id) {
        for(auto& item:devices_)if(item.second->info.id==id)return item.second.get();return nullptr;
    }
public:
    Driver():InputDriver(nullptr,0){}
    ~Driver() override {
        discovery_.request_stop();wake_.notify_all();if(discovery_.joinable())discovery_.join();
        devices_.clear();working_.clear();if(input_)input_->Release();if(window_)DestroyWindow(window_);
    }
    rex::X_STATUS Setup() override {
        if(FAILED(DirectInput8Create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,IID_IDirectInput8W,reinterpret_cast<void**>(&input_),nullptr)))return X_STATUS_UNSUCCESSFUL;
        window_=CreateWindowExW(0,L"STATIC",L"GH2PC DirectInput",WS_OVERLAPPED,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!window_)return X_STATUS_UNSUCCESSFUL;
        refresh();
        discovery_=std::jthread([this](std::stop_token stop){
            const HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
            while(!stop.stop_requested()) {
                std::unique_lock wait_lock(wait_mutex_);
                wake_.wait_for(wait_lock,stop,std::chrono::seconds(1),[]{return false;});
                wait_lock.unlock();if(!stop.stop_requested())refresh();
            }
            if(SUCCEEDED(initialized))CoUninitialize();
        });
        return X_STATUS_SUCCESS;
    }
    void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override {
        std::lock_guard lock(mutex_);if(!input_)return;
        for(auto& item:devices_)out.push_back(item.second->info);
    }
    rex::X_RESULT GetDeviceCapabilities(rex::input::DeviceId id,uint32_t,rex::input::X_INPUT_CAPABILITIES* caps) override {
        std::lock_guard lock(mutex_);if(!find(id))return X_ERROR_DEVICE_NOT_CONNECTED;
        *caps={};caps->type=1;caps->sub_type=1;caps->gamepad.buttons=0xF3FF;
        caps->gamepad.left_trigger=caps->gamepad.right_trigger=255;
        caps->gamepad.thumb_lx=caps->gamepad.thumb_ly=caps->gamepad.thumb_rx=caps->gamepad.thumb_ry=32767;
        return X_ERROR_SUCCESS;
    }
    rex::X_RESULT GetDeviceState(rex::input::DeviceId id,rex::input::X_INPUT_STATE* state) override {
        std::lock_guard lock(mutex_);auto* device=find(id);if(!device)return X_ERROR_DEVICE_NOT_CONNECTED;
        device->handle->Poll();DIJOYSTATE2 raw{};for(auto& pov:raw.rgdwPOV)pov=0xFFFFFFFF;
        HRESULT result=device->handle->GetDeviceState(sizeof(raw),&raw);
        if(result==DIERR_INPUTLOST||result==DIERR_NOTACQUIRED){device->handle->Acquire();device->handle->Poll();result=device->handle->GetDeviceState(sizeof(raw),&raw);}
        if(FAILED(result))return X_ERROR_DEVICE_NOT_CONNECTED;
        {std::lock_guard live_lock(live_mutex);live_name=device->info.name;}
        for(unsigned i=0;i<128;++i)live_buttons[i].store((raw.rgbButtons[i]&0x80)!=0);
        const LONG axes[]={raw.lX,raw.lY,raw.lZ,raw.lRx,raw.lRy,raw.lRz,raw.rglSlider[0],raw.rglSlider[1]};
        for(unsigned i=0;i<8;++i)live_axes[i].store(axes[i]);
        *state=convert(raw);state->packet_number=++device->packet;
        if(!is_active())state->gamepad={};return X_ERROR_SUCCESS;
    }
    rex::X_RESULT SetDeviceVibration(rex::input::DeviceId,rex::input::X_INPUT_VIBRATION*) override{return ERROR_NOT_SUPPORTED;}
    rex::X_RESULT GetDeviceKeystroke(rex::input::DeviceId,uint32_t,rex::input::X_INPUT_KEYSTROKE*) override{return X_ERROR_EMPTY;}
};

class NamedXInput final : public rex::input::InputDriver {
    rex::input::xinput::XinputInputDriver native_{nullptr,0};
    HMODULE module_=nullptr;
    struct ExtendedCaps {XINPUT_CAPABILITIES caps;WORD vendor,product,version,unknown;DWORD unknown2;};
    using Query=DWORD(WINAPI*)(DWORD,DWORD,DWORD,ExtendedCaps*);
    Query query_=nullptr;
    using CapsQuery=DWORD(WINAPI*)(DWORD,DWORD,XINPUT_CAPABILITIES*);
    CapsQuery caps_query_=nullptr;
    std::mutex catalog_mutex_,wait_mutex_;
    std::condition_variable_any wake_;
    std::vector<rex::input::DeviceInfo> catalog_;
    std::jthread discovery_;
public:
    NamedXInput():InputDriver(nullptr,0){}
    ~NamedXInput()override{
        discovery_.request_stop();wake_.notify_all();if(discovery_.joinable())discovery_.join();
        if(module_)FreeLibrary(module_);
    }
    rex::X_STATUS Setup()override{
        const auto result=native_.Setup();module_=LoadLibraryW(L"xinput1_4.dll");
        if(module_){query_=reinterpret_cast<Query>(GetProcAddress(module_,MAKEINTRESOURCEA(108)));
            caps_query_=reinterpret_cast<CapsQuery>(GetProcAddress(module_,"XInputGetCapabilities"));}
        if(result==X_STATUS_SUCCESS){refresh();discovery_=std::jthread([this](std::stop_token stop){
            while(!stop.stop_requested()){
                std::unique_lock lock(wait_mutex_);wake_.wait_for(lock,stop,std::chrono::seconds(1),[]{return false;});
                lock.unlock();if(!stop.stop_requested())refresh();
            }
        });}
        return result;
    }
    void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out)override{
        std::lock_guard lock(catalog_mutex_);out.insert(out.end(),catalog_.begin(),catalog_.end());
    }
private:
    void refresh(){
        std::vector<rex::input::DeviceInfo> devices;
        for(unsigned slot=0;slot<4;++slot){
            XINPUT_CAPABILITIES caps{};if(!caps_query_||caps_query_(slot,0,&caps)!=0)continue;
            rex::input::DeviceInfo device;device.id=rex::input::DeviceId(0x58490000ULL|slot);
            device.synthetic=false;device.guid="xinput:"+std::to_string(slot);
            std::string name=(caps.SubType==6||caps.SubType==7||caps.SubType==11)?"XInput Guitar":"XInput Gamepad";
            ExtendedCaps extended{};
            if(query_&&query_(1,slot,0,&extended)==0){
                std::lock_guard lock(names_mutex);auto found=product_names.find(uint32_t(extended.vendor)|(uint32_t(extended.product)<<16));
                if(found!=product_names.end()&&found->second.size()==1&&!found->second.begin()->empty())name=*found->second.begin();
            }
            device.name=name+" (XInput "+std::to_string(slot+1)+")";
            {std::lock_guard lock(names_mutex);display_names[device.guid]=device.name;}
            devices.push_back(std::move(device));
        }
        std::lock_guard lock(catalog_mutex_);catalog_.swap(devices);
    }
public:
    rex::X_RESULT GetDeviceState(rex::input::DeviceId id,rex::input::X_INPUT_STATE* state)override{return native_.GetDeviceState(id,state);}
    rex::X_RESULT GetDeviceCapabilities(rex::input::DeviceId id,uint32_t flags,rex::input::X_INPUT_CAPABILITIES* caps)override{return native_.GetDeviceCapabilities(id,flags,caps);}
    rex::X_RESULT SetDeviceVibration(rex::input::DeviceId id,rex::input::X_INPUT_VIBRATION* value)override{return native_.SetDeviceVibration(id,value);}
    rex::X_RESULT GetDeviceKeystroke(rex::input::DeviceId id,uint32_t flags,rex::input::X_INPUT_KEYSTROKE* value)override{return native_.GetDeviceKeystroke(id,flags,value);}
};
}
