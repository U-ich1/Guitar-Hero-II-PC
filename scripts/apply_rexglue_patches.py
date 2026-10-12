"""Reproducible, guarded fixes for the experimental LLVM-MinGW SDK build."""
from pathlib import Path
import shutil

root = Path('tools/rexglue-sdk')
for source, destination in [('runtime/audio_delay.h', 'include/rex/audio/gh2_audio_delay.h'),
                            ('patches/gh2_audio_offset.inl', 'include/rex/audio/gh2_audio_offset.inl'),
                            ('runtime/present_policy.h','include/rex/ui/gh2_present_policy.h'),
                            ('runtime/frame_limiter.h','include/rex/ui/gh2_frame_limiter.h'),
                            ('patches/gh2_present_settings.inl','include/rex/ui/gh2_present_settings.inl')]:
    contents = Path(source).read_bytes()
    target = root / destination
    if not target.exists() or target.read_bytes() != contents:
        target.write_bytes(contents)
for header in Path('patches/rexglue-headers').glob('*.h'):
    target = root / 'include/rex/ui/d3d12' / header.name
    if not target.exists() or target.read_bytes() != header.read_bytes():
        shutil.copyfile(header, target)
def replace(relative, before, after):
    path = root / relative
    source = path.read_text()
    if after in source:
        return
    if relative=='src/ui/d3d12/d3d12_presenter.cpp' and 'DXGI_PRESENT_RESTART' in before and 'gh2_present_metric' in source and 'HRESULT present_result = paint_context_.swap_chain->Present(policy.interval,policy.flags);' in source:
        return
    if relative == 'CMakeLists.txt' and 'add_library(gh2_rex_code OBJECT' in after:
        final_after = after.replace('target_link_libraries(gh2_pc PRIVATE gh2_rex_code imgui)',
            'target_link_libraries(gh2_pc PRIVATE gh2_rex_code)\ntarget_include_directories(gh2_pc PRIVATE "${REXGLUE_ROOT}/thirdparty/imgui")')
        if final_after in source:
            return
        optimized_after=final_after.replace('target_compile_options(gh2_rex_code PRIVATE -O0 -g0 -msse4.1 -mavx)',
            'target_compile_options(gh2_rex_code PRIVATE $<IF:$<CONFIG:Debug>,-O0,-O2> -g0 -msse4.1 -mavx -fwrapv -fno-strict-aliasing -ffp-model=strict)').replace('target_compile_options(gh2_pc PRIVATE -O0 -g0 -msse4.1 -mavx)',
            'target_compile_options(gh2_pc PRIVATE $<IF:$<CONFIG:Debug>,-O0,-O2> -g0 -msse4.1 -mavx -fwrapv -fno-strict-aliasing -ffp-model=strict)')
        if optimized_after in source:return
    if before not in source:
        raise RuntimeError(f'Pinned SDK pattern changed: {relative}')
    path.write_text(source.replace(before, after, 1))

# Persist strings using the TOML library's formatter. Raw Windows backslashes,
replace('include/rex/ui/imgui_dialog.h','  void Draw();',
        '  void Draw();\n  virtual bool WantsContinuousRepaint() const { return true; }')
replace('include/rex/ui/imgui_drawer.h','  void AddDialog(ImGuiDialog* dialog);',
        '  void AddDialog(ImGuiDialog* dialog);\n  void RequestRepaint();')
replace('src/ui/imgui_drawer.cpp','void ImGuiDrawer::Draw(UIDrawContext& ui_draw_context) {',
        '''void ImGuiDrawer::RequestRepaint() {
  if (presenter_) presenter_->RequestUIPaintFromUIThread();
}

void ImGuiDrawer::Draw(UIDrawContext& ui_draw_context) {''')
replace('include/rex/ui/overlay/achievement_toast.h','  std::mutex mutex_;','  mutable std::mutex mutex_;')
replace('include/rex/ui/overlay/achievement_toast.h','  void OnDraw(ImGuiIO& io) override;',
        '''  void OnDraw(ImGuiIO& io) override;
  bool WantsContinuousRepaint() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    return !queue_.empty();
  }''')
replace('src/ui/imgui_drawer.cpp',
        '''    if (presenter_) {
      presenter_->RequestUIPaintFromUIThread();
    }''',
        '''    bool continuous = false;
    for (auto* dialog : dialogs_) continuous |= dialog->WantsContinuousRepaint();
    if (presenter_ && continuous) {
      presenter_->RequestUIPaintFromUIThread();
    }''')

# quotes and control characters otherwise invalidate the entire saved config.
replace('src/core/cvar.cpp','#include <optional>','#include <optional>\n#include <sstream>')
replace('src/core/cvar.cpp','std::string SerializeToTOML() {', '''static std::string SerializeTomlString(const std::string& value) {
  std::ostringstream stream;
  stream << toml::value<std::string>{value};
  return stream.str();
}

std::string SerializeToTOML() {''')
path=root/'src/core/cvar.cpp'
source=path.read_text()
before=r'''result += entry.name + " = \"" + entry.getter() + "\"\n";'''
after=r'''result += entry.name + " = " + SerializeTomlString(entry.getter()) + "\n";'''
if source.count(before)==2:
    path.write_text(source.replace(before,after))
elif source.count(after)!=2:
    raise RuntimeError('Pinned SDK string-serialization patterns changed')

replace('src/ui/d3d12/d3d12_presenter.cpp','#include <rex/ui/surface_win.h>',
        '#include <rex/ui/surface_win.h>\n#include <rex/ui/gh2_present_policy.h>\n#include <rex/ui/gh2_present_settings.inl>')
replace('include/rex/ui/d3d12/d3d12_presenter.h','#include <rex/ui/presenter.h>',
        '#include <rex/ui/presenter.h>\n#include <rex/ui/gh2_frame_limiter.h>')
replace('include/rex/ui/d3d12/d3d12_presenter.h','  const D3D12Provider& provider_;',
        '  const D3D12Provider& provider_;\n  gh2::graphics::FrameLimiter frame_limiter_;')
replace('src/ui/d3d12/d3d12_presenter.cpp',
        'Presenter::PaintResult D3D12Presenter::PaintAndPresentImpl(bool execute_ui_drawers) {',
        '''Presenter::PaintResult D3D12Presenter::PaintAndPresentImpl(bool execute_ui_drawers) {
  if (!frame_limiter_.allow(gh2::graphics::presentation_time_ns(),gh2::graphics::display_fps_limit.load())) {
    // Keep UI paint ticks scheduled without sleeping or blocking guest output.
    if (execute_ui_drawers) RequestUIPaintFromUIThread();
    return PaintResult::kNotPresented;
  }''')
replace('src/ui/d3d12/d3d12_presenter.cpp',
        '''  HRESULT present_result = paint_context_.swap_chain->Present(
      0, DXGI_PRESENT_RESTART |
             (paint_context_.swap_chain_allows_tearing ? DXGI_PRESENT_ALLOW_TEARING : 0));''',
        '''  const auto policy=gh2::graphics::present_policy(gh2::graphics::display_vsync.load(),paint_context_.swap_chain_allows_tearing);
  HRESULT present_result = paint_context_.swap_chain->Present(policy.interval,policy.flags);''')
replace('src/graphics/d3d12/command_processor.cpp',
        '  shared_memory_ = std::make_unique<D3D12SharedMemory>(*this, *memory_);',
        '  REXGPU_INFO("GH2 internal render scale initialized: {}x{}",draw_resolution_scale_x,draw_resolution_scale_y);\n  shared_memory_ = std::make_unique<D3D12SharedMemory>(*this, *memory_);')

replace('include/rex/audio/sdl/sdl_audio_driver.h', '#include <rex/audio/audio_driver.h>',
        '#include <rex/audio/audio_driver.h>\n#include <rex/audio/gh2_audio_delay.h>')
replace('include/rex/audio/sdl/sdl_audio_driver.h', '  std::mutex frames_mutex_ = {};',
        '  std::mutex frames_mutex_ = {};\n  gh2::AudioDelay output_delay_;')
replace('src/audio/sdl/sdl_audio_driver.cpp', '#include <SDL3/SDL.h>',
        '#include <SDL3/SDL.h>\n#include <rex/audio/gh2_audio_offset.inl>')
replace('src/audio/audio_system.cpp',
        '    "Max buffered audio frames (range 4-64). Lower reduces latency but may cause stuttering.");',
        '    "Max buffered audio frames (range 4-64). Lower reduces latency but may cause stuttering.")\n    .range(4, 64).lifecycle(rex::cvar::Lifecycle::kRequiresRestart);')
replace('src/audio/sdl/sdl_audio_driver.cpp', '  const float gain = GetOutputGain();',
        '  const float gain = GetOutputGain();\n  const int audio_offset = gh2::audio_offset_ms();')
replace('src/audio/sdl/sdl_audio_driver.cpp',
        '      std::memset(data, 0, len);\n      if (!SDL_PutAudioStreamData(stream, data, len)) {',
        '      std::memset(data, 0, len);\n      driver->output_delay_.process(data, channel_samples_, driver->sdl_device_channels_, audio_offset);\n      if (REXCVAR_GET(audio_mute)) std::memset(data, 0, len);\n      if (!SDL_PutAudioStreamData(stream, data, len)) {')
replace('src/audio/sdl/sdl_audio_driver.cpp',
        '      if (!SDL_PutAudioStreamData(stream, data, len)) {\n        REXAPU_ERROR("SDL_PutAudioStreamData() failed: {}", SDL_GetError());',
        '      driver->output_delay_.process(data, channel_samples_, driver->sdl_device_channels_, audio_offset);\n      if (REXCVAR_GET(audio_mute)) std::memset(data, 0, len);\n      if (!SDL_PutAudioStreamData(stream, data, len)) {\n        REXAPU_ERROR("SDL_PutAudioStreamData() failed: {}", SDL_GetError());')

# DLL CRT state may not contain the executable's program-name pointer.
# Ask Windows for the process executable, including paths longer than MAX_PATH.
replace('src/core/filesystem_win.cpp',
        '''  wchar_t* path;
  auto error = _get_wpgmptr(&path);
  return !error ? std::filesystem::path(path) : std::filesystem::path();''',
        '''  std::wstring path(512, L'\\0');
  for (;;) {
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length) return {};
    if (length < path.size()) {
      path.resize(length);
      return std::filesystem::path(path);
    }
    if (path.size() >= 32768) return {};
    path.resize(path.size() * 2);
  }''')
# MinGW UCRT supplies the Windows CRT interfaces but not the MSVC-only
# version header. The header is used only to select legacy (<14) link hacks.
replace('thirdparty/FFmpeg/libavutil/internal.h', '#include <crtversion.h>',
        '#if !defined(__MINGW32__)\n#include <crtversion.h>\n#endif')
# Git on Windows may materialize symlinks as text. Compile the actual source
# directory instead of the cabextract symlink facade.
replace('thirdparty/CMakeLists.txt',
        'set(MSPACK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libmspack/cabextract/mspack")',
        'set(MSPACK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libmspack/libmspack/mspack")')
replace('include/rex/chrono/chrono.h',
        '#ifdef __APPLE__\n// Apple libc++ does not expose clock_time_conversion or clock_cast.',
        '#if defined(_LIBCPP_VERSION) && (!defined(__cpp_lib_chrono) || __cpp_lib_chrono < 201907L)\n// libc++ lacks clock conversion support on both Apple and MinGW.')
replace('include/rex/ui/d3d12/d3d12_api.h', '#include <DXProgrammableCapture.h>', '#include "DXProgrammableCapture.h"')
replace('include/rex/ui/d3d12/d3d12_api.h', '#include "DXProgrammableCapture.h"',
        '#ifndef NTDDI_WINBLUE\n#define NTDDI_WINBLUE 0x06030000\n#endif\n#include "DXProgrammableCapture.h"\n#if defined(__MINGW32__)\n__CRT_UUID_DECL(IDXGraphicsAnalysis, 0x9f251514, 0x9d4d, 0x4902, 0x9d, 0x60, 0x18, 0x98, 0x8a, 0xb7, 0xd4, 0xb5)\n#endif')
replace('include/rex/ui/d3d12/d3d12_api.h', '#include "DxbcConverter.h"',
        '''#include "DxbcConverter.h"
#if defined(__MINGW32__)
// Interface IDs from the SDK's pinned Microsoft DXC headers.
__CRT_UUID_DECL(IDxbcConverter, 0x5F956ED5, 0x78D1, 0x4B15, 0x82, 0x47, 0xF7, 0x18, 0x76, 0x14, 0xA0, 0x41)
__CRT_UUID_DECL(IDxcUtils, 0x4605C4CB, 0x2019, 0x492A, 0xAD, 0xA4, 0x65, 0xF2, 0x0B, 0xB7, 0xD6, 0x7F)
__CRT_UUID_DECL(IDxcCompiler, 0x8c210bf3, 0x011f, 0x4422, 0x8d, 0x70, 0x6f, 0x9a, 0xcb, 0x8d, 0xb6, 0x17)
#endif''')
replace('src/core/threading_win.cpp',
        'void raise_thread_name_exception(HANDLE thread, const std::string& name) {',
        'void raise_thread_name_exception(HANDLE thread, const std::string& name) {\n#if defined(__MINGW32__)\n  // Modern SetThreadDescription above already names the thread. MinGW does\n  // not support the legacy MSVC debugger exception syntax.\n  (void)thread; (void)name;\n#else')
replace('src/core/threading_win.cpp',
        '  } __except (EXCEPTION_EXECUTE_HANDLER) {  // NOLINT\n  }\n}',
        '  } __except (EXCEPTION_EXECUTE_HANDLER) {  // NOLINT\n  }\n#endif\n}')
replace('src/ui/d3d12/d3d12_provider.cpp', '_uuidof(ID3D12Device)', '__uuidof(ID3D12Device)')
replace('src/graphics/graphics_system.cpp',
        '  command_processor_->InitializeRingBuffer(ptr, size_log2);',
        '  REXGPU_INFO("GH2 diagnostic ring buffer: physical={:08X} size_log2={}", ptr, size_log2);\n  command_processor_->InitializeRingBuffer(ptr, size_log2);')
replace('src/graphics/graphics_system.cpp',
        '  command_processor_->EnableReadPointerWriteBack(ptr, block_size_log2);',
        '  REXGPU_INFO("GH2 diagnostic read-pointer writeback: physical={:08X} block_log2={}", ptr, block_size_log2);\n  command_processor_->EnableReadPointerWriteBack(ptr, block_size_log2);')
replace('src/graphics/graphics_system.cpp',
        '      command_processor_->UpdateWritePointer(value);',
        '      static std::atomic<uint32_t> logged_writes{0};\n      if (logged_writes.fetch_add(1) < 16) REXGPU_INFO("GH2 diagnostic ring write pointer: {}", value);\n      command_processor_->UpdateWritePointer(value);')
# Normalize the old diagnostic block as a whole. Applying successive
# substring patches here previously duplicated its static variables.
path = root / 'src/graphics/command_processor.cpp'
source = path.read_text()
a = source.index('    // Execute. Note that we handle wraparound transparently.')
b = source.index('    // TODO(benvanik): use reader->Read_update_freq_', a)
block = '    // Execute. Note that we handle wraparound transparently.\n    read_ptr_index_ = ExecutePrimaryBuffer(read_ptr_index_, write_ptr_index);\n\n'
if source[a:b] != block:
    path.write_text(source[:a] + block + source[b:])
for field, value in [('XInputGetCapabilities_', 'xigc'), ('XInputGetState_', 'xigs'),
                     ('XInputGetStateEx_', 'xigsEx'), ('XInputGetKeystroke_', 'xigk'),
                     ('XInputSetState_', 'xiss'), ('XInputEnable_', 'xie')]:
    replace('src/input/xinput/xinput_input_driver.cpp', f'{field} = {value};',
            f'{field} = reinterpret_cast<void*>({value});')

# GH2 chooses its entire fret scheme from the device subtype. Keyboard input
# must not switch a connected physical guitar into standard-gamepad mode.
replace('src/input/input_system.cpp',
        '''  auto* driver = DriverForDevice(chosen);
  if (!driver) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  return driver->GetDeviceCapabilities(chosen, flags, out_caps);''',
        '''  for (DeviceId id : ids) {
    const auto* info = DeviceInfoFor(id);
    auto* physical_driver = DriverForDevice(id);
    if (!info || info->synthetic || !physical_driver) continue;
    X_INPUT_CAPABILITIES candidate{};
    if (physical_driver->GetDeviceCapabilities(id, flags, &candidate) == X_ERROR_SUCCESS &&
        (candidate.sub_type == 6 || candidate.sub_type == 7 || candidate.sub_type == 11)) {
      *out_caps = candidate;
      return X_ERROR_SUCCESS;
    }
  }
  auto* driver = DriverForDevice(chosen);
  if (!driver) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  return driver->GetDeviceCapabilities(chosen, flags, out_caps);''')
print('ReXGlue MinGW compatibility patches applied.')

path = root / 'CMakeLists.txt'
source = path.read_text()
marker = '# GH2 backend integration probe'
if marker not in source:
    source += '''
# GH2 backend integration probe
if(MINGW)
    target_compile_options(rexcore PRIVATE -msse4.1 -mavx)
endif()
add_executable(gh2_rex_gpu_smoke "${REXGLUE_ROOT}/../../tests/rex_gpu_smoke.cpp")
target_link_libraries(gh2_rex_gpu_smoke PRIVATE rexruntime)
add_dependencies(gh2_rex_gpu_smoke rexgpu-xenos)
'''
    path.write_text(source)

replace('CMakeLists.txt',
        '    target_compile_options(rexcore PRIVATE -msse4.1 -mavx)',
        '    foreach(target rexcore rexaudio rexui rexruntime rexgpu-xenos)\n        target_compile_options(${target} PRIVATE -msse4.1 -mavx)\n        target_compile_definitions(${target} PRIVATE NTDDI_VERSION=0x0A000000 _WIN32_WINNT=0x0A00)\n    endforeach()')
replace('CMakeLists.txt',
        'add_dependencies(gh2_rex_gpu_smoke rexgpu-xenos)',
        'add_dependencies(gh2_rex_gpu_smoke rexgpu-xenos)\nset_target_properties(rexgpu-xenos PROPERTIES PREFIX "")')

source = path.read_text()
if '# GH2 translated SDK runtime' not in source:
    source += '''
# GH2 translated SDK runtime
set(GH2_TRANSLATED_DIR "${REXGLUE_ROOT}/../../build/rex-generated")
file(GLOB GH2_REX_TRANSLATIONS "${GH2_TRANSLATED_DIR}/ppc_recomp.*.cpp")
add_executable(gh2_rex_bootstrap ${GH2_REX_TRANSLATIONS}
    "${GH2_TRANSLATED_DIR}/ppc_func_mapping.cpp"
    "${GH2_TRANSLATED_DIR}/gh2_rex_imports.cpp"
    "${REXGLUE_ROOT}/../../runtime/rex_bootstrap.cpp"
    "${REXGLUE_ROOT}/../../runtime/guest_clock.cpp")
target_include_directories(gh2_rex_bootstrap BEFORE PRIVATE "${GH2_TRANSLATED_DIR}")
target_compile_options(gh2_rex_bootstrap PRIVATE -O0 -g0 -msse4.1 -mavx)
target_compile_definitions(gh2_rex_bootstrap PRIVATE GH2_TRACE_ENTRY)
target_link_libraries(gh2_rex_bootstrap PRIVATE rexruntime)
add_dependencies(gh2_rex_bootstrap rexgpu-xenos)
'''
    path.write_text(source)

replace('CMakeLists.txt', '''add_executable(gh2_rex_bootstrap ${GH2_REX_TRANSLATIONS}
    "${GH2_TRANSLATED_DIR}/ppc_func_mapping.cpp"
    "${GH2_TRANSLATED_DIR}/gh2_rex_imports.cpp"
    "${REXGLUE_ROOT}/../../runtime/rex_bootstrap.cpp"
    "${REXGLUE_ROOT}/../../runtime/guest_clock.cpp")
target_include_directories(gh2_rex_bootstrap BEFORE PRIVATE "${GH2_TRANSLATED_DIR}")
target_compile_options(gh2_rex_bootstrap PRIVATE -O0 -g0 -msse4.1 -mavx)
target_compile_definitions(gh2_rex_bootstrap PRIVATE GH2_TRACE_ENTRY)
target_link_libraries(gh2_rex_bootstrap PRIVATE rexruntime)
add_dependencies(gh2_rex_bootstrap rexgpu-xenos)''', '''add_library(gh2_rex_code OBJECT ${GH2_REX_TRANSLATIONS}
    "${GH2_TRANSLATED_DIR}/ppc_func_mapping.cpp"
    "${GH2_TRANSLATED_DIR}/gh2_rex_imports.cpp"
    "${REXGLUE_ROOT}/../../runtime/guest_clock.cpp")
target_include_directories(gh2_rex_code BEFORE PUBLIC "${GH2_TRANSLATED_DIR}")
target_compile_options(gh2_rex_code PRIVATE -O0 -g0 -msse4.1 -mavx)
target_compile_definitions(gh2_rex_code PUBLIC GH2_TRACE_ENTRY)
target_link_libraries(gh2_rex_code PUBLIC rexruntime)
add_executable(gh2_rex_bootstrap "${REXGLUE_ROOT}/../../runtime/rex_bootstrap.cpp")
target_compile_options(gh2_rex_bootstrap PRIVATE -O0 -g0 -msse4.1 -mavx)
target_link_libraries(gh2_rex_bootstrap PRIVATE gh2_rex_code)
add_dependencies(gh2_rex_bootstrap rexgpu-xenos)

add_executable(gh2_pc WIN32 "${REXGLUE_ROOT}/../../runtime/rex_bootstrap.cpp"
    "${REXGLUE_ROOT}/../../runtime/rex_windowed.cpp")
target_compile_options(gh2_pc PRIVATE -O0 -g0 -msse4.1 -mavx)
target_compile_definitions(gh2_pc PRIVATE GH2_REX_WINDOWED)
target_link_libraries(gh2_pc PRIVATE gh2_rex_code imgui)
if(MINGW)
    target_link_options(gh2_pc PRIVATE -municode)
endif()
set(REXGLUE_SHARE_DIR "${REXGLUE_ROOT}/src/ui")
include("${REXGLUE_ROOT}/cmake/rexglue_helpers.cmake")
rexglue_configure_target(gh2_pc GPU_PLUGINS xenos)''')

# Use the UI library's exported ImGui functions and context. Linking a second
# static copy before rexruntime creates a separate GImGui in the executable.
replace('CMakeLists.txt', 'target_link_libraries(gh2_pc PRIVATE gh2_rex_code imgui)',
        'target_link_libraries(gh2_pc PRIVATE gh2_rex_code)\ntarget_include_directories(gh2_pc PRIVATE "${REXGLUE_ROOT}/thirdparty/imgui")')

# A trailing -O0 previously disabled Release optimization for the game code.
# Keep Debug conservative; preserve PPC wraparound and strict FP semantics.
replace('CMakeLists.txt', 'target_compile_options(gh2_rex_code PRIVATE -O0 -g0 -msse4.1 -mavx)',
        'target_compile_options(gh2_rex_code PRIVATE $<IF:$<CONFIG:Debug>,-O0,-O2> -g0 -msse4.1 -mavx -fwrapv -fno-strict-aliasing -ffp-model=strict)')
replace('CMakeLists.txt', 'target_compile_options(gh2_pc PRIVATE -O0 -g0 -msse4.1 -mavx)',
        'target_compile_options(gh2_pc PRIVATE $<IF:$<CONFIG:Debug>,-O0,-O2> -g0 -msse4.1 -mavx -fwrapv -fno-strict-aliasing -ffp-model=strict)')

replace('CMakeLists.txt','rexglue_configure_target(gh2_pc GPU_PLUGINS xenos)',
        '''rexglue_configure_target(gh2_pc GPU_PLUGINS xenos)
set_target_properties(gh2_pc PROPERTIES OUTPUT_NAME "Guitar Hero 2")
enable_language(RC)
target_sources(gh2_pc PRIVATE "${REXGLUE_ROOT}/../../runtime/game.rc")
target_include_directories(gh2_pc PRIVATE "${REXGLUE_ROOT}/../../launcher/assets")''')

import runpy
runpy.run_path('scripts/apply_performance_patches.py')
