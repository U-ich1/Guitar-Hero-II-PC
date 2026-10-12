#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_module.h>
#include <rex/ppc/context.h>
#include "../runtime/local_profile.h"
#include <iostream>
#include <stdexcept>
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main() {
    try {
        rex::Runtime runtime("private","build/local-profile-tests");
        rex::RuntimeConfig config;config.tool_mode=true;config.kernel_init=rex::kernel::InitializeKernel;
        require(runtime.Setup(std::move(config))==0,"Runtime initialization failed");
        auto* dispatcher=runtime.function_dispatcher();
        require(dispatcher->InitializeFunctionTable(0x82000000,0x10000,0x82000000,0x1000000,true),"Test dispatch table failed");
        auto resolve=[&](const char* name)->PPCFunc* {
            for(const auto& table:runtime.export_resolver()->tables())for(auto* entry:table.exports_by_name()) {
                if(!entry||std::strcmp(entry->name,name))continue;
                auto module=runtime.kernel_state()->GetKernelModule(table.module_name());
                if(!module)continue;
                const auto address=module->GetProcAddressByOrdinal(entry->ordinal,0x82000000);
                if(address)return dispatcher->GetFunction(address);
            }
            throw std::runtime_error("Profile API not resolved");
        };
        auto signin=resolve("XamUserGetSigninState"),name=resolve("XamUserGetName"),xuid=resolve("XamUserGetXUID");
        const uint32_t buffer=runtime.memory()->SystemHeapAlloc(128);
        const uint64_t original=runtime.kernel_state()->user_profile()->xuid();
        for(unsigned active=0;active<4;++active) {
            gh2::profile::observe_buttons(active,0x1000,0);
            const unsigned changes=gh2::profile::sign_in();
            require(changes&(1U<<active),"Sign-in notification missed new controller");
            for(unsigned user=0;user<4;++user) {
                PPCContext context{};context.r3.u32=gh2::profile::runtime_user(user);
                signin(context,runtime.virtual_membase());require(context.r3.u32==(user==active?1U:0U),"More than one controller owns the profile or active player unsigned");
            }
            PPCContext context{};context.r3.u32=gh2::profile::runtime_user(active);context.r4.u32=7;context.r5.u32=buffer;
            xuid(context,runtime.virtual_membase());require(context.r3.u32==0,"Active controller XUID query failed");
            require(rex::memory::load_and_swap<uint64_t>(runtime.memory()->TranslateVirtual(buffer))==original,"Signing in changed save identity");
            context={};context.r3.u32=gh2::profile::runtime_user(active);context.r4.u32=buffer;context.r5.u32=16;
            name(context,runtime.virtual_membase());require(context.r3.u32==0&&std::strcmp(reinterpret_cast<char*>(runtime.memory()->TranslateVirtual(buffer)),"User")==0,"Local profile name query failed");
        }
        const auto before=gh2::profile::last_button_player.load();
        gh2::profile::observe_buttons(0,0x1000,0x1000);require(gh2::profile::last_button_player.load()==before,"Held button stole profile selection");
        require(gh2::profile::runtime_user(0xFF)==0xFF,"Invalid user was turned into a valid profile");
        runtime.memory()->SystemHeapFree(buffer);runtime.Shutdown();
        std::cout<<"PASS: four controller ports activate one offline profile; sign-in/name/XUID queries succeed; old ports unsigned; original save XUID retained.\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
