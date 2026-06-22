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

  Rectangle {
    id: visualCapsule
    x: Style.pixelAlignCenter(parent.width, width)
    y: Style.pixelAlignCenter(parent.height, height)
    width: root.contentWidth
    height: root.contentHeight
    color: {
      if (isRecording) return colorRecord
      if (isProcessing) return colorProcess
      if (lastError && !isRecording && !isProcessing) return colorError
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

      Repeater {
        model: 4
        Rectangle {
          x: (parent.width - width) / 2
          y: (parent.height - height) / 2
          width: iconSize
          height: iconSize
          radius: width / 2
          color: "transparent"
          border.color: Qt.rgba(Color.mOnError.r, Color.mOnError.g, Color.mOnError.b, 0.55 - index * 0.1)
          border.width: Math.max(1.5, capsuleHeight * 0.045)
          opacity: isRecording ? (0.18 + displayLevel * (0.55 - index * 0.1)) : 0
          scale: isRecording ? (1.0 + displayLevel * (3.0 - index * 0.4)) : 1.0

          Behavior on scale { NumberAnimation { duration: 60 + index * 40 } }
          Behavior on opacity { NumberAnimation { duration: 60 + index * 40 } }
        }
      }

      Rectangle {
        id: outerSpinner
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        width: iconSize * 1.8
        height: width
        radius: width / 2
        color: "transparent"
        border.color: Qt.rgba(colorProcess.r, colorProcess.g, colorProcess.b, 0.5)
        border.width: Math.max(2, capsuleHeight * 0.035)
        opacity: isProcessing ? 1.0 : 0
        visible: isProcessing

        Behavior on opacity { NumberAnimation { duration: 300 } }

        PropertyAnimation on rotation {
          loops: Animation.Infinite
          running: isProcessing
          from: 0
          to: 360
          duration: 1200
        }
      }

      Rectangle {
        id: innerSpinner
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        width: iconSize * 1.2
        height: width
        radius: width / 2
        color: "transparent"
        border.color: Qt.rgba(colorProcess.r, colorProcess.g, colorProcess.b, 0.35)
        border.width: Math.max(1.5, capsuleHeight * 0.025)
        opacity: isProcessing ? 1.0 : 0
        visible: isProcessing

        Behavior on opacity { NumberAnimation { duration: 300 } }

        PropertyAnimation on rotation {
          loops: Animation.Infinite
          running: isProcessing
          from: 360
          to: 0
          duration: 800
        }
      }

      NIcon {
        id: micIcon
        anchors.centerIn: parent
        icon: "microphone"
        color: {
          if (isRecording) return "#ffffff"
          if (isProcessing) return Qt.lighter(colorProcess, 1.5)
          if (lastError && !isRecording) return "#ffffff"
          return Color.mOnSurface
        }
        pointSize: iconSize * 0.8
        opacity: isProcessing ? 0.6 : 1.0

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
