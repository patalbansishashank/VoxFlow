#!/usr/bin/env bash
# Install VoxFlow as a Noctalia plugin.
#
#   Source of truth : this folder (edit + version-control here)
#   Plugin runtime  : ~/.config/noctalia/plugins/voxflow/   (Noctalia loads + runs from here)
#   Backend binary  : ~/.config/noctalia/plugins/voxflow/bin/voxflow-backend  (spawned by Main.qml)
#
# We BUILD the C++ backend and COPY the whole plugin into the plugin dir by default
# (not symlink): this source folder lives on /media/DEV, an ntfs3 `nofail` (removable)
# mount, and Noctalia launches the backend from the plugin dir — a login must never
# depend on that drive being present. A symlink also makes Noctalia write your API keys
# (settings.json) straight back into the git repo; a real copy keeps them out.
#
# Usage:
#   bash install.sh            # build + copy-install (recommended)
#   bash install.sh --link     # symlink instead (dev iteration; accepts mount + key-leak risk)
#   bash install.sh --no-build # copy-install using the already-built bin/voxflow-backend
set -euo pipefail

SRC="$(cd "$(dirname "$0")" && pwd)"
PLUGIN_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/noctalia/plugins/voxflow"
LINK=0
BUILD=1
for arg in "$@"; do
  case "$arg" in
    --link)     LINK=1 ;;
    --no-build) BUILD=0 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

echo "── VoxFlow install ───────────────────────────────"

# 1) build the backend (never ship a plugin whose backend won't compile)
if [[ "$BUILD" == 1 ]]; then
  echo "· building C++ backend (make)…"
  if ! make -C "$SRC/backend" >/dev/null; then
    echo "✗ backend build failed — aborting, nothing installed"; exit 1
  fi
  echo "✓ backend built"
fi
if [[ ! -x "$SRC/bin/voxflow-backend" ]]; then
  echo "✗ no backend binary at bin/voxflow-backend (run without --no-build) — aborting"; exit 1
fi
echo "✓ backend binary present"

# 2) dev symlink mode — reintroduces the mount + key-leak risk, on purpose
if [[ "$LINK" == 1 ]]; then
  rm -rf "$PLUGIN_DIR"
  mkdir -p "$(dirname "$PLUGIN_DIR")"
  ln -sfn "$SRC" "$PLUGIN_DIR"
  echo "✓ linked  $PLUGIN_DIR -> $SRC   (dev mode; drive must stay mounted)"
  echo "  Restart Noctalia to load it (see CLAUDE.md)."
  exit 0
fi

# 3) preserve the user's live settings.json (API keys, provider, language).
#    Noctalia writes it into the plugin dir at runtime; reading through a current
#    symlink picks up whatever the running plugin last saved.
KEEP_SETTINGS=""
if [[ -f "$PLUGIN_DIR/settings.json" ]]; then
  KEEP_SETTINGS="$(mktemp)"
  cp "$PLUGIN_DIR/settings.json" "$KEEP_SETTINGS"
  echo "· preserving existing settings.json"
fi

# 4) if the plugin dir is a symlink (old dev install), drop it so we lay down a real
#    local copy. rm on a symlink removes only the link, not the source repo.
if [[ -L "$PLUGIN_DIR" ]]; then
  rm -f "$PLUGIN_DIR"
  echo "· replaced symlink with a real local copy"
fi
mkdir -p "$PLUGIN_DIR/bin"

# 5) copy the plugin payload (skip dev/build cruft, PKGBUILD, .git, settings.json)
for item in manifest.json Main.qml BarWidget.qml Settings.qml HistoryPanel.qml README.md i18n; do
  [[ -e "$SRC/$item" ]] && cp -r "$SRC/$item" "$PLUGIN_DIR/"
done
# install the backend atomically (temp + rename): a copy-in-place would hit ETXTBSY
# if the currently-loaded plugin is still running the old binary from this path.
install -m 755 "$SRC/bin/voxflow-backend" "$PLUGIN_DIR/bin/voxflow-backend.new"
mv -f "$PLUGIN_DIR/bin/voxflow-backend.new" "$PLUGIN_DIR/bin/voxflow-backend"
echo "✓ copied plugin -> $PLUGIN_DIR"

# 6) restore the user's settings (or seed from the repo copy if none was installed yet)
if [[ -n "$KEEP_SETTINGS" ]]; then
  cp "$KEEP_SETTINGS" "$PLUGIN_DIR/settings.json"; rm -f "$KEEP_SETTINGS"
  echo "✓ restored settings.json"
elif [[ -f "$SRC/settings.json" ]]; then
  cp "$SRC/settings.json" "$PLUGIN_DIR/settings.json"
  echo "✓ seeded settings.json from source"
fi

cat <<EOF

Done. To finish:

  1) Restart Noctalia so it loads the local copy (the running shell still holds the
     old QML in memory). See CLAUDE.md → "Restarting Noctalia". Until then the running
     session keeps using the previous build.

  2) Enable "VoxFlow" in Noctalia → Settings → Plugins → Installed (if not already),
     and set your API key(s) in the plugin's settings.

  3) Set the toggle shortcut in the plugin's settings → "Keyboard Shortcut" (defaults
     to SUPER + Z). VoxFlow registers it in Hyprland for you and retires any hand-written
     bind in ~/.config/hypr/hyprland.lua (a .voxflow-kb-backup is kept). No manual editing.

The plugin now runs from local disk — /media/DEV no longer needs to be mounted at login.
EOF
