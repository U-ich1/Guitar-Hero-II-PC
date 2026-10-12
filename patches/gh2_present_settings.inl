#include <atomic>
#include <rex/cvar.h>
#include <charconv>
#include <limits>
namespace gh2::graphics {
static std::atomic<bool> display_vsync{false};
static std::atomic<int> display_fps_limit{0};
}
static auto gh2_display_sync_registration=rex::cvar::FlagRegistrar({
    "gh2_display_vsync",rex::cvar::FlagType::Boolean,"Graphics/Display",
    "Synchronize host display presentation to refresh; does not change GH2's gameplay clock.",
    [](std::string_view value){
        if(value!="true"&&value!="false"&&value!="1"&&value!="0")return false;
        gh2::graphics::display_vsync.store(value=="true"||value=="1");return true;
    },[]{return gh2::graphics::display_vsync.load()?"true":"false";},{},rex::cvar::Lifecycle::kHotReload,{},"false"
});
static auto gh2_display_fps_registration=rex::cvar::FlagRegistrar({
    "gh2_display_fps_limit",rex::cvar::FlagType::Int32,"Graphics/Display",
    "Maximum host presentation FPS; 0 is unlimited. Gameplay timing is unchanged.",
    [](std::string_view text){int value=0;auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
        if(error!=std::errc{}||end!=text.data()+text.size()||value<0)return false;
        gh2::graphics::display_fps_limit.store(value);return true;},
    []{return std::to_string(gh2::graphics::display_fps_limit.load());},{},rex::cvar::Lifecycle::kHotReload,{},"0"
}).range(0,std::numeric_limits<int>::max());
