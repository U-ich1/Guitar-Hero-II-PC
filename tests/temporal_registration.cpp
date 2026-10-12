#include <rex/cvar.h>
#include <windows.h>
#include <iostream>
int main() {
    // Load renderer registrations without creating a window or starting GH2.
    if(!LoadLibraryW(L"rexgpu-xenos.dll")){std::cerr<<"Renderer load failed: "<<GetLastError()<<'\n';return 1;}
    if(!rex::cvar::InvokeCommand("gh2_capture_dlss_inputs","build/temporal-registration.jsonl")) {
        std::cerr<<"Temporal capture command unavailable\n";return 1;
    }
    if(!rex::cvar::InvokeCommand("gh2_probe_temporal_renderer","")) {
        std::cerr<<"Native depth/history producer command unavailable\n";return 1;
    }
    std::cout<<"PASS: actual renderer DLL registers temporal capture and opt-in native depth/history producer commands. No game launched.\n";
}
