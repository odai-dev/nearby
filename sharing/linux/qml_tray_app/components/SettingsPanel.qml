import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Drawer {
    id: root
    edge: Qt.RightEdge
    width: parent ? Math.min(380, parent.width) : 380
    height: parent ? parent.height : 0
    implicitWidth: width
    implicitHeight: parent ? parent.height : 0

    readonly property color bg: "#09090b"
    readonly property color surface: "#18181b"
    readonly property color accent: "#10b981"
    readonly property color accentLight: "#064e3b"
    readonly property color borderColor: "#27272a"
    readonly property color textPrimary: "#fafafa"
    readonly property color textMuted: "#a1a1aa"
    readonly property bool compact: width < 360
    readonly property int sideMargin: compact ? 14 : 20

    background: Rectangle { color: root.bg }

    ColumnLayout {
        width: root.width
        height: root.height
        spacing: 0

        // Header
        Rectangle {
            Layout.fillWidth: true
            height: 64
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: root.sideMargin
                anchors.rightMargin: root.sideMargin

                Label {
                    text: "Settings"
                    font.pixelSize: compact ? 18 : 20
                    font.weight: Font.Bold
                    color: root.textPrimary
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                Rectangle {
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    radius: 8
                    color: closeArea.containsMouse ? "#27272a" : "transparent"

                    Label {
                        anchors.centerIn: parent
                        text: "✕"
                        font.pixelSize: 14
                        color: root.textMuted
                    }

                    MouseArea {
                        id: closeArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.close()
                    }
                }
            }
        }

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: settingsCol.implicitHeight + root.sideMargin * 2
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            Column {
                id: settingsCol
                x: root.sideMargin
                y: root.sideMargin
                width: Math.max(0, flick.width - root.sideMargin * 2)
                spacing: compact ? 16 : 20

                SectionLabel { text: "DEVICE" }
                SectionCard {
                    width: settingsCol.width

                    ColumnLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 12
                        spacing: 12

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Label {
                                text: "Device name"
                                font.pixelSize: 13
                                color: root.textMuted
                                Layout.preferredWidth: compact ? 92 : 110
                                elide: Text.ElideRight
                            }
                            ThemedField {
                                text: fileShareController.deviceName
                                onEditingFinished: fileShareController.deviceName = text
                            }
                        }
                    }
                }

                SectionLabel { text: "SHARING" }
                SectionCard {
                    width: settingsCol.width

                    ColumnLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 12
                        spacing: 12

                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: 12
                            color: root.textMuted
                            text: "Nearby Sharing uses built-in transport and discovery settings."
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Label {
                                Layout.fillWidth: true
                                color: root.textPrimary
                                font.pixelSize: 13
                                text: "Auto-accept incoming"
                            }
                            ThemedToggle {
                                checked: fileShareController.autoAcceptIncoming
                                onToggled: fileShareController.autoAcceptIncoming = checked
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Label {
                                Layout.fillWidth: true
                                color: root.textPrimary
                                font.pixelSize: 13
                                text: "Enable 5 GHz hotspot"
                            }
                            ThemedToggle {
                                checked: fileShareController.enable5GhzHotspot
                                onToggled: fileShareController.enable5GhzHotspot = checked
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Label {
                                Layout.fillWidth: true
                                color: root.textPrimary
                                font.pixelSize: 13
                                text: "Start at login"
                            }
                            ThemedToggle {
                                checked: fileShareController.startOnLogin
                                onToggled: fileShareController.startOnLogin = checked
                            }
                        }
                    }
                }

                SectionLabel {
                    text: "DIAGNOSTICS"
                    visible: fileShareController.diagnosticsSummary.length > 0
                }
                SectionCard {
                    width: settingsCol.width
                    visible: fileShareController.diagnosticsSummary.length > 0

                    ColumnLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 12
                        spacing: 12

                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: 12
                            color: "#facc15"
                            text: fileShareController.diagnosticsSummary
                        }
                    }
                }

                SectionLabel { text: "LOGGING" }
                SectionCard {
                    width: settingsCol.width

                    ColumnLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 12
                        spacing: 12

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Label {
                                text: "Log path"
                                font.pixelSize: 13
                                color: root.textMuted
                                Layout.preferredWidth: compact ? 92 : 110
                                elide: Text.ElideRight
                            }
                            ThemedField {
                                font.pixelSize: 11
                                text: fileShareController.logPath
                                onEditingFinished: fileShareController.logPath = text
                            }
                        }
                    }
                }
            }
        }
    }

    component SectionLabel: Label {
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.letterSpacing: 0.8
        color: root.accent
    }

    component SectionCard: Rectangle {
        radius: 12
        color: root.surface
        border.color: root.borderColor
        height: (children.length > 0 ? children[0].implicitHeight : 0) + 24
    }

    component ThemedField: TextField {
        Layout.fillWidth: true
        implicitHeight: 38
        font.pixelSize: 13
        leftPadding: 12
        rightPadding: 12
        topPadding: 0
        bottomPadding: 0
        verticalAlignment: TextInput.AlignVCenter
        color: root.textPrimary
        background: Rectangle {
            radius: 8
            color: "#09090b"
            border.color: parent.activeFocus ? root.accent : root.borderColor
            border.width: parent.activeFocus ? 2 : 1
        }
    }

    component ThemedToggle: Switch {
        palette.highlight: root.accent
    }
}
