# DLSS integration checkpoint

The current game supports bilinear, CAS and FSR 1. DLSS is **not enabled** by this development update. No DLSS quality selections are shown until their renderer inputs can be validated.

NVIDIA's [Streamline DLSS guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md) requires color, depth and motion-vector resources and per-frame camera constants. Performance, Balanced, Quality and DLAA are available through the SDK, with optimal input dimensions queried from `slDLSSGetOptimalSettings`. DLAA keeps input and output dimensions equal; it still needs temporal inputs. The SDK's mode query must ultimately drive actual renderer resolution rather than just resize the completed image. The local renderer currently scales Xenos drawing by integer factors.

The D3D12 `IssueSwap` path currently resolves a finished color texture and hands it to the presenter. It does not carry a validated scene depth image, motion vectors, previous object positions, a jittered projection or separate HUD image. Depth targets exist in the host render-target cache, but choosing the last bound depth target is insufficient: it can belong to another pass, include padding/MSAA, or no longer match the final color composition. The stage, highway, notes, crowd and overlays must be identified from actual draws before generating motion data. Camera-only reprojection would miss moving geometry.

## Bounded input investigation

Open **F5 > DLSS development diagnostics > Capture DLSS input candidates** while a song is playing. Close the F5 panel after clicking. The next guest swap starts a capture of 16 consecutive guest frames, with up to 2048 submitted title draws per frame. The capture ends automatically; no game restart, termination, render-target transition or GPU readback is performed. Default play has no capture. Do not use these frames for performance comparison: snapshot allocation and copies add CPU work.

Files are written to `Documents/GH2PC/logs/dlss-inputs-*.jsonl`. A background worker writes a `.partial` file and renames it after a successful flush. Incomplete sessions are marked `complete:false`; overflowing draw budgets are visible through `seen_draws`. The game log reports start/completion/write failures. Captures contain shader hashes, normalized depth control, color/depth target registers, viewport/scissor bits, used vertex constant bits and vertex-fetch descriptors. Host depth descriptions are metadata, not depth pixels. Raw bits preserve NaNs and signed zero without interpreting them as matrices.

Summarize a capture with `python scripts/analyze_temporal_capture.py <capture.jsonl>`. The summary lists candidate shader/target passes and varying constants, and explicitly leaves `validated_dlss_inputs:false`. Different objects can use the same shader and different constants in one frame; constant variation alone does not establish object history or motion.

`python scripts/inspect_shader_cache.py` disassembles the closed game's local XESH cache into ignored `build/temporal-shaders`. It validates the pinned cache version and shader hashes before analysis, and uses the SDK's actual microcode decoder. It does not launch GH2 or modify the cache. These game-derived files are excluded from source packages.

The initial local cache contains 173 shaders, including 101 vertex shaders. Several directly compute clip position with constants 4–7 after object transformation with constants 24–26. `python scripts/analyze_position_shaders.py build/temporal-shaders` produces a map of direct projection candidates. This identifies code patterns, not verified title camera/object state; live draw observations must correlate their usage.

The depth metadata accessor also corrects the pinned SDK's cached target declaration from an array of pointer-to-pointer values to an array of target pointers, matching the `memcpy` source used in `SetCommandListRenderTargets`. Pointer sizes and layout are unchanged.

## Remaining implementation

1. Correlate captured passes with disassembled position shaders; establish projection, object transforms, animated skinning and title HUD boundaries.
2. Export depth with the matching color frame, correct Xenos depth conversion, active extents, MSAA handling and GPU resource lifetimes. Validate visualizations of stage and highway depth.
3. Generate previous/current clip positions for moving and skinned objects; validate motion vectors, disappearance/disocclusion, cuts and loading resets. Apply subpixel jitter to the appropriate projections and maintain unjittered constants.
4. Integrate the official DLSS SDK with adapter/driver checks, resource tagging, frame identity, synchronization and command-state restoration. Keep spatial fallback when prerequisites or hardware support fail.
5. Expose Performance/Balanced/Quality/Native AA, persist selections, use queried dimensions, and compare note clarity and actual frame-time spikes on full songs.

The recorder tests and renderer build verify the development tooling. They do not demonstrate DLSS reconstruction, image quality or a performance improvement.

## GPU depth-conversion implementation

`runtime/depth_conversion.h` implements a GPU compute pass from an explicitly cropped depth SRV to a single-sample R32_FLOAT texture. Both single-sample and multisampled shaders are compiled. It reverses the supplied viewport depth scale/offset and selects the nearest canonical depth among valid samples. Values outside the supplied layer range produce far depth rather than being clamped into foreground. The output is canonical projection depth, not linear world depth. This pass cannot identify layer ownership where ranges overlap, such as highway and HUD: the producer must run before those later writes or supply a matching mask.

The caller owns source SRV format/plane, source and output resource states, descriptor lifetime, allocation, GPU completion and command-list state restoration. Recording adds no waits or readback. The converter uses explicit source origin and active dimensions and rejects out-of-allocation crops, invalid scale and incompatible output resources. The native renderer uses a deferred command list, so the future hook must record this work at the correct point in that list rather than execute an immediate command list ahead of pending draws.

`tests/check_depth_conversion.py` passed 18 actual GPU conversions on hardware: the two observed typeless depth resource formats with matching depth SRVs, 1/2/4 samples and three observed layer ranges. Every cropped output pixel was compared with the expected depth while padded background was excluded. These tests use the same depth in every sample; mixed coverage/disocclusion and real title layer boundaries remain to be validated. See `docs/depth_conversion_validation.json`.

The converter is not yet hooked into GH2 or consumed by DLSS. Previous moving/skinned vertex production, native deferred recording, layer snapshots and final presentation wiring remain unfinished. No production executable was replaced for this isolated implementation step.

## Native producer hooks

The staged renderer now includes opt-in depth/history producers behind the native command `gh2_probe_temporal_renderer`. It requests 16 frames on the serialized GPU worker. Normal play does not enable the producer. There is no DLSS toggle yet and the dispatch is not called by these hooks.

The F5 development diagnostics expose **Run native depth/history check**. Pause a song first: the initial run prepares compute shaders and can stall briefly. The check records 16 rendered frames and writes producer results to the normal game log. It verifies native layer/crop behavior beyond the isolated tests; it is not a performance benchmark or a DLSS image-quality test.

### EDRAM tile correction after the native probe

The first native check rejected 2560x1440 viewport crops against a 1280-wide depth allocation, and every history identity was duplicated. The full draw capture also shows paired scene draws with scissors covering opposite halves and a 640-pixel surface pitch. Together these identify the title's horizontal EDRAM tiling: each half replays the scene into reusable narrower targets, with signed window offsets placing it in the full image.

The revised producer intersects the host viewport with the native scissor and allocation, then uses enabled rasterizer window offsets and resolution scale to place that crop in a full-size per-layer atlas. Separate descriptor heaps remain immutable while queued tile dispatches share the atlas. The twelve-slot pool prevents recycling an atlas until every referencing submission completes. The first tile clears the atlas to far depth; missing areas remain explicitly missing. Non-overlapping rectangle coverage must fill the entire image before `coverage_complete` becomes true. Pixel depth quality and alignment with the final color frame are still unvalidated.

History keys now include pixel-shader and pass state. Used VS constants are retained and unused banks normalized out. A matching key replayed in another tile with identical used constants and geometry revision counts as a tile replay, rather than a conflicting duplicate. Same-tile duplicates or changed constants remain ambiguous. This is a candidate association; previous animated vertex/index correspondence is still not certified and native motion eligibility remains false.

GPU fixtures verify assembly of two different depth crops at separate destination offsets, correct untouched far-depth pixels, destination bounds, and shared-atlas lifetime. Pure placement tests reproduce the observed 2560-wide viewport/1280-wide allocation at scale 2 with signed -640 window offset. The revised native check must confirm complete coverage in a song before these atlases can be used by DLSS.

Before the stage-to-highway and highway-to-HUD candidate transitions, the hook obtains the currently bound native depth SRV and records conversion in the actual deferred command stream. It uses the computed host viewport and NDC conversion to derive the depth mapping, rather than assuming raw guest Z scale matches host float24 conversion. Incomplete/out-of-allocation crops are rejected, not truncated and advertised as full-frame depth. Depth-resource state is restored immediately in the stream. Native descriptor heaps are restored, cached pipeline/root-table bindings are invalidated, and a UAV ordering barrier is recorded. Six fence-protected output slots skip rather than wait when busy. The textures remain development candidates: real layer ownership/crops have not been validated in a song.

The same opt-in path stores used draw identity candidates and full VS constant banks, including bone-palette constants. It copies unique referenced guest vertex-buffer ranges into a bounded four-slot GPU history pool (16 MiB per slot), with physical range/capacity checks and existing submission-fence lifetime. Memory-export draws are skipped. Prior frame buffers are kept separate and cannot be recycled merely because their fence completed. Copies use the backend's existing shared-memory transitions and queued copy commands. Probe restart drops old history; shutdown releases pools after the native GPU drain.

`geometry_history.h` rejects duplicate identities in either frame, missing previous vertices, changed geometry revisions, history cuts and overflow. The native producer deliberately keeps `previous_vertices_preserved=false` for eligibility: raw range snapshots do not yet establish stable object identity, changing index topology or correspondence for reused dynamic buffers. This means its `verified_previous` counter is expected to be zero until that producer contract is fulfilled. No velocity field is synthesized from ambiguous matches.

Validation now executes the converter through the SDK's concrete `DeferredCommandList`, with actual hardware output readback for all 18 depth cases. It also tests six-slot busy skipping/reuse, GPU byte preservation across two vertex frames, four-slot pressure/capacity checks and constant-history rejection cases. These are native queue/resource tests, not validation of guest layer boundaries or final motion textures. See `docs/deferred_depth_validation.json`, `docs/vertex_frame_pool_validation.json` and `docs/geometry_history_validation.json`.

## Complete song capture and GPU dispatch

The expanded song capture contains all 1,215-1,217 draws per frame for 16 frames, with no truncated frames. Stage draws use depth scale/offset `(-0.9,0.9)`, later highway candidates use `(-0.1,1.0)` and HUD candidates use `(-0.02,1.0)`. Constants 4-7 change between those groups. They cannot be represented accurately by blindly attaching the stage camera to the completed composite image. These are draw-pattern identifications; visual layer ownership remains to be validated when producing the resources.

`runtime/temporal_geometry.h` implements composition of projection/object matrices, backward pixel motion, projection jitter including the negative Y viewport, and inversion of the three observed depth ranges. It rejects invalid/behind-eye projections and resolution changes. Range inversion alone is not depth linearization or per-pixel ownership. The tests use independently specified expected values; there is no injected game velocity shader yet.

`runtime/dlss_dispatch.h` implements the official SDK's DLSS dispatch: optimal dimensions, K/M presets for the requested modes, unique frame token, per-frame constants, frame-based tagging of color/depth/motion/output, evaluation and cleanup after GPU drain. Mode and dimension changes reset history. The host must initialize Streamline before D3D12, supply matching validated object motion and depth, retain GPU resources and restore its command-list state. No waits occur in the dispatch. The bridge accepts single-sample R32_FLOAT depth and floating-point two-channel motion; Xenos depth must be converted before calling it.

`python tests/check_dlss_capability.py --evaluate` now executes actual GPU DLSS in Performance, Balanced, Quality and Native AA on an isolated flat-color stationary synthetic scene, copies results back after GPU completion, and verifies output color. All four modes returned success and produced expected output on the RTX 5070. This proves the SDK bridge and GPU reconstruction path work; it does not demonstrate GH2 motion quality, jitter accumulation, UI clarity or performance. The current game executable does not load or call this bridge. Game resource production and connection to presentation remain unfinished.

## First song recordings

Both original captures completed, but every frame exceeded the original 512-draw budget. The latest contains 1,049–1,155 title draws per frame. The revised recorder keeps up to 2,048 draws for 16 frames, including later passes while bounding memory. Early observations distinguish shadow viewports from the full stage viewport; full stage depth uses reversed scaling and a padded multisampled resource. These observations do not identify final highway/HUD depth yet.

The optional official Streamline SDK 2.14.1 archive is checksum-pinned by `scripts/setup_dlss_sdk.py`. The initial capability probe initialized it on the local NVIDIA GPU, registered a D3D12 device, and queried Performance/Balanced/Quality/DLAA dimensions at 1080p, 1440p and 4K. All 12 queries passed. Later GPU reconstruction fixtures are described above. No NVIDIA DLLs are installed into the game. See `docs/dlss_capability_validation.json`.

## Latest native probe: tile coverage and descriptor continuation

The latest paused-song probe recorded both horizontal tiles at destinations x=0 and x=1280 and logged complete 2560x1440 coverage for stage and highway candidates in its first four frames. History reported 346 unique candidates, 8 ambiguous keys and 353 identical tile replays per frame. These records validate native tile placement and candidate association, not depth pixel quality or motion reconstruction.

Later frames skipped the second tile because the pool over-protected completed descriptor leases that referenced the current live atlas. The corrected rule distinguishes resetting an image from appending another tile to the same explicitly shared image. A completed descriptor heap may be reused for that continuation; it cannot reset the atlas while any referencing submission remains in flight. Regression fixtures reproduce this fence sequence and verify both successful continuation and rejection of premature image reuse. No additional user capture is required to test that resource-lifetime rule. Continuous native coverage after the fix is still unconfirmed.

## GPU motion raster implementation

`runtime/motion_raster.h` now implements rasterization of paired current/previous homogeneous clip positions into an R16G16_FLOAT backward pixel-motion texture and an R8_UINT validity mask. It uses perspective-correct previous clip interpolation and D3D pixel centers. Previous W behind/on the eye plane, non-finite positions and missing vertex history produce invalid motion. Current scene-depth mismatch discards the replay, so an occluded primitive cannot erase previously written foreground vectors. Output scissoring allows independent EDRAM tile replays into a full atlas.

The producer must supply verified current and previous clip payloads, matching depth/visibility, compatible resources and GPU states/lifetimes. The pass accepts triangle lists only, rejects oversized buffers and unverified producer contracts, and issues no allocation, readback or waits while recording. It changes command-list graphics/IA state, which the caller must restore. It is not yet hooked into the native renderer: previous guest position-program execution, index/vertex correspondence and visibility certification remain pending. A preserved guest buffer or a unique draw key alone does not satisfy those contracts. The per-pixel mask is not currently tagged into DLSS.

`tests/check_motion_raster.py` passes nine hardware cases with output readback: stationary geometry, horizontal and vertical backward motion, homogeneous scaling, perspective interpolation, invalid previous position/history, preservation of an existing foreground vector behind occluded geometry, and tile scissoring. These fixtures use explicitly supplied synthetic clip pairs and do not validate GH2's notes, skinning, final image quality or frame time. See `docs/motion_raster_validation.json`.

## Native mapped-motion diagnostic connection

The native draw snapshots now retain the actual host NDC conversion, viewport, enabled tile offsets, full-image dimensions and Xbox coordinate-format flags. Position generation bakes compatible current/previous full-image projection mappings into the verified linear title program. Resolution discontinuities, unknown depth conventions and nonlinear divided/reciprocal formats are rejected before allocating clip outputs. The mapped GPU fixture compares 40 cases and 960 components against the exact cached shader interpreter followed by explicit CPU mapping, including distinct current/previous tile mappings.

The opt-in native check now rasterizes a partial diagnostic motion field from those generated GPU positions. It uses complete per-layer depth atlases, with up to eight supported linear draws per layer, and a six-slot fence-protected R16G16_FLOAT/R8_UINT motion/mask pool. Depth reads extend the atlas lifetime to the consuming submission. Depth resource state is restored after candidate rendering. Native descriptor heaps, pipeline/root bindings, topology, viewport/scissor and render-target cache state are invalidated or restored before subsequent title work. Normal play keeps this path inactive.

These textures are explicitly uncertified diagnostic candidates. The mask encodes numerical history/depth visibility from the supported replay, not proof of stable object identity or full shader/alpha/skin coverage. `RecordUncertifiedDiagnostic` is separate from the verified producer entry point, and every pooled output retains `certified=false`. No texture is tagged into DLSS and the game has no enabled DLSS mode. Skinned shaders, remaining scrolling geometry, alpha/stencil behavior, stable identity and complete scene coverage remain unresolved.

`tests/check_deferred_motion.py` verifies the actual queued raster path and pool busy skipping/reuse with hardware readback. Together with mapped shader comparisons and patch-idempotence tests this validates the implementation and resource contracts, not the in-game output. The next native check should show `GH2 temporal positions:` with `mapping_rejected` counts and `GH2 temporal motion:` with partial draw counts. That confirmation is still required; a nonzero draw count alone is not an image-quality certification.

### Native history boundary repair

The first installed motion probe had complete tiled depth for all 16 frames, but no position/motion lines. A layered source-patch migration had inserted a second vertex-history advancement after position generation. That second call advanced from a null active slot, erasing the previous frame each time. The frame-boundary repair removes the duplicate, and the guarded patch pipeline now asserts exactly one position-generation call and one vertex-history advancement. Repeated patch application is checked for byte idempotence. The generator also logs missing current/previous snapshots instead of silently returning; one warm-up frame without previous history is expected. Native motion after this repair remains to be checked.

## Title position-program extraction

### Four-bone motion implementation

The current producer additionally supports five exact four-bone programs:
`b6d10de26caf4b00`, `47b0406889d62288`, `17c6d38225de0653`,
`72180349f1ac86ee`, and `86495a03257c4f49`. These read 48-byte vertices,
three weights at byte offset 12, and four matrix groups from c24-38. The
fourth weight follows the original c255 calculation, including its value
from each frame. Current and previous palettes and weights are independent;
the compute path preserves the title's bone accumulation order.

All five programs passed 200 GPU cases / 4,800 component comparisons against
their original shader interpreter, through the native deferred queue and
full-image mapping. Cases include single-bone and mixed weights, fourth-bone
weight, changed pose and vertex weights, all byte orders, index sizes,
base/clamp behavior and changed topology. The original three rigid programs
also passed their 120 cases after the shared parameter expansion.

The joined compute-to-raster fixture now has 12 cases, including stationary
and XY motion caused by palette changes while raw vertex positions stay
unchanged. Every output pixel and validity value is checked in these three
additional cases. Native constant allocations now use the aligned parameter
size (768 bytes for the 736-byte payload); source-patch application remains
idempotent. Normal play still leaves this diagnostic producer inactive.

This supersedes the earlier statement that all skinned programs are excluded.
Other programs, stable object identity, alpha/stencil visibility, complete
native motion coverage, jitter and final DLSS presentation remain unfinished.
No DLSS image-quality or performance result is claimed from these fixtures.

### Consolidated offline validation and expanded linear support

The current linear position family includes `2eba09b0974055eb`,
`cb9d92c934e26a03`, and `8f72b1ddc2edc8e1`. The latter programs permute
temporary register lanes but export the same model/projection position. All
three passed 40 GPU cases each (2,880 component comparisons total) against
their original cached microcode, including mapped output, endian modes,
index formats, base/clamp behavior and topology invalidation. The native
snapshot and generation gates now accept those three exact hashes only.
Skinned programs are still excluded.

`tests/check_deferred_motion.py --chain` runs position compute, the resource
transition, descriptor/root changes and motion raster together in the SDK's
actual deferred stream. Stationary and horizontal/vertical translation are
checked at every pixel of the 64x64 output, including validity. The remaining
six raster scenarios check perspective, invalid history, occlusion and scissor
behavior. `check_vertex_frame_pool.py` now exercises all 16 frames of the native
probe duration, including buffer reuse. Its original two-frame fixture had
never exercised the required transition back to COPY_DEST; the extended test
now mirrors that transition already present in the native renderer.

The shader-comparison runner retains a local hash-checked oracle cache under
`build/temporal-shaders`, excluded from source releases, so subsequent offline
comparisons need not interrupt a running game. `--candidates` compares all
three original programs. `analyze_motion_coverage.py` inventories an existing
capture and distinguishes supported draw counts from actual pixel coverage.

These results validate computation and queue integration, not complete GH2
motion, reconstructed image quality or performance. DLSS remains disabled.

`runtime/linear_motion_clips.h` implements the exact position calculation for the supported cached GH2 vertex shader `2eba09b0974055eb`: high-level `vf0` (hardware fetch constant 95), float3 positions at offset 0 with a 9-word stride, object rows c24-c26 followed by projection rows c4-c7. The GPU kernel preserves the program's zxyw dot-product swizzles. It reads preserved current/previous vertex and index bytes, handles vertex/index byte order, and implements 24-bit index masking, base adjustment and min/max clamping. Changed resulting index correspondence produces an invalid payload. It rejects unknown shaders, incoherent snapshots, invalid bounds, unsupported stride/offset and unaligned constant-buffer addresses. It records no waits or readback.

The comparison fixture validates the local XESH cache header/hash, loads the actual title microcode, and runs it through the SDK interpreter with isolated physical memory and generated vertex bytes. Forty GPU cases and 960 position-component comparisons pass, including all four vertex byte orders, 16/32-bit and automatic indices, topology changes, base/clamp settings and ignored high index bits. The same tests pass through the actual SDK deferred command stream. No guest game is launched. See `docs/linear_motion_clips_validation.json`.

The native opt-in producer now preserves separate vertex/index bytes at each eligible draw, rather than treating a shared scratch-buffer address as immutable within a frame. It supports this specific shader on non-tessellated triangle lists with direct guest DMA indices. At frame end it generates at most 16 candidate current/previous raw-clip payloads from unambiguous paired snapshots. A 48-slot output pool skips busy resources; upload constants and source buffers remain protected through the consuming submission. Native descriptor heaps and pipeline/table caches are restored before subsequent work. GPU outputs are labelled raw clips and are not passed to the motion raster or DLSS yet.

The remaining requirements include native validation of the extraction, host/full-frame viewport conversion, wider position-program coverage (including skinned and expanded geometry), stable object correspondence, visibility and full motion-mask coverage. As a potential broader path, D3D12 can capture the live vertex/geometry-stage results with [stream output](https://learn.microsoft.com/en-us/windows/win32/direct3d12/stream-output-counters), which requires an appropriate root-signature flag and GPU stream-output storage/counter. That approach has been inspected but is not implemented in this build.

## Full-image clip mapping

`runtime/host_clip_mapping.h` implements the native shader-completion coordinate rules (reciprocal W and divided XY/Z), host NDC conversion, viewport mapping and signed tile placement. It returns clip coordinates for the complete image, retaining canonical depth for comparison with the assembled depth atlas. Unknown depth conventions and invalid coordinates are rejected. For ordinary homogeneous position shaders, `BakeLinearHostMapping` folds the full-image XY conversion into the projection rows used by the existing GPU clip generator; nonlinear reciprocal/divided formats are deliberately rejected by that path. Tests compare both tile layouts and verify baked projection against explicit mapping. These helpers are not wired into native motion rasterization yet.
