
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root

    title: "CachyDrop"
    width: 760
    height: 700
    minimumWidth: 600
    minimumHeight: 560
    visible: true

    Connections {
        target: receiverServer

        function onPendingRequestChanged() {
            if (receiverServer.hasPendingRequest)
                approvalDialog.open()
            else
                approvalDialog.close()
        }

        function onPairingRequestChanged() {
            if (receiverServer.hasPendingPairing)
                pairingDialog.open()
            else
                pairingDialog.close()
        }
    }

    pageStack.initialPage: Kirigami.Page {
        title: "CachyDrop"

        Flickable {
            anchors.fill: parent
            contentWidth: availableWidth
            contentHeight: mainColumn.implicitHeight
                           + Kirigami.Units.largeSpacing * 2
            clip: true

            Column {
                id: mainColumn

                width: Math.min(parent.width - 40, 480)
                x: Math.round((parent.width - width) / 2)
                y: Kirigami.Units.largeSpacing
                spacing: Kirigami.Units.largeSpacing

                Kirigami.Heading {
                    width: parent.width
                    text: "CachyDrop"
                    level: 1
                    horizontalAlignment: Text.AlignHCenter
                }

                QQC2.Label {
                    width: parent.width
                    text: receiverServer.running
                          ? "●  Receiving service is running"
                          : "Ready to receive files"
                    horizontalAlignment: Text.AlignHCenter
                    color: receiverServer.running
                           ? Kirigami.Theme.positiveTextColor
                           : Kirigami.Theme.textColor
                }

                QQC2.Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: receiverServer.running
                          ? "Stop receiving"
                          : "Start receiving"
                    icon.name: receiverServer.running
                               ? "media-playback-stop"
                               : "download"
                    highlighted: true

                    onClicked: {
                        if (receiverServer.running) {
                            receiverServer.stop()
                        } else if (!receiverServer.start()) {
                            errorDialog.open()
                        }
                    }
                }

                Rectangle {
                    id: connectionCard

                    width: parent.width
                    height: connectionColumn.implicitHeight
                            + Kirigami.Units.largeSpacing * 2
                    radius: 16
                    color: Kirigami.Theme.backgroundColor
                    border.width: 1
                    border.color: Kirigami.Theme.alternateBackgroundColor
                    visible: receiverServer.running

                    Column {
                        id: connectionColumn

                        width: parent.width - 32
                        x: 16
                        y: 16
                        spacing: Kirigami.Units.mediumSpacing

                        Kirigami.Heading {
                            width: parent.width
                            text: "Connect your phone"
                            level: 2
                            horizontalAlignment: Text.AlignHCenter
                        }

                        QQC2.Label {
                            width: parent.width
                            text: "Scan this QR code with your phone camera."
                            wrapMode: Text.Wrap
                            horizontalAlignment: Text.AlignHCenter
                            opacity: 0.8
                        }

                        Rectangle {
                            id: qrCard

                            width: 248
                            height: 248
                            radius: 12
                            color: "#ffffff"
                            border.width: 1
                            border.color: "#d8dce3"
                            visible: receiverServer.qrRows.length > 0
                            anchors.horizontalCenter: parent.horizontalCenter

                            Canvas {
                                id: qrCanvas

                                width: 224
                                height: 224
                                anchors.centerIn: parent

                                property var qrRows: receiverServer.qrRows

                                onQrRowsChanged: requestPaint()

                                onPaint: {
                                    const ctx = getContext("2d")
                                    const rows = qrRows
                                    const count = rows.length

                                    ctx.clearRect(0, 0, width, height)
                                    ctx.fillStyle = "#ffffff"
                                    ctx.fillRect(0, 0, width, height)

                                    if (count === 0)
                                        return

                                    const quietZone = 4
                                    const modules = count + quietZone * 2
                                    const scale = Math.floor(
                                        Math.min(width, height) / modules
                                    )
                                    const size = scale * modules
                                    const offsetX = Math.floor((width - size) / 2)
                                    const offsetY = Math.floor((height - size) / 2)

                                    ctx.fillStyle = "#000000"

                                    for (let y = 0; y < count; ++y) {
                                        for (let x = 0; x < rows[y].length; ++x) {
                                            if (rows[y].charAt(x) === "1") {
                                                ctx.fillRect(
                                                    offsetX + (x + quietZone) * scale,
                                                    offsetY + (y + quietZone) * scale,
                                                    scale,
                                                    scale
                                                )
                                            }
                                        }
                                    }
                                }

                                Component.onCompleted: requestPaint()
                            }
                        }

                        QQC2.Label {
                            width: parent.width
                            visible: !qrCard.visible
                            text: "QR code unavailable"
                            horizontalAlignment: Text.AlignHCenter
                            opacity: 0.7
                        }

                        QQC2.Label {
                            width: parent.width
                            text: "Or open this address manually:"
                            horizontalAlignment: Text.AlignHCenter
                            opacity: 0.8
                        }

                        QQC2.Label {
                            width: parent.width
                            text: receiverServer.address
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WrapAnywhere
                            font.family: "monospace"
                            color: Kirigami.Theme.linkColor
                        }

                        Kirigami.Separator {
                            width: parent.width
                        }

                        QQC2.Label {
                            width: parent.width
                            text: "Pairing code"
                            horizontalAlignment: Text.AlignHCenter
                            opacity: 0.8
                        }

                        Kirigami.Heading {
                            width: parent.width
                            text: receiverServer.pairingCode
                            level: 1
                            horizontalAlignment: Text.AlignHCenter
                        }

                        QQC2.Label {
                            width: parent.width
                            text: "Scan the QR code to request device pairing."
                            wrapMode: Text.Wrap
                            horizontalAlignment: Text.AlignHCenter
                            opacity: 0.8
                        }
                    }
                }

                Rectangle {
                    id: transferCard

                    width: parent.width
                    height: transferColumn.implicitHeight
                            + Kirigami.Units.largeSpacing * 2
                    radius: 16
                    color: Kirigami.Theme.backgroundColor
                    border.width: 1
                    border.color: Kirigami.Theme.alternateBackgroundColor

                    Column {
                        id: transferColumn

                        width: parent.width - 32
                        x: 16
                        y: 16
                        spacing: Kirigami.Units.mediumSpacing

                        Kirigami.Heading {
                            width: parent.width
                            text: "Transfer files"
                            level: 2
                        }

                        QQC2.Label {
                            width: parent.width
                            text: "Send files from this computer to your phone."
                            wrapMode: Text.Wrap
                            opacity: 0.8
                        }

                        QQC2.Button {
                            width: parent.width
                            text: "Select files to send to phone"
                            icon.name: "document-send"
                            enabled: receiverServer.running
                            highlighted: true
                            onClicked: fileDialog.open()
                        }

                        QQC2.Label {
                            width: parent.width
                            text: receiverServer.sharedFileCount === 0
                                  ? "No outgoing files selected"
                                  : receiverServer.sharedFileCount
                                    + " file(s) available to download"
                            visible: receiverServer.running
                            wrapMode: Text.Wrap
                            horizontalAlignment: Text.AlignHCenter
                            opacity: 0.8
                        }

                        QQC2.Label {
                            width: parent.width
                            text: "Incoming transfers require your approval."
                            wrapMode: Text.Wrap
                            horizontalAlignment: Text.AlignHCenter
                            opacity: 0.8
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: fileDialog
        title: "Select files to send to your phone"
        fileMode: FileDialog.OpenFiles

        onAccepted: {
            const paths = selectedFiles.map(file => {
                const url = file.toString()
                return decodeURIComponent(url.replace(/^file:\/\/+/, "/"))
            })

            receiverServer.shareFiles(paths)
        }
    }

    QQC2.Dialog {
        id: errorDialog
        parent: root.overlay
        anchors.centerIn: parent
        width: Math.min(root.width - 48, 380)
        title: "Could not start receiving"
        modal: true
        standardButtons: QQC2.Dialog.Ok

        contentItem: QQC2.Label {
            text: "CachyDrop could not start the receiving service. Check the terminal for errors or verify that port 8765 is available."
            wrapMode: Text.Wrap
        }
    }

    // Device pairing approval — separate from file-transfer approval.
    QQC2.Dialog {
        id: pairingDialog
        parent: root.overlay
        anchors.centerIn: parent
        width: Math.min(root.width - 48, 420)
        title: "Device wants to pair"
        modal: true
        closePolicy: QQC2.Popup.NoAutoClose

        contentItem: Column {
            spacing: Kirigami.Units.mediumSpacing
            width: parent ? parent.width : 360

            QQC2.Label {
                width: parent.width
                text: "A device is requesting permission to pair with CachyDrop."
                wrapMode: Text.Wrap
            }

            QQC2.Label {
                width: parent.width
                text: "Device: " + receiverServer.pendingDevice
                wrapMode: Text.Wrap
                font.bold: true
            }

            QQC2.Label {
                width: parent.width
                text: "Only approve this request if you initiated it from your own phone."
                wrapMode: Text.Wrap
                opacity: 0.8
            }
        }

        footer: QQC2.DialogButtonBox {
            standardButtons: QQC2.DialogButtonBox.NoButton

            QQC2.Button {
                text: "Reject"
                QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.RejectRole

                onClicked: {
                    receiverServer.rejectPairing()
                    pairingDialog.close()
                }
            }

            QQC2.Button {
                text: "Approve device"
                highlighted: true
                QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.AcceptRole

                onClicked: {
                    receiverServer.approvePairing()
                    pairingDialog.close()
                }
            }
        }
    }

    // Incoming file-transfer approval remains separate.
    QQC2.Dialog {
        id: approvalDialog
        parent: root.overlay
        anchors.centerIn: parent
        width: Math.min(root.width - 48, 420)
        title: "Incoming transfer"
        modal: true
        closePolicy: QQC2.Popup.NoAutoClose

        contentItem: Column {
            spacing: Kirigami.Units.mediumSpacing
            width: parent ? parent.width : 360

            QQC2.Label {
                width: parent.width
                text: "A device wants to send files to this computer."
                wrapMode: Text.Wrap
            }

            QQC2.Label {
                width: parent.width
                text: "File: " + receiverServer.pendingFilename
                wrapMode: Text.Wrap
                font.bold: true
            }

            QQC2.Label {
                width: parent.width
                text: "Size: " + receiverServer.pendingSize + " bytes"
                wrapMode: Text.Wrap
            }

            QQC2.Label {
                width: parent.width
                text: "Sender: " + receiverServer.pendingSender
                wrapMode: Text.Wrap
                opacity: 0.8
            }
        }

        footer: QQC2.DialogButtonBox {
            standardButtons: QQC2.DialogButtonBox.NoButton

            QQC2.Button {
                text: "Reject"
                QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.RejectRole

                onClicked: {
                    receiverServer.rejectRequest()
                    approvalDialog.close()
                }
            }

            QQC2.Button {
                text: "Approve transfer"
                highlighted: true
                QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.AcceptRole

                onClicked: {
                    receiverServer.approveRequest()
                    approvalDialog.close()
                }
            }
        }
    }
}
