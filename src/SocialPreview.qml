import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A 360 × 640 logical phone frame, scaled as a unit by Preview, wearing the
// chrome of one app: Instagram Reels, Facebook Reels, YouTube Shorts or
// TikTok. All app content flows through layouts; neither long copy nor the
// action rail can push past the video. Decorative only; never included in an
// export.
Item {
    id: root
    objectName: "socialPreview"
    property string platform: "instagram"
    property string caption: ""
    property string hashtags: ""
    // YouTube shows the video's title where the others show the caption.
    property string title: ""
    property string username: "your_account"
    signal editRequested()
    clip: true

    readonly property bool tiktok: platform === "tiktok"
    readonly property bool facebook: platform === "facebook"
    readonly property bool youtube: platform === "youtube"
    readonly property bool instagram: !tiktok && !facebook && !youtube

    // What each app puts where.
    readonly property var rail: tiktok ? ["heart", "comment", "bookmark", "shareArrow"]
                              : facebook ? ["thumbUp", "comment", "shareArrow", "more"]
                              : youtube ? ["thumbUp", "thumbDown", "comment", "shareArrow", "remix"]
                              : ["heart", "comment", "share", "more"]
    readonly property var railLabels: ({
        thumbDown: qsTr("Dislike"), shareArrow: youtube ? qsTr("Share") : "—", remix: qsTr("Remix")
    })
    readonly property var nav: tiktok ? ["home", "friends", "plus", "comment", "user"]
                             : facebook ? ["home", "video", "friends", "bell", "menu"]
                             : youtube ? ["home", "shorts", "plus", "subscriptions", "user"]
                             : ["home", "search", "reel", "plus", "user"]
    readonly property var navLabels: tiktok ? [qsTr("Home"), qsTr("Friends"), "", qsTr("Inbox"), qsTr("Profile")]
                                   : youtube ? [qsTr("Home"), qsTr("Shorts"), "", qsTr("Subscriptions"), qsTr("You")]
                                   : []

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: "#55000000" }
            GradientStop { position: 0.22; color: "transparent" }
            GradientStop { position: 0.60; color: "transparent" }
            GradientStop { position: 1; color: "#b3000000" }
        }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            objectName: "socialTop"
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.preferredHeight: 32
            spacing: 14
            Label {
                Layout.fillWidth: true
                visible: root.instagram || root.facebook
                text: qsTr("Reels")
                color: "white"; font.pixelSize: 22; font.bold: true
            }
            Item { visible: root.tiktok || root.youtube; Layout.fillWidth: true }
            Label { visible: root.tiktok; text: qsTr("Following"); color: "#cccccc"; font.pixelSize: 15; font.bold: true }
            Label { visible: root.tiktok; text: qsTr("For You"); color: "white"; font.pixelSize: 16; font.bold: true; bottomPadding: 3
                background: Rectangle { color: "transparent"; Rectangle { anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; width: 28; height: 2; color: "white" } }
            }
            Item { visible: root.tiktok; Layout.fillWidth: true }
            SocialIcon { name: root.instagram ? "camera" : "search" }
            SocialIcon { visible: root.facebook || root.youtube; name: root.facebook ? "user" : "moreVertical" }
        }
        Item { Layout.fillHeight: true }
        RowLayout {
            objectName: "socialBody"
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 10
            Layout.bottomMargin: 12
            spacing: 12
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.alignment: Qt.AlignBottom
                spacing: 7
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Rectangle {
                        visible: !root.tiktok
                        implicitWidth: 28; implicitHeight: 28; radius: 14
                        color: "#55555f"; border.color: "white"
                        SocialIcon { anchors.centerIn: parent; width: 17; height: 17; name: "user" }
                    }
                    Label {
                        Layout.fillWidth: root.instagram || root.tiktok
                        Layout.minimumWidth: 0
                        text: (root.tiktok || root.youtube ? "@" : "") + root.username
                        elide: Text.ElideRight
                        color: "white"; font.pixelSize: 14; font.bold: true
                    }
                    Label {
                        // Facebook: "Page name · Follow", the button just text.
                        visible: root.facebook
                        Layout.fillWidth: true
                        text: "· " + qsTr("Follow")
                        color: "white"; font.pixelSize: 14; font.bold: true
                    }
                    Label {
                        visible: root.instagram || root.youtube
                        text: root.youtube ? qsTr("Subscribe") : qsTr("Follow")
                        color: root.youtube ? "#0f0f0f" : "white"
                        font.pixelSize: 11; font.bold: root.youtube
                        leftPadding: root.youtube ? 10 : 8; rightPadding: leftPadding; topPadding: 3; bottomPadding: 3
                        background: Rectangle {
                            color: root.youtube ? "white" : "transparent"
                            border.color: "white"
                            radius: root.youtube ? height / 2 : 4
                        }
                    }
                    // YouTube's Subscribe sits right after the channel name.
                    Item { visible: root.youtube; Layout.fillWidth: true }
                }
                Label {
                    objectName: "socialCaption"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: root.youtube ? (root.title || root.caption || qsTr("Add a title…"))
                                       : (root.caption || qsTr("Add a caption…"))
                    color: "white"; font.pixelSize: 13
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    maximumLineCount: root.tiktok ? 3 : 2
                    elide: Text.ElideRight
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.editRequested() }
                }
                Label {
                    objectName: "socialHashtags"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    // A Short shows its title; the hashtags stay in the description.
                    visible: text.length > 0 && !root.youtube
                    text: root.hashtags
                    color: "white"; font.pixelSize: 13; font.bold: true
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.editRequested() }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    SocialIcon { name: "music"; implicitWidth: 13; implicitHeight: 13 }
                    Label {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: qsTr("Original audio · %1").arg(root.username)
                        color: "white"; font.pixelSize: 11; elide: Text.ElideRight
                    }
                }
            }
            ColumnLayout {
                objectName: "socialChrome-" + root.platform
                Layout.preferredWidth: 42
                Layout.alignment: Qt.AlignBottom
                spacing: root.tiktok ? 16 : root.youtube ? 13 : 18
                Rectangle {
                    visible: root.tiktok
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: 36; implicitHeight: 36; radius: 18
                    color: "#55555f"; border.color: "white"
                    SocialIcon { anchors.centerIn: parent; name: "user"; width: 22; height: 22 }
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom; anchors.bottomMargin: -6
                        width: 16; height: 16; radius: 8; color: "#fe2c55"
                        SocialIcon { anchors.centerIn: parent; name: "plus"; width: 12; height: 12 }
                    }
                }
                Repeater {
                    model: root.rail
                    ColumnLayout {
                        required property string modelData
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 4
                        SocialIcon { Layout.alignment: Qt.AlignHCenter; name: modelData; implicitWidth: 27; implicitHeight: 27 }
                        Label {
                            visible: !root.instagram || modelData === "heart" || modelData === "comment"
                            Layout.alignment: Qt.AlignHCenter
                            text: root.railLabels[modelData] || (modelData === "more" ? "" : "—")
                            color: "white"; font.pixelSize: 10
                        }
                    }
                }
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: 28; implicitHeight: 28
                    radius: root.tiktok ? 14 : root.youtube ? 6 : 4
                    color: "#333338"; border.color: "white"
                    SocialIcon { anchors.centerIn: parent; name: "music"; width: 16; height: 16 }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.navLabels.length > 0 ? 49 : 44
            color: root.facebook ? "#ee18191a" : root.youtube ? "#ee0f0f0f" : "#ee080808"
            RowLayout {
                objectName: "socialNav"
                uniformCellSizes: true
                anchors.fill: parent
                anchors.leftMargin: 16; anchors.rightMargin: 16
                spacing: 0
                Repeater {
                    model: root.nav
                    Item {
                        required property string modelData
                        required property int index
                        objectName: "socialNavCell" + index
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 1
                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: 2
                            SocialIcon {
                                Layout.alignment: Qt.AlignHCenter
                                name: modelData
                                // YouTube's create button is the larger one.
                                implicitWidth: root.youtube && modelData === "plus" ? 30 : 21
                                implicitHeight: implicitWidth
                            }
                            Label {
                                // Reserve a label row beneath the create
                                // button so all icons share a baseline.
                                visible: root.navLabels.length > 0
                                Layout.preferredHeight: 10
                                Layout.alignment: Qt.AlignHCenter
                                text: root.navLabels[index] || ""
                                color: "white"; font.pixelSize: 8
                            }
                        }
                    }
                }
            }
        }
    }
}
