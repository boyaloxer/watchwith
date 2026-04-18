# WatchWith

A watch-party app for long-distance couples. Share your screen, webcam, and audio with your partner in a simple fullscreen overlay — no alt-tabbing, no UI clutter.

Built on top of [OBS Studio](https://github.com/obsproject/obs-studio)'s `libobs` engine for rock-solid capture, with peer-to-peer WebRTC connectivity via [libdatachannel](https://github.com/paullouisageneau/libdatachannel).

## Features

- **Fullscreen video sharing** — capture any window (browser, media player, etc.) and share it
- **Webcam overlays** — draggable, resizable webcam feeds on top of the shared video
- **Per-app audio capture** — isolate audio from a specific application (Windows)
- **Peer-to-peer** — direct WebRTC connection, no server required
- **Copy-paste connection** — exchange a connection code to link up, no accounts needed
- **Mix-minus audio** — movie audio goes to both users, mic audio only goes to your partner

## Requirements

### Build Dependencies

- **OBS Studio source** (v32+) — clone from https://github.com/obsproject/obs-studio
- **OBS prebuilt dependencies** — downloaded automatically by OBS's CMake
- **CMake** 3.28+
- **Qt6** (Widgets) — included in OBS deps
- **MbedTLS** — included in OBS deps

### Platform-Specific

| Platform | Compiler | Graphics |
|----------|----------|----------|
| Windows  | Visual Studio 2022 (MSVC) | Direct3D 11 |
| macOS    | Xcode / Clang | OpenGL / Metal |
| Linux    | GCC / Clang | OpenGL |

## Building

### 1. Clone OBS Studio

```bash
git clone --recurse-submodules https://github.com/obsproject/obs-studio.git
```

### 2. Clone WatchWith into the OBS tree

```bash
cd obs-studio
git clone --recurse-submodules https://github.com/boyaloxer/watchwith.git watchwith
```

### 3. Add WatchWith to OBS's CMakeLists.txt

Add the following before `message_configuration()` at the end of the root `CMakeLists.txt`:

```cmake
option(ENABLE_WATCHWITH "Build WatchWith app" ON)
if(ENABLE_WATCHWITH AND ENABLE_UI)
  add_subdirectory(watchwith)
endif()
```

### 4. Configure and build

**Windows:**
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DENABLE_BROWSER=OFF -DENABLE_WEBSOCKET=OFF
cmake --build build --config RelWithDebInfo --target watchwith
```

**macOS:**
```bash
cmake -S . -B build -G Xcode -DENABLE_BROWSER=OFF -DENABLE_WEBSOCKET=OFF
cmake --build build --config RelWithDebInfo --target watchwith
```

The built executable will be in `build/rundir/RelWithDebInfo/bin/64bit/`.

## Usage

1. Launch WatchWith
2. Click **+ Window** to share a browser or video player
3. Click **+ Webcam** to add your camera
4. Click **+ App Audio** to capture audio from a specific application
5. Click **Start / Join** to create or join a session
   - **Host**: Click "Start Session", copy the connection code, send it to your partner
   - **Guest**: Paste the host's code, copy the response code, send it back
6. Drag and resize video overlays to your liking

## License

This project uses OBS Studio's libobs (GPL-2.0) and libdatachannel (MPL-2.0).
