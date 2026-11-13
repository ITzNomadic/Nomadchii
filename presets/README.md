# Nomadchii Optional Hardware & Game Presets

Nomadchii keeps its built-in defaults safe and hardware-adaptive for everyone:
- Core fixes (LRU safety guards, OOM memory reclaim, EXP65 query bloom filter, EXP66 front write batching, pipeline retention, and automatic 8 GB vs 10+ GB VRAM image-pool scaling) work automatically with **zero extra configuration**.
- Hardware-, display-, or game-specific switches are kept **out of global defaults** and stored here as optional editable files so you only apply what fits your PC.

## How to Use a Preset

1. Open one or more preset `.txt` files in this folder and copy the lines you want.
2. Paste them into your personal override file:
   - Windows (installed): `%APPDATA%\nomadchii\nx2_switches.txt`
   - Linux: `$XDG_CONFIG_HOME/nomadchii/nx2_switches.txt` (usually `~/.config/nomadchii/nx2_switches.txt`)
   - Portable setup: create/edit `nx2_switches.txt` next to `nomadchii.exe` (or put a `user/` folder next to it).
3. Restart Nomadchii for changes to take effect.

User-folder entries override the bundled `nx2_switches.txt` without touching your installation or game files.

## Included Presets

- [`8gb-vram.txt`](8gb-vram.txt): For 8 GB VRAM GPUs where high-resolution textures pressure memory (keeps texture cache collection tuned around ~5.0–5.75 GiB and caps EXP42 pooled render targets). **Skip this on 12–24 GB GPUs.**
- [`40fps-lossless-scaling.txt`](40fps-lossless-scaling.txt): Sets `EDEN_NX2_VSYNC_HZ=1000` so a `67%` (40 FPS) emulator speed limit paces evenly with external frame generation like Lossless Scaling (X2 -> 80 FPS). **Do not enable this if playing unlocked or at native 60 FPS without an external cap.**
- [`oot-performance.txt`](oot-performance.txt): CPU/GPU scheduler and AMD app-cache switches tuned for *The Legend of Zelda: Ocarina of Time* (Switch 2). Remove `EDEN_NX2_AMD_APP_CACHE=1` if you are on NVIDIA/Intel.
