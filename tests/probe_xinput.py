"""Read actual XInput device subtype and button state without modifying it."""
import ctypes
import json
from pathlib import Path

class Pad(ctypes.Structure):
    _fields_ = [('buttons', ctypes.c_uint16), ('lt', ctypes.c_uint8), ('rt', ctypes.c_uint8),
                ('lx', ctypes.c_int16), ('ly', ctypes.c_int16), ('rx', ctypes.c_int16), ('ry', ctypes.c_int16)]
class Vibration(ctypes.Structure):
    _fields_ = [('left', ctypes.c_uint16), ('right', ctypes.c_uint16)]
class Caps(ctypes.Structure):
    _fields_ = [('type', ctypes.c_uint8), ('subtype', ctypes.c_uint8), ('flags', ctypes.c_uint16), ('pad', Pad), ('vibration', Vibration)]
class State(ctypes.Structure):
    _fields_ = [('packet', ctypes.c_uint32), ('pad', Pad)]
dll = ctypes.WinDLL('xinput1_4.dll')
dll.XInputGetCapabilities.argtypes = [ctypes.c_uint32, ctypes.c_uint32, ctypes.POINTER(Caps)]
dll.XInputGetCapabilities.restype = ctypes.c_uint32
dll.XInputGetState.argtypes = [ctypes.c_uint32, ctypes.POINTER(State)]
dll.XInputGetState.restype = ctypes.c_uint32
report = []
for slot in range(4):
    caps, state = Caps(), State()
    result = dll.XInputGetCapabilities(slot, 0, ctypes.byref(caps))
    state_result = dll.XInputGetState(slot, ctypes.byref(state))
    report.append(dict(slot=slot, result=hex(result), type=caps.type, subtype=caps.subtype,
                       state_result=hex(state_result), buttons=hex(state.pad.buttons), left_trigger=state.pad.lt, right_trigger=state.pad.rt))
Path('docs/xinput_device_validation.json').write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
