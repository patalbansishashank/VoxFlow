import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Quickshell.Hyprland
import qs.Commons
import qs.Services.UI

// Overlay picker for past transcripts ("transcripts" mode) or general clipboard
// history via cliphist ("clipboard" mode, VoxFlow transcripts filtered out).
// Arrow keys / mouse to select, Enter/click to paste into the previously focused
// window, Esc (or clicking the dimmer) to close. Opened by Main.qml from the
// plugin-owned SUPER+SHIFT+Z / SUPER+V chords.
PanelWindow {
  id: win

  property var main: null
  property string mode: "transcripts"   // "transcripts" | "clipboard"
  property var transcripts: []          // [{ts, text}] — data in transcripts mode,
                                        // filter list in clipboard mode
  signal dismissed()

  // {label, sub, data} — data is the full text (transcripts) or cliphist id (clipboard)
  property var entries: []
  property string filter: ""
  property int selIdx: 0
  property bool busy: mode === "clipboard" && !clipCollector.done

  // Show on the monitor the user is actually on (falls back to default otherwise).
  screen: Quickshell.screens.find(s => s.name === Hyprland.focusedMonitor?.name) ?? null

  anchors { top: true; bottom: true; left: true; right: true }
  color: "transparent"
  visible: true

  WlrLayershell.namespace: "voxflow-history"
  WlrLayershell.layer: WlrLayer.Overlay
  WlrLayershell.keyboardFocus: WlrKeyboardFocus.Exclusive
  WlrLayershell.exclusionMode: ExclusionMode.Ignore

  function norm(s) { return (s || "").replace(/\s+/g, " ").trim() }

  function isTranscript(preview) {
    var p = norm(preview).slice(0, 50).toLowerCase()
    if (!p.length) return false
    for (var i = 0; i < transcripts.length; i++) {
      var t = norm(transcripts[i].text).slice(0, 50).toLowerCase()
      if (t.length && (p.indexOf(t) === 0 || t.indexOf(p) === 0)) return true
    }
    return false
  }

  Component.onCompleted: {
    if (mode === "transcripts") {
      var out = []
      for (var i = 0; i < transcripts.length; i++) {
        var it = transcripts[i]
        out.push({
          label: norm(it.text),
          sub: it.ts ? Qt.formatDateTime(new Date(it.ts * 1000), "MMM d, hh:mm") : "",
          data: it.text
        })
      }
      entries = out
    } else {
      clipProc.running = true
    }
  }

  // cliphist list -> "id\tpreview" per line; drop entries that are our transcripts.
  Process {
    id: clipProc
    command: ["cliphist", "list"]
    stdout: StdioCollector {
      id: clipCollector
      property bool done: false
      onStreamFinished: {
        var out = []
        var lines = text.split("\n")
        for (var i = 0; i < lines.length; i++) {
          if (!lines[i].length) continue
          var tab = lines[i].indexOf("\t")
          if (tab <= 0) continue
          var preview = lines[i].slice(tab + 1)
          if (win.isTranscript(preview)) continue
          out.push({ label: win.norm(preview), sub: "", data: lines[i].slice(0, tab) })
        }
        win.entries = out
        done = true
      }
    }
  }

  // Selected clipboard entry -> clipboard, then compositor Ctrl+V (via Main.qml,
  // delayed so focus has returned to the previous window first).
  Process {
    id: decodeProc
    property string entryId: ""
    command: ["sh", "-c", "cliphist decode '" + entryId + "' | wl-copy"]
    onExited: {
      if (win.main) win.main.sendPasteSoon()
      win.dismissed()
    }
  }

  readonly property var shown: {
    var f = filter.toLowerCase()
    if (!f.length) return entries
    var out = []
    for (var i = 0; i < entries.length; i++)
      if (entries[i].label.toLowerCase().indexOf(f) !== -1) out.push(entries[i])
    return out
  }
  onShownChanged: selIdx = 0
  onSelIdxChanged: list.positionViewAtIndex(selIdx, ListView.Contain)

  function choose() {
    if (selIdx < 0 || selIdx >= shown.length) return
    var e = shown[selIdx]
    if (mode === "transcripts") {
      if (main) main.pasteText(e.data)
      dismissed()
    } else {
      decodeProc.entryId = e.data
      decodeProc.running = true
    }
  }

  // Dimmer (click to close)
  Rectangle {
    anchors.fill: parent
    color: Qt.alpha(Color.mSurface, 0.35)
    MouseArea { anchors.fill: parent; onClicked: win.dismissed() }
  }

  // Centered card
  Rectangle {
    id: card
    anchors.centerIn: parent
    width: Math.min(Math.round(620 * Style.uiScaleRatio), parent.width - Style.marginL * 2)
    height: Math.min(Math.round(480 * Style.uiScaleRatio), parent.height - Style.marginL * 2)
    radius: Style.radiusL
    color: Color.mSurface
    border.color: Style.boxBorderColor
    border.width: Style.borderS

    Column {
      anchors.fill: parent
      anchors.margins: Style.marginL
      spacing: Style.marginM

      Text {
        text: win.mode === "transcripts" ? "VoxFlow — Transcript History"
                                         : "VoxFlow — Clipboard History"
        color: Color.mOnSurface
        font.pixelSize: Math.round(16 * Style.uiScaleRatio)
        font.weight: Font.DemiBold
      }

      // Search box
      Rectangle {
        width: parent.width
        height: Math.round(36 * Style.uiScaleRatio)
        radius: Style.radiusM
        color: Qt.alpha(Color.mOnSurface, 0.06)
        border.color: Style.boxBorderColor
        border.width: Style.borderS

        TextInput {
          id: search
          anchors.fill: parent
          anchors.leftMargin: Style.marginM
          anchors.rightMargin: Style.marginM
          verticalAlignment: TextInput.AlignVCenter
          color: Color.mOnSurface
          font.pixelSize: Math.round(14 * Style.uiScaleRatio)
          focus: true
          clip: true
          onTextChanged: win.filter = text

          Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Escape) { win.dismissed(); event.accepted = true }
            else if (event.key === Qt.Key_Down) {
              win.selIdx = Math.min(win.selIdx + 1, win.shown.length - 1)
              event.accepted = true
            } else if (event.key === Qt.Key_Up) {
              win.selIdx = Math.max(win.selIdx - 1, 0)
              event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
              win.choose()
              event.accepted = true
            }
          }

          Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "Type to filter…"
            color: Qt.alpha(Color.mOnSurface, 0.4)
            font.pixelSize: search.font.pixelSize
            visible: !search.text.length
          }
        }
      }

      ListView {
        id: list
        width: parent.width
        height: parent.height - y
        clip: true
        model: win.shown
        spacing: Math.round(2 * Style.uiScaleRatio)

        delegate: Rectangle {
          width: list.width
          height: Math.round(44 * Style.uiScaleRatio)
          radius: Style.radiusM
          color: index === win.selIdx ? Qt.alpha(Color.mPrimary, 0.25) : "transparent"

          Column {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Style.marginM
            anchors.rightMargin: Style.marginM
            spacing: Math.round(1 * Style.uiScaleRatio)

            Text {
              width: parent.width
              text: modelData.label
              color: Color.mOnSurface
              font.pixelSize: Math.round(13 * Style.uiScaleRatio)
              elide: Text.ElideRight
              maximumLineCount: 1
            }
            Text {
              width: parent.width
              text: modelData.sub
              color: Qt.alpha(Color.mOnSurface, 0.5)
              font.pixelSize: Math.round(10 * Style.uiScaleRatio)
              visible: !!modelData.sub
              elide: Text.ElideRight
              maximumLineCount: 1
            }
          }

          MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onEntered: win.selIdx = index
            onClicked: win.choose()
          }
        }

        Text {
          anchors.centerIn: parent
          text: win.busy ? "Loading…" : "Nothing here yet"
          color: Qt.alpha(Color.mOnSurface, 0.5)
          font.pixelSize: Math.round(13 * Style.uiScaleRatio)
          visible: win.shown.length === 0
        }
      }
    }
  }
}
