// Read-only host capability probe; creates no window and renders no game frame.
#include <windows.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <iostream>
#include <iomanip>
int main() {
    IDXGIFactory1* factory = nullptr;
    HRESULT status = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
    if (FAILED(status)) return 1;
    unsigned usable = 0;
    for (UINT i = 0; ; ++i) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        if (!adapter) continue;
        DXGI_ADAPTER_DESC1 description{}; adapter->GetDesc1(&description);
        ID3D12Device* device = nullptr;
        status = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void**>(&device));
        std::wcout << description.Description << L": VRAM=" << description.DedicatedVideoMemory / (1024 * 1024)
                   << L" MiB; software=" << bool(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                   << L"; D3D12 HRESULT=0x" << std::hex << uint32_t(status) << std::dec;
        if (device) {
            D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
            if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
                std::wcout << L"; resource binding tier=" << options.ResourceBindingTier << L"; ROV=" << options.ROVsSupported;
            if (!(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) ++usable;
            device->Release();
        }
        std::wcout << L'\n'; adapter->Release();
    }
    factory->Release();
    return usable ? 0 : 2;
}
