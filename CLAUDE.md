# VoxFlow — working notes for Claude

Push-to-talk speech-to-text as a **Noctalia 4 plugin**: hold/toggle a key, speak, and the
transcript is pasted into the focused window. Thin QML front-end + a fast **C++ backend**
(miniaudio capture → Soniox/Sarvam streaming WebSocket → wl-clipboard/wtype paste).

Sibling project with the same deploy/restart shape: **HyperZone** (`../Hyperzone/CLAUDE.md`).
Read that too — the Noctalia restart gotchas below are shared and were learned there.

## Layout & source of truth

- **Repo (edit here):** `/media/DEV/Scripts/Linux/VoxFlow/`
  - `Main.qml` — orchestrator: owns the backend as a child `Process`, JSON-RPC over stdio,
    exposes the `plugin:voxflow` IPC handler (`startRecording`/`stopRecording`/`toggleRecording`).
  - `BarWidget.qml` — the bar widget (reactive audio-level ring).
  - `Settings.qml` — settings pane (API keys, provider, language, append-newline).
  - `backend/` — the C++ backend (`src/*.cpp`, `vendor/` header-only deps, `Makefile`).
  - `manifest.json`, `install.sh`, `README.md`, `PKGBUILD`, `i18n/en.json`.
- **Installed plugin (what actually runs):** `~/.config/noctalia/plugins/voxflow/`
  - Noctalia loads the QML and spawns the backend **from here**, at
    `<pluginDir>/bin/voxflow-backend` (see `Main.qml` `backendPath`).
  - `/media/DEV` is an ntfs3 `nofail` (removable) drive → **build + copy-install, never
    symlink.** A login must not depend on the drive, and the backend must not run off it.
  - It *used* to be a symlink into this repo. That also made Noctalia write `settings.json`
    (your API keys) straight back into the repo — see "Secrets" below. `install.sh` replaces
    the symlink with a real local copy.

## Deploy (after editing the repo)

`install.sh` is the whole story — it builds the backend, copies the plugin payload into the
plugin dir, and preserves your live `settings.json`:

```bash
bash /media/DEV/Scripts/Linux/VoxFlow/install.sh        # build + copy-install (recommended)
bash install.sh --no-build                              # copy-install, reuse existing binary
bash install.sh --link                                  # dev symlink (drive must stay mounted)
```

Targeted iteration without the full script (QML-only change, no backend rebuild):

```bash
SRC=/media/DEV/Scripts/Linux/VoxFlow; DST=~/.config/noctalia/plugins/voxflow
cp "$SRC"/{Main.qml,BarWidget.qml,Settings.qml,manifest.json} "$DST/"
```

Either way the change is **not live until `qs` restarts** (next section).

## Building the C++ backend

```bash
make -C backend            # -> bin/voxflow-backend
make -C backend clean && make -C backend
```

- Toolchain: `g++` (C++20), `make`. Libs: `libcurl`, `wayland-client`, `-lm -pthread`
  (all present on this machine). Runtime tools: `wtype`, `wl-clipboard`.
- Vendored headers (`vendor/miniaudio.h`, `vendor/nlohmann/json.hpp`) are committed, so a
  build needs **no network**. The Makefile only curls them if missing.
- `backend/build/`, `bin/`, `*.o`, `*.d` are git-ignored — build artifacts stay out of the repo.

## Restarting Noctalia to test — READ THIS, it has teeth

The plugin's QML is **compiled/loaded once when `qs` (Quickshell) starts**; editing files on
disk does NOT hot-reload it, and copy-installing over a running session does not either. You
must restart the `qs` process. Same footguns as HyperZone (see [[hyperzone-project]]):

1. **The process arg is `qs -c noctalia-shell`, NOT `/usr/bin/qs …`.** `pgrep`/`kill` patterns
   matching `/usr/bin/qs` match nothing → the "restart" is a silent no-op.
2. **`pkill -f "qs -c noctalia-shell"` also matches THIS shell's own command line** and kills
   your own tool call (exit 144) before the relaunch runs. Kill by explicit PID.
3. **Nothing auto-restarts it** — the login `qs` runs in a `session-NN.scope`, not a service.
   Kill it and you MUST relaunch it or the user loses their bar.
4. **Launch detached** via a transient systemd user unit (Hyprland's Lua config makes
   `dispatch exec` unreliable).

Reliable restart sequence:

```bash
rm -rf ~/.cache/noctalia-qs/qmlcache
PID=$(pgrep -f "qs -c noctalia-shell" | head -1); kill "$PID"; sleep 2   # kills bar (+ voxflow backend)
systemd-run --user \
  --setenv=WAYLAND_DISPLAY="$WAYLAND_DISPLAY" \
  --setenv=HYPRLAND_INSTANCE_SIGNATURE="$HYPRLAND_INSTANCE_SIGNATURE" \
  --setenv=XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" --setenv=DISPLAY="$DISPLAY" \
  --unit=noctalia-manual-restart --collect  qs -c noctalia-shell
sleep 5
ps -eo pid,lstart,args | grep "qs -c noctalia-shell" | grep -v grep   # start time must be ~now
pgrep -af voxflow-backend                                             # backend respawned (Main.qml keeps it running)
journalctl --user -u noctalia-manual-restart --since "40 sec ago" | grep -iE "voxflow|error"
```

The bar disappears for ~2–5 s; that's expected. The user normally starts Noctalia from their
Hyprland autostart, so this transient unit is only for the test cycle. **Restarting the shell
is the #1 footgun on this machine — prefer to avoid it unless you actually need to test QML.**

## Keybind

VoxFlow exposes an IPC toggle; bind it to a key that calls:

```
qs -c noctalia-shell ipc call plugin:voxflow toggleRecording
```

- **This machine runs Hyprland with the Lua / non-legacy config parser** (like HyperZone).
  Add to `~/.config/hypr/hyprland.lua`:
  ```lua
  hl.bind("SUPER + Z", hl.dsp.exec_cmd("qs -c noctalia-shell ipc call plugin:voxflow toggleRecording"))
  ```
  Or register it live (no reload) via the running Hyprland socket:
  `eval hl.bind("SUPER + Z", hl.dsp.exec_cmd("qs -c noctalia-shell ipc call plugin:voxflow toggleRecording"))`.
  `hyprctl keyword bind …` is rejected here ("keyword can't work with non-legacy parsers").
- **Niri** (documented in README): `Mod+Z { spawn "qs" "-c" "noctalia-shell" "ipc" "call" "plugin:voxflow" "toggleRecording"; }`.
- It's a single toggle — press to start recording, press again to stop and transcribe.

## Secrets / settings.json — IMPORTANT

- `pluginApi.pluginSettings` is persisted by Noctalia to `<pluginDir>/settings.json`. It holds
  the **Soniox and Sarvam API keys**, provider, language. This is **user data, not repo data.**
- `settings.json` is **git-ignored** — never commit it. When the plugin dir was a symlink into
  this repo, Noctalia wrote the keys into the repo and they were committed in the initial commit
  (public GitHub), so **those keys are compromised and should be rotated.** A fresh clone gets
  defaults from `manifest.json` (`defaultSettings`); Noctalia creates a new `settings.json`.
- `install.sh` preserves an existing `settings.json` in the plugin dir across reinstalls.

## Git / push

Remote is HTTPS (`github.com/patalbansishashank/VoxFlow`, **public**). No stored git creds /
SSH keys, but the GitHub CLI is authed: `gh auth setup-git` (once) then `git push origin main`.
Solo repo — commits go straight to `main`. Don't stage `settings.json` or `bin/`.

## Architecture rule

Keep the heavy lifting (audio, streaming, clipboard) in the **C++ backend**; QML stays a thin
form + orchestrator that talks JSON-RPC over the backend's stdio. That keeps the plugin portable
and the UI cheap to re-skin for future Noctalia versions.
