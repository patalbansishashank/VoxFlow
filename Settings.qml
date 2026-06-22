import QtQuick
import QtQuick.Layouts
import qs.Commons
import qs.Widgets

ColumnLayout {
  id: root

  property var pluginApi: null

  property string editSonioxKey:     ""
  property string editSarvamKey:     ""
  property string editProvider:      "soniox"
  property string editLanguage:      "en-IN"
  property bool   editAppendNewline: true
  property bool   _loaded: false

  spacing: Style.marginL

  function _load() {
    if (!pluginApi?.pluginSettings) return
    _loaded = false
    editSonioxKey     = pluginApi.pluginSettings.sonioxApiKey     || ""
    editSarvamKey     = pluginApi.pluginSettings.sarvamApiKey     || ""
    editProvider      = pluginApi.pluginSettings.provider         || "soniox"
    editLanguage      = pluginApi.pluginSettings.language         || "en-IN"
    editAppendNewline = pluginApi.pluginSettings.appendNewline    ?? true
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

  NLabel { label: "Keyboard Shortcut" }

  NText {
    text: "Set in Niri keybinds.kdl:\nMod+R { on-pressed \"qs -c noctalia-shell ipc call plugin:voxflow start-recording\" ; on-released \"qs -c noctalia-shell ipc call plugin:voxflow stop-recording\" }"
    pointSize: Style.fontSizeXS
    color: Color.mOnSurfaceVariant
    wrapMode: Text.WordWrap
    Layout.fillWidth: true
  }
}
