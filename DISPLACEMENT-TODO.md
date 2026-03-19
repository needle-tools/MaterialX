# Vertex Displacement — Implementation Status & Plan

## Overview

Vertex displacement is working end-to-end for GLSL and ESSL backends. Displacement nodes in the shader graph evaluate their dependency chain (noise, position, math, texture) in the vertex shader and offset vertex positions before transformation. Both procedural (fractal3d) and texture-based (image node) displacement are supported.

## Key Files

### Core Implementation
| File | Purpose |
|------|---------|
| `source/MaterialXGenGlsl/GlslShaderGenerator.cpp` | Main vertex stage emission with displacement detection, dependency chain evaluation, and position offset |
| `source/MaterialXGenGlsl/Nodes/DisplacementNodeGlsl.h/cpp` | GLSL/ESSL displacement node implementation (struct construction, float marker varying) |
| `source/MaterialXGenGlsl/Nodes/SurfaceNodeGlsl.cpp` | Normal recomputation via dFdx/dFdy when displacement marker is detected |
| `source/MaterialXGenShader/Nodes/SourceCodeNode.cpp` | Modified to allow vertex-stage emission when displacement flag is set |
| `source/MaterialXGenShader/Nodes/HwPositionNode.cpp` | Emits output variable in vertex stage for displacement dependencies |
| `source/MaterialXGenShader/Nodes/HwTexCoordNode.cpp` | Emits output variable in vertex stage for texture displacement deps |
| `source/MaterialXGenShader/GenContext.h` | `emitVertexDisplacement` flag |
| `source/MaterialXGenShader/HwShaderGenerator.h/cpp` | `T_DISPLACEMENT_ACTIVE` constant for vertex data marker |

### Library Files
| File | Purpose |
|------|---------|
| `libraries/pbrlib/pbrlib_defs.mtlx` | Node definitions: `ND_displacement_float`, `ND_displacement_vector3` |
| `libraries/pbrlib/genglsl/mx_displacement_float.glsl` | Float → vec3(d,d,d) conversion |
| `libraries/pbrlib/genglsl/mx_displacement_vector3.glsl` | Direct vec3 pass-through |

### Test Materials
| File | Description |
|------|-------------|
| `resources/Materials/TestSuite/pbrlib/displacement/displacement.mtlx` | Standalone displacement graphs (fractal3d float & vector3) |
| `resources/Materials/TestSuite/pbrlib/displacement/displaced_material.mtlx` | Surface + displacement combined (amplitude=10, extreme) |
| `resources/Materials/TestSuite/pbrlib/displacement/texture_displacement.mtlx` | Texture-based displacement (image node sampling) |

---

## Done

- [x] **Displacement node detection** in shader graph (`Type::DISPLACEMENTSHADER`)
- [x] **DisplacementNodeGlsl** implementation — constructs `displacementshader` struct in vertex stage
- [x] **Dependency chain evaluation** — recursively collects upstream nodes, emits in topological order in vertex stage
- [x] **Context flag** (`emitVertexDisplacement`) gates vertex-stage emission in `SourceCodeNode`
- [x] **Function definitions** emitted for both stages when displacement is active
- [x] **HwPositionNode** emits output variable in vertex stage
- [x] **MaterialNode** classification propagation for displacement
- [x] **Public uniforms** emitted directly as GLSL uniform declarations in vertex shader (bypassing uniform block system)
- [x] **Float displacement** applied along vertex normal (`offset.z * scale`)
- [x] **Vector3 displacement** applied directly (`offset * scale`)
- [x] **No regression** on non-displacement materials
- [x] **MaterialXView** renders displacement correctly
- [x] **MaterialXGraphEditor** renders displacement correctly (shares GlslShaderGenerator)
- [x] **Procedural noise** displacement tested (fractal3d, dotproduct)
- [x] **Normal recomputation** via dFdx/dFdy of displaced world position in pixel stage
- [x] **HwTexCoordNode** emits output variable in vertex stage for texture displacement deps
- [x] **Token substitution** ($fileTransformUv) set before vertex stage emission
- [x] **Texture-based displacement** — sampler2D uniforms emitted without file path initializers, string-type inputs (enums) mapped to int correctly
- [x] **ESSL (WebGL 2) support** — float marker varying instead of struct (ESSL 300 doesn't support struct varyings), no uniform initializers
- [x] **Displacement detection** via `displacementActive` float marker in vertex data (compatible with both GLSL interface blocks and ESSL flat varyings)

---

## TODO

### P0 — Must Fix

#### Normal Recomputation After Displacement — DONE
**Implemented:** Using `dFdx`/`dFdy` in the pixel stage on the displaced world position. Gives correct per-fragment geometric normals with faceted appearance. Detected by checking for `displacementActive` marker in vertex data block.

**Future improvement:** Smooth displaced normals via vertex shader finite differences or tangent-space perturbation (would give smoother results than dFdx/dFdy which is faceted).

#### Texture-Based Displacement — DONE
**Fixed:** Two bugs in vertex stage uniform emission:
1. sampler2D uniforms were emitted with file path initializers (`uniform sampler2D tex = path/file.png;`) — the `/` caused GLSL syntax errors
2. String-typed uniforms (layer, framerange enums) were incorrectly skipped — GLSL maps these to `int` via `GlslStringTypeSyntax`

### P1 — Other Backends

#### MSL (Metal) Backend
**Plan:** Mirror the GLSL implementation:
1. Create `DisplacementNodeMsl.h/cpp` (identical logic to GLSL version but with MSL syntax)
2. Register in `MslShaderGenerator.cpp` (same pattern as GLSL)
3. Modify `MslShaderGenerator::emitVertexStage()` — same displacement detection and emission logic
4. Test with MaterialXView Metal backend (`-DUSE_OPENGL_BACKEND_ON_APPLE_PLATFORM=OFF`)

**Files to create:** `source/MaterialXGenMsl/Nodes/DisplacementNodeMsl.h/cpp`
**Files to modify:** `source/MaterialXGenMsl/MslShaderGenerator.cpp`

#### ESSL (WebGL) Backend — DONE
**Implemented:** ESSL inherits from GLSL. Three ESSL-specific fixes applied:
1. Float marker varying (`displacementActive`) instead of struct (ESSL 300 doesn't support struct varyings)
2. Uniform declarations without initializers (ESSL 300 doesn't support `uniform int x = 0;`)
3. Correct varying assignment (no local variable shadowing)

#### WebGPU (WGSL) Backend
**Plan:** The WGSL backend (`source/MaterialXGenWgsl/`) would need:
1. WGSL-specific displacement node implementation
2. WGSL struct definition for `displacementshader`
3. WGSL vertex stage emission with displacement

**Files to create:** `source/MaterialXGenWgsl/Nodes/DisplacementNodeWgsl.h/cpp`

### P2 — Architecture Cleanup

#### Uniform Block System Fix
**Problem:** Adding uniforms to the vertex stage's `PUBLIC_UNIFORMS` block breaks MaterialXView's uniform binding. The `GlslProgram::updateUniformsList()` processes pixel stage uniforms first, then vertex stage — when the same uniform name exists in both, the vertex stage processing overwrites the pixel stage's value/path metadata, causing incorrect uniform binding.

**Plan:**
1. In `GlslProgram::updateUniformsList()` (line 1034-1060), skip vertex stage uniforms that already exist in `_uniformList` from the pixel stage (they're the same GLSL uniform)
2. Or: change the vertex stage processing to only ADD new entries, not overwrite existing ones
3. Then the uniform block approach in `HwShaderGenerator.cpp` can be used properly (add to both stages' blocks) and the direct GLSL emission workaround can be removed

**Files to modify:** `source/MaterialXRenderGlsl/GlslProgram.cpp` (lines 1034-1060)

#### Remove HwPositionNode Vertex Output Bloat
**Problem:** `HwPositionNode` now always emits `vec3 pos_out = i_position` in the vertex stage even for non-displacement materials.

**Plan:** Guard the extra emission with a displacement check (e.g. check if the node is in a displacement dependency chain, or if the context flag is set).

**Files to modify:** `source/MaterialXGenShader/Nodes/HwPositionNode.cpp`

### P3 — Web Integration

#### Three.js / Needle Engine
**Plan:**
1. The ESSL shader generator produces vertex + fragment shaders
2. Three.js `ShaderMaterial` supports custom vertex shaders with displacement
3. The `MaterialXMaterial` class in `needle-engine-materialx` already patches vertex shaders
4. Need to ensure texture uniforms are bound to the vertex stage in Three.js
5. Test with WebGL 2 (required for vertex texture fetch)

**Files to modify:** `modules/needle-engine/modules/needle-engine-materialx/src/materialx.material.js`

---

## Architecture Notes

### How Displacement Flows Through the System

```
MaterialX Document (.mtlx)
  └── surfacematerial
       ├── surfaceshader → standard_surface (pixel stage)
       └── displacementshader → displacement node
            └── displacement input (float/vec3)
                 └── dependency chain (noise, position, math, texture)

Shader Generation:
  1. GlslShaderGenerator::emitVertexStage() detects displacement node
  2. Collects dependency set (recursive upstream walk)
  3. Sets context.emitVertexDisplacement = true
  4. Emits public uniforms directly as GLSL declarations (skipping initializers for ESSL)
  5. Emits function definitions (SourceCodeNode checks flag)
  6. In main(): emits dependency chain function calls
  7. Emits displacement struct construction
  8. Sets displacementActive = 1.0 marker varying
  9. Applies: displacedPosition = position + normal * offset.z * scale
  10. Transforms: gl_Position = viewProj * world * displacedPosition
  11. Emits surface shader vertex data connectors (position/normal)
  12. Clears displacement flag

Normal Recomputation (pixel stage):
  1. SurfaceNodeGlsl checks vertex data for displacementActive marker
  2. If found: N = normalize(cross(dFdx(positionWorld), dFdy(positionWorld)))
  3. If not: N = normalize(normalWorld)
```

### Key Design Decisions

**Context Flag (`emitVertexDisplacement`):** Only `SourceCodeNode::emitFunctionCall` and `SourceCodeNode::emitFunctionDefinition` check it, allowing vertex-stage emission for displacement dependency nodes while keeping all other nodes pixel-only.

**Float Marker Varying:** The `displacementActive` float varying was chosen over passing the full `displacementshader` struct because ESSL 300 (WebGL 2) does not support struct varyings. The float marker is compatible with both GLSL interface blocks and ESSL flat varyings, and the pixel stage only needs to know displacement is active (not the actual values).

**Direct Uniform Emission:** Public uniforms for displacement are emitted as direct `uniform` declarations in the generated GLSL, bypassing the uniform block system. This avoids a MaterialXView bug where `GlslProgram::updateUniformsList()` corrupts uniform bindings when the same name appears in both vertex and pixel stage uniform blocks.
