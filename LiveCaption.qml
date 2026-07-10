import QtQuick
import Quickshell
import Quickshell.Wayland
import Quickshell.Hyprland
import qs.Commons

// Click-through live caption shown while dictating: what the server has heard so far
// (confirmed text + the still-revisable tail), so you can look away from the cursor
// and still know the words landed. Display-only — no keyboard grab, clicks pass
// through (OSD idiom). Loaded by Main.qml while recording/processing when the
// showLiveCaption setting is on.
PanelWindow {
  id: win

  property var main: null
  readonly property bool recording: main?.recording ?? false
  readonly property bool processing: main?.processing ?? false
  readonly property string liveText: main?.partialText ?? ""

  // Newest words matter most — show the tail of long dictations.
  readonly property string shownText: {
    if (liveText.length === 0)
      return recording ? "Listening…" : "Transcribing…"
    var t = liveText.replace(/\s+/g, " ")
    return t.length > 220 ? "…" + t.slice(-220) : t
  }

  screen: Quickshell.screens.find(s => s.name === Hyprland.focusedMonitor?.name) ?? null

  anchors { bottom: true }
  margins { bottom: Math.round(64 * Style.uiScaleRatio) }
  implicitWidth: pill.implicitWidth
  implicitHeight: pill.implicitHeight
  color: "transparent"
  visible: true

  WlrLayershell.namespace: "voxflow-caption"
  WlrLayershell.layer: WlrLayer.Overlay
  WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
  WlrLayershell.exclusionMode: ExclusionMode.Ignore

  // Click-through — display only, no input wanted.
  mask: Region {}

  Rectangle {
    id: pill
    implicitWidth: Math.min(row.implicitWidth + Style.marginL * 2,
                            Math.round(900 * Style.uiScaleRatio))
    implicitHeight: row.implicitHeight + Style.marginM * 2
    radius: Style.radiusL
    color: Qt.alpha(Color.mSurface, 0.92)
    border.color: Style.boxBorderColor
    border.width: Style.borderS

    Row {
      id: row
      anchors.centerIn: parent
      spacing: Style.marginM

      // Recording dot: red pulse while listening, steady accent while transcribing.
      Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: Math.round(10 * Style.uiScaleRatio)
        height: width
        radius: width / 2
        color: win.recording ? Color.mError : Color.mPrimary

        SequentialAnimation on opacity {
          running: win.recording
          loops: Animation.Infinite
          NumberAnimation { from: 1.0; to: 0.3; duration: 600 }
          NumberAnimation { from: 0.3; to: 1.0; duration: 600 }
        }
        opacity: win.recording ? 1.0 : 0.8
      }

      Text {
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(implicitWidth,
                        Math.round(900 * Style.uiScaleRatio) - Style.marginL * 4)
        text: win.shownText
        color: win.liveText.length ? Color.mOnSurface
                                   : Qt.alpha(Color.mOnSurface, 0.55)
        font.pixelSize: Math.round(14 * Style.uiScaleRatio)
        elide: Text.ElideLeft
        maximumLineCount: 1
      }
    }
  }
}
