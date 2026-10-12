#pragma once
#include <rex/input/device_assignment.h>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <array>

namespace gh2::controls {
// 0: automatic, 1..4: native XInput slot 0..3 feeds guest player one.
inline std::atomic<int> player_one_device{0};
// Both APIs remain discoverable; this filters which physical devices can feed
// gameplay while retaining the keyboard and synthetic fallback devices.
inline std::atomic<int> controller_api{0}; // 0 auto, 1 XInput, 2 DirectInput
inline std::mutex catalog_mutex;
inline std::vector<rex::input::DeviceInfo> device_catalog;
inline std::string selected_device_key;
inline std::string selected_key(){std::lock_guard lock(catalog_mutex);return selected_device_key;}
inline std::vector<rex::input::DeviceInfo> catalog(){std::lock_guard lock(catalog_mutex);return device_catalog;}
inline void publish_devices(const std::vector<rex::input::DeviceInfo>& devices) {
    std::lock_guard lock(catalog_mutex);device_catalog.clear();
    for(const auto& device:devices)if(!device.synthetic)device_catalog.push_back(device);
}
class ControllerAssignment final : public rex::input::DeviceAssignment {
    mutable std::mutex mutex_;
    rex::input::SlotAssignment automatic_;
    std::array<rex::input::SlotAssignment,2> api_assignments_;
    std::vector<rex::input::DeviceInfo> devices_;
public:
    void OnDevicesChanged(const std::vector<rex::input::DeviceInfo>& devices) override {
        std::lock_guard lock(mutex_);
        devices_=devices; automatic_.OnDevicesChanged(devices);
        for(unsigned i=0;i<2;++i){auto filtered=devices;std::erase_if(filtered,[&](const auto& d){return !d.synthetic&&!d.guid.starts_with(i==0?"xinput:":"dinput:");});api_assignments_[i].OnDevicesChanged(filtered);}
        publish_devices(devices);
    }
    void DevicesForUser(uint32_t user,std::vector<rex::input::DeviceId>& out) const override {
        std::lock_guard lock(mutex_);
        const int slot=player_one_device.load();
        const auto key=selected_key();
        const auto filter=[&]{const int api=controller_api.load();if(!api)return;
            std::erase_if(out,[&](auto id){for(const auto& d:devices_)if(d.id==id)
                return !d.synthetic && !(d.guid.starts_with(api==1?"xinput:":"dinput:"));return false;});};
        if(slot==0&&key.empty()){
            const int api=controller_api.load();if(api==0)automatic_.DevicesForUser(user,out);else api_assignments_[api-1].DevicesForUser(user,out);
            return;}
        auto selected=static_cast<rex::input::DeviceId>(0x58490000ULL|uint64_t(std::max(slot,1)-1));
        if(!key.empty()) {
            selected=rex::input::DeviceId::kInvalid;
            for(const auto& device:devices_)if(device.guid==key){selected=device.id;break;}
        }
        out.clear();
        if(user==0) {
            for(const auto& device:devices_)
                if(device.synthetic||device.id==selected)out.push_back(device.id);
        } else {
            const int api=controller_api.load();if(api==0)automatic_.DevicesForUser(user,out);else api_assignments_[api-1].DevicesForUser(user,out);
            std::erase(out,selected);
        }
        filter();
    }
};
}
