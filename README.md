# NRG Seminar 2.9 — Image-Based Lighting

Real-time PBR renderer with GPU-baked IBL (diffuse irradiance, specular prefilter, split-sum BRDF LUT) using **WebGPU (Dawn)** and **WGSL**.

## Requirements

- Windows 10/11
- CMake 3.22+
- Visual Studio 2022 (or compatible MSVC)
- [Dawn](https://dawn.googlesource.com/dawn) built and installed (default cache path: `C:/libs/dawn/install/Release`)
- HDR environment map (`.hdr`) and optional glTF model — see [assets/README.md](assets/README.md)

## Build

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Shaders are copied to `build/Release/shaders/` on build. Run the executable from that directory (or ensure `shaders/` sits next to `Renderer.exe`).

## Run

```powershell
cd build/Release
.\Renderer.exe path\to\environment.hdr [path\to\model.glb]
```

If the second argument is omitted, the loader looks for `assets/DamagedHelmet.glb` relative to the working directory.

### Controls

| Input | Action |
|-------|--------|
| LMB drag | Orbit camera |
| Scroll | Zoom |
| Keyboard UI | See below (window title shows FPS and bake time) |

### Keyboard UI

Use the **IBL controls** ImGui panel (same pattern as HW1, with `IMGUI_IMPL_WEBGPU_BACKEND_DAWN`):

- IBL preset combo + **Rebake IBL** button (or press **R**)
- Environment yaw slider
- Object picker + metallic / roughness / albedo sliders
- **LMB drag** outside the UI to orbit; **scroll** to zoom

**Reflections:** lighting is **image-based** (HDR → cubemap / irradiance / prefilter). The floor is *not* a mirror: you will see the **environment** on glossy surfaces, but **not** reflections of the other spheres. That would need planar reflections, SSR, or ray tracing (out of scope for this seminar pipeline).

## Documentation (for the seminar report)

| Document | Contents |
|----------|----------|
| [docs/PIPELINE_AND_DEBEVEC.md](docs/PIPELINE_AND_DEBEVEC.md) | Pipeline stages, Debevec comparison, split-sum rationale |
| [docs/EVALUATION.md](docs/EVALUATION.md) | Quality presets, benchmarks, experiment protocol |

## Project layout

```
shaders/          WGSL bake + runtime shaders
ibl/              IBL precompute (cubemap, irradiance, prefilter, BRDF LUT)
gfx/              PBR renderer, camera, UI
scene/            Scene graph and default layout
io/               HDR and glTF loading
mesh/             Procedural meshes
docs/             Report-ready documentation
assets/           Asset download instructions (not committed)
```

## CMake note

Set `DAWN_INSTALL_PREFIX` if Dawn is installed elsewhere:

```powershell
cmake -S . -B build -DDAWN_INSTALL_PREFIX="C:/path/to/dawn/install/Release"
```
