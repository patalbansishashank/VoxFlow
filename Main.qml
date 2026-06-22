import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Services.UI

Item {
  id: root

  property var pluginApi: null

  property bool recording: false
  property bool processing: false
  property real audioLevel: 0.0
  property string lastError: ""
  property string partialText: ""

  readonly property string backendPath:
    pluginApi?.pluginDir + "/bin/voxflow-backend"

  function startRecording() {
    if (recording || processing) return
    recording = true
    audioLevel = 0.0
    lastError = ""
    partialText = ""

    backend.write('{"method":"start_recording","params":{"sample_rate":16000,"channels":1}}\n')
  }

  function stopRecording() {
    if (!recording) return
    recording = false
    processing = true
    processingTimer.restart()

    backend.write('{"id":1,"method":"stop_recording","params":{}}\n')
  }

  function toggleRecording() {
    if (recording) stopRecording()
    else startRecording()
  }

  function resetProcessing() {
    processing = false
    processingTimer.stop()
    partialText = ""
  }

  Timer {
    id: processingTimer
    interval: 30000
    onTriggered: {
      Logger.w("VoxFlow", "Processing timed out")
      root.resetProcessing()
      ToastService.showError("VoxFlow", "Transcription timed out")
    }
  }

  function updateConfig() {
    if (!pluginApi) return

    var cfg = {
      method: "set_config",
      params: {
        soniox_api_key: pluginApi.pluginSettings?.sonioxApiKey || "",
        sarvam_api_key: pluginApi.pluginSettings?.sarvamApiKey || "",
        provider: pluginApi.pluginSettings?.provider || "soniox",
        language: pluginApi.pluginSettings?.language || "en-IN",
        append_newline: pluginApi.pluginSettings?.appendNewline ?? true
      }
    }
    backend.write(JSON.stringify(cfg) + "\n")
  }

  IpcHandler {
    target: "plugin:voxflow"

    function startRecording() { root.startRecording() }
    function stopRecording() { root.stopRecording() }
    function toggleRecording() { root.toggleRecording() }
  }

  Process {
    id: backend
    command: [root.backendPath]

    running: pluginApi !== null && pluginApi.manifest !== null
    stdinEnabled: true

    stdout: SplitParser {
      onRead: (data) => {
        var line = data.trim()
        if (line !== "") root.handleMessage(line)
      }
    }

    stderr: SplitParser {
      onRead: (data) => {
        var line = data.trim()
        if (line === "") return
        try {
          var msg = JSON.parse(line)
          if (msg.event === "notify") {
            if (msg.data?.message) {
              ToastService.showNotice(msg.data.message)
            }
          }
        } catch (e) {}
      }
    }

    onStarted: {
      Logger.i("VoxFlow", "Backend process started")
      root.updateConfig()
    }

    onExited: (exitCode, exitStatus) => {
      Logger.w("VoxFlow", "Backend exited:", exitCode)
      root.recording = false
      root.resetProcessing()
    }
  }

  function handleMessage(line) {
    try {
      var msg = JSON.parse(line)
    } catch (e) {
      return
    }

    if (msg.event === "ready") {
      Logger.i("VoxFlow", "Backend ready:", msg.data?.version, msg.data?.mode)
      updateConfig()
      return
    }

    if (msg.event === "level") {
      audioLevel = msg.data?.value ?? 0.0
      return
    }

    if (msg.event === "recording_started") {
      ToastService.showNotice("VoxFlow", "Recording...")
      return
    }

    if (msg.event === "transcript") {
      var text = msg.data?.text || ""
      var isFinal = msg.data?.is_final ?? false

      if (isFinal && text.length > 0) {
        partialText = ""
      } else if (text.length > 0) {
        partialText = text
      }
      return
    }

    if (msg.event === "processing") {
      processing = true
      return
    }

    if (msg.event === "error") {
      lastError = msg.data?.message || "Unknown error"
      root.resetProcessing()
      ToastService.showError("VoxFlow", lastError)
      return
    }

    if (msg.event === "config_updated") {
      Logger.d("VoxFlow", "Config applied")
      return
    }

    if (msg.id === 1 && msg.result) {
      var resultText = msg.result.text || ""
      root.resetProcessing()
      audioLevel = 0.0
      if (resultText.length > 0) {
        var preview = resultText.substring(0, 60) + (resultText.length > 60 ? "..." : "")
        ToastService.showNotice("VoxFlow", preview)
      }
      return
    }

    if (msg.id === 1 && msg.error) {
      root.resetProcessing()
      lastError = msg.error.message || "Transcription failed"
      ToastService.showError("VoxFlow", lastError)
      return
    }
  }

  Component.onCompleted: {
    if (pluginApi) {
      Logger.i("VoxFlow", "Plugin loaded, ID:", pluginApi.pluginId)
    }
  }
}
