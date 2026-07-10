import QtQuick
import QtQuick.Layouts
import Quickshell
import qs.Commons
import qs.Widgets
import qs.Services.UI

Item {
  id: root

  property var pluginApi: null
  property ShellScreen screen
  property string widgetId: ""
  property string section: ""
  property int sectionWidgetIndex: -1
  property int sectionWidgetsCount: 0

  readonly property string screenName: screen?.name ?? ""
  readonly property string barPosition: Settings.getBarPositionForScreen(screenName)
  readonly property bool isBarVertical: barPosition === "left" || barPosition === "right"
  readonly property real capsuleHeight: Style.getCapsuleHeightForScreen(screenName)
  readonly property real barFontSize: Style.getBarFontSizeForScreen(screenName)

  readonly property bool isRecording: pluginApi?.mainInstance?.recording ?? false
  readonly property bool isProcessing: pluginApi?.mainInstance?.processing ?? false
  readonly property real audioLevel: pluginApi?.mainInstance?.audioLevel ?? 0.0
  readonly property string lastError: pluginApi?.mainInstance?.lastError ?? ""
  readonly property string partialText: pluginApi?.mainInstance?.partialText ?? ""

  readonly property color colorIdle: Style.capsuleColor
  readonly property color colorRecord: Color.mError
  readonly property color colorProcess: Color.mPrimary
  readonly property color colorError: Color.mError

  readonly property color borderIdle: Style.capsuleBorderColor

  property real smoothLevel: 0.0

  readonly property real iconSize: capsuleHeight * 0.55

  readonly property real contentWidth: isBarVertical ? capsuleHeight : (capsuleHeight * 2.5)
  readonly property real contentHeight: isBarVertical ? capsuleHeight * 2.5 : capsuleHeight

  implicitWidth: contentWidth
  implicitHeight: contentHeight

  readonly property real displayLevel:
    isRecording ? Math.pow(Math.min(1.0, smoothLevel * 12.0), 0.5) : 0.0

  function toggle() {
    if (pluginApi?.mainInstance) {
      pluginApi.mainInstance.toggleRecording()
    }
  }

  Timer {
    id: levelTimer
    interval: 30
    running: isRecording
    repeat: true
    onTriggered: {
      smoothLevel = smoothLevel * 0.75 + audioLevel * 0.25
    }
  }

  // Steady sine pulse that drives the concentric rings while transcribing. Uses the SAME
  // Timer mechanism as the recording level meter (levelTimer) rather than an
  // `Animation on ...` value source, which didn't reliably run — so the rings visibly
  // grow and shrink, making the "working" state obvious even when the server is slow.
  property real pulseDrive: 0.0
  property real pulsePhase: 0.0
  Timer {
    id: pulseTimer
    interval: 33
    running: isProcessing
    repeat: true
    onTriggered: {
      pulsePhase += 0.14                                   // ~1.5s per grow+shrink cycle
      pulseDrive = 0.42 + 0.34 * Math.sin(pulsePhase)      // oscillates ~0.08 .. 0.76
    }
    onRunningChanged: if (!running) pulseDrive = 0.0
  }

  Rectangle {
    id: visualCapsule
    x: Style.pixelAlignCenter(parent.width, width)
    y: Style.pixelAlignCenter(parent.height, height)
    width: root.contentWidth
    height: root.contentHeight
    color: {
      if (isRecording) return colorRecord
      if (isProcessing) return colorProcess
      // Errors (incl. "No transcript received") are surfaced via a toast, not a
      // lingering tint: colorError == colorRecord, so a stuck red bar looks exactly
      // like it's still recording. Fall through to idle once recording/processing end.
      return mouseArea.containsMouse ? Color.mHover : colorIdle
    }
    radius: Style.radiusL
    border.color: {
      if (isRecording) return Qt.lighter(colorRecord, 1.3)
      if (isProcessing) return Qt.lighter(colorProcess, 1.3)
      return borderIdle
    }
    border.width: Style.capsuleBorderWidth

    Behavior on color { ColorAnimation { duration: 250 } }
    Behavior on border.color { ColorAnimation { duration: 250 } }

    Item {
      id: visualizerContainer
      anchors.fill: parent
      clip: true

      // Concentric rings for both live states: while RECORDING they pulse with your voice
      // (displayLevel, on-error colour); while TRANSCRIBING they pulse to a steady rhythm
      // (pulseDrive, on-primary colour). Same visual language, high-contrast per state.
      Repeater {
        model: 4
        Rectangle {
          readonly property real drive: isRecording ? displayLevel
                                       : (isProcessing ? root.pulseDrive : 0.0)
          readonly property color ring: isRecording ? Color.mOnError : Color.mOnPrimary
          x: (parent.width - width) / 2
          y: (parent.height - height) / 2
          width: iconSize
          height: iconSize
          radius: width / 2
          color: "transparent"
          border.color: Qt.rgba(ring.r, ring.g, ring.b, 0.6 - index * 0.1)
          border.width: Math.max(1.5, capsuleHeight * 0.045)
          opacity: (isRecording || isProcessing) ? (0.18 + drive * (0.6 - index * 0.1)) : 0
          scale: (isRecording || isProcessing) ? (1.0 + drive * (3.0 - index * 0.4)) : 1.0

          Behavior on scale { NumberAnimation { duration: 60 + index * 40 } }
          Behavior on opacity { NumberAnimation { duration: 60 + index * 40 } }
        }
      }

      NIcon {
        id: micIcon
        anchors.centerIn: parent
        icon: "microphone"
        color: {
          if (isRecording) return "#ffffff"
          if (isProcessing) return Color.mOnPrimary   // high contrast on the primary capsule
          return Color.mOnSurface
        }
        pointSize: iconSize * 0.8
        opacity: 1.0

        Behavior on color { ColorAnimation { duration: 250 } }
        Behavior on opacity { NumberAnimation { duration: 250 } }
      }
    }
  }

  MouseArea {
    id: mouseArea
    anchors.fill: parent
    hoverEnabled: true
    cursorShape: Qt.PointingHandCursor

    onClicked: root.toggle()

    onEntered: {
      var tip = ""
      if (isRecording) {
        if (partialText.length > 0) {
          tip = partialText.substring(0, 80) + (partialText.length > 80 ? "..." : "")
        } else {
          tip = pluginApi?.tr("widget.recording_tooltip") || "Recording... press to stop"
        }
      }
      else if (isProcessing) tip = pluginApi?.tr("widget.processing_tooltip") || "Transcribing..."
      else tip = pluginApi?.tr("widget.idle_tooltip") || "Click to record"
      TooltipService.show(root, tip, BarService.getTooltipDirection())
    }

    onExited: TooltipService.hide()
  }

  Component.onCompleted: {
    Logger.i("VoxFlow", "BarWidget loaded")
  }
}
