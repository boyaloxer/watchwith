# WatchWith

A watch-party app for long-distance couples. Share your screen, webcam, and audio with your partner in a simple fullscreen overlay — no alt-tabbing, no UI clutter.

Built on [OBS Studio](https://github.com/obsproject/obs-studio)'s `libobs` engine for rock-solid A/V capture, with peer-to-peer WebRTC connectivity via [libdatachannel](https://github.com/paullouisageneau/libdatachannel).

## Features

- **Fullscreen video sharing** — capture any window (browser, media player, etc.)
- **Webcam overlays** — draggable, resizable webcam feeds on top of the shared video
- **Per-app audio capture** — isolate audio from a specific application (Windows)
- **Peer-to-peer** — direct WebRTC connection, no server required
- **Copy-paste connection** — exchange a connection code to link up, no accounts needed
- **Mix-minus audio** — movie audio goes to both users, mic audio only goes to your partner

## Quick Start (Windows)

### Prerequisites

- **Visual Studio 2022** with the "Desktop development with C++" workload
- **Git**

### Build

```powershell
git clone --recurse-submodules https://github.com/boyaloxer/watchwith.git
cd watchwith
.\scripts\build-windows.ps1
```

The build script automatically:
1. Initializes all submodules (OBS, libdatachannel)
2. Downloads OBS's prebuilt dependencies (~1 GB)
3. Builds OBS's core library and plugins
4. Builds WatchWith

### Run

```powershell
.\scripts\run-windows.ps1
```

## Usage

1. Launch WatchWith
2. Click **+ Window** to share a browser or video player
3. Click **+ Webcam** to add your camera
4. Click **+ App Audio** to capture audio from a specific application
5. Click **Start / Join** to create or join a session
   - **Host**: Click "Start Session", copy the connection code, send it to your partner
   - **Guest**: Paste the host's code, copy the response code, send it back
6. Drag and resize video overlays to your liking

## Project Structure

```
watchwith/
├── CMakeLists.txt              # Top-level build (drives everything)
├── src/
│   ├── main.cpp                # Entry point
│   ├── app.h/cpp               # libobs lifecycle, scene, source management
│   ├── ui/
│   │   ├── main-window.h/cpp   # Main window, toolbar, menus
│   │   ├── canvas-view.h/cpp   # OBS display, drag/resize overlays
│   │   └── session-dialog.h/cpp # Connection UI
│   └── net/
│       ├── session.h/cpp       # Session state management
│       ├── peer-connection.h/cpp # WebRTC via libdatachannel
│       └── media-bridge.h/cpp  # OBS ↔ WebRTC A/V pipeline
├── deps/
│   ├── obs-studio/             # OBS source (git submodule)
│   └── libdatachannel/         # WebRTC library (git submodule)
└── scripts/
    ├── build-windows.ps1       # One-command Windows build
    └── run-windows.ps1         # Launch helper
```

## How It Works

WatchWith uses OBS's `libobs` as a library — not as a plugin. It creates its own OBS scene, loads OBS plugins (window capture, webcam, audio), and renders everything through OBS's GPU-accelerated compositor. For networking, raw video/audio frames from the OBS pipeline are sent over a WebRTC DataChannel (via libdatachannel with STUN for NAT traversal). On the receiving end, frames are pushed into a custom OBS source that appears as a draggable overlay.

## License

This project uses OBS Studio's libobs (GPL-2.0) and libdatachannel (MPL-2.0).
