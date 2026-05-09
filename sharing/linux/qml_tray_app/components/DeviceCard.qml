import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var modelData

    width: 116
    height: deviceColumn.implicitHeight

    readonly property color surface: "#1c1c1f"
    readonly property color textPrimary: "#fafafa"
    readonly property color textMuted: "#a1a1aa"
    readonly property color avatarFill: "#27272a"
    readonly property color avatarBorder: "#3f3f46"
    readonly property color ringBase: "#3f3f46"
    readonly property color ringActive: "#10b981"
    readonly property color ringComplete: "#10b981"
    readonly property color ringFailed: "#ef4444"
    property bool canSend: fileShareController.mode === "Send"
                                    && fileShareController.pendingSendFileCount > 0

    readonly property string targetName: modelData && modelData.name && modelData.name.length > 0
                                         ? modelData.name : "Unknown device"
    readonly property var transferData: transferForTarget()
    readonly property string transferStatus: transferData ? String(transferData.status || "") : ""
    readonly property bool hasTransfer: transferData !== null
    readonly property bool isTransferActive: transferStatus === "InProgress"
                                             || transferStatus === "Queued"
                                             || transferStatus === "Connecting"
                                             || transferStatus === "AwaitingLocalConfirmation"
                                             || transferStatus === "AwaitingRemoteAcceptance"
    readonly property bool isTransferComplete: transferStatus === "Complete"
    readonly property bool isTransferFailed: hasTransfer && !isTransferActive && !isTransferComplete
    readonly property bool isConnecting: transferStatus === "Connecting"
    property bool showCompletionTick: false
    property string previousTransferStatus: ""
    readonly property real transferProgress: {
        if (!hasTransfer)
            return 0
        if (isTransferComplete || isTransferFailed)
            return 1
        var numeric = Number(transferData.progress)
        if (!isFinite(numeric) || numeric < 0)
            numeric = 0
        if (isTransferActive && numeric === 0)
            return 0.08
        return Math.max(0, Math.min(1, numeric))
    }
    readonly property color ringColor: isTransferComplete ? ringComplete
                                        : isTransferFailed ? ringFailed
                                        : ringActive

    function initialLetter(label) {
        if (!label || label.length === 0) return "?"
        return label.charAt(0).toUpperCase()
    }

    function transferForTarget() {
        if (!modelData)
            return null
        var transfers = fileShareController.transfers
        for (var i = 0; i < transfers.length; ++i) {
            var entry = transfers[i]
            if (entry && entry.targetId === modelData.id)
                return entry
        }
        return null
    }

    function formatBytes(bytes) {
        if (!bytes || bytes === 0) return "0 B"
        var k = 1024
        var sizes = ["B", "KB", "MB", "GB", "TB"]
        var i = Math.floor(Math.log(bytes) / Math.log(k))
        return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i]
    }

    function formatTransferSize(transferred, total) {
        if (!total || total === 0) return formatBytes(transferred)
        var k = 1024
        var sizes = ["B", "KB", "MB", "GB", "TB"]
        var i = Math.floor(Math.log(total) / Math.log(k))
        var totalStr = parseFloat((total / Math.pow(k, i)).toFixed(1))
        var transStr = parseFloat((transferred / Math.pow(k, i)).toFixed(1))
        return transStr + "/" + totalStr + " " + sizes[i]
    }

    Column {
        id: deviceColumn
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width
        spacing: 10

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 84
            height: 84
            radius: 42
            color: surface
            opacity: canSend ? 1.0 : 0.5

            Canvas {
                id: progressRing
                anchors.fill: parent
                antialiasing: true
                transformOrigin: Item.Center
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)

                    var lineWidth = 5
                    var radius = (Math.min(width, height) - lineWidth) / 2
                    var center = width / 2

                    ctx.lineWidth = lineWidth
                    ctx.lineCap = "round"

                    if (!root.hasTransfer)
                        return

                    ctx.beginPath()
                    ctx.strokeStyle = root.ringColor
                    ctx.arc(center, center, radius, -Math.PI / 2,
                            -Math.PI / 2 + Math.PI * 2 * root.transferProgress, false)
                    ctx.stroke()
                }
            }

            NumberAnimation {
                id: connectingSpin
                target: progressRing
                property: "rotation"
                from: 0
                to: 360
                duration: 1100
                easing.type: Easing.Linear
                loops: Animation.Infinite
                running: root.isConnecting
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: 9
                radius: width / 2
                color: avatarFill

                Label {
                    anchors.centerIn: parent
                    text: initialLetter(targetName)
                    font.pixelSize: 28
                    font.weight: Font.DemiBold
                    color: textPrimary
                }
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: 9
                radius: width / 2
                color: "#16a34a"
                opacity: showCompletionTick ? 0.94 : 0.0
                visible: opacity > 0

                Behavior on opacity {
                    NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
                }

                Canvas {
                    anchors.centerIn: parent
                    width: 28
                    height: 28

                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        ctx.strokeStyle = "#ffffff"
                        ctx.lineWidth = 4
                        ctx.lineCap = "round"
                        ctx.lineJoin = "round"
                        ctx.beginPath()
                        ctx.moveTo(width * 0.18, height * 0.56)
                        ctx.lineTo(width * 0.42, height * 0.8)
                        ctx.lineTo(width * 0.84, height * 0.24)
                        ctx.stroke()
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                enabled: canSend
                onClicked: fileShareController.sendPendingFilesToTarget(modelData.id)
            }
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: targetName
            font.pixelSize: 13
            font.weight: Font.Bold
            elide: Text.ElideRight
            maximumLineCount: 2
            wrapMode: Text.Wrap
            color: textPrimary
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: isTransferActive && !isConnecting && transferData && transferData.totalBytes > 0
            text: Math.floor(transferProgress * 100) + "%"
            font.pixelSize: 12
            font.weight: Font.Medium
            color: "#10b981"
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: isTransferActive && !isConnecting && transferData && transferData.totalBytes > 0
            text: transferData ? formatTransferSize(transferData.transferredBytes, transferData.totalBytes) : ""
            font.pixelSize: 11
            color: textMuted
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: isTransferActive && !isConnecting && transferData && transferData.transferSpeed > 0
            text: transferData ? formatBytes(transferData.transferSpeed) + "/s" : ""
            font.pixelSize: 11
            color: textMuted
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: isTransferActive && transferData && transferData.connectionMedium.length > 0
            text: transferData ? transferData.connectionMedium : ""
            font.pixelSize: 10
            font.weight: Font.Medium
            color: textMuted
        }
    }

    Timer {
        id: completionTickTimer
        interval: 1200
        repeat: false
        onTriggered: root.showCompletionTick = false
    }

    onTransferDataChanged: progressRing.requestPaint()
    onTransferProgressChanged: progressRing.requestPaint()
    onRingColorChanged: progressRing.requestPaint()
    onIsConnectingChanged: {
        if (!isConnecting)
            progressRing.rotation = 0
    }
    onTransferStatusChanged: {
        if (transferStatus === "Complete" && previousTransferStatus.length > 0
                && previousTransferStatus !== "Complete") {
            showCompletionTick = true
            completionTickTimer.restart()
        } else if (transferStatus !== "Complete") {
            showCompletionTick = false
            completionTickTimer.stop()
        }

        previousTransferStatus = transferStatus
    }

    Component.onCompleted: previousTransferStatus = transferStatus
}
