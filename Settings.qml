import QtQuick
import QtQuick.Layouts
import qs.Commons
import qs.Services.UI
import qs.Widgets

ColumnLayout {
  id: root

  property var pluginApi: null
  readonly property var main: pluginApi ? pluginApi.mainInstance : null

  property string editSonioxKey:     ""
  property string editSarvamKey:     ""
  property string editProvider:      "soniox"
  property string editLanguage:      "en-IN"
  property bool   editAppendNewline: true
  property var    editKeybinds:      ["SUPER + Z"]
  property string editHistoryKb:     "SUPER + SHIFT + Z"
  property string editClipboardKb:   "SUPER + V"
  property bool   _loaded: false

  // ---- keybind recorder state ----
  property string kbRecording: ""              // action id capturing a chord ("" = none)
  readonly property int maxKeybinds: 3
  readonly property string defaultChord: "SUPER + Z"

  spacing: Style.marginL

  function _load() {
    if (!pluginApi?.pluginSettings) return
    _loaded = false
    editSonioxKey     = pluginApi.pluginSettings.sonioxApiKey     || ""
    editSarvamKey     = pluginApi.pluginSettings.sarvamApiKey     || ""
    editProvider      = pluginApi.pluginSettings.provider         || "soniox"
    editLanguage      = pluginApi.pluginSettings.language         || "en-IN"
    editAppendNewline = pluginApi.pluginSettings.appendNewline    ?? true
    var kb = pluginApi.pluginSettings.keybinds
    editKeybinds = (kb && kb.length !== undefined) ? JSON.parse(JSON.stringify(kb)) : [defaultChord]
    // ?? (not ||): "" is a valid value meaning "shortcut disabled".
    editHistoryKb   = pluginApi.pluginSettings.transcriptHistoryKeybind ?? "SUPER + SHIFT + Z"
    editClipboardKb = pluginApi.pluginSettings.clipboardHistoryKeybind  ?? "SUPER + V"
    _loaded = true
  }

  function saveSettings() {
    if (!pluginApi || !_loaded) return
    pluginApi.pluginSettings.sonioxApiKey     = root.editSonioxKey
    pluginApi.pluginSettings.sarvamApiKey     = root.editSarvamKey
    pluginApi.pluginSettings.provider         = root.editProvider
    pluginApi.pluginSettings.language         = root.editLanguage
    pluginApi.pluginSettings.appendNewline    = root.editAppendNewline
    pluginApi.saveSettings()
    if (pluginApi.mainInstance) {
      pluginApi.mainInstance.updateConfig()
    }
  }

  // ---------- keybind helpers ----------
  // Normalise a chord to Hyprland's canonical form: "super+z" -> "SUPER + Z".
  function normalizeCombo(s) {
    var parts = String(s || "").split("+").map(function (p) { return p.trim() }).filter(function (p) { return p.length })
    if (!parts.length) return ""
    var modMap = { "super": "SUPER", "win": "SUPER", "meta": "SUPER", "mod": "SUPER", "cmd": "SUPER",
                   "ctrl": "CTRL", "control": "CTRL", "alt": "ALT", "mod1": "ALT", "shift": "SHIFT" }
    var order = ["SUPER", "CTRL", "ALT", "SHIFT"], mods = [], key = ""
    for (var i = 0; i < parts.length; i++) {
      var low = parts[i].toLowerCase()
      if (modMap[low]) { if (mods.indexOf(modMap[low]) === -1) mods.push(modMap[low]) }
      else key = parts[i]
    }
    if (!key) return ""
    mods.sort(function (a, b) { return order.indexOf(a) - order.indexOf(b) })
    var kl = key.toLowerCase()
    if (["left", "right", "up", "down", "space", "tab", "return", "escape"].indexOf(kl) !== -1) key = kl
    else if (key.length === 1) key = key.toUpperCase()
    return mods.concat([key]).join(" + ")
  }

  // Chord storage per recorder action: "toggle" is a list, the two pickers are single
  // chords ("" = disabled).
  function chordsFor(action) {
    if (action === "toggle") return editKeybinds
    var v = action === "history" ? editHistoryKb : editClipboardKb
    return v ? [v] : []
  }
  function kbAdd(action, raw) {
    var combo = normalizeCombo(raw)
    if (!combo) { ToastService.showNotice("VoxFlow", "Press a shortcut like SUPER + Z"); return }
    if (action === "toggle") {
      if (editKeybinds.indexOf(combo) !== -1) return
      if (editKeybinds.length >= maxKeybinds) { ToastService.showNotice("VoxFlow", "Up to " + maxKeybinds + " shortcuts"); return }
      editKeybinds = editKeybinds.concat([combo])
    } else if (action === "history") {
      editHistoryKb = combo
    } else {
      editClipboardKb = combo
    }
    saveKeybinds()
  }
  function kbRemove(action, combo) {
    if (action === "toggle")
      editKeybinds = editKeybinds.filter(function (c) { return c !== combo })
    else if (action === "history") editHistoryKb = ""
    else editClipboardKb = ""
    saveKeybinds()
  }
  function kbReset() {
    editKeybinds = [defaultChord]
    editHistoryKb = "SUPER + SHIFT + Z"
    editClipboardKb = "SUPER + V"
    saveKeybinds()
  }
  function saveKeybinds() {
    if (!pluginApi || !_loaded) return
    pluginApi.pluginSettings.keybinds = editKeybinds
    pluginApi.pluginSettings.transcriptHistoryKeybind = editHistoryKb
    pluginApi.pluginSettings.clipboardHistoryKeybind = editClipboardKb
    pluginApi.saveSettings()
    if (pluginApi.mainInstance) pluginApi.mainInstance.updateConfig()
  }

  Component.onCompleted: _load()
  onPluginApiChanged:    _load()

  NText {
    text: pluginApi?.tr("settings.title") || "VoxFlow Settings"
    pointSize: Style.fontSizeL
    font.weight: Font.Bold
    color: Color.mOnSurface
    Layout.fillWidth: true
    Layout.bottomMargin: Style.marginS
  }

  NTextInput {
    Layout.fillWidth: true
    label: pluginApi?.tr("settings.soniox_key") || "Soniox API Key"
    description: pluginApi?.tr("settings.soniox_key_desc") || "API key from console.soniox.com"
    placeholderText: "Enter Soniox API key..."
    text: root.editSonioxKey
    onTextChanged: { root.editSonioxKey = text; saveSettings() }
  }

  NTextInput {
    Layout.fillWidth: true
    label: pluginApi?.tr("settings.sarvam_key") || "Sarvam AI API Key"
    description: pluginApi?.tr("settings.sarvam_key_desc") || "API subscription key from dashboard.sarvam.ai"
    placeholderText: "Enter Sarvam AI API key..."
    text: root.editSarvamKey
    onTextChanged: { root.editSarvamKey = text; saveSettings() }
  }

  NDivider { Layout.fillWidth: true }

  ColumnLayout {
    Layout.fillWidth: true
    spacing: Style.marginM

    NLabel { label: pluginApi?.tr("settings.provider") || "Provider" }

    NComboBox {
      id: providerCombo
      Layout.fillWidth: true
      label: pluginApi?.tr("settings.provider_desc") || "Speech-to-text service provider"
      model: [
        { key: "soniox", name: pluginApi?.tr("provider.soniox") || "Soniox AI" },
        { key: "sarvam", name: pluginApi?.tr("provider.sarvam") || "Sarvam AI" }
      ]
      currentKey: root.editProvider
      onSelected: (key) => { root.editProvider = key; saveSettings() }
      defaultValue: "soniox"
    }
  }

  NTextInput {
    Layout.fillWidth: true
    label: pluginApi?.tr("settings.language") || "Language"
    description: pluginApi?.tr("settings.language_desc") || "Language code (e.g. en-IN, hi-IN)"
    placeholderText: "en-IN"
    text: root.editLanguage
    onTextChanged: { root.editLanguage = text; saveSettings() }
  }

  NToggle {
    Layout.fillWidth: true
    label: pluginApi?.tr("settings.append_newline") || "Append Newline"
    description: pluginApi?.tr("settings.append_newline_desc") || "Add a newline after pasted text"
    checked: root.editAppendNewline
    onToggled: (v) => { root.editAppendNewline = v; saveSettings() }
  }

  NDivider { Layout.fillWidth: true }

  // ================= KEYBIND =================
  ColumnLayout {
    Layout.fillWidth: true
    spacing: Style.marginS

    NLabel {
      label: pluginApi?.tr("settings.keybind") || "Keyboard Shortcut"
      description: pluginApi?.tr("settings.keybind_desc") || "Press to start recording, press again to stop and transcribe"
    }

    NText {
      Layout.fillWidth: true; wrapMode: Text.WordWrap
      text: "Click “Record”, then press the shortcut — e.g. hold Super and tap Z. VoxFlow binds " +
            "it in Hyprland for you (any hand-written bind in hyprland.lua is retired, a backup is kept)."
      pointSize: Style.fontSizeXS; color: Color.mOnSurfaceVariant
    }

    KeybindRecorder {
      Layout.fillWidth: true
      Layout.topMargin: Style.marginXS
      label: "Toggle recording"
      action: "toggle"
    }

    KeybindRecorder {
      Layout.fillWidth: true
      Layout.topMargin: Style.marginXS
      label: "Transcript history"
      action: "history"
    }

    KeybindRecorder {
      Layout.fillWidth: true
      Layout.topMargin: Style.marginXS
      label: "Clipboard history"
      action: "clipboard"
    }

    NText {
      Layout.fillWidth: true; wrapMode: Text.WordWrap
      text: "History pickers: type to filter, ↑/↓ to choose, Enter pastes into the window " +
            "you came from. Removing a pill disables that shortcut."
      pointSize: Style.fontSizeXS; color: Color.mOnSurfaceVariant
    }

    NButton {
      text: "Reset all shortcuts to defaults"
      outlined: true
      Layout.topMargin: Style.marginXS
      onClicked: root.kbReset()
    }
  }

  // ======================= inline sub-editor: keybind recorder =======================
  // Existing chords render as removable pills + a "Record" pill that captures a pressed
  // chord. While recording we suspend the live binds via the backend (setCaptureMode) so
  // Hyprland doesn't fire an already-bound chord before the recorder sees it, and flag
  // PanelService so the panel's own key handlers stand down.
  component KeybindRecorder: RowLayout {
    id: rec
    property string label: ""
    property string action: "toggle"   // "toggle" (list) | "history" | "clipboard" (single)
    readonly property bool recording: root.kbRecording === rec.action
    readonly property var chords: root.chordsFor(rec.action)
    readonly property int maxAllowed: rec.action === "toggle" ? root.maxKeybinds : 1
    spacing: Style.marginM

    function keyName(key, text) {
      switch (key) {
      case Qt.Key_Left: return "left"
      case Qt.Key_Right: return "right"
      case Qt.Key_Up: return "up"
      case Qt.Key_Down: return "down"
      case Qt.Key_Space: return "space"
      case Qt.Key_Tab: return "tab"
      case Qt.Key_Return:
      case Qt.Key_Enter: return "return"
      case Qt.Key_Home: return "home"
      case Qt.Key_End: return "end"
      case Qt.Key_Delete: return "delete"
      case Qt.Key_Backspace: return "backspace"
      }
      if (key >= Qt.Key_F1 && key <= Qt.Key_F35) return "F" + (key - Qt.Key_F1 + 1)
      if (key >= Qt.Key_A && key <= Qt.Key_Z) return String.fromCharCode(key)
      if (key >= Qt.Key_0 && key <= Qt.Key_9) return String.fromCharCode(key)
      var t = String(text || "").trim()
      return t.length === 1 ? t.toUpperCase() : ""
    }
    function comboFromEvent(e) {
      var parts = []
      if (e.modifiers & Qt.MetaModifier) parts.push("SUPER")
      if (e.modifiers & Qt.ControlModifier) parts.push("CTRL")
      if (e.modifiers & Qt.AltModifier) parts.push("ALT")
      if (e.modifiers & Qt.ShiftModifier) parts.push("SHIFT")
      var k = rec.keyName(e.key, e.text)
      if (!k) return ""
      parts.push(k)
      return parts.join(" + ")
    }
    function startRec() {
      root.kbRecording = rec.action
      PanelService.isKeybindRecording = true
      if (root.main) root.main.setCaptureMode(true)
      capture.forceActiveFocus()
    }
    function stopRec() {
      if (root.kbRecording === rec.action) root.kbRecording = ""
      PanelService.isKeybindRecording = false
      if (root.main) root.main.setCaptureMode(false)
    }
    Component.onDestruction: if (rec.recording) rec.stopRec()

    NText {
      text: rec.label; Layout.preferredWidth: 140; Layout.alignment: Qt.AlignTop
      Layout.topMargin: Style.marginXS
    }
    Flow {
      Layout.fillWidth: true
      spacing: Style.marginXS
      Repeater {
        model: rec.chords
        delegate: Rectangle {
          radius: height / 2
          color: Color.mSurfaceVariant
          border.color: Color.mOutline; border.width: 1
          implicitWidth: pill.implicitWidth + Style.marginM * 2
          implicitHeight: pill.implicitHeight + Style.marginXS * 2
          RowLayout {
            id: pill
            anchors.centerIn: parent; spacing: Style.marginXS
            NText { text: modelData; pointSize: Style.fontSizeS }
            NIconButton {
              icon: "close"; baseSize: Style.baseWidgetSize * 0.55; tooltipText: "Remove"
              onClicked: root.kbRemove(rec.action, modelData)
            }
          }
        }
      }
      Rectangle {
        visible: rec.recording || rec.chords.length < rec.maxAllowed
        radius: height / 2
        color: rec.recording ? Color.mSecondary : "transparent"
        border.color: rec.recording ? Color.mPrimary : Color.mOutline
        border.width: 1
        implicitWidth: addRow.implicitWidth + Style.marginM * 2
        implicitHeight: addRow.implicitHeight + Style.marginXS * 2
        RowLayout {
          id: addRow
          anchors.centerIn: parent; spacing: Style.marginXS
          NIcon {
            icon: rec.recording ? "circle-dot" : "keyboard"
            color: rec.recording ? Color.mOnSecondary : Color.mOnSurfaceVariant
            pointSize: Style.fontSizeM
          }
          NText {
            text: rec.recording ? "Press keys…  (Esc cancels)" : "Record"
            pointSize: Style.fontSizeS
            color: rec.recording ? Color.mOnSecondary : Color.mOnSurfaceVariant
          }
        }
        MouseArea {
          anchors.fill: parent; cursorShape: Qt.PointingHandCursor
          onClicked: rec.recording ? rec.stopRec() : rec.startRec()
        }
      }
      // hidden key sink: grabs focus while recording, captures the chord
      Item {
        id: capture
        width: 0; height: 0
        Keys.onPressed: function (e) {
          if (!rec.recording) return
          if (e.key === Qt.Key_Escape) { rec.stopRec(); e.accepted = true; return }
          if (e.key === Qt.Key_Shift || e.key === Qt.Key_Control || e.key === Qt.Key_Alt
              || e.key === Qt.Key_Meta || e.key === Qt.Key_Super_L || e.key === Qt.Key_Super_R) {
            e.accepted = true; return   // wait for a real key
          }
          var combo = rec.comboFromEvent(e)
          if (combo) { root.kbAdd(rec.action, combo); rec.stopRec() }
          e.accepted = true
        }
      }
    }
  }
}
