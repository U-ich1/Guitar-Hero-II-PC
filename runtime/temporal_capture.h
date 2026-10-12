#pragma once
// Read-only renderer observations. These are candidates, not validated DLSS inputs.
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace gh2::temporal {
struct Constant { uint32_t index; std::array<uint32_t,4> bits; };
struct Fetch { uint32_t index; std::array<uint32_t,2> bits; };
struct Draw {
    uint64_t vertex_shader=0, pixel_shader=0;
    uint32_t vertices=0, primitive=0, depth_control=0, depth_info=0;
    uint32_t surface_info=0, color_mask=0, vertex_constant_base=0;
    std::array<uint32_t,4> color_info{};
    std::array<uint32_t,6> viewport{};
    std::array<uint32_t,2> scissor{};
    uint64_t depth_width=0;
    uint32_t depth_height=0, depth_format=0, depth_samples=0;
    bool host_targets=false;
    std::vector<Constant> constants;
    std::vector<Fetch> fetches;
};
struct Frame {
    uint64_t sequence=0;
    uint32_t input_width=0,input_height=0,seen_draws=0;
    std::vector<Draw> draws;
};
// The command is invoked on the UI thread; consumption is on the GPU worker.
class Mailbox {
    std::atomic<bool> pending_{false};
    std::mutex mutex_;
    std::filesystem::path path_;
public:
    bool Request(const std::filesystem::path& path) {
        if(path.empty() || path.extension()!=L".jsonl")return false;
        std::lock_guard lock(mutex_);
        if(pending_.load(std::memory_order_relaxed))return false;
        path_=path;pending_.store(true,std::memory_order_release);return true;
    }
    std::optional<std::filesystem::path> Take() {
        if(!pending_.load(std::memory_order_acquire))return {};
        std::lock_guard lock(mutex_);
        auto result=std::move(path_);pending_.store(false,std::memory_order_release);return result;
    }
};
inline Mailbox requests;

inline void Write(const std::filesystem::path& path,const std::vector<Frame>& frames,
                  bool complete,std::atomic<int>& status) noexcept {
    try {
        if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
        auto temporary=path;temporary+=L".partial";
        std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
        out<<"{\"schema\":1,\"kind\":\"gh2-temporal-input-candidates\",\"complete\":"
           <<(complete?"true":"false")<<",\"validated_dlss_inputs\":false}\n";
        for(const auto& frame:frames) {
            out<<"{\"frame\":"<<frame.sequence<<",\"input\":["<<frame.input_width<<','<<frame.input_height
               <<"],\"seen_draws\":"<<frame.seen_draws<<",\"draws\":[";
            bool first=true;
            for(const auto& d:frame.draws) {
                if(!first)out<<',';first=false;
                out<<"{\"vs\":"<<d.vertex_shader<<",\"ps\":"<<d.pixel_shader
                   <<",\"vertices\":"<<d.vertices<<",\"primitive\":"<<d.primitive
                   <<",\"depth_control\":"<<d.depth_control<<",\"depth_info\":"<<d.depth_info
                   <<",\"surface_info\":"<<d.surface_info<<",\"color_mask\":"<<d.color_mask
                   <<",\"vs_constant_base\":"<<d.vertex_constant_base
                   <<",\"host_targets\":"<<(d.host_targets?"true":"false")
                   <<",\"depth_resource\":["<<d.depth_width<<','<<d.depth_height<<','<<d.depth_format<<','<<d.depth_samples<<']';
                auto array=[&](const char* name,const auto& a){out<<",\""<<name<<"\":[";bool first=true;for(auto v:a){if(!first)out<<',';first=false;out<<v;}out<<']';};
                array("color_info",d.color_info);array("viewport_bits",d.viewport);array("scissor",d.scissor);
                out<<",\"constants\":[";bool cf=true;
                for(const auto& c:d.constants){if(!cf)out<<',';cf=false;out<<'['<<c.index;for(auto b:c.bits)out<<','<<b;out<<']';}
                out<<"],\"fetches\":[";bool ff=true;
                for(const auto& f:d.fetches){if(!ff)out<<',';ff=false;out<<'['<<f.index<<','<<f.bits[0]<<','<<f.bits[1]<<']';}
                out<<"]}";
            }
            out<<"]}\n";
        }
        out.flush();if(!out)throw std::runtime_error("capture write failed");out.close();
        std::filesystem::rename(temporary,path);
        status.store(1,std::memory_order_release);
    }catch(...){status.store(-1,std::memory_order_release);}
}

class Capture {
    std::filesystem::path path_;
    std::vector<Frame> frames_;
    Frame current_;
    uint64_t sequence_=0;
    bool active_=false;
    bool reported_=false;
    std::atomic<int> writer_status_{0};
    std::jthread writer_;
    void Finish(bool complete) {
        active_=false;
        auto frames=std::move(frames_);auto path=path_;
        writer_=std::jthread([this,path=std::move(path),frames=std::move(frames),complete]{Write(path,frames,complete,writer_status_);});
    }
public:
    static constexpr size_t kFrames=16,kDraws=2048;
    ~Capture(){if(active_&&!frames_.empty())Finish(false);if(writer_.joinable())writer_.join();}
    bool Start(const std::filesystem::path& path) {
        if(active_ || (writer_.joinable()&&writer_status_.load(std::memory_order_acquire)==0))return false;
        if(writer_.joinable())writer_.join();
        frames_.clear();frames_.reserve(kFrames);current_=Frame{};current_.sequence=sequence_;
        path_=path;writer_status_.store(0,std::memory_order_relaxed);reported_=false;active_=true;return true;
    }
    bool WantsDraw() {
        if(!active_)return false;
        ++current_.seen_draws;
        return current_.draws.size()<kDraws;
    }
    void AddDraw(Draw draw){current_.draws.push_back(std::move(draw));}
    void Swap(uint32_t width,uint32_t height) {
        if(active_) {
            current_.input_width=width;current_.input_height=height;
            frames_.push_back(std::move(current_));
            if(frames_.size()==kFrames)Finish(true);
        }
        ++sequence_;
        if(active_){current_=Frame{};current_.sequence=sequence_;}
    }
    bool Active()const{return active_;}
    int WriterStatus()const{return writer_status_.load(std::memory_order_acquire);}
    int TakeCompletion() {
        const int status=WriterStatus();
        if(!status || reported_)return 0;
        reported_=true;return status;
    }
};
}
