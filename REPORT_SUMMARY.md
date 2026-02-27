# MaterialX `--report` Output Reference

## Usage

Both `MaterialXView` and `MaterialXGraphEditor` support `--report [text|json]`:

```bash
# JSON report (default) — validation errors + renderable analysis
MaterialXView --material foo.mtlx --report json

# Text report — plain validation errors only
MaterialXView --material foo.mtlx --report text

# Exit code: 0 = valid, 1 = invalid
```

Output goes to **stderr**. JSON is always a single line for easy piping.

---

## JSON Schema

```jsonc
{
  "materialXVersion": "1.39.4",          // MaterialX library version (major.minor.patch)
  "valid": true,                            // document passes MaterialX validation
  "errors": [
    {
      "message": "...",             // human-readable error description
      "path": "NG_Foo/bar/in",     // XPath-like element path in the document
      "source": "<input ...>",     // XML serialization of the offending element
      "file": "/abs/path.mtlx",    // source file URI
      "severity": "error"          // "error" | "warning" | "hint"
    }
  ],
  "renderables": [
    {
      "path": "MyMaterial",
      "file": "/abs/path.mtlx",
      "type": "material",                           // element type
      "shaderNode": "gltf_pbr",                     // shader node category
      "shaderNodeDef": "ND_gltf_pbr_surfaceshader", // resolved nodedef name
      "shaderNodeDefVersion": "2.0.1",              // nodedef version (omitted when not set)
      "isUnlit": false,                              // true for surface_unlit (no lighting)
      "transparency": true,                          // isTransparentSurface() result
      "alphaMode": "mask",                           // "opaque" | "mask" | "blend"
      "alphaCutoff": 0.5,                            // only present when alphaMode == "mask"
      "displacement": false,
      "transparencyInputs": [                        // only present when non-empty
        {
          "name": "alpha",
          "valueType": "float",
          "value": "0.7",           // literal value, or "[connected:nodecategory]"
          "opaqueAt": 1.0           // the value at which this input is considered opaque
        }
      ]
    }
  ]
}
```

---

## How Transparency Detection Works

### `isTransparentSurface()` — the `transparency` field

This is MaterialX's built-in static analysis. It determines whether the HW shader
generator will set `GenOptions::hwTransparency = true`, which has **concrete
rendering consequences** (see [Rendering Impact](#what-transparencytrue-actually-changes-in-rendering) below).

The analysis is **conservative** — when it cannot statically determine that a
value will be opaque at runtime, it assumes transparent. This means some
materials that would produce fully opaque output at runtime are still flagged
`transparency: true` and rendered through the transparency pipeline.

This is **not a false positive** — when `transparency: true`, the generated
shader **will** include alpha computation and discard logic, the renderer **will**
enable alpha blending and two-pass rendering, and depth write behavior **will**
change. Even if the computed alpha is 1.0 at runtime, the material is processed
through a fundamentally different rendering path.

#### Detection logic

It checks a combined list of transparency-relevant inputs:

**Hardcoded defaults** (checked on every surface shader):

| Input name     | Opaque value | Transparent when…          |
|---------------|-------------|---------------------------|
| `opacity`      | 1.0          | value ≠ 1.0, or connected  |
| `existence`    | 1.0          | value ≠ 1.0, or connected  |
| `alpha`        | 1.0          | value ≠ 1.0, or connected  |
| `transmission` | 0.0          | value ≠ 0.0, or connected  |

**Hint-based** (from nodedef `hint` attribute — see [Hint System](#the-hint-system) below):

| Hint value        | Opaque value | Used by                                |
|------------------|-------------|----------------------------------------|
| `"transparency"`  | 0.0          | `open_pbr_surface.transmission_weight` |
| `"opacity"`       | 1.0          | `open_pbr_surface.geometry_opacity`    |

For each input in the combined list:
- If **connected to a node** (and the node is not in the `adjustment` or `channel`
  node groups) → **transparent**
- If **literal value ≠ opaque value** → **transparent**
- For `mix` category nodes: recursively checks both `fg` and `bg` inputs

#### Why `gltf_pbr` is always flagged transparent

The `gltf_pbr` nodedef's implementation graph has an internal `ifequal` node chain
that computes the final `opacity` value fed to the `surface` constructor. Even
when all inputs are at defaults (`alpha=1, alpha_mode=0`), the `surface`
constructor's `opacity` input is **connected** to the `ifequal` node, which has
`nodegroup="conditional"` — not `"adjustment"` or `"channel"`. So the static
analysis conservatively returns `true`.

At runtime, the `ifequal` chain evaluates to `opacity = 1.0` and the generated
shader computes `outAlpha = 1.0` — but the shader generator and renderer don't
know this at compile time.

### What `transparency:true` Actually Changes in Rendering

When `isTransparentSurface()` returns `true`, the following concrete changes occur:

#### In the generated shader

| Feature | `transparency: false` | `transparency: true` |
|---------|----------------------|---------------------|
| Alpha output | Hardcoded `vec4(color, 1.0)` | Computed `vec4(color, outAlpha)` |
| `u_alphaThreshold` uniform | Not present | Added (default 0.001) |
| `discard` / `discard_fragment()` | Not emitted | Emitted: `if (outAlpha < u_alphaThreshold) discard;` |
| `HW::ATTR_TRANSPARENT` | Not set on shader | Set on shader |

The alpha computation for all surface types (lit and unlit) is:
```glsl
float outAlpha = clamp(1.0 - dot(surfaceshader.transparency, vec3(0.3333)), 0.0, 1.0);
```

#### In the renderer (MaterialXView two-pass system)

| Pass | Threshold | Behavior |
|------|-----------|----------|
| **Opaque pass** | `u_alphaThreshold = 0.99` | All materials rendered. Fragments with `alpha < 0.99` are discarded (pre-pass cutout). |
| **Transparent pass** | `u_alphaThreshold = 0.001` | Only `transparency: true` materials rendered again with alpha blending enabled. |

| Renderer behavior | `transparency: false` | `transparency: true` |
|-------------------|----------------------|---------------------|
| Number of draw calls | 1 | 2 (opaque pass + transparent pass) |
| Alpha blending | Disabled | Enabled (`SRC_ALPHA / ONE_MINUS_SRC_ALPHA`) |
| Depth writes (Metal, transparent pass) | N/A | Disabled |
| Depth writes (GL, transparent pass) | N/A | Remain enabled |
| Standalone renderers | Normal single draw | Two-draw order (back faces first, then front) |

---

## The Hint System

The hint system allows nodedef authors to mark inputs as transparency-relevant
without relying on the hardcoded input name list. This is used when a nodedef
uses non-standard input names for opacity and transmission.

### How hints work

A nodedef input can have a `hint` attribute in the `.mtlx` library file:

```xml
<input name="transmission_weight" type="float" value="0.0" hint="transparency" />
<input name="geometry_opacity" type="float" value="1" hint="opacity" />
```

At detection time, `NodeDef::getInputHints()` iterates all inputs with
`hasHint()` and returns a `StringMap` of `{inputName → hintValue}`.

The transparency detector then adds these to the hardcoded check list:

| Hint value | Interpretation | Opaque test value |
|------------|---------------|-------------------|
| `"transparency"` | Input contributes to transparency (like `transmission`) | Opaque at `0.0` |
| `"opacity"` | Input controls opacity (like `opacity`) | Opaque at `1.0` |
| `"anisotropy"` | (Defined but not used for transparency detection) | — |

### Which nodedefs use hints

Currently, **only `open_pbr_surface`** uses the hint system:

| NodeDef | Input | Hint | Meaning |
|---------|-------|------|---------|
| `ND_open_pbr_surface_surfaceshader` | `transmission_weight` | `"transparency"` | Transparent when ≠ 0.0 |
| `ND_open_pbr_surface_surfaceshader` | `geometry_opacity` | `"opacity"` | Transparent when ≠ 1.0 |

All other nodedefs (`gltf_pbr`, `standard_surface`, `UsdPreviewSurface`, etc.)
rely on having inputs with names that match the hardcoded list (`opacity`,
`alpha`, `transmission`, `existence`).

### Why hints matter

Without hints, `open_pbr_surface` would be invisible to transparency detection:
its inputs are named `geometry_opacity` and `transmission_weight`, which don't
match any of the hardcoded names. The hint system ensures these inputs are still
checked during static analysis.

Custom nodedef authors should use hints on any inputs that control surface
opacity or transmission to ensure correct transparency detection.

---

## Two Distinct Thresholds: `alpha_cutoff` vs `u_alphaThreshold`

These are **completely different mechanisms** that operate at different levels:

### `alpha_cutoff` — graph-level, baked into the shader math

This is a **nodedef input** on `gltf_pbr` (and `opacityThreshold` on
`UsdPreviewSurface`). It flows through the nodedef's implementation graph and
affects the **computed `opacity` value** before it reaches the `surface`
constructor.

For `gltf_pbr`, the internal graph implements this chain:

```
alpha → ifgreatereq(alpha, alpha_cutoff) → ifequal(alpha_mode, 1, ...) → ifequal(alpha_mode, 0, ...) → surface.opacity
```

Concretely:

1. **`opacity_mask_cutoff`** = `ifgreatereq(alpha, alpha_cutoff, 1.0, 0.0)`
   - Binary step function: if `alpha >= alpha_cutoff` → 1.0, else → 0.0
   - This is the **cutout operation**

2. **`opacity_mask`** = `ifequal(alpha_mode, 1, opacity_mask_cutoff, alpha)`
   - If `alpha_mode == 1` (MASK) → use the binary cutout result
   - Otherwise → pass through raw `alpha`

3. **`opacity`** = `ifequal(alpha_mode, 0, 1.0, opacity_mask)`
   - If `alpha_mode == 0` (OPAQUE) → force `1.0` (ignore alpha entirely)
   - Otherwise → use result from step 2

The final `opacity` value feeds into the `surface` constructor's `opacity`
input, which the lit surface node (SurfaceNodeGlsl/Msl) applies as:
```glsl
outColor *= surfaceOpacity;
outTransparency = mix(vec3(1.0), outTransparency, surfaceOpacity);
```

For `UsdPreviewSurface`, a similar chain exists with `opacityThreshold`:
```
opacity → ifgreatereq(opacity, opacityThreshold) → switch(opacityMode, ...) → surface.opacity
```
Note: USD **always** uses cutout-style opacity. There is no smooth blend mode.

### `u_alphaThreshold` — render-time uniform, controlled by the application

This is a **shader uniform** added by the HW shader generator when
`hwTransparency = true`. It has nothing to do with any nodedef input. The
application sets it at render time to control the discard threshold.

The generated shader code:
```glsl
// After all surface shading is computed:
float outAlpha = clamp(1.0 - dot(surfaceshader.transparency, vec3(0.3333)), 0.0, 1.0);
out = vec4(surfaceshader.color, outAlpha);
if (outAlpha < u_alphaThreshold) {
    discard;  // or discard_fragment() on Metal
}
```

MaterialXView uses `u_alphaThreshold` in its two-pass system:
- Opaque pass: **0.99** — discards anything not nearly-fully-opaque
- Transparent pass: **0.001** — discards only fully invisible fragments

Other applications can set `u_alphaThreshold` to any value. For example,
setting it to `alpha_cutoff` would implement a traditional alpha-test pipeline
in a single pass.

### How they interact

Both thresholds compose — `alpha_cutoff` affects the **value** of `outAlpha`,
while `u_alphaThreshold` determines whether that value causes a `discard`:

| `alpha_mode` | `alpha_cutoff` effect on `outAlpha` | `u_alphaThreshold` effect |
|-------------|-------------------------------------|--------------------------|
| 0 (OPAQUE) | `outAlpha = 1.0` always | Never discards (1.0 > any threshold) |
| 1 (MASK) | `outAlpha = 1.0` or `0.0` (binary) | Discards the 0.0 fragments |
| 2 (BLEND) | `outAlpha = raw alpha value` | Discards below threshold |

---

## Alpha Mode Detection — the `alphaMode` field

The `alphaMode` field provides a **shader-specific** classification that reflects
what the shader graph will compute, going beyond the binary `transparency` flag.

### `gltf_pbr` (glTF PBR)

Reads the `alpha_mode` and `alpha_cutoff` inputs directly from the shader node:

| `alpha_mode` value | Name   | `alphaMode` | Behavior |
|-------------------|--------|-------------|----------|
| 0 (default)        | OPAQUE | `"opaque"`  | Alpha forced to 1.0 by the graph, `alpha` input ignored |
| 1                  | MASK   | `"mask"`    | Binary step: `alpha >= alpha_cutoff` → 1.0, else 0.0 → **cutout** |
| 2                  | BLEND  | `"blend"`   | Raw alpha passes through → **smooth transparency** |

`alphaCutoff` is only meaningful (and only present in JSON) when `alphaMode == "mask"`.
Default `alpha_cutoff` is `0.5`.

### `UsdPreviewSurface`

Has `opacityMode` (0 = transparent, 1 = presence) and `opacityThreshold`:

| `opacityMode` | Name        | `alphaMode` | Behavior |
|--------------|-------------|-------------|----------|
| 0             | transparent | `"mask"`    | Cutout at `opacityThreshold` |
| 1             | presence    | `"mask"`    | Same, with extra zero-check for `opacity == 0.0` |

USD provides **no smooth alpha blending mode** — both modes are cutout-style.
If `isTransparentSurface()` returns false, `alphaMode` falls back to `"opaque"`.

### `standard_surface` (Autodesk Standard Surface)

No explicit alpha mode input. Transparency comes from:
- `opacity` (color3, default `1, 1, 1`) — per-channel surface opacity
- `transmission` (float, default `0`) — physical transmission weight

When `isTransparentSurface()` returns true → `alphaMode = "blend"`.
When false → `alphaMode = "opaque"`.
There is no cutout/mask path for standard_surface.

### `open_pbr_surface` (OpenPBR)

Uses the **hint system** instead of hardcoded input names:
- `geometry_opacity` (hint: `"opacity"`) — opaque at 1.0
- `transmission_weight` (hint: `"transparency"`) — opaque at 0.0

Same as standard_surface: `"blend"` when transparent, `"opaque"` otherwise.

### `surface_unlit`

Uses the same transparency detection as lit surfaces. Has both `opacity` and
`transmission` inputs. When transparent, classified as `"blend"` (no cutout path).

### Other / custom shader nodes

For any shader node not specifically recognized, the generic fallback is:
- `transparency: true` → `alphaMode = "blend"`
- `transparency: false` → `alphaMode = "opaque"`

---

## Lit vs Unlit Surface Shader Alpha Paths

The `surface` (lit) and `surface_unlit` nodes both produce a `surfaceshader`
struct with `.color` and `.transparency` fields, but compute them very
differently.

### The `surfaceshader` struct (same for both)

```glsl
struct surfaceshader { vec3 color; vec3 transparency; };  // GLSL
struct surfaceshader { float3 color; float3 transparency; };  // MSL
```

### How alpha is computed: lit vs unlit

| Aspect | `surface` (lit) | `surface_unlit` |
|--------|----------------|-----------------|
| **Implementation** | C++ codegen class (SurfaceNodeGlsl / SurfaceNodeMsl) | Plain GLSL function (`mx_surface_unlit.glsl`) |
| **Classification** | `SHADER \| SURFACE \| CLOSURE` | `SHADER \| SURFACE \| UNLIT` |
| **Lighting** | Full light loop, IBL, shadow maps | None — `requiresLighting()` returns false |
| **Color computation** | BSDF/EDF closure evaluation with light integration | `emission * emission_color * opacity` |
| **Transparency source** | BSDF transmission closure (physically-based light transport) | `transmission * transmission_color` (scalar multiply) |
| **Opacity application** | Separate pass: `outColor *= surfaceOpacity; outTransparency = mix(vec3(1.0), outTransparency, surfaceOpacity)` | Combined: `result.transparency = mix(vec3(1.0), transmission * transmission_color, opacity)` |
| **Final alpha→discard** | Same code path (in generator) | Same code path (in generator) |

### Key difference in opacity behavior

**Lit surface** (two-step):
```glsl
// Step 1: BSDF transmission closure adds to outTransparency
outTransparency += bsdf_transmission.response;
// Step 2: surfaceOpacity modulates both:
outColor *= surfaceOpacity;
outTransparency = mix(vec3(1.0), outTransparency, surfaceOpacity);
```

**Unlit surface** (single-step):
```glsl
result.color = emission * emission_color * opacity;
result.transparency = mix(vec3(1.0), transmission * transmission_color, opacity);
```

In both cases, `opacity = 0.0` yields `transparency = vec3(1.0)` (fully
transparent / cutout) and `opacity = 1.0` yields transparency determined purely
by the transmission inputs.

### Unlit `surface_unlit` inputs

| Input | Type | Default | Role |
|-------|------|---------|------|
| `emission` | float | 1.0 | Emission intensity multiplier |
| `emission_color` | color3 | 1, 1, 1 | Base emission color |
| `transmission` | float | 0.0 | How much light passes through (0 = none) |
| `transmission_color` | color3 | 1, 1, 1 | Color of transmitted light |
| `opacity` | float | 1.0 | Surface opacity (0 = fully cutout) |

### OSL: unlit alpha is entirely different

In OSL, `surface_unlit` uses native closures rather than the struct:
```osl
result.bsdf = trans * transmission_color * transparent();
result.edf  = (1.0 - trans) * emission_weight * emission_color * emission();
result.opacity = clamp(opacity, 0.0, 1.0);
```

Here `transparent()` is an OSL built-in closure — there is no discard or alpha
threshold concept. The `opacity` scalar is stored directly (not converted to a
vec3 transparency).

---

## How HW Shader Backends Use Transparency Decisions

The `transparency` field maps directly to `GenOptions::hwTransparency` in the
shader generation pipeline. When a material is loaded for rendering:

```cpp
_hasTransparency = isTransparentSurface(_elem, generator.getTarget());
context.getOptions().hwTransparency = _hasTransparency;
// shader is now generated with or without transparency code
```

### GLSL / MSL (hardware rasterization)

When `hwTransparency = true`:

1. **Shader code**: Converts `surfaceshader.transparency` (vec3/float3) to scalar alpha:
   ```glsl
   float outAlpha = clamp(1.0 - dot(outTransparency, vec3(0.3333)), 0.0, 1.0);
   ```
2. **Alpha test / discard**: Always emitted when transparency is on:
   ```glsl
   if (outAlpha < u_alphaThreshold) discard;  // GLSL
   // or: discard_fragment();                  // MSL
   ```
3. **`u_alphaThreshold` uniform** (default `0.001`): Controlled by the application at
   render time — see [Two Distinct Thresholds](#two-distinct-thresholds-alpha_cutoff-vs-u_alphathreshold).

When `hwTransparency = false`:
- Alpha is hardcoded to `1.0`, no discard emitted, no `u_alphaThreshold` uniform.

#### Transmission rendering mode

Controlled by `GenOptions::hwTransmissionRenderMethod`:
- `TRANSMISSION_REFRACTION` (default) — adds transmission response to the color channel (refraction approximation)
- `TRANSMISSION_OPACITY` — adds transmission to the transparency channel (opacity-based)

This affects how BSDF transmission is routed in the lit surface node and has no
effect on unlit surfaces.

### OSL (offline / path tracing)

No `hwTransparency` flag. Transparency is handled natively:
```osl
output = (result.bsdf + result.edf) * opacity_weight + transparent() * (1.0 - opacity_weight);
```
No discard concept — uses OSL's `transparent()` closure for blending.

### MDL (NVIDIA)

No `hwTransparency` flag. Uses MDL's native `cutout_opacity` mechanism:
```mdl
geometry: material_geometry(cutout_opacity: mxp_opacity)
```
This is MDL's stochastic alpha test — no separate discard or blend modes.

---

## The `transparencyInputs` Array

Lists only those transparency-relevant inputs whose values **deviate from their
opaque defaults**. This is the precise set of inputs that cause the material to
be classified as potentially transparent by value (as opposed to by graph
connectivity).

Each entry:

| Field       | Description |
|------------|-------------|
| `name`      | Input name on the shader node (e.g. `"opacity"`, `"transmission"`, `"alpha"`) |
| `valueType` | MaterialX type (e.g. `"float"`, `"color3"`) |
| `value`     | Current value as string, or `"[connected:nodecategory]"` if driven by a node graph |
| `opaqueAt`  | The value at which this input is considered fully opaque |

When `transparency: true` but `transparencyInputs` is empty, it means the
material was flagged transparent due to graph connectivity (e.g. the `gltf_pbr`
internal `ifequal` chain) rather than actual non-opaque input values. The
material will still be processed through the transparency pipeline, but at
runtime the computed alpha will be 1.0.

---

## Surface Shader NodeDef Reference

| NodeDef | Node | Version | Lit/Unlit | Transparency Inputs | Alpha Mode Inputs | Notes |
|---------|------|---------|-----------|--------------------|--------------------|-------|
| `ND_gltf_pbr_surfaceshader` | `gltf_pbr` | 2.0.1 | Lit | `alpha` (1.0), `transmission` (0.0) | `alpha_mode`, `alpha_cutoff` | 3 alpha modes; always flagged transparent |
| `ND_standard_surface_surfaceshader` | `standard_surface` | 1.0.1 | Lit | `opacity` (color3, 1,1,1), `transmission` (0.0) | — | Always blend when transparent |
| `ND_open_pbr_surface_surfaceshader` | `open_pbr_surface` | 1.1 | Lit | `geometry_opacity` (hint, 1.0), `transmission_weight` (hint, 0.0) | — | Uses hint system |
| `ND_UsdPreviewSurface_surfaceshader` | `UsdPreviewSurface` | 2.6 | Lit | `opacity` (1.0) | `opacityMode`, `opacityThreshold` | Always cutout-style, no blend |
| `ND_surface` | `surface` | — | Lit | `opacity` (1.0) | — | Low-level constructor; BSDF/EDF closure inputs |
| `ND_surface_unlit` | `surface_unlit` | — | Unlit | `opacity` (1.0), `transmission` (0.0) | — | No lighting; scalar emission+transmission model |

---

## Validating Cutout Roundtrip

To verify that a material exported as cutout from an external tool (e.g. Unity,
Blender) is correctly represented in MaterialX:

1. Check `alphaMode == "mask"` — this confirms the material is classified as cutout
2. Check `alphaCutoff` matches the expected threshold (typically `0.5`)
3. Check `transparencyInputs` contains an `alpha` entry with the expected value
   (or `"[connected:...]"` if driven by a texture)
4. Note that `transparency` will be `true` for any `gltf_pbr` material regardless
   of alpha mode — this is expected behavior due to conservative static analysis
5. For `gltf_pbr`: verify `alpha_mode == 1` in the source MTLX
6. For `UsdPreviewSurface`: any transparent material is automatically cutout (no
   blend mode exists)

### Example: verifying a cutout material roundtrip

Source material in Unity: alpha cutout with threshold 0.5

Expected report output:
```json
{
  "shaderNode": "gltf_pbr",
  "transparency": true,
  "alphaMode": "mask",
  "alphaCutoff": 0.5,
  "transparencyInputs": [
    {"name": "alpha", "valueType": "float", "value": "[connected:image]", "opaqueAt": 1}
  ]
}
```

If `alphaMode` is `"opaque"` or `"blend"` instead of `"mask"`, the cutout
information was lost during export.
