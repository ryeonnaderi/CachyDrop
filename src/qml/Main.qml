import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root

    title: "CachyDrop"
    width: 760
    height: 600
    minimumWidth: 600
    minimumHeight: 420
    visible: true

    Connections {
        target: receiverServer

        function onPendingRequestChanged() {
            if (receiverServer.hasPendingRequest)
                approvalDialog.open()
            else
                approvalDialog.close()
        }
    }

    pageStack.initialPage: Kirigami.Page {
        title: "CachyDrop"

        Flickable {
            anchors.fill: parent
            contentWidth: parent.width
            contentHeight: mainColumn.implicitHeight + Kirigami.Units.largeSpacing * 2
            clip: true

            Column {
                id: mainColumn
                anchors.horizontalCenter: parent.horizontalCenter
                y: Kirigami.Units.largeSpacing
                spacing: Kirigami.Units.largeSpacing
                width: Math.min(parent.width - 48, 440)

                Kirigami.Heading {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "CachyDrop"
                    level: 1
                }

                QQC2.Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: receiverServer.running
                          ? "Receiving service is running"
                          : "Ready to receive files"
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

                QQC2.Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: receiverServer.address
                    visible: receiverServer.running
                    font.bold: true
                }

                QQC2.Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: receiverServer.running
                          ? "Pairing code"
                          : ""
                    visible: receiverServer.running
                }

                Kirigami.Heading {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: receiverServer.pairingCode
                    visible: receiverServer.running
                    level: 2
                }

                QQC2.Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: receiverServer.running
                          ? "Enter this code on your phone"
                          : "Local network • Port 8765"
                    opacity: 0.7
                    wrapMode: Text.Wrap
                }

                QQC2.Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Select files to send to phone"
                    icon.name: "document-send"
                    enabled: receiverServer.running
                    onClicked: fileDialog.open()
                }

                QQC2.Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: receiverServer.sharedFileCount === 0
                          ? "No outgoing files selected"
                          : receiverServer.sharedFileCount
                            + " file(s) available to download"
                    visible: receiverServer.running
                    opacity: 0.7
                    wrapMode: Text.Wrap
                }

                QQC2.Label {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    visible: receiverServer.running
                    text: "On your phone, open the address above and choose “Download files from this computer”."
                    opacity: 0.7
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
        id: fileSelectionError
        parent: root.overlay
        anchors.centerIn: parent
        width: Math.min(root.width - 48, 380)
        title: "No files could be shared"
        standardButtons: QQC2.Dialog.Ok
        modal: true

        contentItem: QQC2.Label {
            text: "Make sure the selected files exist, are readable, and are no larger than 100 MiB each."
            wrapMode: Text.WordWrap
        }
    }

    QQC2.Dialog {
        id: approvalDialog
        parent: root.overlay
        anchors.centerIn: parent
        width: Math.min(root.width - 48, 420)
        title: "Approve incoming transfer"
        modal: true
        closePolicy: QQC2.Popup.NoAutoClose

        contentItem: Column {
            spacing: Kirigami.Units.mediumSpacing
            width: parent ? parent.width : 360

            QQC2.Label {
                width: parent.width
                wrapMode: Text.Wrap
                text: "A device wants to send a file to this computer."
            }

            QQC2.Label {
                width: parent.width
                wrapMode: Text.Wrap
                font.bold: true
                text: receiverServer.pendingFilename
            }

            QQC2.Label {
                width: parent.width
                text: "Size: "
                      + (receiverServer.pendingSize / (1024 * 1024)).toFixed(2)
                      + " MiB"
            }

            QQC2.Label {
                width: parent.width
                text: "Sender: " + receiverServer.pendingSender
                wrapMode: Text.Wrap
                opacity: 0.7
            }

            Row {
                spacing: Kirigami.Units.mediumSpacing
                width: parent.width

                QQC2.Button {
                    text: "Reject"
                    onClicked: receiverServer.rejectRequest()
                }

                QQC2.Button {
                    text: "Accept transfer"
                    highlighted: true
                    onClicked: receiverServer.approveRequest()
                }
            }
        }
    }

    QQC2.Dialog {
        id: errorDialog
        parent: root.overlay
        anchors.centerIn: parent
        width: Math.min(root.width - 48, 380)
        title: "Could not start receiving"
        standardButtons: QQC2.Dialog.Ok
        modal: true

        contentItem: QQC2.Label {
            text: "Port 8765 may already be in use. Close the other service and try again."
            wrapMode: Text.WordWrap
            width: parent ? parent.width : 320
        }
    }
}