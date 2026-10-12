#define DIRECTINPUT_VERSION 0x0800
#include "config.h"
#include "../runtime/controller_mapping.h"
#include <commctrl.h>
#include <tlhelp32.h>
#include <dinput.h>
#include <xinput.h>
#include <shellapi.h>
#include <uxtheme.h>
#include "capture.h"
#include "save_manager.h"
#include <shobjidl.h>
#include <wincodec.h>
#include <vector>
#include <algorithm>
#include <iostream>
#include <set>

using gh2::launcher::Value;
namespace fs=std::filesystem;
std::wstring wide(std::string_view value){
    int size=MultiByteToWideChar(CP_UTF8,0,value.data(),int(value.size()),nullptr,0);
    std::wstring result(size,L'\0');MultiByteToWideChar(CP_UTF8,0,value.data(),int(value.size()),result.data(),size);return result;
}
std::string utf8(std::wstring_view value){
    int size=WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);
    std::string result(size,'\0');WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),result.data(),size,nullptr,nullptr);return result;
}
std::wstring text(HWND control){int size=GetWindowTextLengthW(control);std::wstring result(size+1,L'\0');GetWindowTextW(control,result.data(),size+1);result.resize(size);return result;}
bool game_running(){
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)throw std::runtime_error("Could not check whether GH2 is running.");
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);bool found=false;
    if(Process32FirstW(snapshot,&entry))do{if(_wcsicmp(entry.szExeFile,L"Guitar Hero 2.exe")==0||_wcsicmp(entry.szExeFile,L"gh2_pc.exe")==0){found=true;break;}}while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);return found;
}
void install_updates(const fs::path& root){
    if(game_running())throw std::runtime_error("GH2 is already running. Close it normally before launching again.");
    const auto directory=root/L"tools/rexglue-sdk/out/win-amd64";
    if(!fs::exists(directory/L"Guitar Hero 2.exe")&&fs::exists(directory/L"gh2_pc.exe"))fs::rename(directory/L"gh2_pc.exe",directory/L"Guitar Hero 2.exe");
    for(const auto& name:{std::wstring(L"Guitar Hero 2"),std::wstring(L"librexruntime"),std::wstring(L"rexgpu-xenos")}){
        const std::wstring extension=name==L"Guitar Hero 2"?L".exe":L".dll";
        const auto pending=directory/(name+L".next"+extension),active=directory/(name+extension),backup=directory/(name+L".previous"+extension);
        if(!fs::exists(pending))continue;
        if(fs::exists(active))fs::copy_file(active,backup,fs::copy_options::overwrite_existing);
        fs::copy_file(pending,active,fs::copy_options::overwrite_existing);fs::remove(pending);
    }
}
void launch_game(const fs::path& root){
    install_updates(root);
    const auto executable=root/L"tools/rexglue-sdk/out/win-amd64/Guitar Hero 2.exe";
    if(!fs::exists(executable)||!fs::exists(root/L"private/default.xex"))throw std::runtime_error("The game files could not be found beside this launcher.");
    fs::create_directories(root/L"build/windowed-state");
    std::wstring command=L"\""+executable.wstring()+L"\" --log_level=info \"--log_file="+(root/L"build/windowed-state/gh2.log").wstring()+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,root.c_str(),&startup,&process))throw std::runtime_error("Windows could not start GH2.");
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
}

struct Choice{std::wstring label;Value value;};
struct Field{HWND control{};int page{};std::string key;Value initial;std::vector<Choice> choices;int minimum=0,maximum=0;bool checkbox=false;int capture_kind=0;Value current=int64_t(0);};
struct Widget{HWND control;int x,y,w,h;};
struct Page{HWND window{};std::vector<Widget> widgets;int height=560;};
struct App{
    fs::path root,config;toml::table loaded;HWND window{},tabs{},status{},save_button{},play_button{};
    HFONT font{},title_font{};HBITMAP logo{};int logo_width{},logo_height{};HBRUSH background=CreateSolidBrush(RGB(255,255,255)),panel=CreateSolidBrush(RGB(255,255,255)),input_brush=CreateSolidBrush(RGB(255,255,255)),red=CreateSolidBrush(RGB(255,255,255));
    struct CaptureSession;std::shared_ptr<CaptureSession> capture;int capture_field=-1;ULONGLONG capture_deadline=0;
    std::vector<Field> fields;std::array<Page,6> pages{};int scale=96,current=0;bool ready=false,was_running=false,settings_mode=false;HWND tooltips{};std::map<HWND,std::wstring> tips;
    int px(int value)const{return MulDiv(value,scale,96);}
    void error(const std::exception& error){MessageBoxW(window,wide(error.what()).c_str(),L"Guitar Hero 2 Launcher",MB_OK|MB_ICONERROR);}
    HWND widget(int page,const wchar_t* type,const std::wstring& label,DWORD style,int x,int y,int w,int h,int id=0){
        if(std::wstring(type)==L"BUTTON"&&((style&BS_TYPEMASK)==BS_PUSHBUTTON||(style&BS_TYPEMASK)==BS_DEFPUSHBUTTON))style=(style&~DWORD(BS_TYPEMASK))|BS_OWNERDRAW;
        HWND parent=page<0?window:pages[page].window;
        HWND handle=CreateWindowExW(type==std::wstring(L"EDIT")?WS_EX_CLIENTEDGE:0,type,label.c_str(),WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(w),px(h),parent,HMENU(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        if(!handle)throw std::runtime_error("Could not create a launcher control.");
        SendMessageW(handle,WM_SETFONT,WPARAM(font),TRUE);
        SetWindowTheme(handle,L"",L"");
        if(page>=0)pages[page].widgets.push_back({handle,x,y,w,h});return handle;
    }
    void tip(int page,HWND handle,const std::wstring& text){
        if(page!=0)return;
        const std::map<std::wstring,std::wstring> explanations{
            {L"Internal resolution",L"Resolution used to draw the stage and highway. Higher values sharpen the image but increase GPU work. 1x is 720p; 2x is 1440p. Output window size is independent."},
            {L"Anti-aliasing",L"FXAA smooths jagged edges after rendering. Strong FXAA smooths more aggressively and may soften text and notes."},
            {L"Texture filtering",L"Anisotropic filtering keeps angled textures sharper, especially the highway. Game default respects the original texture settings."},
            {L"Window width (pixels)",L"Width of the desktop output window. Changing it does not change internal rendering resolution. Zero uses the default."},
            {L"Window height (pixels)",L"Height of the desktop output window. Zero uses the default. Internal resolution is configured separately."},
            {L"Monitor",L"Display used for borderless fullscreen. System default follows Windows' primary display."},
            {L"Display FPS limit (0 = unlimited)",L"Maximum display presentation rate. Zero removes the cap. GH2 song simulation and highway output remain at their native 60 Hz; raising this does not add new animation frames."},
            {L"Borderless fullscreen",L"Fills the selected display without changing its desktop resolution. Disable it to play in a window."},
            {L"Display VSync",L"Synchronizes presentation to your monitor to prevent tearing. It may add latency and limit the output rate to the display refresh rate."},
            {L"Preserve aspect ratio",L"Adds bars when necessary to keep the original 16:9 picture instead of stretching it."},
            {L"Show display-update counter",L"Shows presentation and game output rates. New game output is normally 60 Hz even on faster monitors."}};
        auto found=explanations.find(text);if(found==explanations.end())return;tips[handle]=found->second;
        TOOLINFOW info{UINT(TTTOOLINFOW_V2_SIZE)};info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=pages[page].window;info.uId=UINT_PTR(handle);info.lpszText=tips[handle].data();SendMessageW(tooltips,TTM_ADDTOOLW,0,LPARAM(&info));
    }
    void label(int page,std::wstring text,int x,int y,int w=410,int h=22){auto handle=widget(page,L"STATIC",text,SS_LEFT|SS_NOTIFY,x,y,w,h);tip(page,handle,text);}
    void combo(int page,std::string key,std::wstring label_text,int x,int y,Value fallback,std::vector<Choice> choices,int width=420){
        label(page,label_text,x,y,width);HWND control=widget(page,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,x,y+24,width,260);
        auto initial=gh2::launcher::value(loaded,key,fallback);
        if(std::none_of(choices.begin(),choices.end(),[&](const auto& item){return item.value==initial;})){
            std::wstring custom=L"Saved custom value: ";std::visit([&](const auto& item){if constexpr(std::is_same_v<std::decay_t<decltype(item)>,std::string>)custom+=wide(item);else custom+=std::to_wstring(item);},initial);
            choices.push_back({custom,initial});
        }
        for(size_t i=0;i<choices.size();++i){SendMessageW(control,CB_ADDSTRING,0,LPARAM(choices[i].label.c_str()));if(choices[i].value==initial)SendMessageW(control,CB_SETCURSEL,i,0);}
        fields.push_back({control,page,key,initial,std::move(choices)});
    }
    void check(int page,std::string key,std::wstring title,int x,int y,bool fallback){
        auto initial=gh2::launcher::value(loaded,key,Value(fallback));HWND control=widget(page,L"BUTTON",title,BS_AUTOCHECKBOX|WS_TABSTOP,x,y,420,25);
        SendMessageW(control,BM_SETCHECK,std::get<bool>(initial)?BST_CHECKED:BST_UNCHECKED,0);fields.push_back({control,page,key,initial,{},0,0,true});
        tip(page,control,title);
    }
    void number(int page,std::string key,std::wstring title,int x,int y,int fallback,int minimum,int maximum){
        label(page,title,x,y,300);auto initial=gh2::launcher::value(loaded,key,Value(int64_t(fallback)));
        HWND control=widget(page,L"EDIT",std::to_wstring(std::get<int64_t>(initial)),ES_NUMBER|ES_AUTOHSCROLL|WS_TABSTOP,x+305,y-3,95,28);
        fields.push_back({control,page,key,initial,{},minimum,maximum});
    }
    Value read(const Field& field){
        if(field.capture_kind)return field.current;
        if(field.checkbox)return SendMessageW(field.control,BM_GETCHECK,0,0)==BST_CHECKED;
        if(!field.choices.empty()){const auto selected=SendMessageW(field.control,CB_GETCURSEL,0,0);if(selected<0||size_t(selected)>=field.choices.size())throw std::runtime_error("Choose a value for every dropdown.");return field.choices[selected].value;}
        const auto value=text(field.control);size_t consumed=0;int64_t result;
        try{result=std::stoll(value,&consumed);}catch(...){throw std::runtime_error("Enter a valid number for "+field.key+".");}
        if(consumed!=value.size()||result<field.minimum||result>field.maximum)throw std::runtime_error(field.key+" must be between "+std::to_string(field.minimum)+" and "+std::to_string(field.maximum)+".");return result;
    }
    void save(){
        if(game_running())throw std::runtime_error("GH2 is running. Use its settings menus, or close it normally before saving from the launcher.");
        std::map<std::string,Value> changes;
        for(auto& field:fields){auto setting=read(field);if(setting!=field.initial)changes[field.key]=setting;}
        if(changes.contains("player_one_device_key")||changes.contains("gh2_controller_api"))changes["player_one_device"]=int64_t(0);
        gh2::launcher::save(config,changes);for(auto& field:fields)field.initial=read(field);
        SetWindowTextW(status,L"Saved. Play will load these settings.");
    }
    void refresh_state(){
        bool running=game_running();EnableWindow(save_button,!running&&!capture);EnableWindow(play_button,!running&&!capture);
        SetWindowTextW(play_button,running?L"Game running":L"PLAY");
        if(!capture){if(running)SetWindowTextW(status,L"GH2 is running. Launcher saves are paused; use the in-game settings menus.");
        else if(was_running)SetWindowTextW(status,L"Game closed. Reload settings to pick up changes made in game.");}
        was_running=running;
    }
    void populate();void devices();void layout();void show_page();void reload();void view(bool);void save_action(int);
    void start_capture(HWND);void poll_capture();void finish_capture(int value=-1);void binding(int page,std::string key,std::wstring label,int x,int y,int fallback,int kind);
};
App* app=nullptr;
template<class T>struct Release{void operator()(T* value)const{if(value)value->Release();}};
template<class T>using Com=std::unique_ptr<T,Release<T>>;
void load_logo(App& state,HINSTANCE instance){
    auto resource=FindResourceW(instance,MAKEINTRESOURCEW(103),RT_RCDATA);if(!resource)throw std::runtime_error("Title logo resource did not load.");
    const DWORD size=SizeofResource(instance,resource);const void* bytes=LockResource(LoadResource(instance,resource));
    HGLOBAL storage=GlobalAlloc(GMEM_MOVEABLE,size);if(!storage)throw std::runtime_error("Logo allocation failed.");auto memory=GlobalLock(storage);memcpy(memory,bytes,size);GlobalUnlock(storage);
    IStream* stream_raw=nullptr;if(FAILED(CreateStreamOnHGlobal(storage,TRUE,&stream_raw))){GlobalFree(storage);throw std::runtime_error("Logo stream failed.");}Com<IStream> stream(stream_raw);
    IWICImagingFactory* factory_raw=nullptr;if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory_raw))))throw std::runtime_error("Windows image decoder unavailable.");Com<IWICImagingFactory> factory(factory_raw);
    IWICBitmapDecoder* decoder_raw=nullptr;if(FAILED(factory->CreateDecoderFromStream(stream.get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder_raw)))throw std::runtime_error("Logo PNG could not be decoded.");Com<IWICBitmapDecoder> decoder(decoder_raw);
    IWICBitmapFrameDecode* frame_raw=nullptr;if(FAILED(decoder->GetFrame(0,&frame_raw)))throw std::runtime_error("Logo image missing.");Com<IWICBitmapFrameDecode> frame(frame_raw);
    UINT width,height;frame->GetSize(&width,&height);state.logo_width=state.px(390);state.logo_height=MulDiv(state.logo_width,height,width);
    IWICBitmapScaler* scaler_raw=nullptr;if(FAILED(factory->CreateBitmapScaler(&scaler_raw)))throw std::runtime_error("Logo scaler unavailable.");Com<IWICBitmapScaler> scaler(scaler_raw);
    if(FAILED(scaler->Initialize(frame.get(),state.logo_width,state.logo_height,WICBitmapInterpolationModeFant)))throw std::runtime_error("Logo scaling failed.");
    IWICFormatConverter* converter_raw=nullptr;if(FAILED(factory->CreateFormatConverter(&converter_raw)))throw std::runtime_error("Logo converter unavailable.");Com<IWICFormatConverter> converter(converter_raw);
    if(FAILED(converter->Initialize(scaler.get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))throw std::runtime_error("Logo alpha conversion failed.");
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=state.logo_width;info.bmiHeader.biHeight=-state.logo_height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* pixels=nullptr;
    state.logo=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0);if(!pixels||FAILED(converter->CopyPixels(nullptr,state.logo_width*4,state.logo_width*state.logo_height*4,static_cast<BYTE*>(pixels))))throw std::runtime_error("Logo rendering failed.");
}
std::vector<Choice> integers(std::initializer_list<std::pair<const wchar_t*,int>> items){std::vector<Choice> result;for(auto [name,value]:items)result.push_back({name,int64_t(value)});return result;}
std::vector<Choice> strings(std::initializer_list<std::pair<const wchar_t*,const char*>> items){std::vector<Choice> result;for(auto [name,value]:items)result.push_back({name,std::string(value)});return result;}

struct DeviceScan{
    std::map<uint32_t,std::wstring> products;std::vector<std::pair<DIDEVICEINSTANCEW,bool>> instances;IDirectInput8W* input=nullptr;
    static BOOL CALLBACK found(const DIDEVICEINSTANCEW* instance,void* opaque){
        auto& self=*static_cast<DeviceScan*>(opaque);self.products[instance->guidProduct.Data1]=instance->tszProductName;
        bool xinput=false;IDirectInputDevice8W* device=nullptr;
        if(SUCCEEDED(self.input->CreateDevice(instance->guidInstance,&device,nullptr))){
            DIPROPGUIDANDPATH path{};path.diph.dwSize=sizeof(path);path.diph.dwHeaderSize=sizeof(path.diph);path.diph.dwHow=DIPH_DEVICE;
            if(SUCCEEDED(device->GetProperty(DIPROP_GUIDANDPATH,&path.diph))){std::wstring name=path.wszPath;for(auto& value:name)value=std::towupper(value);xinput=name.find(L"IG_")!=std::wstring::npos;}
            device->Release();
        }
        self.instances.push_back({*instance,xinput});return DIENUM_CONTINUE;
    }
    std::vector<Choice> scan(){
        std::vector<Choice> choices{{L"Automatic",std::string("")}};std::set<uint32_t> native_products;
        if(SUCCEEDED(DirectInput8Create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,IID_IDirectInput8W,reinterpret_cast<void**>(&input),nullptr))){input->EnumDevices(DI8DEVCLASS_GAMECTRL,found,this,DIEDFL_ATTACHEDONLY);input->Release();input=nullptr;}
        HMODULE module=LoadLibraryW(L"xinput1_4.dll");
        if(module){
            using GetCaps=DWORD(WINAPI*)(DWORD,DWORD,XINPUT_CAPABILITIES*);
            struct Extended{XINPUT_CAPABILITIES caps;WORD vendor,product,version,unknown;DWORD unknown2;};
            using GetExtended=DWORD(WINAPI*)(DWORD,DWORD,DWORD,Extended*);
            auto caps_query=reinterpret_cast<GetCaps>(GetProcAddress(module,"XInputGetCapabilities"));auto extended_query=reinterpret_cast<GetExtended>(GetProcAddress(module,MAKEINTRESOURCEA(108)));
            for(unsigned slot=0;caps_query&&slot<4;++slot){XINPUT_CAPABILITIES caps{};if(caps_query(slot,0,&caps))continue;
                std::wstring name=(caps.SubType==6||caps.SubType==7||caps.SubType==11)?L"Guitar":L"Gamepad";Extended extended{};
                if(extended_query&&extended_query(1,slot,0,&extended)==0){uint32_t product=extended.vendor|(uint32_t(extended.product)<<16);native_products.insert(product);if(products.contains(product))name=products[product];}
                choices.push_back({name+L" (XInput "+std::to_wstring(slot+1)+L")",std::string("xinput:")+std::to_string(slot)});
            }FreeLibrary(module);
        }
        for(const auto& [instance,is_native]:instances){if(is_native||native_products.contains(instance.guidProduct.Data1))continue;wchar_t guid[40]{};StringFromGUID2(instance.guidInstance,guid,40);
            choices.push_back({std::wstring(instance.tszProductName)+L" (DirectInput)","dinput:"+utf8(guid)});}
        return choices;
    }
};
struct App::CaptureSession{
    using Query=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);HMODULE module=nullptr;Query query=nullptr;
    struct Native{unsigned slot;bool guitar;gh2::launcher::EdgeCapture edge;};
    struct Direct{IDirectInputDevice8W* device;gh2::launcher::EdgeCapture edge;gh2::launcher::RawEdgeCapture raw;};
    std::vector<Native> native;std::vector<Direct> direct;std::map<std::string,int> settings;bool raw_only=false;
    static gh2::controls::State convert(const XINPUT_STATE& input,bool guitar){const auto& pad=input.Gamepad;return gh2::launcher::capture_state({pad.wButtons,pad.bLeftTrigger,pad.bRightTrigger,{pad.sThumbLX,pad.sThumbLY,pad.sThumbRX,pad.sThumbRY}},guitar);}
    static BOOL CALLBACK range(const DIDEVICEOBJECTINSTANCEW* object,void* opaque){DIPROPRANGE property{};property.diph.dwSize=sizeof(property);property.diph.dwHeaderSize=sizeof(property.diph);property.diph.dwHow=DIPH_BYID;property.diph.dwObj=object->dwType;property.lMin=-32768;property.lMax=32767;static_cast<IDirectInputDevice8W*>(opaque)->SetProperty(DIPROP_RANGE,&property.diph);return DIENUM_CONTINUE;}
    static bool state(IDirectInputDevice8W* device,DIJOYSTATE2& raw){raw={};for(auto& pov:raw.rgdwPOV)pov=0xFFFFFFFF;device->Poll();HRESULT result=device->GetDeviceState(sizeof(raw),&raw);if(result==DIERR_INPUTLOST||result==DIERR_NOTACQUIRED){device->Acquire();device->Poll();result=device->GetDeviceState(sizeof(raw),&raw);}return SUCCEEDED(result);}
    CaptureSession(HWND window,const std::string& selected,bool raw_capture,const std::map<std::string,int>& config,int api):settings(config),raw_only(raw_capture){
        auto choices=DeviceScan{}.scan();module=LoadLibraryW(L"xinput1_4.dll");if(module)query=reinterpret_cast<Query>(GetProcAddress(module,"XInputGetState"));
        using CapsQuery=DWORD(WINAPI*)(DWORD,DWORD,XINPUT_CAPABILITIES*);auto caps_query=module?reinterpret_cast<CapsQuery>(GetProcAddress(module,"XInputGetCapabilities")):nullptr;
        if(!raw_only&&api!=2)for(unsigned slot=0;query&&slot<4;++slot){if(!selected.empty()&&selected!="xinput:"+std::to_string(slot))continue;XINPUT_STATE raw{};if(query(slot,&raw)!=0)continue;XINPUT_CAPABILITIES caps{};bool guitar=caps_query&&caps_query(slot,0,&caps)==0&&gh2::controls::is_guitar(caps.SubType);Native device{slot,guitar,{}};device.edge.baseline(convert(raw,guitar));native.push_back(device);}
        IDirectInput8W* input=nullptr;
        if(SUCCEEDED(DirectInput8Create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,IID_IDirectInput8W,reinterpret_cast<void**>(&input),nullptr))){
            for(const auto& choice:choices){const auto key=std::get<std::string>(choice.value);if(api==1||!key.starts_with("dinput:")||(!selected.empty()&&selected!=key))continue;
                auto guid_text=wide(key.substr(7));GUID guid{};if(FAILED(CLSIDFromString(guid_text.data(),&guid)))continue;IDirectInputDevice8W* device=nullptr;
                if(FAILED(input->CreateDevice(guid,&device,nullptr)))continue;
                if(FAILED(device->SetDataFormat(&c_dfDIJoystick2))||FAILED(device->SetCooperativeLevel(window,DISCL_NONEXCLUSIVE|DISCL_BACKGROUND))){device->Release();continue;}
                device->EnumObjects(range,device,DIDFT_AXIS);device->Acquire();DIJOYSTATE2 raw{};
                if(!state(device,raw)){device->Unacquire();device->Release();continue;}
                Direct item{device,{},{}};item.edge.baseline(gh2::launcher::convert_directinput(raw,settings));item.raw.baseline(raw);direct.push_back(item);
            }input->Release();
        }
    }
    ~CaptureSession(){for(auto& item:direct){item.device->Unacquire();item.device->Release();}if(module)FreeLibrary(module);}
    int poll(){
        for(auto& item:native){XINPUT_STATE raw{};if(query(item.slot,&raw)==0){int input=item.edge.poll(convert(raw,item.guitar));if(input)return input;}}
        for(auto& item:direct){DIJOYSTATE2 raw{};if(state(item.device,raw)){int input=raw_only?item.raw.poll(raw):item.edge.poll(gh2::launcher::convert_directinput(raw,settings));if(input)return input;}}
        return 0;
    }
};
std::wstring binding_text(const Field& field){int value=int(std::get<int64_t>(field.current));return field.capture_kind==2?(value?L"Button "+std::to_wstring(value):L"Unbound"):(value>=0&&value<gh2::controls::SourceCount?wide(gh2::controls::source_names[value]):L"Unbound");}
LRESULT CALLBACK BindingProc(HWND window,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR){
    if(message==WM_RBUTTONUP){SendMessageW(app->window,WM_APP+5,WPARAM(window),0);return 0;}return DefSubclassProc(window,message,wparam,lparam);
}
void App::binding(int page,std::string key,std::wstring title,int x,int y,int fallback,int kind){
    label(page,title,x,y,185);auto initial=gh2::launcher::value(loaded,key,int64_t(fallback));
    HWND control=widget(page,L"BUTTON",L"",BS_PUSHBUTTON|WS_TABSTOP,x+190,y-5,230,30,2000+int(fields.size()));
    Field field{control,page,key,initial,{},0,kind==2?128:gh2::controls::SourceCount-1,false,kind,initial};
    SetWindowTextW(control,binding_text(field).c_str());SetWindowSubclass(control,BindingProc,1,0);fields.push_back(field);
}
void App::start_capture(HWND control){
    if(capture)finish_capture();auto found=std::find_if(fields.begin(),fields.end(),[&](const auto& field){return field.control==control&&field.capture_kind;});if(found==fields.end())return;
    std::string key;int api=0;std::map<std::string,int> settings;
    for(auto& field:fields){if(field.key=="gh2_controller_api")api=int(std::get<int64_t>(read(field)));if(field.key=="player_one_device_key")key=std::get<std::string>(read(field));if(field.key.starts_with("dinput_"))settings[field.key]=int(std::get<int64_t>(read(field)));}
    auto session=std::make_shared<CaptureSession>(window,key,found->capture_kind==2,settings,api);
    if(session->native.empty()&&session->direct.empty())throw std::runtime_error(found->capture_kind==2?"Select a connected DirectInput controller first, then click the mapping again.":"Connect the selected controller, refresh devices, then click the mapping again.");
    capture_field=int(found-fields.begin());capture=session;capture_deadline=GetTickCount64()+15000;
    SetWindowTextW(control,L"PRESS A BUTTON...");SetWindowTextW(status,L"Press the desired controller button. Release held inputs first. Escape cancels; right-click a mapping clears it.");
    EnableWindow(GetDlgItem(window,102),FALSE);EnableWindow(GetDlgItem(pages[1].window,104),FALSE);SetTimer(window,2,16,nullptr);refresh_state();
}
void App::finish_capture(int value){
    KillTimer(window,2);if(capture_field>=0&&size_t(capture_field)<fields.size()){
        auto& field=fields[capture_field];if(value>=0){field.current=int64_t(value);
            if(field.capture_kind==1){const std::string mode=field.key.starts_with("guitar_")?"guitar_mode":"gamepad_mode";for(auto& candidate:fields)if(candidate.key==mode&&std::get<int64_t>(read(candidate))==0)SendMessageW(candidate.control,CB_SETCURSEL,1,0);}}
        SetWindowTextW(field.control,binding_text(field).c_str());InvalidateRect(field.control,nullptr,TRUE);
    }
    capture.reset();capture_field=-1;EnableWindow(GetDlgItem(window,102),TRUE);EnableWindow(GetDlgItem(pages[1].window,104),TRUE);
    SetWindowTextW(status,value>=0?L"Mapped. Save settings or press Play to keep your new binding.":L"Mapping cancelled. Your previous binding was kept.");refresh_state();
}
void App::poll_capture(){if(!capture)return;if(GetTickCount64()>=capture_deadline){finish_capture();return;}int value=capture->poll();if(value)finish_capture(value);}
void App::devices(){
    int api=0;for(auto& field:fields)if(field.key=="gh2_controller_api")api=int(std::get<int64_t>(read(field)));
    for(auto& field:fields)if(field.key=="player_one_device_key"){
        auto selected=read(field);auto choices=DeviceScan{}.scan();
        auto allowed=[&](const Value& value){auto key=std::get<std::string>(value);return key.empty()||api==0||key.starts_with(api==1?"xinput:":"dinput:");};
        std::erase_if(choices,[&](const auto& item){return !allowed(item.value);});
        if(!allowed(selected))selected=std::string("");
        if(std::none_of(choices.begin(),choices.end(),[&](const auto& item){return item.value==selected;}))choices.push_back({L"Saved controller (currently disconnected)",selected});
        SendMessageW(field.control,CB_RESETCONTENT,0,0);for(size_t i=0;i<choices.size();++i){SendMessageW(field.control,CB_ADDSTRING,0,LPARAM(choices[i].label.c_str()));if(choices[i].value==selected)SendMessageW(field.control,CB_SETCURSEL,i,0);}field.choices=std::move(choices);break;
    }
}
void App::populate(){
    const auto axis_scale=loaded["draw_resolution_scale_x"].value_or(int64_t(1));
    if(axis_scale!=1)loaded.insert_or_assign("resolution_scale",axis_scale);
    combo(0,"resolution_scale",L"Internal resolution",18,22,int64_t(1),integers({{L"1x - 1280 x 720",1},{L"2x - 2560 x 1440",2},{L"3x - 3840 x 2160 (4K)",3},{L"4x - 5120 x 2880",4}}));
    combo(0,"swap_post_effect",L"Anti-aliasing",465,22,std::string("none"),strings({{L"Game default","none"},{L"FXAA","fxaa"},{L"FXAA strong","fxaa_extreme"}}));
    combo(0,"anisotropic_override",L"Texture filtering",18,98,int64_t(3),integers({{L"Game default",-1},{L"Off",0},{L"1x",1},{L"2x",2},{L"4x",3},{L"8x",4},{L"16x",5}}));
    number(0,"window_width",L"Window width (pixels)",465,110,1280,0,8192);number(0,"window_height",L"Window height (pixels)",465,151,720,0,8192);
    std::vector<Choice> monitors{{L"System default",int64_t(0)}};DISPLAY_DEVICEW display{};display.cb=sizeof(display);int index=0;
    for(DWORD i=0;EnumDisplayDevicesW(nullptr,i,&display,0);++i){if(display.StateFlags&DISPLAY_DEVICE_ATTACHED_TO_DESKTOP){DISPLAY_DEVICEW monitor{};monitor.cb=sizeof(monitor);std::wstring name=display.DeviceString;if(EnumDisplayDevicesW(display.DeviceName,0,&monitor,0))name=monitor.DeviceString;monitors.push_back({L"Display "+std::to_wstring(++index)+L" - "+name,int64_t(index)});}display={};display.cb=sizeof(display);}
    combo(0,"monitor",L"Monitor",18,190,int64_t(0),monitors);
    check(0,"fullscreen",L"Borderless fullscreen",18,280,true);check(0,"gh2_display_vsync",L"Display VSync",465,280,false);
    check(0,"present_letterbox",L"Preserve aspect ratio",18,322,true);check(0,"gh2_show_fps",L"Show display-update counter",465,322,false);
    number(0,"gh2_display_fps_limit",L"Display FPS limit (0 = unlimited)",18,376,0,0,2147483647);
    check(0,"gh2_no_audience",L"No audience (hide crowd models)",18,455,false);
    check(0,"gh2_discord_presence",L"Discord Rich Presence (mode, song and difficulty)",18,515,true);
    pages[0].height=565;
    auto device_choices=DeviceScan{}.scan();std::string key=loaded["player_one_device_key"].value_or(std::string(""));
    const int legacy=int(loaded["player_one_device"].value_or(int64_t(0)));
    if(key.empty()&&legacy>0&&legacy<=4){key="xinput:"+std::to_string(legacy-1);loaded.insert_or_assign("player_one_device_key",key);}
    combo(1,"gh2_controller_api",L"Controller input mode",18,15,int64_t(0),integers({{L"Automatic (XInput + DirectInput)",0},{L"XInput",1},{L"DirectInput",2}}));
    widget(1,L"BUTTON",L"DirectInput button / axis setup",BS_PUSHBUTTON|WS_TABSTOP,465,39,405,28,114);
    combo(1,"player_one_device_key",L"Player 1 controller",18,90,std::string(""),device_choices,650);widget(1,L"BUTTON",L"Refresh devices",BS_PUSHBUTTON|WS_TABSTOP,700,114,170,28,104);
    std::vector<Choice> sources;for(int i=0;i<gh2::controls::SourceCount;++i)sources.push_back({wide(gh2::controls::source_names[i]),int64_t(i)});
    for(int profile=0;profile<2;++profile){int x=profile?465:18;std::string prefix=profile?"guitar_":"gamepad_";auto defaults=gh2::controls::defaults(profile!=0);
        combo(1,prefix+"mode",profile?L"Guitar profile":L"Gamepad profile",x,170,int64_t(0),integers({{L"Native GH2 controls",0},{L"Custom bindings",1},{L"Custom guitar with strum",2}}));
        for(int i=0;i<gh2::controls::ActionCount;++i)binding(1,prefix+"bind_"+std::to_string(i),wide(gh2::controls::action_names[i]),x,252+i*35,defaults.sources[i],1);
        combo(1,prefix+"whammy",L"Whammy axis",x,576,int64_t(0),integers({{L"Native axes",0},{L"Left stick X",1},{L"Left stick Y",2},{L"Right stick X",3},{L"Right stick Y",4}}));
    }
    check(1,"mnk_mode",L"Enable keyboard alongside controllers",18,659,true);
    label(1,L"Click a mapping, then press its controller button. Right-click to clear. Capturing enables Custom bindings. Guitar-with-strum mode remains your choice.",18,699,855,42);pages[1].height=763;
    constexpr const wchar_t* buttons[]={L"A",L"B",L"X",L"Y",L"LB",L"RB",L"Back",L"Start",L"Left stick click",L"Right stick click",L"Left trigger",L"Right trigger",L"D-pad up",L"D-pad down",L"D-pad left",L"D-pad right"};
    constexpr int button_defaults[]={1,2,3,4,5,6,9,10,11,12,7,8,0,0,0,0};
    widget(2,L"BUTTON",L"Back to Controls",BS_PUSHBUTTON|WS_TABSTOP,18,12,200,30,115);
    label(2,L"Map raw DirectInput buttons into controller inputs. 0 = unbound; 1-128 = physical button number.",245,16,610,44);
    for(int i=0;i<16;++i)binding(2,"dinput_button_"+std::to_string(i),buttons[i],i<8?18:465,83+(i%8)*36,button_defaults[i],2);
    auto axes=integers({{L"X",0},{L"Y",1},{L"Z",2},{L"RX",3},{L"RY",4},{L"RZ",5},{L"Slider 1",6},{L"Slider 2",7}});
    constexpr const wchar_t* axis_names[]={L"Left stick X",L"Left stick Y",L"Right stick X",L"Right stick Y"};constexpr int axis_defaults[]={0,1,3,4};
    for(int i=0;i<4;++i)combo(2,"dinput_axis_"+std::to_string(i),axis_names[i],i%2?465:18,400+(i/2)*75,int64_t(axis_defaults[i]),axes);
    auto triggers=integers({{L"Digital button only",0},{L"X",1},{L"Y",2},{L"Z",3},{L"RX",4},{L"RY",5},{L"RZ",6},{L"Slider 1",7},{L"Slider 2",8}});
    for(int i=0;i<2;++i){int x=i?465:18;combo(2,"dinput_trigger_axis_"+std::to_string(i),i?L"Right trigger axis":L"Left trigger axis",x,559,int64_t(0),triggers);
        combo(2,"dinput_trigger_mode_"+std::to_string(i),L"Trigger rest / direction",x,633,int64_t(0),integers({{L"Minimum",0},{L"Maximum",1},{L"Center positive",2},{L"Center negative",3}}));}
    label(2,L"Raw button and axis monitoring remains available in the game's F6 menu. These raw mappings are shared by DirectInput devices.",18,724,855,44);pages[2].height=795;
    label(3,L"AUDIO CALIBRATION",18,14,855);
    number(3,"audio_offset_ms",L"Added audio delay (milliseconds)",18,38,0,0,250);
    combo(3,"audio_maxqframes",L"Audio buffering",18,103,int64_t(8),integers({{L"Low latency - 4 frames",4},{L"Normal - 8 frames",8},{L"16 frames",16},{L"32 frames",32},{L"64 frames",64}}));
    check(3,"audio_mute",L"Mute game audio",18,196,false);
    label(3,L"Zero delay preserves the original timing. Positive values play audio later. Lower buffering may reduce latency but can cause audio stuttering.",18,258,855,60);
    label(3,L"Use F8 in game for the flash/click calibration reference.",18,330,855,24);
    label(3,L"VIDEO CALIBRATION",18,390,855);
    label(3,L"Use Options > Calibrate Lag in the game. Launcher video calibration is planned for a future update.",18,432,855,48);pages[3].height=510;
    combo(4,"gh2_hyperspeed",L"Hyperspeed",18,22,int64_t(-1),integers({{L"Original cheat codes",-1},{L"Off",0},{L"Level 1 - 1.50x (GH2 original)",1},{L"Level 2 - 1.75x",2},{L"Level 3 - 2.00x",3},{L"Level 4 - 2.25x",4},{L"Level 5 - 2.50x",5}}));
    label(4,L"Higher levels scroll the highway faster; music and note timing stay unchanged. Choose before loading a song.",465,30,410,55);
    const auto toggles=integers({{L"Original cheat codes",-1},{L"Off",0},{L"On",1}});
    combo(4,"gh2_cheat_air_guitar",L"Air guitar",18,116,int64_t(-1),toggles);
    combo(4,"gh2_cheat_performance",L"Performance mode (hide highway)",465,116,int64_t(-1),toggles);
    combo(4,"gh2_cheat_flaming_head",L"Flaming head",18,210,int64_t(-1),toggles);
    combo(4,"gh2_cheat_crowd_heads",L"Crowd heads",465,210,int64_t(-1),integers({{L"Original cheat codes",-1},{L"Off",0},{L"Eyeballs",1},{L"Monkeys",2}}));
    combo(4,"gh2_cheat_unlock_all",L"Unlock all",18,304,int64_t(-1),toggles);
    label(4,L"Unlock All refreshes the song list on the main menu. Return there after changing it elsewhere. It disables career and saving while enabled; turn it off to restore normal play.",465,312,410,70);
    label(4,L"Original cheat codes preserves the game's own fret-code setting. Select Off to override it. In-game cheats: F9.",18,420,855,48);pages[4].height=490;
    label(5,L"SAVE MANAGEMENT",18,22,855);
    label(5,L"Career progress and achievement data live in Documents\\GH2PC\\Saves. Close GH2 before importing or deleting saves.",18,68,855,48);
    widget(5,L"BUTTON",L"Export saves...",BS_PUSHBUTTON|WS_TABSTOP,18,146,260,40,120);
    widget(5,L"BUTTON",L"Import saves...",BS_PUSHBUTTON|WS_TABSTOP,310,146,260,40,121);
    widget(5,L"BUTTON",L"Delete saves...",BS_PUSHBUTTON|WS_TABSTOP,602,146,260,40,122);
    widget(5,L"BUTTON",L"Open save folder",BS_PUSHBUTTON|WS_TABSTOP,18,224,260,36,103);
    widget(5,L"BUTTON",L"Open backups",BS_PUSHBUTTON|WS_TABSTOP,310,224,260,36,123);
    label(5,L"Export creates a portable folder. Import accepts that folder and replaces career progress. Import and deletion keep the previous files in Save Backups so they can be restored.",18,302,855,65);pages[5].height=400;
    devices();
}
void App::show_page(){if(capture)finish_capture();constexpr int map[]={0,1,3,4,5};current=map[std::clamp(TabCtrl_GetCurSel(tabs),0,4)];for(int i=0;i<6;++i)ShowWindow(pages[i].window,settings_mode&&i==current?SW_SHOW:SW_HIDE);SetScrollPos(pages[current].window,SB_VERT,0,TRUE);SetScrollPos(pages[current].window,SB_HORZ,0,TRUE);SendMessageW(pages[current].window,WM_SIZE,0,0);InvalidateRect(tabs,nullptr,TRUE);}
void App::view(bool settings){
    if(capture)finish_capture();settings_mode=settings;
    RECT available{};SystemParametersInfoW(SPI_GETWORKAREA,0,&available,0);
    SetWindowPos(window,nullptr,0,0,std::min(px(settings?1000:440),int(available.right-available.left-20)),std::min(px(settings?800:560),int(available.bottom-available.top-20)),SWP_NOMOVE|SWP_NOZORDER);
    layout();show_page();InvalidateRect(window,nullptr,TRUE);
}
void App::layout(){
    RECT client{};GetClientRect(window,&client);int width=client.right,height=client.bottom;
    for(int id:{101,102,105,112})ShowWindow(GetDlgItem(window,id),settings_mode?SW_SHOW:SW_HIDE);
    for(int id:{110,111,113})ShowWindow(GetDlgItem(window,id),settings_mode?SW_HIDE:SW_SHOW);
    ShowWindow(tabs,settings_mode?SW_SHOW:SW_HIDE);ShowWindow(status,settings_mode?SW_SHOW:SW_HIDE);
    if(!settings_mode){
        for(auto& page:pages)ShowWindow(page.window,SW_HIDE);
        const int x=(width-px(220))/2;int y=std::max(px(245),height-px(230));
        SetWindowPos(play_button,nullptr,x,y,px(220),px(40),SWP_NOZORDER);
        SetWindowPos(GetDlgItem(window,111),nullptr,x,y+px(54),px(220),px(40),SWP_NOZORDER);
        SetWindowPos(GetDlgItem(window,110),nullptr,x,y+px(108),px(220),px(40),SWP_NOZORDER);
        SetWindowPos(GetDlgItem(window,113),nullptr,px(20),height-px(34),width-px(40),px(23),SWP_NOZORDER);return;
    }
    SetWindowPos(tabs,nullptr,px(24),px(18),width-px(48),px(34),SWP_NOZORDER);
    for(auto& page:pages)SetWindowPos(page.window,nullptr,px(34),px(56),width-px(68),height-px(186),SWP_NOZORDER);
    SetWindowPos(status,nullptr,px(24),height-px(95),width-px(48),px(30),SWP_NOZORDER);
    SetWindowPos(save_button,nullptr,px(24),height-px(54),px(140),px(34),SWP_NOZORDER);
    SetWindowPos(GetDlgItem(window,102),nullptr,px(176),height-px(54),px(140),px(34),SWP_NOZORDER);
    SetWindowPos(GetDlgItem(window,105),nullptr,px(328),height-px(54),px(120),px(34),SWP_NOZORDER);
    SetWindowPos(GetDlgItem(window,112),nullptr,px(460),height-px(54),px(100),px(34),SWP_NOZORDER);
    SetWindowPos(play_button,nullptr,width-px(224),height-px(54),px(200),px(34),SWP_NOZORDER);
}
fs::path pick_folder(HWND owner,const wchar_t* title){
    IFileDialog* dialog=nullptr;if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))throw std::runtime_error("Could not open the folder picker.");
    struct Guard{IFileDialog* p;~Guard(){p->Release();}}guard{dialog};
    DWORD options;dialog->GetOptions(&options);dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);dialog->SetTitle(title);
    HRESULT result=dialog->Show(owner);if(result==HRESULT_FROM_WIN32(ERROR_CANCELLED))return {};if(FAILED(result))throw std::runtime_error("Folder selection failed.");
    IShellItem* item=nullptr;if(FAILED(dialog->GetResult(&item)))throw std::runtime_error("Could not read selected folder.");PWSTR name=nullptr;result=item->GetDisplayName(SIGDN_FILESYSPATH,&name);item->Release();if(FAILED(result))throw std::runtime_error("Choose a local folder.");fs::path path(name);CoTaskMemFree(name);return path;
}
void App::save_action(int action){
    if(game_running())throw std::runtime_error("Close GH2 normally before exporting, importing or deleting saves.");
    const auto user=config.parent_path();
    if(action==120){auto folder=pick_folder(window,L"Choose where to export your saves");if(folder.empty())return;auto destination=folder/(L"GH2PC-Saves-"+gh2::launcher::save_stamp());gh2::launcher::copy_saves(user/L"Saves",destination);SetWindowTextW(status,(L"Exported to "+destination.wstring()).c_str());}
    if(action==121){auto folder=pick_folder(window,L"Select an exported GH2PC save folder");if(folder.empty())return;if(!gh2::launcher::valid_save_bundle(folder))throw std::runtime_error("Select the exported folder containing the career save and header.");if(MessageBoxW(window,L"Replace your current career progress with this save? Your current files will be backed up.",L"Import saves",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES)return;gh2::launcher::import_saves(folder,user);SetWindowTextW(status,L"Save imported. Previous files are in Save Backups.");}
    if(action==122){if(MessageBoxW(window,L"Delete your career save and achievements? Files will be moved to Save Backups, where you can restore them.",L"Delete saves",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return;gh2::launcher::delete_saves(user);SetWindowTextW(status,L"Saves deleted. Previous files are in Save Backups.");}
}
void App::reload(){
    loaded=gh2::launcher::load(config);fields.clear();for(auto& [handle,tip]:tips){TOOLINFOW info{UINT(TTTOOLINFOW_V2_SIZE)};info.hwnd=GetParent(handle);info.uId=UINT_PTR(handle);SendMessageW(tooltips,TTM_DELTOOLW,0,LPARAM(&info));}tips.clear();for(auto& page:pages){for(auto& item:page.widgets)DestroyWindow(item.control);page.widgets.clear();}
    populate();layout();show_page();SetWindowTextW(status,L"Settings reloaded from Documents.");
}
void paint_backdrop(HDC dc,RECT area,bool page){
    FillRect(dc,&area,app->background);if(page)return;
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(28,28,28));auto old=SelectObject(dc,app->title_font);
    if(app->settings_mode){}
    else{
        const int w=std::min(app->px(390),int(area.right)-app->px(30)),h=MulDiv(w,app->logo_height,app->logo_width),x=(area.right-w)/2,y=app->px(25);
        HDC source=CreateCompatibleDC(dc);auto bitmap=SelectObject(source,app->logo);BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        AlphaBlend(dc,x,y,w,h,source,0,0,app->logo_width,app->logo_height,blend);SelectObject(source,bitmap);DeleteDC(source);
    }
    SelectObject(dc,old);
}
LRESULT draw_item(DRAWITEMSTRUCT* item){
    RECT rect=item->rcItem;bool tab=item->CtlType==ODT_TAB,selected=tab?item->itemID==unsigned(TabCtrl_GetCurSel(app->tabs)):(item->itemState&ODS_SELECTED)!=0;
    bool enabled=(item->itemState&ODS_DISABLED)==0;bool play=!tab&&GetDlgCtrlID(item->hwndItem)==106;
    COLORREF fill=selected?RGB(232,232,232):RGB(255,255,255);if(!enabled)fill=RGB(248,248,248);
    HBRUSH brush=CreateSolidBrush(fill);FillRect(item->hDC,&rect,brush);DeleteObject(brush);
    HPEN edge=CreatePen(PS_SOLID,app->px(selected?2:1),enabled?RGB(180,180,180):RGB(220,220,220));auto previous=SelectObject(item->hDC,edge);auto old_brush=SelectObject(item->hDC,GetStockObject(NULL_BRUSH));Rectangle(item->hDC,rect.left,rect.top,rect.right,rect.bottom);SelectObject(item->hDC,previous);SelectObject(item->hDC,old_brush);DeleteObject(edge);
    std::wstring label;
    if(tab){wchar_t buffer[80]{};TCITEMW entry{};entry.mask=TCIF_TEXT;entry.pszText=buffer;entry.cchTextMax=80;TabCtrl_GetItem(app->tabs,item->itemID,&entry);label=buffer;}
    else label=text(item->hwndItem);
    for(const auto& field:app->fields)if(field.control==item->hwndItem&&field.key.find("_bind_")!=std::string::npos){int action=std::stoi(field.key.substr(field.key.rfind('_')+1));if(action<5){constexpr COLORREF colors[]={RGB(78,149,55),RGB(186,49,42),RGB(219,184,40),RGB(58,118,177),RGB(213,123,34)};HBRUSH fret=CreateSolidBrush(colors[action]);auto old=SelectObject(item->hDC,fret);Ellipse(item->hDC,rect.left+app->px(11),rect.top+app->px(7),rect.left+app->px(27),rect.top+app->px(23));SelectObject(item->hDC,old);DeleteObject(fret);rect.left+=app->px(23);}break;}
    SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,enabled?RGB(25,25,25):RGB(150,150,150));SelectObject(item->hDC,app->font);DrawTextW(item->hDC,label.c_str(),-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    if(item->itemState&ODS_FOCUS){InflateRect(&rect,-app->px(4),-app->px(4));DrawFocusRect(item->hDC,&rect);}return TRUE;
}
LRESULT CALLBACK NavigationProc(HWND window,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR){
    if(message==WM_PAINT){PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT area{};GetClientRect(window,&area);FillRect(dc,&area,app->background);
        for(int i=0;i<TabCtrl_GetItemCount(window);++i){DRAWITEMSTRUCT item{};item.CtlType=ODT_TAB;item.itemID=i;item.hwndItem=window;item.hDC=dc;TabCtrl_GetItemRect(window,i,&item.rcItem);draw_item(&item);}EndPaint(window,&paint);return 0;}
    if(message==WM_ERASEBKGND)return 1;return DefSubclassProc(window,message,wparam,lparam);
}
LRESULT CALLBACK PageProc(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
    if(!app)return DefWindowProcW(window,message,wparam,lparam);
    if(message==WM_COMMAND||message==WM_DRAWITEM||message==WM_CTLCOLORSTATIC||message==WM_CTLCOLORBTN||message==WM_CTLCOLOREDIT||message==WM_CTLCOLORLISTBOX)return SendMessageW(app->window,message,wparam,lparam);
    if(message==WM_ERASEBKGND){RECT area{};GetClientRect(window,&area);paint_backdrop(HDC(wparam),area,true);return 1;}
    if(message==WM_SIZE||message==WM_VSCROLL||message==WM_HSCROLL||message==WM_MOUSEWHEEL){
        auto found=std::find_if(app->pages.begin(),app->pages.end(),[&](const auto& page){return page.window==window;});
        if(found==app->pages.end())return DefWindowProcW(window,message,wparam,lparam);
        RECT client{};GetClientRect(window,&client);
        for(int bar:{SB_HORZ,SB_VERT}){SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(window,bar,&info);info.nMin=0;info.nMax=app->px(bar==SB_HORZ?885:found->height);info.nPage=bar==SB_HORZ?client.right:client.bottom;
            if(message==(bar==SB_HORZ?WM_HSCROLL:WM_VSCROLL))switch(LOWORD(wparam)){case SB_LINEUP:info.nPos-=app->px(24);break;case SB_LINEDOWN:info.nPos+=app->px(24);break;case SB_PAGEUP:info.nPos-=info.nPage;break;case SB_PAGEDOWN:info.nPos+=info.nPage;break;case SB_THUMBTRACK:info.nPos=info.nTrackPos;break;}
            if(message==WM_MOUSEWHEEL&&bar==SB_VERT)info.nPos-=GET_WHEEL_DELTA_WPARAM(wparam)/WHEEL_DELTA*app->px(72);
            SetScrollInfo(window,bar,&info,TRUE);
        }
        const int x=GetScrollPos(window,SB_HORZ),y=GetScrollPos(window,SB_VERT);
        for(auto& item:found->widgets)SetWindowPos(item.control,nullptr,app->px(item.x)-x,app->px(item.y)-y,app->px(item.w),app->px(item.h),SWP_NOZORDER);
        InvalidateRect(window,nullptr,TRUE);return 0;
    }
    return DefWindowProcW(window,message,wparam,lparam);
}
LRESULT CALLBACK MainProc(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
    if(message==WM_CREATE){app->window=window;return 0;}
    if(!app)return DefWindowProcW(window,message,wparam,lparam);
    try{
        switch(message){
        case WM_GETMINMAXINFO:{auto* bounds=reinterpret_cast<MINMAXINFO*>(lparam);bounds->ptMinTrackSize={app->px(app->settings_mode?850:410),app->px(app->settings_mode?500:520)};return 0;}
        case WM_SIZE:if(app->ready)app->layout();return 0;
        case WM_PAINT:{PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT rect{};GetClientRect(window,&rect);paint_backdrop(dc,rect,false);EndPaint(window,&paint);return 0;}
        case WM_NOTIFY:if(reinterpret_cast<NMHDR*>(lparam)->hwndFrom==app->tabs&&reinterpret_cast<NMHDR*>(lparam)->code==TCN_SELCHANGE)app->show_page();return 0;
        case WM_DRAWITEM:return draw_item(reinterpret_cast<DRAWITEMSTRUCT*>(lparam));
        case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:{SetBkColor(HDC(wparam),RGB(255,255,255));SetTextColor(HDC(wparam),RGB(30,30,30));return LRESULT(app->background);}
        case WM_ERASEBKGND:{RECT rect{};GetClientRect(window,&rect);paint_backdrop(HDC(wparam),rect,false);return 1;}
        case WM_TIMER:if(wparam==2)app->poll_capture();else app->refresh_state();return 0;
        case WM_APP+5:{auto found=std::find_if(app->fields.begin(),app->fields.end(),[&](const auto& field){return field.control==HWND(wparam);});if(found!=app->fields.end()&&found->capture_kind){if(app->capture)app->finish_capture();app->capture_field=int(found-app->fields.begin());app->finish_capture(0);}return 0;}
        case WM_COMMAND:switch(LOWORD(wparam)){
            case 101:app->save();return 0;
            case 102:app->reload();return 0;
            case 103:fs::create_directories(app->config.parent_path()/L"Saves");ShellExecuteW(window,L"open",(app->config.parent_path()/L"Saves").c_str(),nullptr,nullptr,SW_SHOW);return 0;
            case 104:app->devices();return 0;
            case 105:fs::create_directories(app->root/L"build/windowed-state");ShellExecuteW(window,L"open",(app->root/L"build/windowed-state").c_str(),nullptr,nullptr,SW_SHOW);return 0;
            case 106:app->save();launch_game(app->root);DestroyWindow(window);return 0;
            case 110:DestroyWindow(window);return 0;
            case 111:app->view(true);return 0;
            case 112:app->view(false);return 0;
            case 114:if(app->capture)app->finish_capture();app->current=2;SetScrollPos(app->pages[2].window,SB_VERT,0,TRUE);SendMessageW(app->pages[2].window,WM_SIZE,0,0);ShowWindow(app->pages[1].window,SW_HIDE);ShowWindow(app->pages[2].window,SW_SHOW);return 0;
            case 115:app->show_page();return 0;
            case 120:case 121:case 122:app->save_action(LOWORD(wparam));return 0;
            case 123:{auto folder=app->config.parent_path()/L"Save Backups";fs::create_directories(folder);ShellExecuteW(window,L"open",folder.c_str(),nullptr,nullptr,SW_SHOW);return 0;}
            default:if(HIWORD(wparam)==CBN_SELCHANGE){for(auto& field:app->fields)if(field.control==HWND(lparam)&&field.key=="gh2_controller_api"){if(app->capture)app->finish_capture();app->devices();return 0;}}if(LOWORD(wparam)>=2000&&HIWORD(wparam)==BN_CLICKED)app->start_capture(HWND(lparam));return 0;
        }break;
        case WM_CLOSE:DestroyWindow(window);return 0;
        case WM_DESTROY:PostQuitMessage(0);return 0;
        }
    }catch(const std::exception& error){app->error(error);return 0;}
    return DefWindowProcW(window,message,wparam,lparam);
}
int self_test(const fs::path& root){
    const auto file=root/L"build/launcher-tests/settings.toml";fs::create_directories(file.parent_path());
    toml::table initial{{"unknown_advanced_setting",std::string("keep me")},{"log_file",std::string(R"(C:\Users\Test User\GH2PC\gh2.log)")},{"audio_offset_ms",int64_t(35)}};
    {std::ofstream output(file);output<<initial;}
    gh2::launcher::save(file,{{"resolution_scale",int64_t(3)},{"gh2_hyperspeed",int64_t(1)},{"swap_post_effect",std::string("fxaa_extreme")},{"present_effect",std::string("fsr")},{"player_one_device_key",std::string("dinput:{TEST}")}});
    const auto read=gh2::launcher::load(file);
    if(read["present_effect"].value_or(std::string())!="bilinear")throw std::runtime_error("Legacy upscaler migration failed");
    if(read["resolution_scale"].value_or(0)!=3||read["draw_resolution_scale_x"].value_or(0)!=3||read["draw_resolution_scale_y"].value_or(0)!=3||read["audio_offset_ms"].value_or(0)!=35||read["unknown_advanced_setting"].value_or(std::string())!="keep me"||read["log_file"].value_or(std::string())!=R"(C:\Users\Test User\GH2PC\gh2.log)")throw std::runtime_error("Launcher settings regression failed");
    const auto before=fs::file_size(file);bool rejected=false;{std::ofstream output(file);output<<"broken = \"C:\\Users\"\n";}
    try{gh2::launcher::save(file,{{"resolution_scale",int64_t(1)}});}catch(const toml::parse_error&){rejected=true;}
    if(!rejected)throw std::runtime_error("Invalid settings were overwritten");
    {std::ofstream output(file);output<<read;}
    std::ofstream report(root/L"docs/launcher_validation.json");report<<"{\"result\":\"pass\",\"unknown_settings_preserved\":true,\"windows_path_round_trip\":true,\"scale_axes_synchronized\":true,\"invalid_config_protected\":true,\"scope\":\"Workspace fixtures only; game launch and Documents settings not modified.\"}";
    return before?0:1;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR command,int){
    try{
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);App state;app=&state;state.root=fs::path(executable).parent_path();
        if(std::wstring(command)==L"--self-test")return self_test(state.root);
        const bool ui_test=std::wstring(command)==L"--ui-test",preview=std::wstring(command)==L"--preview";
        state.config=ui_test||preview?state.root/L"build/launcher-tests/settings.toml":gh2::documents_root()/L"gh2.toml";
        if(!ui_test&&!preview)gh2::repair_legacy_config_path(state.config);state.loaded=gh2::launcher::load(state.config);state.scale=GetDpiForSystem();
        INITCOMMONCONTROLSEX common{sizeof(common),ICC_TAB_CLASSES|ICC_WIN95_CLASSES};InitCommonControlsEx(&common);
        state.font=CreateFontW(-state.px(15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        state.title_font=CreateFontW(-state.px(42),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Impact");
        load_logo(state,instance);
        WNDCLASSW main_class{};main_class.hInstance=instance;main_class.lpfnWndProc=MainProc;main_class.lpszClassName=L"GH2PCLauncher";main_class.hCursor=LoadCursorW(nullptr,IDC_ARROW);main_class.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));RegisterClassW(&main_class);
        main_class.lpfnWndProc=PageProc;main_class.lpszClassName=L"GH2PCLauncherPage";main_class.hbrBackground=state.background;RegisterClassW(&main_class);
        RECT available{};SystemParametersInfoW(SPI_GETWORKAREA,0,&available,0);
        HWND window=CreateWindowExW(WS_EX_CONTROLPARENT,L"GH2PCLauncher",L"Guitar Hero 2 Launcher",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,std::min(state.px(440),int(available.right-available.left-30)),std::min(state.px(560),int(available.bottom-available.top-30)),nullptr,nullptr,instance,nullptr);
        if(!window)throw std::runtime_error("Could not create the launcher window.");
        SendMessageW(window,WM_SETICON,ICON_BIG,LPARAM(LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),0)));
        SendMessageW(window,WM_SETICON,ICON_SMALL,LPARAM(LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),0)));
        state.tooltips=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,0,0,0,0,window,nullptr,instance,nullptr);
        if(!state.tooltips)throw std::runtime_error("Tooltip window could not be created.");SendMessageW(state.tooltips,TTM_SETMAXTIPWIDTH,0,state.px(440));SendMessageW(state.tooltips,TTM_SETDELAYTIME,TTDT_INITIAL,350);
        state.tabs=state.widget(-1,WC_TABCONTROLW,L"",WS_TABSTOP|TCS_OWNERDRAWFIXED,24,98,920,580);
        SetWindowSubclass(state.tabs,NavigationProc,2,0);
        const wchar_t* names[]={L"Graphics",L"Controls",L"Calibration",L"Cheats",L"Saves"};
        for(int i=0;i<5;++i){TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=const_cast<wchar_t*>(names[i]);TabCtrl_InsertItem(state.tabs,i,&item);}
        for(int i=0;i<6;++i)state.pages[i].window=CreateWindowExW(WS_EX_CONTROLPARENT,L"GH2PCLauncherPage",L"",WS_CHILD|WS_CLIPCHILDREN|WS_VSCROLL|WS_HSCROLL,0,0,0,0,window,nullptr,instance,nullptr);
        state.status=state.widget(-1,L"STATIC",L"Settings are shared with the game in Documents\\GH2PC.",SS_LEFT,24,704,850,30);
        state.save_button=state.widget(-1,L"BUTTON",L"Save settings",BS_PUSHBUTTON|WS_TABSTOP,24,750,130,34,101);
        state.widget(-1,L"BUTTON",L"Reload settings",BS_PUSHBUTTON|WS_TABSTOP,166,750,130,34,102);
        state.widget(-1,L"BUTTON",L"Back",BS_PUSHBUTTON|WS_TABSTOP,460,750,100,34,112);
        state.widget(-1,L"BUTTON",L"SETTINGS",BS_PUSHBUTTON|WS_TABSTOP,0,0,220,40,111);
        state.widget(-1,L"BUTTON",L"EXIT",BS_PUSHBUTTON|WS_TABSTOP,0,0,220,40,110);
        state.widget(-1,L"STATIC",L"Version 0.1.0",SS_LEFT,0,0,320,24,113);
        state.widget(-1,L"BUTTON",L"Open logs",BS_PUSHBUTTON|WS_TABSTOP,450,750,120,34,105);
        state.play_button=state.widget(-1,L"BUTTON",L"Play Guitar Hero II",BS_DEFPUSHBUTTON|WS_TABSTOP,740,750,230,34,106);
        state.populate();state.ready=true;state.layout();state.show_page();state.refresh_state();SetTimer(window,1,1000,nullptr);
        if(ui_test){
            if(!SendMessageW(window,WM_GETICON,ICON_BIG,0)||!SendMessageW(window,WM_GETICON,ICON_SMALL,0))throw std::runtime_error("Launcher icon resource did not load");
            for(auto& field:state.fields)state.read(field);
            state.view(true);for(int page=0;page<5;++page){TabCtrl_SetCurSel(state.tabs,page);state.show_page();if(!IsWindowVisible(state.pages[state.current].window)&&IsWindowVisible(window))throw std::runtime_error("Tab did not become visible");if(GetScrollPos(state.pages[state.current].window,SB_VERT)!=0)throw std::runtime_error("Tab did not open at the top");}
            TabCtrl_SetCurSel(state.tabs,1);state.show_page();SendMessageW(window,WM_COMMAND,114,0);if(state.current!=2)throw std::runtime_error("DirectInput setup not reachable from Controls");SendMessageW(window,WM_COMMAND,115,0);if(state.current!=1)throw std::runtime_error("Controls return failed");
            if(SendMessageW(state.tooltips,TTM_GETTOOLCOUNT,0,0)!=11)throw std::runtime_error("Graphics hover explanations incomplete: "+std::to_string(SendMessageW(state.tooltips,TTM_GETTOOLCOUNT,0,0)));
            std::ofstream report(state.root/L"docs/launcher_ui_validation.json");report<<"{\"result\":\"pass\",\"tabs\":5,\"editable_fields\":"<<state.fields.size()<<",\"native_controls_created\":true,\"scope\":\"Hidden launcher UI creation and field validation only; no game launched or Documents settings changed.\"}";
            DestroyWindow(window);DeleteObject(state.font);DeleteObject(state.title_font);DeleteObject(state.background);CoUninitialize();return 0;
        }
        ShowWindow(window,SW_SHOW);UpdateWindow(window);MSG message{};
        while(GetMessageW(&message,nullptr,0,0)>0){if(state.capture&&message.message==WM_KEYDOWN&&message.wParam==VK_ESCAPE){state.finish_capture();continue;}if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
        state.capture.reset();DeleteObject(state.font);DeleteObject(state.title_font);DeleteObject(state.logo);DeleteObject(state.background);DeleteObject(state.panel);DeleteObject(state.input_brush);DeleteObject(state.red);CoUninitialize();return 0;
    }catch(const std::exception& error){
        if(std::wstring(command).starts_with(L"--")){std::ofstream output("build/launcher-test-error.txt");output<<error.what();return 1;}
        MessageBoxW(nullptr,wide(error.what()).c_str(),L"Guitar Hero 2 Launcher",MB_OK|MB_ICONERROR);return 1;
    }
}




