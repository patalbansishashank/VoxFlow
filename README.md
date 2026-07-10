# VoxFlow

Push-to-talk speech-to-text bar widget for **Noctalia v4** (Quickshell-based desktop environment on Wayland).

Record your voice with a hotkey, get instant transcription via **Soniox** or **Sarvam AI** streaming WebSocket APIs, and have the result pasted directly into your active window.

## Features

- **Push-to-talk** — hold/press a key, speak, release, text appears
- **Dual provider** — Soniox ($0.12/hr) or Sarvam AI (~₹30/hr)
- **WebSocket streaming** — no post-recording latency, transcribes as you speak
- **Audio visualizer** — reactive ring animation in your bar widget
- **Hinglish support** — Hindi words Romanized automatically with Soniox `language_hints_strict`
- **C++ backend** — fast startup (~5ms), low memory (~8MB), portable to native C++ toolkits

## Prerequisites

- **Noctalia v3.6+** (Quickshell-based desktop shell)
- **Niri** (Wayland compositor) or any WM with keybinding support
- Runtime dependencies:
  - `quickshell` — the shell runtime
  - `libcurl` — WebSocket connections
  - `wtype` — simulate keyboard paste
  - `wl-clipboard` — clipboard read/write

## Installation

### Option 1: AUR (recommended)

```bash
paru -S voxflow
```

Then symlink the plugin:

```bash
mkdir -p ~/.config/noctalia/plugins
ln -sf /usr/share/noctalia/plugins/voxflow ~/.config/noctalia/plugins/voxflow
```

### Option 2: Manual

Clone the repo and build:

```bash
git clone https://github.com/patalbansishashank/VoxFlow.git
cd VoxFlow/backend
make
```

Symlink to Noctalia:

```bash
mkdir -p ~/.config/noctalia/plugins
ln -sf /path/to/VoxFlow ~/.config/noctalia/plugins/voxflow
```

## Configuration

### API Keys

Open **VoxFlow Settings** from the Noctalia bar widget menu and enter at least one API key:

| Provider | Sign Up | Pricing |
|---|---|---|
| **Soniox** | [console.soniox.com](https://console.soniox.com) | ~$0.12/hr |
| **Sarvam AI** | [api.sarvam.ai](https://api.sarvam.ai) | ~₹30/hr |

### Keyboard shortcut

Open **VoxFlow Settings** → **Keyboard Shortcut**, click **Record**, and press the chord you
want (default **Super + Z**). VoxFlow registers it for you — no config-file editing.

- **Hyprland:** the plugin binds it live via the Hyprland socket and retires any hand-written
  `plugin:voxflow` bind in `~/.config/hypr/hyprland.lua` (a `.voxflow-kb-backup` is kept). It's
  re-asserted automatically each time the plugin loads.
- **Niri / other compositors:** the plugin can't register binds there, so add one yourself that
  calls the toggle IPC:
  ```kdl
  binds {
      Mod+Z { spawn "qs" "-c" "noctalia-shell" "ipc" "call" "plugin:voxflow" "toggleRecording"; }
  }
  ```

It's a single toggle — press to start recording, press again to stop and transcribe.

### History pickers (Hyprland)

Two more plugin-managed shortcuts open an overlay picker (type to filter, arrow keys,
Enter pastes into the window you came from):

- **Super + Shift + Z** — your past transcripts (recorded locally, never uploaded)
- **Super + V** — general clipboard history via `cliphist` (transcripts filtered out;
  needs `cliphist` + a `wl-paste --watch cliphist store` watcher, which Noctalia runs)

All three shortcuts are editable in **VoxFlow Settings → Keyboard Shortcut** (removing a
picker's pill disables that shortcut).

### Language

Set the language code in Settings (e.g. `en-IN`, `hi-IN`, `en-US`). Soniox uses `language_hints_strict` so only the specified language is transcribed (prevents Devanagari/Bengali script output).

## Building from Source

```bash
cd backend
make clean && make
```

The binary is output to `bin/voxflow-backend`.

### Dependencies (build)

- `gcc` (C++20)
- `make`
- `curl` (development headers)
- `wayland-client` (protocol libraries)

## Project Structure

```
VoxFlow/
├── Main.qml              # Orchestrator (Process, events, timers)
├── BarWidget.qml         # Audio visualizer bar widget
├── Settings.qml          # Configuration UI
├── manifest.json         # Noctalia plugin manifest
├── backend/
│   ├── src/
│   │   ├── main.cpp           # Entry point, JSON-RPC dispatch
│   │   ├── stream_api.cpp     # WebSocket streaming (Soniox/Sarvam)
│   │   ├── audio.cpp          # Miniaudio capture
│   │   ├── clipboard.cpp      # wl-copy/wl-paste/wtype
│   │   ├── json_rpc.cpp       # JSON-RPC protocol
│   │   └── config.h           # AppConfig struct
│   └── vendor/
│       ├── miniaudio.h
│       └── nlohmann/json.hpp
└── PKGBUILD              # Arch packaging
```

## Architecture

```
┌─────────────────────────────────────────────────┐
│  Noctalia Bar (QML)                             │
│  ┌──────────┐  ┌──────────────┐  ┌──────────┐  │
│  │Main.qml  │  │BarWidget.qml │  │Settings  │  │
│  │orchestr. │  │visualizer    │  │  .qml    │  │
│  └────┬─────┘  └──────────────┘  └──────────┘  │
│       │ stdin/stdout JSON-RPC                   │
├───────┼─────────────────────────────────────────┤
│       ▼                                          │
│  voxflow-backend (C++ process)                   │
│  ┌──────────┐  ┌────────────┐  ┌──────────────┐ │
│  │Audio     │─▶│WebSocket   │─▶│Server (sock) │ │
│  │(miniaudio)│  │Stream      │  │Soniox/Sarvam │ │
│  └──────────┘  └─────┬──────┘  └──────────────┘ │
│                       │                          │
│                  ┌────▼──────┐                   │
│                  │Clipboard  │                   │
│                  │wl-copy    │                   │
│                  │wtype paste│                   │
│                  └───────────┘                   │
└─────────────────────────────────────────────────┘
```

## Provider Comparison

| Feature | Soniox | Sarvam AI |
|---|---|---|
| **Pricing** | ~$0.12/hr | ~₹30/hr ($0.36) |
| **Script output** | Latin only (with `language_hints_strict`) | Depends on language code |
| **Hinglish** | Romanized | Romanized with `en-IN` |
| **Auth** | API key in config JSON | API key in HTTP header |
| **Audio framing** | Raw binary PCM | Base64 in JSON |
| **End-of-stream** | WebSocket close frame | `{"type":"flush"}` |

## License

MIT
