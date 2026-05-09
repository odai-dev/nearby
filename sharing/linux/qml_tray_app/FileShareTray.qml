import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "components"

ApplicationWindow {
    id: root
    width: 980
    height: 760
    minimumWidth: 820
    minimumHeight: 620
    visible: true
    title: "Quick Share"

    background: Rectangle { color: "#09090b" }

    onClosing: function(close) {
        close.accepted = false
        root.hide()
        fileShareController.hideToTray()
    }

    SettingsPanel {
        id: settingsPanel
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        AppHeader {
            onSettingsRequested: settingsPanel.open()
        }

        // ── Body ─────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            SideBar {}

            // ── Main content (white panel) ────────────────────────────────
            Rectangle {
                id: mainContent
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#18181b"
                radius: 48
                clip: true
                border.color: "#27272a"
                border.width: 1

                readonly property bool isSendMode: fileShareController.pendingSendFilePath.length > 0

                // ── Idle: animated blob ───────────────────────────────────
                AnimatedBlob { visible: !mainContent.isSendMode }

                // ── Non-idle: scrollable device + transfer cards ──────────
                Flickable {
                    id: mainFlickable
                    anchors.fill: parent
                    clip: true
                    visible: mainContent.isSendMode
                    contentWidth: width
                    contentHeight: mainCol.implicitHeight + 96
                    ScrollBar.vertical: ScrollBar {}

                    ColumnLayout {
                        id: mainCol
                        width: parent.width - 96
                        x: 48
                        y: 48
                        spacing: 16

                        SendUrlPanel {
                            Layout.alignment: Qt.AlignHCenter
                            width: Math.max(240, Math.min(mainCol.width, 420))
                        }


                        Label {
                            text: "Nearby devices"
                            font.pixelSize: 20
                            font.weight: Font.Medium
                            color: "#fafafa"
                        }

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: deviceFlow.childrenRect.height
                            visible: fileShareController.discoveredTargets.length > 0

                            Flow {
                                id: deviceFlow
                                width: parent.width
                                spacing: 20

                                Repeater {
                                    model: fileShareController.discoveredTargets
                                    delegate: DeviceCard {}
                                }
                            }
                        }

                    }
                }

                DropArea {
                    id: dropArea
                    anchors.fill: parent
                    onEntered: (drag) => {
                        if (drag.hasUrls) {
                            drag.accept(Qt.LinkAction)
                        }
                    }
                    onDropped: (drop) => {
                        if (drop.hasUrls) {
                            var path = drop.urls[0].toString()
                            if (path.startsWith("file://")) {
                                path = path.substring(7)
                            }
                            fileShareController.switchToSendModeWithFile(path)
                            drop.accept()
                        }
                    }
                    
                    Rectangle {
                        anchors.fill: parent
                        color: "#10b981"
                        opacity: dropArea.containsDrag ? 0.08 : 0
                        border.color: "#10b981"
                        border.width: dropArea.containsDrag ? 4 : 0
                        radius: 48
                        
                        Behavior on opacity { NumberAnimation { duration: 150 } }

                        Column {
                            anchors.centerIn: parent
                            spacing: 12
                            visible: dropArea.containsDrag
                            
                            Label {
                                text: "Drop to share"
                                font.pixelSize: 24
                                font.weight: Font.Bold
                                color: "#d1fae5"
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                            
                            Label {
                                text: "Release to select files"
                                font.pixelSize: 16
                                color: "#a7f3d0"
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                        }
                    }
                }
            }
        }
    }
}
