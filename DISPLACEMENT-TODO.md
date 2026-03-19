# Vertex Displacement — Implementation Status & Plan

## Overview

Vertex displacement is now working end-to-end for the GLSL backend in MaterialXView and MaterialXGraphEditor. Displacement nodes in the shader graph evaluate their dependency chain (noise, position, math, etc.) in the vertex shader and offset vertex positions before transformation.

## Key Files

### Core Implementation
| File | Purpose |
|------|---------|
| `source/MaterialXGenGlsl/GlslShaderGenerator.cpp` | Main vertex stage emission with displacement detection, dependency chain evaluation, and position offset |
| `source/MaterialXGenGlsl/Nodes/DisplacementNodeGlsl.h/cpp` | GLSL-specific displacement node implementation (struct construction, vertex data connector) |
| `source/MaterialXGenShader/Nodes/SourceCodeNode.cpp` | Modified to allow vertex-stage emission when displacement flag is set |
| `source/MaterialXGenShader/Nodes/HwPositionNode.cpp` | Modified to emit output variable in vertex stage for displacement dependencies |
| `source/MaterialXGenShader/Nodes/MaterialNode.cpp` | Propagates displacement classification |
| `source/MaterialXGenShader/GenContext.h` | Added `emitVertexDisplacement` flag |
| `source/MaterialXGenShader/ShaderNode.h/cpp` | Added `DISPLACEMENTSHADER` constant |

### Library Files
| File | Purpose |
|------|---------|
| `libraries/pbrlib/pbrlib_defs.mtlx` | Node definitions: `ND_displacement_float`, `ND_displacement_vector3` |
| `libraries/pbrlib/genglsl/mx_displacement_float.glsl` | Float → vec3(d,d,d) conversion |
| `libraries/pbrlib/genglsl/mx_displacement_vector3.glsl` | Direct vec3 pass-through |
| `libraries/pbrlib/genglsl/pbrlib_genglsl_impl.mtlx` | Implementation mappings |

### Test Materials
| File | Description |
|------|-------------|
| `resources/Materials/TestSuite/pbrlib/displacement/displacement.mtlx` | Standalone displacement graphs (fractal3d float & vector3) |
| `resources/Materials/TestSuite/pbrlib/displacement/displaced_material.mtlx` | Surface + displacement combined (amplitude=10, too high for shaderball) |

---

## Done

- [x] **Displacement node detection** in shader graph (`Type::DISPLACEMENTSHADER`)
- [x] **DisplacementNodeGlsl** implementation — constructs `displacementshader` struct in vertex stage, passes via vertex data connector
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

---

## TODO

### P0 — Must Fix

#### Normal Recomputation After Displacement
**Problem:** Vertex normals are passed through unchanged. Displaced surfaces look smooth instead of bumpy because the pixel shader uses the original smooth normals.

**Plan:**
1. After computing `displacedPosition`, compute displaced normals using finite differences:
   ```glsl
   // Compute tangent-space displacement derivatives
   float eps = 0.001;
   vec3 posX = i_position + i_tangent * eps;
   vec3 posY = i_position + cross(i_normal, i_tangent) * eps;
   // Evaluate displacement at offset positions
   float dX = displaceFunc(posX) - displaceFunc(i_position);
   float dY = displaceFunc(posY) - displaceFunc(i_position);
   // Perturb normal
   vec3 displacedNormal = normalize(i_normal - (dX * i_tangent + dY * bitangent) / eps);
   ```
2. **Alternative (simpler):** Use `dFdx`/`dFdy` in the PIXEL stage on the displaced world position to derive the geometric normal. This avoids vertex shader complexity but gives faceted normals.
3. **Alternative (cheapest):** Skip normal recomputation for now and document it. Many engines do displacement without normal correction for low-frequency displacement.

**Files to modify:** `GlslShaderGenerator.cpp` (vertex stage emission), `SurfaceNodeGlsl.cpp` (pass displaced normal to pixel stage)

#### Texture-Based Displacement
**Problem:** Texture sampling (`image` node) in the vertex shader produces blank output. No shader compile error, but the texture may not be bound for the vertex stage, or `texture()` needs explicit LOD (`textureLod(tex, uv, 0.0)`) in vertex shaders on some platforms.

**Plan:**
1. Check if `HwImageNode` (which is a `SourceCodeNode`) emits correct GLSL for vertex stage — `texture()` should work in GLSL 4.0+ vertex shaders
2. Investigate if the blank output is from the texture uniform not being bound in the vertex stage
3. If `textureLod` is needed, add a vertex-stage variant of the image sampling function
4. Test with explicit LOD override

**Files to modify:** `HwImageNode.cpp` or the image sampling GLSL library functions

### P1 — Other Backends

#### MSL (Metal) Backend
**Plan:** Mirror the GLSL implementation:
1. Create `DisplacementNodeMsl.h/cpp` (identical logic to GLSL version but with MSL syntax)
2. Register in `MslShaderGenerator.cpp` (same pattern as GLSL)
3. Modify `MslShaderGenerator::emitVertexStage()` — same displacement detection and emission logic
4. Test with MaterialXView Metal backend (`-DUSE_OPENGL_BACKEND_ON_APPLE_PLATFORM=OFF`)

**Files to create:** `source/MaterialXGenMsl/Nodes/DisplacementNodeMsl.h/cpp`
**Files to modify:** `source/MaterialXGenMsl/MslShaderGenerator.cpp`

#### ESSL (WebGL) Backend
**Plan:** ESSL inherits from GLSL. The `EsslShaderGenerator` extends `GlslShaderGenerator`. Displacement should mostly work if:
1. ESSL version supports vertex texture fetch (ESSL 3.0+ / WebGL 2)
2. `displacementshader` struct is supported (should be)
3. The function definitions emitted in vertex stage are ESSL-compatible

**Risk:** `fractal3d` and other procedural noise functions may not be available in ESSL vertex shaders on all devices. Test on WebGL 2 target.

**Files to modify:** `source/MaterialXGenGlsl/EsslShaderGenerator.cpp` (if vertex stage override exists)

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
  4. Emits public uniforms directly as GLSL declarations
  5. Emits function definitions (SourceCodeNode checks flag)
  6. In main(): emits dependency chain function calls
  7. Emits displacement struct construction
  8. Applies: displacedPosition = position + normal * offset.z * scale
  9. Transforms: gl_Position = viewProj * world * displacedPosition
  10. Emits surface shader vertex data connectors (position/normal)
  11. Clears displacement flag
```

### Key Design Decision: Context Flag vs Stage Override

The `emitVertexDisplacement` flag on `GenContext` was chosen over:
- **Modifying `DEFINE_SHADER_STAGE` macro** — too broad, would affect all nodes
- **Stage name override** — fragile, would confuse other stage-specific logic
- **Separate vertex emission pass** — would require duplicating all node emission logic

The flag approach is surgical: only `SourceCodeNode::emitFunctionCall` and `SourceCodeNode::emitFunctionDefinition` check it, allowing vertex-stage emission for displacement dependency nodes while keeping all other nodes pixel-only.
