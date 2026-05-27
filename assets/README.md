# Assets

Large HDR and glTF files are **not** committed to the repository. Download them locally and place them as below.

## HDR environment

Use an equirectangular **.hdr** file (RGBE), for example from [Poly Haven](https://polyhaven.com/hdris) or [HDR Haven](https://hdrihaven.com).

Example layout:

```
assets/
  studio.hdr          # your chosen environment
```

Pass the full path on the command line: `Renderer.exe assets/studio.hdr`

## glTF model (default hero)

Recommended: **Damaged Helmet** from the Khronos glTF sample models.

1. Download: https://github.com/KhronosGroup/glTF-Sample-Models/tree/main/2.0/DamagedHelmet
2. Place `DamagedHelmet.glb` in this folder:

```
assets/DamagedHelmet.glb
```

The model includes tangents and metallic-roughness textures, which the loader expects for correct normal mapping.

## Optional screenshots for the report

Save comparison images next to your report (not in git), e.g.:

- `report_ibl_low.png` — IBL preset Low
- `report_ibl_high.png` — IBL preset High
- `report_dielectric_sphere.png` — left debug sphere
- `report_metal_sphere.png` — right debug sphere

See [docs/EVALUATION.md](../docs/EVALUATION.md) for the experiment protocol.
