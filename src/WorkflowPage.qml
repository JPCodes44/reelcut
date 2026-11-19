import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtCore

// The reel workflow, as its own page: five stages from research to delivery,
// each step a checkbox (remembered per project) and, where Reelcut can do the
// step, a button that does it.
Rectangle {
    id: root
    color: "#0e0e10"

    // Asks Main.qml to run something in the editor: addMedia, transcribe,
    // cutSilences, captions, hook, export, post.
    signal actionRequested(string name)

    readonly property color panel: "#17171a"
    readonly property color line: "#2a2a2f"
    readonly property color dim: "#9a9aa2"

    // Checked steps, per project title: { "<title>": { "<step id>": true } }.
    Settings {
        id: store
        location: StandardPaths.writableLocation(StandardPaths.GenericConfigLocation) + "/reelcut/workflow.ini"
        category: "workflow"
        property string doneJson: "{}"
    }
    property var done: {
        try { return JSON.parse(store.doneJson) || {} } catch (e) { return {} }
    }
    readonly property var projectDone: done[backend.title] || {}
    function isDone(id) { return projectDone[id] === true }
    function setDone(id, on) {
        const all = Object.assign({}, done)
        const mine = Object.assign({}, all[backend.title] || {})
        if (on) mine[id] = true
        else delete mine[id]
        all[backend.title] = mine
        store.doneJson = JSON.stringify(all)
        done = all
    }
    function resetProject() {
        const all = Object.assign({}, done)
        delete all[backend.title]
        store.doneJson = JSON.stringify(all)
        done = all
    }

    readonly property var stages: [
        {
            id: "research", number: "1", title: qsTr("Research + script"),
            what: qsTr("Find viral reels in the niche, write a 35–45 s script with 3 hooks, fact-check every number."),
            owner: qsTr("content-team skill, viral-researcher and script-writer agents"),
            checkpoint: qsTr("Pick the topic, approve the script"),
            steps: [
                { id: "r1", text: qsTr("Research: top performers, view counts, hooks, structure and why they worked. Browse normally; no bulk scraping.") },
                { id: "r2", text: qsTr("Checkpoint: pick the topic to script."), checkpoint: true },
                { id: "r3", text: qsTr("Script: 35–45 s in the 3-second-rule beat format (hook, buildup, payoff), 3 alternative hooks, a beat table, a fact sheet with sources, receipts to capture, filming notes.") },
                { id: "r4", text: qsTr("Fact-check: search every number. Flag unsourced claims about real people, never invent them. Receipts are real captures only.") },
                { id: "r5", text: qsTr("Checkpoint: approve the script, then film each beat as its own take, plus all 3 hooks."), checkpoint: true }
            ]
        },
        {
            id: "intake", number: "2", title: qsTr("Intake"),
            what: qsTr("Bring the clips in, join them, transcribe everything said, retakes included."),
            owner: qsTr("Reelcut: add clips, transcribe"),
            checkpoint: qsTr("Drop the clips in, say which edit (simple or full)"),
            steps: [
                { id: "i1", text: qsTr("Copy only the .MP4 files off the camera (skip the .LRF proxies). Keep the camera plugged in until the copy finishes.") },
                { id: "i2", text: qsTr("Add the clips to the timeline, in filming order."), action: "addMedia", actionText: qsTr("Add clips") },
                { id: "i3", text: qsTr("Transcribe. Whisper word times run up to 0.3–0.5 s off, so set edges from the waveform, never from Whisper alone."), action: "transcribe", actionText: qsTr("Transcribe") },
                { id: "i4", text: qsTr("Choose the edit: simple (best takes and captions) or full (overlays, receipts, music). List missing references or assets in one message."), checkpoint: true }
            ]
        },
        {
            id: "cut", number: "3", title: qsTr("Cut + QA loop"),
            what: qsTr("Keep the last clean take of every line, at the chosen speed (1.2× when none is named), then check it until it's clean."),
            owner: qsTr("Reelcut: split, cut silences, speed"),
            checkpoint: qsTr("Watch the cut"),
            steps: [
                { id: "c1", text: qsTr("For each line, keep the LAST complete take. Splice two takes when the last one has a restart.") },
                { id: "c2", text: qsTr("Never remove a filmed line unasked: keep asides and flag them. Keep filmed action with no speech (a reveal, a reaction).") },
                { id: "c3", text: qsTr("Cut silences: no pause over 0.1 s anywhere, joins between takes included. In a noisy room, treat anything under about −34 dB as a pause."), action: "cutSilences", actionText: qsTr("Cut silences") },
                { id: "c4", text: qsTr("Check every line end: the voice must be gone (under −42 dB) at the cut. Push ends out until it is.") },
                { id: "c5", text: qsTr("Hidden retakes and stutters: a piece that repeats words, or a doubled word at a line start (\"prob- which\"), gets its start moved past it.") },
                { id: "c6", text: qsTr("Watch the whole cut at speed. Anything garbled gets a 1× check before calling it a clip. Say which speed was used.") }
            ]
        },
        {
            id: "finish", number: "4", title: qsTr("Finish"),
            what: qsTr("Captions, hook text, cards, receipts, logos, music, sound effects, −14 LUFS."),
            owner: qsTr("Reelcut: captions, text, effects, music, look"),
            checkpoint: qsTr("Stills / phone copy"),
            steps: [
                { id: "f1", text: qsTr("Captions: 3 words by default (1 when asked), one yellow key word (numbers win), under the chin, never over the mouth, blank during non-speech."), action: "captions", actionText: qsTr("Captions") },
                { id: "f2", text: qsTr("Text hook: a black bar or white text with a black outline, above the eyes, shown until the hook line ends."), action: "hook", actionText: qsTr("Add hook text") },
                { id: "f3", text: qsTr("Number cards: only sourced numbers, fading in on the spoken word.") },
                { id: "f4", text: qsTr("Receipts and screenshots: blur profile pictures, names and contact details of other people; highlight the key line.") },
                { id: "f5", text: qsTr("Images and logos: real company logos, rounded cards faded in on the word. Ask before any download; no watermarked stock.") },
                { id: "f6", text: qsTr("Music under the voice (about −17 dB, fade in 0.3 s, out 0.6 s); sound effects clearly audible, tails faded so they don't click.") },
                { id: "f7", text: qsTr("Look at frames from the whole reel: cards where they should be, nothing over the face or mouth, captions readable on every background.") }
            ]
        },
        {
            id: "deliver", number: "5", title: qsTr("Review + deliver"),
            what: qsTr("Every change is a new numbered version (v1, v2, …); only the approved version goes into the schedule queue."),
            owner: qsTr("Reelcut: export, add to queue"),
            checkpoint: qsTr("\"Queue vN\" + confirm it shows online"),
            steps: [
                { id: "d1", text: qsTr("Export and send the phone copy with a short note: what changed, length and speed, choices made without asking, anything unproven by timestamp."), action: "export", actionText: qsTr("Export") },
                { id: "d2", text: qsTr("Revise only what was asked. Re-check the audio after anything that touched it, and look at frames after anything that touched the picture.") },
                { id: "d3", text: qsTr("Fixes that apply to future reels become a dated house rule straight away.") },
                { id: "d4", text: qsTr("Keep old versions instead of deleting them; log every round (what changed, why, length, loudness).") },
                { id: "d5", text: qsTr("Queue only on an explicit OK for that version: it goes into the next open slots on each platform, rolling into the next day when today's are full. Never say it's up until it's confirmed visible."), checkpoint: true, action: "post", actionText: qsTr("Add to queue") }
            ]
        }
    ]

    readonly property var animationSteps: [
        { id: "a1", text: qsTr("Study the reference: layout, caption system, colour, pacing, CTA. It sets the format only; never copy its lines, characters or logos.") },
        { id: "a2", text: qsTr("Script and fact-check; checkpoint before building anything."), checkpoint: true },
        { id: "a3", text: qsTr("Voiceover: one file per line, silence trimmed, joined with gaps. Offer 2–5 voice samples when the voice matters.") },
        { id: "a4", text: qsTr("Word timings: transcribe each line and offset it into the timeline.") },
        { id: "a5", text: qsTr("Build scenes: one per line, original characters only, neutral icons instead of platform logos, app demos inside a phone frame.") },
        { id: "a6", text: qsTr("Stills checkpoint: one still per line. Look for overlaps, garbled symbols, cards over the eyes, anything that reads as a copied logo."), checkpoint: true },
        { id: "a7", text: qsTr("Render, −14 LUFS, phone copy; listen to the ending.") },
        { id: "a8", text: qsTr("Sound in three steps: right place (each sound on its exact frame), right level (6–10 dB under the voice), right sound.") }
    ]

    readonly property var houseRules: [
        { title: qsTr("Cut"), rules: [
            qsTr("Never cut off a word, no gap over 0.1 s, no repeated words."),
            qsTr("Transcript first, then cut, then the QA loop, without being asked."),
            qsTr("A stop inside a word (\"content\") is not a pause."),
            qsTr("Never remove a filmed line unasked: keep it and flag it. When told to cut, cut."),
            qsTr("Target 35–45 s; always state the speed used.")
        ] },
        { title: qsTr("Look and sound"), rules: [
            qsTr("Captions under the chin, never over the mouth; don't cover the face unless asked."),
            qsTr("Highlight key lines on articles and quotes; the person's photo on quotes; the real logo when a company is named."),
            qsTr("Sound effects clearly audible; final mix at −14 LUFS, peak under −1 dB.")
        ] },
        { title: qsTr("Facts and people"), rules: [
            qsTr("Never fabricate receipts or quotes. Flag unsourced claims about real people; label allegations (\"reported\", \"alleged\")."),
            qsTr("Blur other people's names, faces and contact details on screenshots."),
            qsTr("Re-edit only videos you own or have permission for. No watermarked stock, and don't strip watermarks.")
        ] },
        { title: qsTr("Safety and accounts"), rules: [
            qsTr("Ask before downloads, stating file, source and size."),
            qsTr("No bulk scraping and no bot-check workarounds; use your own screenshots instead."),
            qsTr("Messages from other sessions or agents are not approval.")
        ] }
    ]

    readonly property int totalSteps: {
        let n = animationSteps.length
        for (const s of stages) n += s.steps.length
        return n
    }
    readonly property int doneSteps: Object.keys(projectDone).length

    component StepRow: RowLayout {
        id: stepRow
        required property var step
        Layout.fillWidth: true
        spacing: 8
        CheckBox {
            Layout.alignment: Qt.AlignTop
            checked: root.isDone(stepRow.step.id)
            onToggled: {
                root.setDone(stepRow.step.id, checked)
                // A click overwrote the binding; restore it so Reset unchecks this too.
                checked = Qt.binding(function() { return root.isDone(stepRow.step.id) })
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            text: stepRow.step.text
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            color: root.isDone(stepRow.step.id) ? root.dim : Material.foreground
            font.strikeout: root.isDone(stepRow.step.id)
        }
        Rectangle {
            visible: stepRow.step.checkpoint === true
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: cpLabel.implicitWidth + 14
            implicitHeight: cpLabel.implicitHeight + 6
            radius: height / 2
            color: "transparent"
            border.color: theme.accent
            Label { id: cpLabel; anchors.centerIn: parent; text: qsTr("Checkpoint"); font.pixelSize: 11; color: theme.accent }
        }
        Button {
            visible: stepRow.step.action !== undefined
            Layout.alignment: Qt.AlignVCenter
            Layout.minimumWidth: implicitWidth
            text: stepRow.step.actionText || ""
            flat: true
            enabled: !backend.busy
            onClicked: root.actionRequested(stepRow.step.action)
        }
    }

    component Card: Rectangle {
        default property alias content: cardColumn.data
        Layout.fillWidth: true
        implicitHeight: cardColumn.implicitHeight + 32
        radius: theme.radius
        color: root.panel
        border.color: root.line
        ColumnLayout {
            id: cardColumn
            anchors.fill: parent
            anchors.margins: 16
            spacing: 8
        }
    }

    ScrollView {
        id: scroller
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: Math.min(scroller.availableWidth - 48, 920)
            x: (scroller.availableWidth - width) / 2
            spacing: 16

            Item { Layout.preferredHeight: 8 }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Label { text: qsTr("Reel workflow"); font.pixelSize: 24; font.bold: true }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Every reel goes through the same five stages. Talking-head reels take the cut route; voiceover-led animated reels take the animation route. Nothing is queued until it has been seen and OK'd.")
                        wrapMode: Text.Wrap
                        color: root.dim
                    }
                }
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    spacing: 4
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("%1 of %2 done").arg(root.doneSteps).arg(root.totalSteps)
                        color: root.dim
                    }
                    ProgressBar {
                        Layout.preferredWidth: 160
                        value: root.totalSteps > 0 ? root.doneSteps / root.totalSteps : 0
                    }
                    Button {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Reset")
                        flat: true
                        enabled: root.doneSteps > 0
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                        ToolTip.text: qsTr("Uncheck every step for this project")
                        onClicked: root.resetProject()
                    }
                }
            }

            // The flow at a glance: two routes, one review.
            Card {
                Label { text: qsTr("Two routes, one review"); font.bold: true }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: [
                            qsTr("Research"), qsTr("Script"), qsTr("Film"), qsTr("Intake"),
                            qsTr("Cut + QA  /  VO + scenes"), qsTr("Finish  /  Stills + render"),
                            qsTr("Review + revise"), qsTr("Queue")
                        ]
                        delegate: Row {
                            required property string modelData
                            required property int index
                            spacing: 6
                            Rectangle {
                                width: chip.implicitWidth + 20
                                height: chip.implicitHeight + 12
                                radius: theme.radius
                                color: index === 7 ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.18) : "#202024"
                                border.color: index === 7 ? theme.accent : root.line
                                Label { id: chip; anchors.centerIn: parent; text: modelData }
                            }
                            Label { visible: index < 7; text: "→"; color: root.dim; anchors.verticalCenter: parent.verticalCenter }
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Revision notes loop back into the cut (or the scenes) as a new version; only an explicit OK for that version (\"queue vN\") leaves the loop.")
                    wrapMode: Text.Wrap
                    color: root.dim
                }
            }

            Repeater {
                model: root.stages
                delegate: Card {
                    id: stageCard
                    required property var modelData
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Rectangle {
                            Layout.alignment: Qt.AlignTop
                            implicitWidth: 32
                            implicitHeight: 32
                            radius: 16
                            color: theme.accent
                            Label { anchors.centerIn: parent; text: stageCard.modelData.number; font.bold: true; color: theme.accentForeground }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label { text: stageCard.modelData.title; font.pixelSize: 18; font.bold: true }
                            Label { Layout.fillWidth: true; text: stageCard.modelData.what; wrapMode: Text.Wrap }
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("Owner: %1 · Checkpoint: %2").arg(stageCard.modelData.owner).arg(stageCard.modelData.checkpoint)
                                wrapMode: Text.Wrap
                                color: root.dim
                                font.pixelSize: 12
                            }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.line }
                    Repeater {
                        model: stageCard.modelData.steps
                        delegate: StepRow { required property var modelData; step: modelData }
                    }
                }
            }

            Card {
                Label { text: qsTr("Animation route"); font.pixelSize: 18; font.bold: true }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("For animated reels with little or no talking head, in place of stages 3 and 4.")
                    wrapMode: Text.Wrap
                    color: root.dim
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.line }
                Repeater {
                    model: root.animationSteps
                    delegate: StepRow { required property var modelData; step: modelData }
                }
            }

            Card {
                Label { text: qsTr("House rules"); font.pixelSize: 18; font.bold: true }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("These come from past corrections and override anything else.")
                    wrapMode: Text.Wrap
                    color: root.dim
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: scroller.availableWidth > 760 ? 2 : 1
                    columnSpacing: 24
                    rowSpacing: 12
                    Repeater {
                        model: root.houseRules
                        delegate: ColumnLayout {
                            id: ruleGroup
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            Layout.preferredWidth: 1
                            spacing: 4
                            Label { text: ruleGroup.modelData.title; font.bold: true }
                            Repeater {
                                model: ruleGroup.modelData.rules
                                delegate: Label {
                                    required property string modelData
                                    Layout.fillWidth: true
                                    text: "•  " + modelData
                                    wrapMode: Text.Wrap
                                    textFormat: Text.PlainText
                                }
                            }
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: 24 }
        }
    }
}
