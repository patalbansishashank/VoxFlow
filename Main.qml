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
        append_newline: pluginApi.pluginSettings?.appendNewline ?? true,
        // Chords the backend registers in Hyprland (hl.bind). The plugin owns these,
        // not hyprland.lua. See Settings.qml keybind recorder.
        keybinds: pluginApi.pluginSettings?.keybinds ?? ["SUPER + Z"],
        transcript_history_keybind:
          pluginApi.pluginSettings?.transcriptHistoryKeybind ?? "SUPER + SHIFT + Z",
        clipboard_history_keybind:
          pluginApi.pluginSettings?.clipboardHistoryKeybind ?? "SUPER + V"
      }
    }
    backend.write(JSON.stringify(cfg) + "\n")
  }

  // Called by the settings keybind recorder: suspend the live binds while capturing a
  // chord (so pressing an already-bound chord reaches the recorder, not the toggle).
  function setCaptureMode(on) {
    if (!pluginApi) return
    backend.write(JSON.stringify({ method: "set_capture_mode", params: { on: on } }) + "\n")
  }

  // ── history pickers ─────────────────────────────────────────────────────────
  // Two plugin-owned chords open an overlay picker (HistoryPanel.qml): past
  // transcripts, or general clipboard history (cliphist) with transcripts filtered
  // out. Both flows fetch the transcript list from the backend first (the clipboard
  // picker needs it for filtering), then open the panel.

  property bool historyOpen: false
  property string historyMode: "transcripts"
  property var historyItems: []

  function showTranscriptHistory() { requestHistory("transcripts") }
  function showClipboardHistory() { requestHistory("clipboard") }

  function requestHistory(mode) {
    if (root.historyOpen) { root.historyOpen = false; return }  // same chord closes it
    root.historyMode = mode
    backend.write('{"id":2,"method":"get_history","params":{"limit":100}}\n')
  }

  // Paste a picked transcript: full backend flow (clipboard saved, compositor
  // Ctrl+V, clipboard restored).
  function pasteText(text) {
    backend.write(JSON.stringify({ method: "paste_text", params: { text: text } }) + "\n")
  }

  // The clipboard picker already wl-copy'd the chosen item; give the panel a beat
  // to close and focus to return to the previous window, then send Ctrl+V.
  function sendPasteSoon() { pasteDelay.restart() }
  Timer {
    id: pasteDelay
    interval: 300
    onTriggered: backend.write('{"method":"send_paste","params":{}}\n')
  }

  Loader {
    active: root.historyOpen
    sourceComponent: HistoryPanel {
      main: root
      mode: root.historyMode
      transcripts: root.historyItems
      onDismissed: root.historyOpen = false
    }
  }

  // Live caption while dictating (click-through OSD; see LiveCaption.qml).
  readonly property bool liveCaptionEnabled:
    pluginApi?.pluginSettings?.showLiveCaption ?? true
  Loader {
    active: root.liveCaptionEnabled && (root.recording || root.processing)
    sourceComponent: LiveCaption { main: root }
  }

  IpcHandler {
    target: "plugin:voxflow"

    function startRecording() { root.startRecording() }
    function stopRecording() { root.stopRecording() }
    function toggleRecording() { root.toggleRecording() }
    function showTranscriptHistory() { root.showTranscriptHistory() }
    function showClipboardHistory() { root.showClipboardHistory() }
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

    // History list arrived -> open the picker.
    if (msg.id === 2 && msg.result) {
      root.historyItems = msg.result.items || []
      root.historyOpen = true
      return
    }
  }

  Component.onCompleted: {
    if (pluginApi) {
      Logger.i("VoxFlow", "Plugin loaded, ID:", pluginApi.pluginId)
    }
  }
}
