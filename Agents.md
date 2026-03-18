# MaterialX Build & Run Runbook (Agents)

This file is a practical checklist for future local runs on macOS (and similar Unix shells).

## Human-only actions protocol

If a required step can only be performed by the user (for example: re-enabling Unity MCP, clicking editor UI, granting permissions, or starting an external app), the agent should use the ASK tool to request that action, wait for completion, and then continue execution immediately.

Do not stop with a generic blocker message when ASK can be used.

## Validated status (2026-03-17)

The commands in this runbook were exercised on this machine.

- Desktop: `MaterialXView` and `MaterialXGraphEditor` run, including `--captureFilename` image output.
- Python: `PyMaterialX*.so` copy + `mxvalidate.py` run successfully on a repo sample `.mtlx`.
- WASM: clean build succeeded with emsdk `3.1.74` and `-DMATERIALX_BUILD_USE_CCACHE=OFF`. Note: emsdk `3.1.74` no longer works with the current codebase (requires `rvp::default_tag` changes from commit 78757ecc).
- Web viewer: `npm start` runs after aligning `webpack-cli` to v5 (`npm i -D webpack-cli@^5`).

## TL;DR

- Desktop apps (`MaterialXView`, `MaterialXGraphEditor`) and Python bindings build from the same `build/` directory.
- Web/WASM (`JsMaterialX`, web viewer) builds from `javascript/build/` with emscripten enabled.
- If you switch between desktop and WASM builds, always reconfigure explicitly to avoid stale cache options.

---

## 1) Prerequisites (macOS)

### Core tools

```bash
sudo xcode-select -switch /Applications/Xcode.app/Contents/Developer
xcodebuild -runFirstLaunch
```

If Metal shader compile fails with:

`cannot execute tool 'metal' due to missing Metal Toolchain`

install it once:

```bash
xcodebuild -downloadComponent MetalToolchain
```

Make sure CMake CLI is on `PATH` (if using the app bundle install):

```bash
PATH="/Applications/CMake.app/Contents/bin":"$PATH"
```

---

## 2) Build Desktop Apps + Python Bindings

From repo root:

```bash
cd /Users/herbst/git/MaterialX
mkdir -p build
cd build

PATH="/Applications/CMake.app/Contents/bin":"$PATH"
cmake .. \
  -DMATERIALX_BUILD_JS=OFF \
  -DMATERIALX_BUILD_GRAPH_EDITOR=ON \
  -DMATERIALX_BUILD_VIEWER=ON \
  -DMATERIALX_BUILD_TESTS=ON \
  -DMATERIALX_BUILD_PYTHON=ON

cmake --build . --config Release -j8
```

Notes:

- `-DMATERIALX_BUILD_JS=OFF` is important for desktop builds. If it is `ON` in cache, desktop builds may fail with `emscripten/bind.h` not found.
- Prefer `cmake --build ...` over mixing with raw `make` commands.

### Optional: OpenGL backend on Apple

```bash
cd /Users/herbst/git/MaterialX/build
cmake .. -DUSE_OPENGL_BACKEND_ON_APPLE_PLATFORM=ON
cmake --build . --config Release -j8
```

---

## 3) Run Desktop Apps

From `build/`:

```bash
./bin/MaterialXView --help
./bin/MaterialXGraphEditor --help
```

If `MaterialXGraphEditor` is missing, build it explicitly:

```bash
cd /Users/herbst/git/MaterialX/build
cmake --build . --target MaterialXGraphEditor --config Release -j8
```

### MaterialXView command-line render

```bash
./bin/MaterialXView \
  --material /path/to/file.mtlx \
  --captureFilename /path/to/output.png \
  --screenWidth 512 \
  --screenHeight 512
```

### Turntable sequence + GIF

```bash
./bin/MaterialXView \
  --material /path/to/file.mtlx \
  --captureFilename ResultingImage.png \
  --screenWidth 512 \
  --screenHeight 512 \
  --enableTurntable true \
  --turntableSteps 32

ffmpeg -framerate 16 -i ResultingImage_%04d.png output.gif -y
```

### Graph editor capture

```bash
./bin/MaterialXGraphEditor \
  --material /path/to/file.mtlx \
  --captureFilename /path/to/output.png \
  --uiScale 1
```

---

## 4) Python Bindings + `mxvalidate`

After build, copy extension modules for local script usage:

```bash
cd /Users/herbst/git/MaterialX
cp build/lib/PyMaterialX*.so python/MaterialX/
```

Run validator:

```bash
PYTHONPATH=/Users/herbst/git/MaterialX/python \
python3 python/Scripts/mxvalidate.py /path/to/file.mtlx
```

---

## 5) Build Web/WASM Viewer (`JsMaterialX`)

### Activate emsdk

```bash
cd /Users/herbst/git/emsdk
./emsdk install latest
./emsdk activate latest
source /Users/herbst/git/emsdk/emsdk_env.sh
```

If `latest` fails for this branch (e.g. embind `toWireType` mismatch), use:

```bash
cd /Users/herbst/git/emsdk
./emsdk install 3.1.74
./emsdk activate 3.1.74
source /Users/herbst/git/emsdk/emsdk_env.sh
```

### Configure + build JS target

```bash
cd /Users/herbst/git/MaterialX
PATH="/Applications/CMake.app/Contents/bin":"$PATH"

cmake -S . -B javascript/build \
  -DMATERIALX_BUILD_JS=ON \
  -DMATERIALX_EMSDK_PATH=/Users/herbst/git/emsdk \
  -DMATERIALX_BUILD_USE_CCACHE=OFF \
  -G Ninja

cmake --build javascript/build --target install --config RelWithDebInfo --parallel 2
```

If switching emsdk versions or seeing linker summary-version errors, clean first:

```bash
rm -rf /Users/herbst/git/MaterialX/javascript/build
```

### Run web viewer

```bash
cd /Users/herbst/git/MaterialX/javascript/MaterialXView
npm i
npm start
```

Open the printed localhost URL (commonly `http://localhost:8080/` or `http://localhost:8082/`).

If dev-server fails with unknown option `_assetEmittingPreviousFiles`, run:

```bash
cd /Users/herbst/git/MaterialX/javascript/MaterialXView
npm i -D webpack-cli@^5
npm start
```

---

## 6) Switching Desktop ↔ WASM Safely

### Desktop config (no emscripten)

```bash
cd /Users/herbst/git/MaterialX/build
cmake .. -DMATERIALX_BUILD_JS=OFF
```

### WASM config (emscripten)

```bash
cd /Users/herbst/git/MaterialX
cmake -S . -B javascript/build -DMATERIALX_BUILD_JS=ON -DMATERIALX_EMSDK_PATH=/Users/herbst/git/emsdk -G Ninja
```

If settings seem stuck, remove cache/build dir and reconfigure:

```bash
rm -rf /Users/herbst/git/MaterialX/build/CMakeCache.txt /Users/herbst/git/MaterialX/build/CMakeFiles
# or fully clean:
rm -rf /Users/herbst/git/MaterialX/build
```

---

## 7) Common Errors

### `emscripten/bind.h` file not found

Cause: `MATERIALX_BUILD_JS` is `ON` in desktop build cache.

Fix:

```bash
cd /Users/herbst/git/MaterialX/build
cmake .. -DMATERIALX_BUILD_JS=OFF
cmake --build . --target MaterialXView --config Release -j8
```

### `zsh: no such file or directory: ./bin/MaterialXView`

Cause: wrong current directory.

Fix: run from `build/`, or use absolute path:

```bash
/Users/herbst/git/MaterialX/build/bin/MaterialXView
```

### `Invalid summary version 12` during JS/WASM link

Cause: stale/incompatible build artifacts when switching emsdk versions (often with cached objects).

Fix:

```bash
cd /Users/herbst/git/MaterialX
rm -rf javascript/build

cd /Users/herbst/git/emsdk
./emsdk activate 3.1.74
source /Users/herbst/git/emsdk/emsdk_env.sh

cd /Users/herbst/git/MaterialX
cmake -S . -B javascript/build \
  -DMATERIALX_BUILD_JS=ON \
  -DMATERIALX_EMSDK_PATH=/Users/herbst/git/emsdk \
  -DMATERIALX_BUILD_USE_CCACHE=OFF \
  -G Ninja
cmake --build javascript/build --target install --config RelWithDebInfo --parallel 2
```
