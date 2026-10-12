#include <rex/input/input_system.h>
#include <rex/input/xinput/xinput_input_driver.h>
#include <iostream>
#include <stdexcept>
using namespace rex;
using namespace rex::input;

class Fixture final : public InputDriver {
public:
    DeviceId id;
    bool synthetic;
    uint8_t subtype;
    uint16_t buttons;
    bool connected = true;
    Fixture(uint64_t key, bool keyboard, uint8_t type, uint16_t state)
        : InputDriver(nullptr, 0), id(static_cast<DeviceId>(key)), synthetic(keyboard), subtype(type), buttons(state) {}
    X_STATUS Setup() override { return X_STATUS_SUCCESS; }
    void EnumerateDevices(std::vector<DeviceInfo>& out) override {
        if (!connected) return;
        DeviceInfo info; info.id = id; info.synthetic = synthetic;
        out.push_back(info);
    }
    X_RESULT GetDeviceCapabilities(DeviceId, uint32_t, X_INPUT_CAPABILITIES* caps) override {
        *caps = {}; caps->type = 1; caps->sub_type = subtype;
        return X_ERROR_SUCCESS;
    }
    X_RESULT GetDeviceState(DeviceId, X_INPUT_STATE* state) override {
        *state = {}; state->gamepad.buttons = buttons;
        return X_ERROR_SUCCESS;
    }
    X_RESULT SetDeviceVibration(DeviceId, X_INPUT_VIBRATION*) override { return X_ERROR_SUCCESS; }
    X_RESULT GetDeviceKeystroke(DeviceId, uint32_t, X_INPUT_KEYSTROKE*) override { return X_ERROR_EMPTY; }
};
int main(int argc, char**) {
    try {
        for (uint8_t subtype : {6, 7, 11}) {
            InputSystem input(nullptr);
            auto guitar = std::make_unique<Fixture>(1, false, subtype, 0x1000);
            auto* physical = guitar.get();
            input.AddDriver(std::move(guitar));
            input.AddDriver(std::make_unique<Fixture>(2, true, 1, 0x10));
            input.SetDeviceAssignment(std::make_unique<SlotAssignment>());
            input.Setup();
            X_INPUT_STATE state{};
            X_INPUT_CAPABILITIES caps{};
            if (input.GetState(0, &state) != X_ERROR_SUCCESS || uint16_t(state.gamepad.buttons) != 0x1010)
                throw std::runtime_error("Input state merge changed button bits");
            // The keyboard was observed last and is the active device. The
            // connected guitar must still determine the game's fret scheme.
            if (input.GetCapabilities(0, 0, &caps) != X_ERROR_SUCCESS || caps.sub_type != subtype)
                throw std::runtime_error("Keyboard replaced physical guitar identity");
            physical->connected = false;
            if (input.GetCapabilities(0, 0, &caps) != X_ERROR_SUCCESS || caps.sub_type != 1)
                throw std::runtime_error("Keyboard fallback failed after guitar disconnect");
            input.Shutdown();
        }
        std::cout << "PASS: guitar subtype 6/7/11 survives active keyboard; button bits preserved; disconnect fallback works.\n";
        if (argc > 1) {
            InputSystem input(nullptr);
            auto driver = std::make_unique<rex::input::xinput::XinputInputDriver>(nullptr, 0);
            if (driver->Setup() != X_STATUS_SUCCESS) throw std::runtime_error("Native XInput driver setup failed");
            input.AddDriver(std::move(driver));
            input.SetDeviceAssignment(std::make_unique<SlotAssignment>());
            input.Setup();
            X_INPUT_CAPABILITIES caps{};
            if (input.GetCapabilities(0, 0, &caps) != X_ERROR_SUCCESS)
                throw std::runtime_error("No physical XInput device at guest player 1");
            std::cout << "Physical player 1 subtype: " << unsigned(caps.sub_type) << '\n';
            if (caps.sub_type != 6 && caps.sub_type != 7 && caps.sub_type != 11)
                throw std::runtime_error("Physical device did not reach guest as guitar");
            std::cout << "PASS: actual connected XInput guitar reaches guest capability API.\n";
            input.Shutdown();
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
