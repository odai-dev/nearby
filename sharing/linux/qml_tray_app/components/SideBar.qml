import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    Layout.preferredWidth: compact ? 240 : 280
    Layout.fillHeight: true

    property bool compact: false
    property bool drawerMode: false

    readonly property color surface: "#18181b"
    readonly property color cardBorder: "#27272a"
    readonly property color textPrimary: "#fafafa"
    readonly property color textMuted: "#a1a1aa"
    readonly property int outerMargin: compact ? 10 : 12
    readonly property int contentInset: compact ? 10 : 12

    Flickable {
        id: sideFlick
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: Math.max(height, sideContent.implicitHeight + root.outerMargin * 2)
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            policy: sideFlick.contentHeight > sideFlick.height
                    ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
        }

        ColumnLayout {
            id: sideContent
            x: root.outerMargin
            y: root.outerMargin
            width: Math.max(0, sideFlick.width - root.outerMargin * 2)
            height: Math.max(sideFlick.height - root.outerMargin * 2, implicitHeight)
            spacing: 0

            // Receive mode: visibility info
            ColumnLayout {
                visible: fileShareController.pendingSendFileCount === 0
                Layout.fillWidth: true
                spacing: 0

                Label {
                    Layout.leftMargin: root.contentInset
                    Layout.topMargin: compact ? 10 : 16
                    Layout.bottomMargin: 8
                    text: "Visibility state"
                    color: textMuted
                    font.pixelSize: compact ? 12 : 13
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: compact ? 48 : 52
                    radius: 12
                    color: "#1c1c1f"
                    border.color: "#3f3f46"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: root.contentInset
                        anchors.rightMargin: root.contentInset

                        Label {
                            Layout.fillWidth: true
                            text: !fileShareController.running
                                  ? "Inactive"
                                  : fileShareController.mode === "Send" ? "Discovering" : "Always visible"
                            font.pixelSize: compact ? 13 : 14
                            font.weight: Font.Medium
                            color: textPrimary
                            elide: Text.ElideRight
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: root.contentInset
                    Layout.topMargin: 8
                    Layout.rightMargin: root.contentInset
                    text: !fileShareController.running
                        ? "The service is not running. Start it to discover or receive files."
                        : fileShareController.mode === "Send"
                        ? "Discovering nearby devices. Select a device below to send your file."
                        : "Nearby devices can share files with you. You'll be notified and must approve each transfer."
                    wrapMode: Text.WordWrap
                    font.pixelSize: compact ? 11 : 12
                    color: textMuted
                }
            }

            // Send mode: outbound file info
            ColumnLayout {
                visible: fileShareController.pendingSendFileCount > 0
                Layout.fillWidth: true
                spacing: 0

                Label {
                    Layout.leftMargin: root.contentInset
                    Layout.topMargin: compact ? 10 : 16
                    Layout.bottomMargin: 8
                    text: fileShareController.pendingSendKind === "text"
                          ? "Sharing text"
                          : fileShareController.pendingSendKind === "link"
                          ? "Sharing link"
                          : fileShareController.pendingSendFileCount === 1
                          ? "Sharing 1 file"
                          : "Sharing " + fileShareController.pendingSendFileCount + " files"
                    font.pixelSize: compact ? 13 : 14
                    font.weight: Font.Medium
                    color: textPrimary
                    elide: Text.ElideRight
                }

                Rectangle {
                    Layout.leftMargin: root.contentInset
                    Layout.preferredWidth: compact ? 60 : 72
                    Layout.preferredHeight: compact ? 60 : 72
                    radius: 12
                    color: surface

                    Label {
                        anchors.centerIn: parent
                        text: fileShareController.pendingSendKind === "link"
                              ? "🔗"
                              : fileShareController.pendingSendKind === "text" ? "Aa" : "📄"
                        font.pixelSize: compact ? 24 : 28
                        color: textPrimary
                    }
                }

                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: root.contentInset
                    Layout.topMargin: 8
                    Layout.rightMargin: root.contentInset
                    text: fileShareController.pendingSendSummary
                    wrapMode: Text.WordWrap
                    maximumLineCount: drawerMode ? 3 : 2
                    elide: Text.ElideRight
                    font.pixelSize: compact ? 12 : 13
                    color: textMuted
                }

                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: root.contentInset
                    Layout.topMargin: 12
                    Layout.rightMargin: root.contentInset
                    text: "Make sure both devices are unlocked, close together, and have Bluetooth turned on."
                    wrapMode: Text.WordWrap
                    font.pixelSize: compact ? 11 : 12
                    color: textMuted
                }
            }

            Item { Layout.fillHeight: true }

            // Cancel (only visible in send mode)
            Rectangle {
                visible: fileShareController.pendingSendFileCount > 0
                Layout.leftMargin: root.contentInset
                Layout.bottomMargin: root.contentInset
                Layout.preferredHeight: compact ? 38 : 40
                Layout.preferredWidth: cancelLbl.implicitWidth + 24
                radius: 12
                color: "#27272a"
                border.color: "#3f3f46"

                Label {
                    id: cancelLbl
                    anchors.centerIn: parent
                    text: "Cancel"
                    font.pixelSize: compact ? 13 : 14
                    font.weight: Font.Medium
                    color: textPrimary
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: fileShareController.switchToReceiveMode()
                }
            }
        }
    }
}
