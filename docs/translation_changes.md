# Translation fixes

The initial instruction and switch diagnostics have been eliminated without replacing unimplemented instructions with no-ops or suppressing out-of-function cases.

`patches/function_analysis.cpp` follows reachable direct branches, conditional-return fallthroughs, and recovered indirect switch edges. Analysis is bounded to the input interval and known function starts. It avoids the original one-word overread and terminates loops using visited instruction addresses. The discovered case bodies extend the relevant leaf-function ranges rather than terminating those functions at `bctr`.

`patches/gh2_instruction_cases.inl` supplies the scalar operations and vector operations reported missing by GH2. Packed results account for XenonRecomp's reversal of all 16 vector bytes, variable shifts mask their lane-sized counts, and new saturation operations retain sticky vector status. Standard `vandc` shares the already implemented VMX128 case. Existing `rlwimi.` and `vcmpgtuh.` now update their condition fields.

`runtime/gh2_ppc_helpers.h` provides alias-safe scalar reference implementations for the more delicate operations. `tests/run_instruction_tests.py` assembles real PPC instructions, runs the actual recompiler, compiles the emitted functions, and executes them against an independently computed guest-byte oracle. The test does not merely call the helper functions directly.

`scripts/prepare_config.py` validates all eight complete helper routines. The initial helper scan confused memory-image RVAs with PE raw-file offsets; `scripts/analyze_image.py` now scans the correct memory-image locations. VMX128 helper signatures include the extended instruction encoding.

One switch at `0x826654D0` places its bounds comparison farther away than XenonAnalyse's fixed search window. The preparation script verifies the comparison, default branch, dispatch instructions, and 29 halfword table entries before adding the switch. It combines this entry with the scanner's 170 tables.

The diagnostic bootstrap reserves guest memory, installs native dispatch mappings, and executes the original entrypoint. Runtime services now initialize the loader/module imports and main PCR, implement TLS and virtual heaps, read real extracted game files, and provide native critical sections, events, semaphores and worker threads. Timebase instructions and the frequency export share a 50 MHz clock. Video-mode queries describe a configured 720p60 output; no renderer is implemented.

Worker threads have independent register contexts, stacks, PCRs and TLS. Kernel service state and file operations are serialized by a runtime mutex, released around waits and contested critical sections. Waits duplicate their host handle to survive concurrent guest handle closure. Function-entry diagnostics are thread-local; unsupported worker services trigger cooperative cancellation. Workers are joined before guest memory is freed. Cancellation at arbitrary instructions, guest exception semantics, full scheduling/affinity flags and PPC reservation fidelity remain limitations.

The boot regression verifies original startup, archive worker reads and video-mode initialization, physical memory allocation, then the current explicit `VdInitializeEngines` stop. Runtime tests exercise two workers sharing a critical section and retaining independent TLS values. No current test establishes rendered frames, audio playback or gameplay.

Physical memory now uses a pagefile-backed shared section with Windows placeholder mappings. The A and C ranges start at physical zero; the E range starts at physical 0x1000. The allocator shares one physical extent map across all page sizes, honors physical range bounds and virtual alignment, and protects freed bytes in every alias. Section pages cannot be decommitted individually; they remain committed until address-space teardown and are reused/zeroed on subsequent allocations. Low 16 MB is reserved for future GPU/system memory. GPU writeback at 0x7F000000 now shares the first 16 MB of physical backing; bidirectional alias and address-conversion tests pass. MMIO and rendering are not implemented.

References: [Xenia memory map](https://github.com/xenia-project/xenia/blob/master/src/xenia/memory.cc), [Windows placeholder mapping API](https://learn.microsoft.com/windows/win32/api/memoryapi/nf-memoryapi-mapviewoffile3).
