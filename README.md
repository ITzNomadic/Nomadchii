# Nomadchii

Nomadchii is an experimental open-source Nintendo Switch / NX2 emulator fork focused on VRAM stability, low-overhead GPU command submission, and clean separation between universal engine improvements and optional per-hardware tuning presets.

> **Legal Notice & Disclaimer:**  
> Nomadchii **does not include games, ROMs, updates, DLC, cryptographic keys (`prod.keys` / `title.keys`), system firmware, or precompiled shader caches**. You must legally own and dump your own console system files and games. Piracy is neither supported nor endorsed.

---

## Lineage & GPL Modification Notice

In accordance with **Section 5(a) of the GNU General Public License v3.0 (GPL-3.0-or-later)**:

- **Notice of Modification (2026-11-13):** This repository is a modified derivative work ("**Nomadchii**") forked by ITzNomadic from **Fritchii** (`fritchii-v5.8.4`), which builds upon **Lichii**, **Eglantine**, **Eden**, **Sudachi**, and **yuzu**.
- Changes applied on top of `fritchii-v5.8.4` rebrand the application (`nomadchii`, `%APPDATA%\nomadchii`), remove third-party Discord/donation/release links, generalize VRAM and query-cache performance improvements for all GPUs, and move hardware/display-specific overrides into optional [`presets/`](presets/README.md) files.
- All original copyright headers, license texts ([LICENSE.txt](LICENSE.txt) and [LICENSES/](LICENSES)), and upstream attributions remain preserved.

---

## What Changed vs. Upstream (`fritchii-v5.8.4`)

### 1. General Engine Improvements (Included for Everyone)
- **Hardware-Adaptive VRAM Texture Pooling (`vk_texture_cache`):** Automatically scales EXP42 pooled render-target byte and frame budgets by GPU VRAM (`256 / 768 MiB` & `90 frames` on `<= 8–9 GiB` GPUs; full `512 / 1536 MiB` & `180 frames` on `10+ GiB` GPUs), with optional `EDEN_NX2_IMAGE_POOL_MIB` and `EDEN_NX2_IMAGE_POOL_FRAMES` overrides.
- **Texture Cache & LRU Safety (`lru_cache`, `texture_cache`):** Adds bounds/sentinel guards for unlinked `lru_index` handles, guards `NULL_IMAGE_ID` / `CORRUPT_ID` paths, and reclaims idle Vulkan staging/pooled memory cleanly on `OutOfMemory()` before retrying allocation.
- **Dynamic 8 GiB Budget Thresholds (`texture_cache`, `buffer_cache`):** Tightens buffer GC thresholds under actual memory pressure on `<= 9 GiB` cards and raises the `6–10 GiB` texture critical threshold to reduce unnecessary ASTC re-eviction churn without affecting `10+ GiB` GPUs.
- **Low-Overhead Front Command Batching (`EXP66` / `FrontWriteRun`):** Batches incrementing Maxwell3D register writes across the split GPU thread boundary in one call.
- **Lock-Free Query Report Bloom Filter (`EXP65`):** Uses a bounded 2048-bit atomic page bloom filter in `QueryCacheBase::HasCachedReports` to avoid unnecessary `MacroDmaFlush` drains when no cached query reports exist in a memory range.
- **Vulkan Pipeline Cache Retention (`vk_pipeline_cache`):** Ensures expired pipeline hashes are retained up to the configured eviction ceiling when saving pipelines across restarts.

### 2. PC / Hardware / Game-Specific Overrides (`presets/`)
Instead of forcing 8 GB VRAM limits, Lossless Scaling frame-pacing anchors (`EDEN_NX2_VSYNC_HZ=1000`), or game-specific switches onto every user, those profiles live separately in editable files inside [`presets/`](presets/README.md):
- `presets/8gb-vram.txt` — Conservative texture cache expected/critical MiB and EXP42 pool caps for 8 GB VRAM GPUs.
- `presets/40fps-lossless-scaling.txt` — Present-timing anchor (`EDEN_NX2_VSYNC_HZ=1000`) for users capping frames at ~40 FPS with Lossless Scaling X2 (`67%` emulator speed limit).
- `presets/oot-performance.txt` — Optional Switch 2 *Ocarina of Time* CPU/GPU/scheduler switch profile (`nx2_switches.txt` format).

By default, Nomadchii ships only safe, general defaults in [`dist/nx2_switches.txt`](dist/nx2_switches.txt).

---

## Building from Source

See [docs/Build.md](docs/Build.md) for full Windows, Linux, and macOS dependencies and toolchains.

Example Windows MSVC + Ninja configure & build:
```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_QT=ON -DYUZU_TESTS=OFF
cmake --build build --target yuzu
```
The compiled GUI executable is output as `nomadchii.exe` (or `nomadchii` on Linux/macOS) alongside `nx2_switches.txt` and `presets/`.

---

## Credits and License

Nomadchii builds directly on the work of:
- **[Fritchii](https://github.com/Frizti/Fritchii)** (Frizti)
- **[Lichii](https://github.com/zortos293/Lichii)** (zortos293)
- **[Eglantine](https://github.com/Djihads80/Eglantine)** (Camille LaVey)
- **Eden**, **Sudachi**, and the **yuzu Emulator Project**
- Application icon and Graphite UI art originally contributed upstream by **YOB (yobslab)** (see [dist/branding](dist/branding))

Licensed under **GPL-3.0-or-later** (with inherited components under **GPL-2.0-or-later**). See [LICENSE.txt](LICENSE.txt) and [LICENSES/](LICENSES).
