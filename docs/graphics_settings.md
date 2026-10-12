# Graphics settings

Use **Launcher → Settings → Graphics** or press **F5** in game. Hover over launcher setting titles for explanations.

## Resolution and display

Internal resolution controls how sharply the stage and highway are rendered. Higher values need more graphics power and memory. Window size controls the output window separately.

| Internal scale | Approximate resolution |
| --- | --- |
| 1× | 1280×720 |
| 2× | 2560×1440 |
| 3× | 3840×2160 |
| 4× | 5120×2880 |

Choose windowed or borderless fullscreen, your monitor, and whether to preserve the original aspect ratio.

## Image quality

Anti-aliasing smooths edges. Texture filtering improves surfaces viewed at an angle. These settings cannot add detail that is absent from the original textures or videos.

Presentation uses bilinear filtering. DLSS and FSR are not available.

## Frame delivery

**Display VSync** synchronizes presentation with the monitor and can increase input delay. **FPS limit** accepts an integer; 0 removes the presentation limit. These settings do not change the game's original simulation speed.

Use the output/display counters to compare settings. If visuals stutter, lower internal resolution and try **No audience**. See [performance](performance.md).

## Saving changes

Save settings before exiting. Internal resolution, anti-aliasing, startup window size and monitor changes need a normal restart. Audience visibility, filtering, aspect ratio and display VSync can be adjusted during play.

Settings are stored in `Documents\GH2PC\gh2.toml`.
