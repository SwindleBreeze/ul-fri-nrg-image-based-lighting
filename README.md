# NRG Seminar 2.9 - Image-Based Lighting

Real-time physically based renderer with GPU-baked image-based lighting:
HDR equirectangular environment -> cubemap -> irradiance convolution +
GGX prefilter mips + split-sum BRDF LUT, then runtime PBR shading in WebGPU
(Dawn) with WGSL shaders.

## Requirements

- Windows 10/11
- CMake 3.22+
- Visual Studio 2022 (MSVC toolchain)
- Dawn installed (default expected path: `C:/libs/dawn/install/Release`)
- One HDR environment file (`.hdr`)
- Optional glTF model (`.glb`)

## Build

```powershell
cmake -S . -B build
cmake --build build --config Release
```

If Dawn is installed elsewhere:

```powershell
cmake -S . -B build -DDAWN_INSTALL_PREFIX="C:/path/to/dawn/install/Release"
```

## Run

```powershell
cd build/Release
.\Renderer.exe path\to\environment.hdr [path\to\model.glb]
```

If model path is omitted, the app attempts `glb/porsche.glb`.

## Controls

| Input | Action |
|-------|--------|
| LMB drag | Orbit camera |
| Scroll | Zoom |
| `R` | Rebake IBL after changing quality preset |
| `E` | Load evaluation camera pose |
| `P` | Save screenshot |

## Evaluation workflow

Run from `build/Release` for predictable output paths:

- App logs: `build/Release/results/`
- SSIM script output: `results/ssim_results.json` (repo root)

Typical flow:

1. Set preset (Low/Medium/High)
2. Press `R` to rebake
3. Press `P` to capture screenshot
4. Run:

```powershell
pip install -r requirements-eval.txt
python tools/eval_ssim.py
```

## Repo layout

```text
app/      app state and bake pipeline orchestration
gfx/      renderer, camera, UI layer
ibl/      IBL precomputation logic
io/       HDR/glTF loading and screenshot support
mesh/     procedural mesh generation
scene/    draw objects, materials, scene setup
shaders/  WGSL bake and runtime shaders
tools/    evaluation scripts
utils/    helper utilities
```

## License

Source code is licensed under the [MIT License](LICENSE).
