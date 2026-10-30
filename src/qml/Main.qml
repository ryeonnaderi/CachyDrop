
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import QtCore
import QtQuick.Layouts


Kirigami.ApplicationWindow {
    id: root
    property int selectedPage: 0

    globalDrawer: Kirigami.GlobalDrawer {
    id: navigationDrawer
    title: "CachyDrop"
    titleIcon: "network-connect"
    modal: false

    actions: [
        Kirigami.Action {
            text: "CachyDrop"
            icon.name: "document-send"
            checked: root.selectedPage === 0
            onTriggered: root.selectedPage = 0
        },
        Kirigami.Action {
            text: "Paired Devices"
            icon.name: "network-connect"
            checked: root.selectedPage === 1
            onTriggered: root.selectedPage = 1
        },
        Kirigami.Action {
            text: "Appearance"
            icon.name: "preferences-desktop-theme"
            checked: root.selectedPage === 2
            onTriggered: root.selectedPage = 2
        }
    ]
}

    color: "transparent"

        Settings {
        id: appSettings
        category: "Appearance"

        property string theme: "System"
        property string accent: "Blue"
        property string style: "KDE Native"
        property bool transparencyEnabled: false
        property real windowOpacity: 0.85
    }

    title: "CachyDrop"
    width: 900
    height: 760
    minimumWidth: 680
    minimumHeight: 600
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
    objectName: "homePage"
    title: "CachyDrop"
    StackLayout {
    id: contentLayout
    anchors.fill: parent
    currentIndex: root.selectedPage
      

        Flickable {
    contentWidth: width
    contentHeight: mainColumn.implicitHeight
                   + Kirigami.Units.largeSpacing * 2
    clip: true

            Column {
                id: mainColumn

                width: Math.min(parent.width - 64, 680)
                x: Math.round((parent.width - width) / 2)
                y: Kirigami.Units.largeSpacing
                spacing: Kirigami.Units.gridUnit

                Kirigami.Heading {
                    width: parent.width
                    text: "CachyDrop"
                    level: 1
                    horizontalAlignment: Text.AlignHCenter
                }

                QQC2.Label {
                    width: parent.width
                    text: "Send files between your phone and computer"
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    opacity: 0.8
                }

                Rectangle {
                    width: parent.width
                    height: 52
                    radius: 12

                    color: receiverServer.running
                           ? "#183b2c"
                           : Kirigami.Theme.alternateBackgroundColor

                    border.width: 1
                    border.color: receiverServer.running
                                  ? "#287a50"
                                  : Kirigami.Theme.disabledTextColor

                    Row {
                        anchors.centerIn: parent
                        spacing: 10

                        Rectangle {
                            width: 10
                            height: 10
                            radius: 5
                            anchors.verticalCenter: parent.verticalCenter

                            color: receiverServer.running
                                   ? "#4ade80"
                                   : Kirigami.Theme.disabledTextColor
                        }

                        QQC2.Label {
                            anchors.verticalCenter: parent.verticalCenter

                            text: receiverServer.running
                                  ? "Receiving service is active"
                                  : "Receiving service is stopped"

                            color: receiverServer.running
                                   ? "#bbf7d0"
                                   : Kirigami.Theme.textColor

                            font.weight: Font.DemiBold
                        }
                    }
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
                    height: connectionColumn.implicitHeight + 32
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
                    height: transferColumn.implicitHeight + 32
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

                        Rectangle {
                            id: dropZone

                            width: parent.width
                            height: 132
                            radius: 12

                            color: dropArea.containsDrag
                                   ? Kirigami.Theme.alternateBackgroundColor
                                   : "transparent"

                            border.width: 2
                            border.color: dropArea.containsDrag
                                          ? Kirigami.Theme.highlightColor
                                          : Kirigami.Theme.disabledTextColor

                            opacity: receiverServer.running ? 1.0 : 0.55

                            Column {
                                anchors.centerIn: parent
                                spacing: Kirigami.Units.smallSpacing

                                Kirigami.Icon {
                                    source: "document-import"
                                    width: 32
                                    height: 32
                                    anchors.horizontalCenter: parent.horizontalCenter
                                }

                                QQC2.Label {
                                    text: dropArea.containsDrag
                                          ? "Drop files here"
                                          : "Drag and drop files here"
                                    font.weight: Font.DemiBold
                                    anchors.horizontalCenter: parent.horizontalCenter
                                }

                                QQC2.Label {
                                    text: "Or use the button below to browse"
                                    opacity: 0.75
                                    anchors.horizontalCenter: parent.horizontalCenter
                                }
                            }

                            DropArea {
                                id: dropArea
                                anchors.fill: parent
                                enabled: receiverServer.running

                                onDropped: function(drop) {
                                    const paths = []

                                    for (let i = 0; i < drop.urls.length; ++i) {
                                        const url = String(drop.urls[i])

                                        if (url.startsWith("file://")) {
                                            let path = url.replace(/^file:\/\/+/, "/")

                                            try {
                                                path = decodeURIComponent(path)
                                            } catch (error) {
                                                // Keep original path if decoding fails.
                                            }

                                            paths.push(path)
                                        }
                                    }

                                    if (paths.length === 0) {
                                        shareStatus.text =
                                            "No local files were found in that drop."
                                        return
                                    }

                                    const count = receiverServer.shareFiles(paths)

                                    shareStatus.text = count > 0
                                        ? count + " file(s) ready to download on your phone."
                                        : "No files could be shared. Check file permissions."
                                }
                            }
                        }

                        QQC2.Button {
    width: parent.width
    text: "Select files to send to phone"
    icon.name: "document-send"
    enabled: receiverServer.running
    highlighted: true

    onClicked: receiverServer.openMultiFilePicker()
}

                        QQC2.Label {
                            id: shareStatus
                            width: parent.width
                            text: ""
                            wrapMode: Text.Wrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        QQC2.Label {
                            width: parent.width

                            text: receiverServer.sharedFileCount === 0
                                  ? "No outgoing files selected"
                                  : receiverServer.sharedFileCount
                                    + (receiverServer.sharedFileCount === 1
                                       ? " file ready to send"
                                       : " files ready to send")

                            visible: receiverServer.running
                            wrapMode: Text.Wrap
                            horizontalAlignment: Text.AlignHCenter

                            font.pixelSize: 20
                            font.weight: Font.DemiBold

                            color: receiverServer.sharedFileCount > 0
                                   ? Kirigami.Theme.positiveTextColor
                                   : Kirigami.Theme.disabledTextColor
                        }

                        Kirigami.Separator {
                            width: parent.width
                        }

                        QQC2.Label {
                            width: parent.width
                            text: "Incoming transfers require your approval."
                            wrapMode: Text.Wrap
                            horizontalAlignment: Text.AlignHCenter
                            font.weight: Font.Medium
                        }
                    }
                         }
            }
        }

        Loader {
            sourceComponent: devicePage
        }

        Loader {
            sourceComponent: appearancePage
        }
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
                text: "Files (" + receiverServer.pendingFileCount + "):\n"
                      + receiverServer.pendingFilenames.join("\n")
                wrapMode: Text.Wrap
                font.bold: true
            }

            QQC2.Label {
                width: parent.width
                text: "Total size: "
                      + (receiverServer.pendingSize / (1024 * 1024)).toFixed(2)
                      + " MiB"
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
            spacing: 8
            alignment: Qt.AlignRight
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

    Component {
        id: devicePage
Item {
    anchors.fill: parent
    objectName: "deviceManagerPage"

            Column {
                width: parent.width
                spacing: Kirigami.Units.largeSpacing

                Kirigami.Heading {
                    text: "Your devices"
                    level: 2
                }

                QQC2.Label {
                    width: parent.width
                    text: "These devices have been approved to connect to CachyDrop. Removing a device revokes its saved authorization."
                    wrapMode: Text.Wrap
                    opacity: 0.8
                }

                QQC2.Label {
                    width: parent.width
                    text: "No paired devices yet."
                    visible: receiverServer.trustedDeviceList.length === 0
                    opacity: 0.7
                }

                Repeater {
                    model: receiverServer.trustedDeviceList

                    delegate: Rectangle {
                        required property var modelData

                        width: parent.width
                        height: deviceDetails.implicitHeight + 24
                        radius: 12
                        color: Kirigami.Theme.backgroundColor
                        border.width: 1
                        border.color: Kirigami.Theme.alternateBackgroundColor

                        Row {
                            id: deviceDetails

                            x: 12
                            y: 12
                            width: parent.width - 24
                            spacing: Kirigami.Units.mediumSpacing

                            Column {
                                width: Math.max(
                                    100,
                                    parent.width - removeButton.width
                                    - parent.spacing
                                )
                                spacing: 4

                                QQC2.Label {
                                    width: parent.width
                                    text: modelData.name
                                    font.bold: true
                                    wrapMode: Text.Wrap
                                }

                                QQC2.Label {
                                    width: parent.width
                                    text: "Paired device"
                                    opacity: 0.7
                                }
                            }

                            QQC2.Button {
                                id: removeButton

                                text: "Remove"
                                icon.name: "edit-delete"
                                anchors.verticalCenter: parent.verticalCenter

                                onClicked: {
                                    receiverServer.removeTrustedDevice(
                                        modelData.token
                                    )
                                }
                            }
                        }
                    }
                }

               
            }
        }
    }

        
Component {
    id: appearancePage

    Item {
    anchors.fill: parent
        objectName: "appearanceSettingsPage"
        


            Column {
                width: parent.width
                spacing: Kirigami.Units.largeSpacing

                Kirigami.Heading {
                    text: "Make CachyDrop yours"
                    level: 2
                }

                QQC2.Label {
                    text: "Choose how CachyDrop looks."
                    opacity: 0.8
                }

                Kirigami.Heading {
                    text: "Color theme"
                    level: 3
                }

                QQC2.ComboBox {
                    width: parent.width
                    model: ["System", "Light", "Dark"]
                    currentIndex: Math.max(0, model.indexOf(appSettings.theme))

                    onActivated: appSettings.theme = currentText
                }

                Kirigami.Heading {
                    text: "Accent color"
                    level: 3
                }

                QQC2.ComboBox {
                    width: parent.width
                    model: ["Blue", "Violet", "Green", "Pink", "Orange"]
                    currentIndex: Math.max(0, model.indexOf(appSettings.accent))

                    onActivated: appSettings.accent = currentText
                }

                Kirigami.Heading {
                    text: "Interface style"
                    level: 3
                }

                QQC2.ComboBox {
                    width: parent.width
                    model: [
                        "KDE Native",
                        "AirDrop Inspired",
                        "CachyOS"
                    ]
                    currentIndex: Math.max(0, model.indexOf(appSettings.style))

                    onActivated: appSettings.style = currentText
                }


Kirigami.Heading {
    text: "Transparency"
    level: 3
}

QQC2.Switch {
    text: "Enable window transparency"
    checked: appSettings.transparencyEnabled
    onToggled: appSettings.transparencyEnabled = checked
}

QQC2.Label {
    text: "Window opacity: " +
          Math.round(appSettings.windowOpacity * 100) + "%"
}

QQC2.Slider {
    width: parent.width
    from: 0.60
    to: 1.0
    stepSize: 0.05
    value: appSettings.windowOpacity
    enabled: appSettings.transparencyEnabled

    onMoved: appSettings.windowOpacity = value
}



                Kirigami.InlineMessage {
                    width: parent.width
                    type: Kirigami.MessageType.Information
                    text: "Your preferences are saved. Visual theme and style changes will be connected in the next step."
                    visible: true
                }

              
            }
        }
    }
}
