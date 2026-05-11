import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "components"

ApplicationWindow {
    id: root
    width: 980
    height: 760
    minimumWidth: 480
    minimumHeight: 520
    visible: !startHidden
    title: "Quick Share"
    flags: Qt.Window
           | Qt.WindowTitleHint
           | Qt.WindowSystemMenuHint
           | Qt.WindowMinimizeButtonHint
           | Qt.WindowMaximizeButtonHint
           | Qt.WindowCloseButtonHint

    readonly property bool narrow: width < 760
    readonly property bool compact: width < 620 || height < 600
    readonly property int headerHeight: compact ? 64 : 80
    readonly property int contentPadding: Math.round(Math.max(20, Math.min(48, width * 0.05)))
    readonly property int panelRadius: Math.round(Math.max(24, Math.min(48, width * 0.045)))
    readonly property int deviceSpacing: compact ? 12 : 20
    readonly property int inlineSidebarWidth: Math.round(Math.max(220, Math.min(280, width * 0.3)))

    background: Rectangle { color: "#09090b" }

    Shortcut {
        sequence: StandardKey.Paste
        onActivated: fileShareController.prepareSendFromClipboard()
    }

    onClosing: function(close) {
        if (trayAvailable) {
            close.accepted = false
            root.hide()
            fileShareController.hideToTray()
        } else {
            close.accepted = true
            Qt.quit()
        }
    }

    onNarrowChanged: {
        if (!narrow)
            sideBarDrawer.close()
    }

    SettingsPanel {
        id: settingsPanel
    }

    Drawer {
        id: sideBarDrawer
        edge: Qt.LeftEdge
        width: Math.min(root.width, Math.max(300, root.width * 0.78))
        height: root.height
        modal: true
        interactive: root.narrow
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: "#09090b"
            border.color: "#27272a"
        }

        SideBar {
            anchors.fill: parent
            compact: true
            drawerMode: true
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        AppHeader {
            compact: root.compact
            height: root.headerHeight
            showSidebarButton: root.narrow
            onSidebarRequested: sideBarDrawer.open()
            onSettingsRequested: settingsPanel.open()
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            SideBar {
                visible: !root.narrow
                compact: root.compact
                drawerMode: false
                Layout.preferredWidth: visible ? root.inlineSidebarWidth : 0
            }

            Rectangle {
                id: mainContent
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#18181b"
                radius: root.panelRadius
                clip: true
                border.color: "#27272a"
                border.width: 1

                readonly property bool isSendMode: fileShareController.pendingSendFileCount > 0

                AnimatedBlob {
                    visible: !mainContent.isSendMode
                    compact: root.compact
                    contentPadding: root.contentPadding
                }

                Flickable {
                    id: mainFlickable
                    anchors.fill: parent
                    clip: true
                    visible: mainContent.isSendMode
                    contentWidth: width
                    contentHeight: Math.max(height, mainCol.implicitHeight + root.contentPadding * 2)
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}

                    ColumnLayout {
                        id: mainCol
                        width: Math.max(0, parent.width - root.contentPadding * 2)
                        x: root.contentPadding
                        y: root.contentPadding
                        spacing: root.compact ? 12 : 16

                        SendUrlPanel {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.fillWidth: true
                            Layout.maximumWidth: 420
                            compact: root.compact || mainCol.width < 360
                            visible: fileShareController.pendingSendKind === "files"
                        }

                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.fillWidth: true
                            Layout.maximumWidth: 520
                            Layout.preferredHeight: textPreview.implicitHeight + 42
                            visible: fileShareController.pendingSendKind === "text"
                                     || fileShareController.pendingSendKind === "link"
                            radius: 12
                            color: "#1c1c1f"
                            border.color: "#3f3f46"

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 8

                                Label {
                                    Layout.fillWidth: true
                                    text: fileShareController.pendingSendKind === "link"
                                          ? "Link ready to send" : "Text ready to send"
                                    color: "#fafafa"
                                    font.pixelSize: root.compact ? 14 : 16
                                    font.weight: Font.Medium
                                    elide: Text.ElideRight
                                }

                                Label {
                                    id: textPreview
                                    Layout.fillWidth: true
                                    text: fileShareController.pendingSendText
                                    color: "#d4d4d8"
                                    font.pixelSize: root.compact ? 12 : 13
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 5
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: "Nearby devices"
                            font.pixelSize: root.compact ? 18 : 20
                            font.weight: Font.Medium
                            color: "#fafafa"
                            elide: Text.ElideRight
                        }

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: deviceFlow.childrenRect.height
                            visible: fileShareController.discoveredTargets.length > 0

                            Flow {
                                id: deviceFlow
                                width: parent.width
                                spacing: root.deviceSpacing

                                Repeater {
                                    model: fileShareController.discoveredTargets
                                    delegate: DeviceCard {
                                        compact: root.compact || mainCol.width < 520
                                    }
                                }
                            }
                        }
                    }
                }

                DropArea {
                    id: dropArea
                    anchors.fill: parent
                    onEntered: (drag) => {
                        if (drag.hasUrls || drag.hasText) {
                            drag.accept(Qt.LinkAction)
                        }
                    }
                    onDropped: (drop) => {
                        if (drop.hasUrls) {
                            fileShareController.switchToSendModeWithUrls(drop.urls)
                            drop.accept()
                        } else if (drop.hasText) {
                            fileShareController.prepareDroppedText(drop.text)
                            drop.accept()
                        }
                    }
                    
                    Rectangle {
                        anchors.fill: parent
                        color: "#10b981"
                        opacity: dropArea.containsDrag ? 0.08 : 0
                        border.color: "#10b981"
                        border.width: dropArea.containsDrag ? 4 : 0
                        radius: root.panelRadius
                        
                        Behavior on opacity { NumberAnimation { duration: 150 } }

                        Column {
                            anchors.centerIn: parent
                            spacing: root.compact ? 8 : 12
                            visible: dropArea.containsDrag
                            
                            Label {
                                text: "Drop to share"
                                font.pixelSize: root.compact ? 20 : 24
                                font.weight: Font.Bold
                                color: "#d1fae5"
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                            
                            Label {
                                text: "Release to select files"
                                font.pixelSize: root.compact ? 13 : 16
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
