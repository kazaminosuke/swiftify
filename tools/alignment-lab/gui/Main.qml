// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Swiftify.AlignmentLab

ApplicationWindow {
    id: window
    required property Lab lab
    property url audioFile
    property url referenceFile
    property var comparisonIds: []
    readonly property var selected: modelList.currentIndex >= 0
                                    ? lab.models[modelList.currentIndex] : null
    readonly property var activeReport: reportChooser.currentIndex >= 0
                                        ? lab.reports[reportChooser.currentIndex] : null
    readonly property var deviceKeys: ["auto", "cpu", "gpu"]
    readonly property string language: languageChooser.currentIndex ? "en" : "ja"
    readonly property string device: deviceKeys[deviceChooser.currentIndex] || "auto"
    readonly property bool ready: !lab.busy && audioFile.toString() !== "" && lyrics.text.trim() !== ""

    width: 1080
    height: 780
    minimumWidth: 760
    minimumHeight: 630
    title: qsTr("Alignment Lab")
    visible: true

    function measure(value, unit, decimals) {
        return value === null || value === undefined ? qsTr("Unavailable") : Number(value).toFixed(decimals) + unit
    }

    function mib(bytes) {
        return bytes === null || bytes === undefined
                ? qsTr("Unavailable") : measure(Number(bytes) / 1048576, " MiB", 1)
    }

    function validForDevice(model) {
        return model && model.runnable && model.devices.includes(device) && model.languages.includes(language)
    }

    function comparisonModels() {
        return lab.models.filter(model => comparisonIds.includes(model.id) && validForDevice(model))
                         .map(model => model.id)
    }

    function toggleComparison(modelId, checked) {
        comparisonIds = checked ? comparisonIds.concat([modelId])
                                : comparisonIds.filter(id => id !== modelId)
    }

    FileDialog {
        id: pickAudio
        title: qsTr("Choose audio")
        nameFilters: [qsTr("Audio files (*.wav *.mp3 *.m4a *.aac *.flac *.ogg *.opus *.webm *.aiff *.wma)"),
                      qsTr("All files (*)")]
        onAccepted: window.audioFile = selectedFile
    }
    FileDialog {
        id: pickLyrics
        title: qsTr("Choose UTF-8 lyric text")
        nameFilters: [qsTr("Text files (*.txt)"), qsTr("All files (*)")]
        onAccepted: {
            const text = window.lab.readLyrics(selectedFile)
            if (!window.lab.error)
                lyrics.text = text
        }
    }
    FileDialog {
        id: pickReference
        title: qsTr("Choose reference timings")
        nameFilters: [qsTr("JSON (*.json)")]
        onAccepted: window.referenceFile = selectedFile
    }
    FileDialog {
        id: pickModel
        title: qsTr("Choose a local ONNX file")
        nameFilters: [qsTr("ONNX model (*.onnx)")]
        onAccepted: {
            if (registration.visible)
                registration.file = selectedFile
            else if (window.selected)
                window.lab.registerModel(window.selected.id, window.selected.profile, selectedFile,
                                         window.selected.name, window.selected.license, window.selected.source)
        }
    }
    FileDialog {
        id: saveResult
        property string format: "json"
        fileMode: FileDialog.SaveFile
        title: qsTr("Export alignment")
        nameFilters: format === "json" ? [qsTr("JSON (*.json)")] : [qsTr("Lyrics (*.lrc)")]
        defaultSuffix: format === "json" ? "json" : "lrc"
        onAccepted: window.lab.save(reportChooser.currentIndex, format, selectedFile)
    }

    Dialog {
        id: registration
        property url file
        title: qsTr("Register custom ONNX export")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Cancel
        onOpened: {
            file = ""
            customId.text = ""
            customName.text = ""
            customLicense.text = ""
            customSource.text = ""
        }

        ColumnLayout {
            width: 370
            TextField { id: customId; placeholderText: qsTr("Identifier, e.g. mms-local"); Layout.fillWidth: true }
            TextField { id: customName; placeholderText: qsTr("Display name"); Layout.fillWidth: true }
            ComboBox { id: customProfile; model: ["MMS-300M-1130 FP32", "MMS-300M-1130 INT8", "Japanese HuBERT INT4"]; Layout.fillWidth: true }
            TextField { id: customLicense; placeholderText: qsTr("Model license"); Layout.fillWidth: true }
            TextField { id: customSource; placeholderText: qsTr("Model source"); Layout.fillWidth: true }
            Button {
                text: registration.file.toString() ? qsTr("Change ONNX file…") : qsTr("Choose ONNX file…")
                onClicked: pickModel.open()
            }
            Button {
                text: qsTr("Register")
                enabled: registration.file.toString() && customId.text && customName.text
                         && customLicense.text && customSource.text && !window.lab.busy
                onClicked: {
                    window.lab.registerModel(customId.text, ["mms-fp32", "mms-q8", "hubert-ja-q4"][customProfile.currentIndex],
                                             registration.file, customName.text, customLicense.text, customSource.text)
                    registration.close()
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            ColumnLayout {
                Layout.preferredWidth: 320
                Layout.maximumWidth: 360
                Layout.fillHeight: true
                Label { text: qsTr("Models"); font.pixelSize: 20 }
                Frame {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ListView {
                        id: modelList
                        anchors.fill: parent
                        clip: true
                        model: window.lab.models
                        spacing: 2
                        ScrollBar.vertical: ScrollBar {}
                        delegate: ItemDelegate {
                            id: modelRow
                            required property var modelData
                            required property int index
                            width: modelList.width
                            highlighted: ListView.isCurrentItem
                            onClicked: modelList.currentIndex = index
                            contentItem: RowLayout {
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Label { text: modelRow.modelData.name; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Label {
                                        text: modelRow.modelData.status
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        opacity: 0.75
                                        font.pixelSize: 11
                                    }
                                }
                                CheckBox {
                                    enabled: window.validForDevice(modelRow.modelData) && !window.lab.busy
                                    checked: window.comparisonIds.includes(modelRow.modelData.id)
                                    Accessible.name: qsTr("Include %1 in comparison").arg(modelRow.modelData.name)
                                    onToggled: window.toggleComparison(modelRow.modelData.id, checked)
                                }
                            }
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: window.selected
                          ? qsTr("%1 · %2").arg(window.selected.license).arg(window.mib(window.selected.model_bytes))
                          : ""
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: window.selected ? window.selected.terms : ""
                    visible: text !== ""
                    opacity: 0.8
                }
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    text: window.selected ? window.selected.path : ""
                    visible: text !== ""
                    ToolTip.text: text
                    ToolTip.visible: pathHover.hovered
                    HoverHandler { id: pathHover }
                }
                Button {
                    text: qsTr("Model source ↗")
                    visible: !!window.selected && window.selected.source.startsWith("https://")
                    onClicked: Qt.openUrlExternally(window.selected.source)
                }
                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        text: qsTr("Assign file…")
                        enabled: !window.lab.busy && !!window.selected && window.selected.can_assign
                        onClicked: pickModel.open()
                    }
                    Button { text: qsTr("Add…"); enabled: !window.lab.busy; onClicked: registration.open() }
                    Button {
                        text: qsTr("Remove")
                        enabled: !window.lab.busy && !!window.selected && window.selected.registered
                        onClicked: window.lab.removeModel(window.selected.id)
                    }
                }
            }

            ColumnLayout {
                Layout.fillHeight: true
                Layout.fillWidth: true
                Label { text: qsTr("Audio and lyrics"); font.pixelSize: 20 }
                RowLayout {
                    Layout.fillWidth: true
                    Button { text: qsTr("Audio…"); onClicked: pickAudio.open(); enabled: !window.lab.busy }
                    Label {
                        text: window.audioFile.toString()
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                    }
                    Button { text: qsTr("Lyric file…"); onClicked: pickLyrics.open(); enabled: !window.lab.busy }
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 120
                    clip: true
                    TextArea {
                        id: lyrics
                        placeholderText: qsTr("Paste plain lyrics, one line per cue")
                        readOnly: window.lab.busy
                        wrapMode: TextEdit.Wrap
                    }
                }
                RowLayout {
                    Label { text: qsTr("Language") }
                    ComboBox {
                        id: languageChooser
                        model: [qsTr("Japanese"), qsTr("English")]
                        enabled: !window.lab.busy
                    }
                    Label { text: qsTr("Device") }
                    ComboBox {
                        id: deviceChooser
                        model: [qsTr("Auto"), "CPU", "GPU"]
                        enabled: !window.lab.busy
                    }
                    Item { Layout.fillWidth: true }
                    Button { text: qsTr("Reference JSON…"); onClicked: pickReference.open(); enabled: !window.lab.busy }
                    ToolButton {
                        text: qsTr("Clear")
                        visible: window.referenceFile.toString() !== ""
                        enabled: !window.lab.busy
                        onClicked: window.referenceFile = ""
                        ToolTip.text: window.referenceFile.toString()
                        ToolTip.visible: hovered
                    }
                }
                RowLayout {
                    Button {
                        text: qsTr("Align")
                        enabled: window.ready && window.validForDevice(window.selected)
                        onClicked: window.lab.run([window.selected.id], window.audioFile, lyrics.text,
                                                  window.language,
                                                  window.device, false, window.referenceFile)
                    }
                    Button {
                        text: qsTr("Benchmark checked (%1)").arg(window.comparisonModels().length)
                        enabled: window.ready && window.comparisonModels().length > 0
                        onClicked: window.lab.run(window.comparisonModels(), window.audioFile, lyrics.text,
                                                  window.language,
                                                  window.device, true, window.referenceFile)
                    }
                    Item { Layout.fillWidth: true }
                    Button { text: qsTr("Cancel"); enabled: window.lab.busy; onClicked: window.lab.cancel() }
                }
                ProgressBar { Layout.fillWidth: true; value: window.lab.progress; visible: window.lab.busy }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: window.lab.error || window.lab.status
                    visible: text !== ""
                }
                RowLayout {
                    Label { text: qsTr("Result") }
                    ComboBox {
                        id: reportChooser
                        Layout.fillWidth: true
                        model: window.lab.reports
                        textRole: "model"
                    }
                    ComboBox {
                        id: levelChooser
                        model: [qsTr("Lines"), qsTr("Words"), qsTr("Syllables / morae")]
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: window.activeReport?.result?.timing === "estimated"
                          ? qsTr("Timings estimated from lyric length across the full audio.")
                          : window.activeReport?.result?.timing === "ctc" ? qsTr("CTC timings") : ""
                    wrapMode: Text.Wrap
                    visible: text !== ""
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: metrics.implicitHeight > 110 ? 110 : metrics.implicitHeight
                    visible: !!window.activeReport
                    clip: true
                    contentWidth: availableWidth
                    Column {
                        id: metrics
                        width: parent.width
                        spacing: 5
                        Label {
                            text: qsTr("Run failure rate %1%").arg(
                                      Number(window.activeReport?.failure_rate ?? 0) * 100)
                        }
                        Repeater {
                            model: window.activeReport?.runs ?? []
                            delegate: Label {
                                required property var modelData
                                width: metrics.width
                                wrapMode: Text.Wrap
                                text: {
                                    const run = modelData
                                    const m = run.metrics
                                    if (run.error)
                                        return qsTr("%1 · %2").arg(m.phase).arg(run.error)
                                    let out = qsTr("%1 · %2 · load %3 · preprocessing %4 · inference %5 · total %6 · RTF %7 · RAM %8 · VRAM %9")
                                        .arg(m.phase).arg(run.device).arg(window.measure(m.load_ms, " ms", 1))
                                        .arg(window.measure(m.preprocess_ms, " ms", 1)).arg(window.measure(m.inference_ms, " ms", 1))
                                        .arg(window.measure(m.total_ms, " ms", 1)).arg(window.measure(m.rtf, "", 3))
                                        .arg(window.mib(m.peak_ram_bytes)).arg(window.mib(m.peak_vram_bytes))
                                    const level = ["lines", "words", "syllables"][levelChooser.currentIndex]
                                    const a = run.accuracy?.[level]
                                    if (!a)
                                        return out + qsTr(" · Reference unavailable")
                                    out += qsTr(" · %1/%2 cues matched").arg(a.matched).arg(a.reference)
                                    for (const boundary of ["start", "end"]) {
                                        const error = a[boundary]
                                        if (error)
                                            out += qsTr(" · %1 error mean %2, median %3, P95 %4 ms").arg(boundary)
                                                      .arg(error.mean_ms).arg(error.median_ms).arg(error.p95_ms)
                                    }
                                    return out
                                }
                            }
                        }
                    }
                }
                Frame {
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    ListView {
                        anchors.fill: parent
                        clip: true
                        model: window.activeReport ? window.lab.cues(reportChooser.currentIndex, levelChooser.currentIndex) : []
                        ScrollBar.vertical: ScrollBar {}
                        delegate: ItemDelegate {
                            id: cue
                            required property var modelData
                            width: ListView.view.width
                            text: qsTr("%1–%2 ms    %3").arg(modelData.start_ms).arg(modelData.end_ms).arg(modelData.text)
                        }
                    }
                }
                RowLayout {
                    ComboBox { id: formatChooser; model: ["LRC", "Enhanced LRC", "JSON"] }
                    Button {
                        text: qsTr("Export…")
                        enabled: !!window.activeReport?.result && !window.lab.busy
                        onClicked: {
                            saveResult.format = ["lrc", "enhanced_lrc", "json"][formatChooser.currentIndex]
                            saveResult.open()
                        }
                    }
                }
            }
        }
    }
}
