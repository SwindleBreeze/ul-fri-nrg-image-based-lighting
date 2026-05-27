# IBL Evaluation and Performance

Protocol for satisfying assignment §2.9: *evaluate visual quality and performance* and *analyze trade-offs between accuracy and real-time performance*.

## Metrics

| Metric | Source | Notes |
|--------|--------|-------|
| Equirect + irradiance bake | Window title / console | `IblBakeTimings` after `BakeDiffuseIrradiance` |
| Env mip generation | Window title / console | Part of `BakeSpecularPrefilter` (`envMipsMs`) |
| Prefilter bake | Window title / console | GGX importance sampling cost |
| BRDF LUT bake | Window title / console | Compute dispatch 512×512 |
| Total bake | Sum of above | One-time per preset (press **R** to rebake) |
| FPS (avg) | Window title | Rolling average over last 120 frames |
| FPS (min) | Window title | Minimum over same window |

GPU work is included in bake timings after queue submission completes.

## Quality presets (defined in `ibl/IblBakeSettings`)

| Preset | env face | irradiance | prefilter | mips | prefilter samples | BRDF samples | irradiance Δ |
|--------|----------|------------|-----------|------|-------------------|--------------|--------------|
| Low | 256 | 16 | 64 | 4 | 256 | 256 | 0.05 |
| Medium (default) | 512 | 32 | 128 | 5 | 1024 | 1024 | 0.025 |
| High | 512 | 64 | 256 | 6 | 4096 | 4096 | 0.0125 |

Env mip count is derived as `floor(log2(envFaceSize))` (minimum 1).

## Experiments for the report

1. **Fixed scene** — Same HDR, camera pose, and objects (helmet + two spheres).
2. **Preset comparison** — Bake **Low** and **High**; screenshot specular on the metal sphere and diffuse on the dielectric sphere.
3. **Record timings** — Fill the table below from the console log and window title after each rebake (press **R**).
4. **Runtime** — Record avg/min FPS with window at 1280×720, orbit idle for 5 s.
5. **Discussion** — One short paragraph: what improved visually in High, what it cost in bake time and FPS (if any).

### What to look for visually

| Feature | Low risk | High benefit |
|---------|----------|--------------|
| Specular sharpness (metal sphere) | Low prefilter size / few mips | High prefilter + samples |
| Specular noise | Few samples | 4096 samples |
| Diffuse color bleeding | Low irradiance resolution | 64 irradiance + smaller Δ |
| Fireflies in bright env | More prefilter samples | High preset |

## Results table (fill in for report)

| Preset | equirect+irr ms | env mips ms | prefilter ms | BRDF ms | total bake ms | avg FPS | min FPS |
|--------|-----------------|-------------|--------------|---------|---------------|---------|---------|
| Low | | | | | | | |
| Medium | | | | | | | |
| High | | | | | | | |

### Example row (replace with your hardware)

| Preset | equirect+irr ms | env mips ms | prefilter ms | BRDF ms | total bake ms | avg FPS | min FPS |
|--------|-----------------|-------------|--------------|---------|---------------|---------|---------|
| Medium | (your run) | | | | | | |

## Suggested report wording (template)

> We implemented three bake presets trading precomputation cost and map resolution against shading fidelity. Low settings reduced cubemap and sample counts, producing softer specular lobes and visible Monte Carlo noise on glossy surfaces. High settings increased prefilter resolution to 256 with 4096 importance samples per texel, sharpening reflections at the cost of longer offline bake time (see table). Runtime frame rate remained near interactive levels because shading performs only a fixed number of texture lookups per pixel (irradiance, prefilter, BRDF LUT), illustrating the split-sum trade-off: heavy work moved to preprocess, light work at draw time.
