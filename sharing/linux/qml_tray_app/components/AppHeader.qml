import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    Layout.fillWidth: true
    height: compact ? 64 : 80

    signal settingsRequested()
    signal sidebarRequested()

    property bool compact: false
    property bool showSidebarButton: false

    readonly property color textPrimary: "#fafafa"
    readonly property color textMuted: "#a1a1aa"
    readonly property color accent: "#10b981"
    readonly property int sideMargin: compact ? 12 : 24

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: root.sideMargin
        anchors.rightMargin: root.sideMargin
        spacing: compact ? 8 : 14

        Rectangle {
            visible: root.showSidebarButton
            Layout.preferredWidth: visible ? 40 : 0
            Layout.preferredHeight: 40
            radius: 12
            color: sidebarBtn.containsMouse ? "#27272a" : "transparent"
            border.color: sidebarBtn.containsMouse ? "#3f3f46" : "transparent"

            Label {
                anchors.centerIn: parent
                text: "☰"
                font.pixelSize: 20
                color: sidebarBtn.containsMouse ? accent : textMuted
            }

            MouseArea {
                id: sidebarBtn
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.sidebarRequested()
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Label {
                Layout.fillWidth: true
                text: "Device name"
                font.pixelSize: compact ? 11 : 12
                color: textMuted
                elide: Text.ElideRight
            }

            Label {
                Layout.fillWidth: true
                text: fileShareController.deviceName
                font.pixelSize: compact ? 18 : 22
                font.weight: Font.Medium
                color: textPrimary
                elide: Text.ElideRight
                maximumLineCount: 1
            }
        }

        Rectangle {
            Layout.preferredWidth: 40
            Layout.preferredHeight: 40
            radius: 12
            color: settingsBtn.containsMouse ? "#064e3b" : "transparent"
            border.color: settingsBtn.containsMouse ? "#10b981" : "transparent"

            Label {
                anchors.centerIn: parent
                text: "⚙"
                font.pixelSize: 18
                color: settingsBtn.containsMouse ? accent : textMuted
            }

            MouseArea {
                id: settingsBtn
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: settingsRequested()
            }
        }
    }
}
