#include "../runtime/discord_presence.h"
#include <iostream>
#include <future>
using namespace gh2::discord;
static void check(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
int main(){try {
    const auto parsed=nlohmann::json::parse(activity({1,"career","surrender","expert"},"Surrender"));
    check(parsed["details"]=="Surrender"&&parsed["state"]=="Career | Expert","Song activity incorrect");
    check(nlohmann::json::parse(activity({2,"practice","x","kDifficultyHard"},"A \"quoted\" song"))["state"]=="Practice | Hard | Paused","Pause/difficulty incorrect");
    check(nlohmann::json::parse(activity({0,"quickplay","stale","expert"},"Old song"))["details"]=="In menus","Stale song escaped menus");
    check(nlohmann::json::parse(activity({3,"multi_coop","x","medium"},"X"))["state"]=="Co-op | Medium | Results","Results incorrect");
    check(nlohmann::json::parse(json_quote("quote\" slash\\ newline\n"))=="quote\" slash\\ newline\n","JSON escaping incorrect");
    std::string utf8;for(unsigned i=0;i<100;++i)utf8+="\xc3\xa9";
    check(nlohmann::json::parse(json_quote(utf8)).get<std::string>().size()==120,"UTF-8 truncated incorrectly");
    std::array<uint8_t,512> memory{};auto store=[&](unsigned offset,uint32_t value){value=std::byteswap(value);std::memcpy(memory.data()+offset,&value,4);};
    for(unsigned i=0;i<5;++i){std::strcpy(reinterpret_cast<char*>(memory.data()+64+i*32),std::string(variable_names[i]).c_str());observe_variable(64+i*32,16+i*8,memory.data());}
    // A cache hit must still refresh the node, and direct-table collisions
    // must not classify a newly encountered interned symbol incorrectly.
    observe_variable(64,104,memory.data());check(nodes[0]==104,"Known symbol did not refresh its node");
    observe_variable(64,16,memory.data());
    std::strcpy(reinterpret_cast<char*>(memory.data()+448),"unrelated");
    for(unsigned i=0;i<100;++i)observe_variable(448,200,memory.data());
    check(nodes[0]==16,"Unrelated cached symbol replaced a presence node");
    store(16,1);store(20,0);
    for(unsigned i=1;i<5;++i){store(16+i*8,256+i*32);store(20+i*8,5);}
    std::strcpy(reinterpret_cast<char*>(memory.data()+288),"career");std::strcpy(reinterpret_cast<char*>(memory.data()+320),"surrender");std::strcpy(reinterpret_cast<char*>(memory.data()+352),"expert");std::strcpy(reinterpret_cast<char*>(memory.data()+384),"small1");
    sample(memory.data());check(snapshot.status==1&&snapshot.song=="surrender"&&snapshot.difficulty=="expert"&&snapshot.venue=="small1","Guest snapshot ABI incorrect");
    check(nlohmann::json::parse(activity({1,"career","surrender","expert","small1"},"Surrender"))["assets"]["large_image"]=="venue_small1","Venue asset incorrect");
    check(nlohmann::json::parse(activity({0,"quickplay","old","expert","small1"},"Old"))["assets"]["large_image"]=="gh2_logo","Stale venue escaped menu");
    check(nlohmann::json::parse(activity({2,"practice","x","hard","stone"},"X"))["assets"]["large_text"]=="Stonehenge","Pause venue incorrect");
    for(auto venue:{"battle","small1","small2","big","theatre","fest","arena","stone"})for(int status=1;status<=3;++status)
        check(nlohmann::json::parse(activity({status,"quickplay","x","expert",venue},"X"))["assets"]["large_image"]==std::string("venue_")+venue,"Venue/status image mismatch");
    check(nlohmann::json::parse(activity({1,"quickplay","x","expert","unknown"},"X"))["assets"]["large_image"]=="gh2_logo","Unknown venue fallback incorrect");
    // A private mock pipe, never Discord: exercise real framing and replies.
    const std::wstring name=L"\\\\?\\pipe\\gh2-presence-test-"+std::to_wstring(GetCurrentProcessId());
    HANDLE server=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,65536,65536,0,nullptr);
    check(server!=INVALID_HANDLE_VALUE,"Mock pipe creation failed");
    std::promise<std::string> result;auto future=result.get_future();
    std::jthread peer([&]{try {
        check(ConnectNamedPipe(server,nullptr)||GetLastError()==ERROR_PIPE_CONNECTED,"Mock connection failed");
        auto read=[&]{uint32_t header[2];DWORD count;check(ReadFile(server,header,8,&count,nullptr)&&count==8,"Header read failed");check(header[1]<=65536,"Bad frame length");
            std::string body(header[1],'\0');check(ReadFile(server,body.data(),header[1],&count,nullptr)&&count==header[1],"Body read failed");return std::pair(header[0],nlohmann::json::parse(body));};
        auto send=[&](uint32_t opcode,std::string body){std::vector<uint8_t> frame(8+body.size());uint32_t size=uint32_t(body.size());std::memcpy(frame.data(),&opcode,4);std::memcpy(frame.data()+4,&size,4);std::memcpy(frame.data()+8,body.data(),body.size());DWORD count;check(WriteFile(server,frame.data(),DWORD(frame.size()),&count,nullptr)&&count==frame.size(),"Mock write failed");};
        auto handshake=read();check(handshake.first==0&&handshake.second["client_id"]==ApplicationId,"Handshake incorrect");send(1,"{\"evt\":\"READY\"}");
        auto request=read();check(request.first==1&&request.second["cmd"]=="SET_ACTIVITY","Activity command incorrect");
        check(request.second["args"]["pid"]==GetCurrentProcessId(),"PID incorrect");
        send(3,"{\"ping\":1}");auto pong=read();check(pong.first==4,"Ping not answered");
        send(1,"{\"cmd\":\"SET_ACTIVITY\",\"evt\":null,\"nonce\":\"7\"}");
        auto clear=read();check(clear.second["args"]["activity"].is_null(),"Disabled activity not cleared");send(1,"{\"cmd\":\"SET_ACTIVITY\",\"evt\":null,\"nonce\":\"8\"}");
        result.set_value("");
    }catch(const std::exception& e){result.set_value(e.what());}DisconnectNamedPipe(server);});
    Pipe pipe;std::stop_source stop;check(pipe.Open(name)&&pipe.Ready(stop.get_token()),"Handshake failed");
    check(pipe.Activity(activity({1,"career","surrender","expert"},"Surrender"),7,stop.get_token()),"Activity ACK failed");
    check(pipe.Activity("null",8,stop.get_token()),"Clear ACK failed");pipe.Close();
    const auto error=future.get();peer.join();CloseHandle(server);check(error.empty(),error.c_str());
    check(!pipe.Open(L"\\\\?\\pipe\\gh2-presence-absent"),"Absent pipe unexpectedly exists");
    std::cout<<"PASS: guest snapshot ABI, mode/song/difficulty, pause/results/menu clearing, UTF-8/JSON escaping, application ID, real named-pipe handshake, ping/pong, null-event ACK and disabling. No GH2 or Discord launched.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
