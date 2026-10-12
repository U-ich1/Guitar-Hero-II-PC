#pragma once
#include "discord_presence_state.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include "../tools/rexglue-sdk/thirdparty/inja/third_party/include/nlohmann/json.hpp"
#include <thread>
#include <fstream>
#include <map>
#include <vector>
namespace gh2::discord {
inline std::atomic_bool enabled{true};
inline auto registration=rex::cvar::FlagRegistrar(rex::cvar::FlagEntry{
    "gh2_discord_presence",rex::cvar::FlagType::Boolean,"General/Discord","Show mode, song and difficulty on Discord",
    [](std::string_view s){if(s!="true"&&s!="false"&&s!="1"&&s!="0")return false;enabled=s=="true"||s=="1";return true;},
    []{return enabled.load()?"true":"false";},{},rex::cvar::Lifecycle::kHotReload,{},"true"});
class Pipe {
    HANDLE pipe_=INVALID_HANDLE_VALUE;
public:
    static std::string Text(const nlohmann::json& reply,const char* key) {
        const auto found=reply.find(key);return found!=reply.end()&&found->is_string()?found->get<std::string>():std::string();
    }
    ~Pipe(){Close();}
    void Close(){if(pipe_!=INVALID_HANDLE_VALUE){CancelIoEx(pipe_,nullptr);CloseHandle(pipe_);pipe_=INVALID_HANDLE_VALUE;}}
    bool Open(const std::wstring& name){Close();pipe_=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);return pipe_!=INVALID_HANDLE_VALUE;}
    bool Transfer(void* buffer,DWORD length,bool write,std::stop_token stop) {
        auto* cursor=static_cast<uint8_t*>(buffer);const auto deadline=GetTickCount64()+1500;
        while(length&&!stop.stop_requested()) {
            OVERLAPPED operation{};operation.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!operation.hEvent)return false;
            DWORD done=0;BOOL okay=write?WriteFile(pipe_,cursor,length,&done,&operation):ReadFile(pipe_,cursor,length,&done,&operation);
            if(!okay&&GetLastError()==ERROR_IO_PENDING) {
                while(!stop.stop_requested()&&GetTickCount64()<deadline&&WaitForSingleObject(operation.hEvent,50)==WAIT_TIMEOUT){}
                if(stop.stop_requested()||GetTickCount64()>=deadline){CancelIoEx(pipe_,&operation);WaitForSingleObject(operation.hEvent,INFINITE);}
                okay=GetOverlappedResult(pipe_,&operation,&done,FALSE);
            }
            CloseHandle(operation.hEvent);
            if(!okay||!done||stop.stop_requested()||done>length)return false;
            cursor+=done;length-=done;
            // Discord requires one contiguous write for a framed payload.
            if(write&&length)return false;
        }
        return !length;
    }
    bool Send(uint32_t opcode,std::string_view body,std::stop_token stop) {
        if(body.size()>65536)return false;
        std::vector<uint8_t> packet(8+body.size());uint32_t size=uint32_t(body.size());
        std::memcpy(packet.data(),&opcode,4);std::memcpy(packet.data()+4,&size,4);std::memcpy(packet.data()+8,body.data(),body.size());
        return Transfer(packet.data(),DWORD(packet.size()),true,stop);
    }
    bool Receive(nlohmann::json& reply,std::stop_token stop) {
        for(unsigned messages=0;messages<8;++messages) {
            uint32_t header[2];if(!Transfer(header,8,false,stop)||header[1]>65536)return false;
            std::string body(header[1],'\0');if(header[1]&&!Transfer(body.data(),header[1],false,stop))return false;
            if(header[0]==3){if(!Send(4,body,stop))return false;continue;}
            if(header[0]!=1)return false;
            reply=nlohmann::json::parse(body,nullptr,false);return !reply.is_discarded()&&reply.is_object();
        }return false;
    }
    bool Ready(std::stop_token stop) {
        if(!Send(0,"{\"v\":1,\"client_id\":\""+std::string(ApplicationId)+"\"}",stop))return false;
        nlohmann::json reply;return Receive(reply,stop)&&Text(reply,"evt")=="READY";
    }
    bool Activity(std::string_view body,uint64_t nonce,std::stop_token stop) {
        if(!Send(1,command(body,GetCurrentProcessId(),nonce),stop))return false;
        nlohmann::json reply;
        if(!Receive(reply,stop))return false;
        if(Text(reply,"evt")=="ERROR") {
            REXLOG_WARN("GH2 Discord: activity rejected by Discord.");return false;
        }
        return Text(reply,"nonce")==std::to_string(nonce)&&Text(reply,"cmd")=="SET_ACTIVITY";
    }
};
class Presence {
    std::jthread worker_;
public:
    void Start(const std::filesystem::path& song_file) {
        std::map<std::string,std::string> titles;std::ifstream input(song_file);std::string line;
        while(std::getline(input,line)){const auto tab=line.find('\t');if(tab!=std::string::npos)titles.emplace(line.substr(0,tab),line.substr(tab+1));}
        worker_=std::jthread([titles=std::move(titles)](std::stop_token stop) {
            Pipe pipe;bool connected=false,was_enabled=false;uint64_t nonce=0;ULONGLONG retry=0,last_sent=0;std::string previous;
            while(!stop.stop_requested()) {
                const auto now=GetTickCount64();const bool active=enabled.load();
                if(!active){if(connected&&was_enabled)pipe.Activity("null",++nonce,stop);pipe.Close();connected=false;previous.clear();retry=0;}
                else if(!connected&&now>=retry) {
                    for(unsigned i=0;i<10&&!stop.stop_requested();++i)
                        if(pipe.Open(L"\\\\?\\pipe\\discord-ipc-"+std::to_wstring(i))&&pipe.Ready(stop)){connected=true;previous.clear();REXLOG_INFO("GH2 Discord: Rich Presence connected.");break;}
                    if(!connected){pipe.Close();retry=now+15000;}
                }
                if(active&&connected&&now-last_sent>=2000) {
                    Snapshot current;{std::lock_guard lock(snapshot_mutex);current=snapshot;}
                    auto found=titles.find(current.song);auto body=activity(current,found==titles.end()?current.song:found->second);
                    if(body!=previous||now-last_sent>=15000) {
                        if(pipe.Activity(body,++nonce,stop)){previous=body;last_sent=now;}
                        else {pipe.Close();connected=false;retry=now+15000;}
                    }
                }
                was_enabled=active;for(unsigned tick=0;tick<5&&!stop.stop_requested();++tick)std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            // Closing the IPC session removes its presence without a shutdown wait.
            pipe.Close();
        });
    }
    void Stop(){worker_.request_stop();if(worker_.joinable())worker_.join();}
    ~Presence(){Stop();}
};
}
