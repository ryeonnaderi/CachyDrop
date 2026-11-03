# CachyDrop

**A simple Linux desktop application for transferring files between your computer and your phone over a local network.**

CachyDrop is built with C++, Qt 6, Qt Quick, and KDE Kirigami. It provides a native desktop interface for connecting devices, managing pairing requests, and controlling incoming file transfers.

> [!WARNING]
> **Active development:** CachyDrop is still under development. Security-sensitive functionality, including cryptographic identity management, authenticated key exchange, and encrypted file transfers, must not be assumed to be fully implemented or security-audited. Do not use it to transfer sensitive files until these protections have been implemented and verified.

## Features

- **QR-code pairing** — Connect a phone to your computer using the connection details displayed by CachyDrop.
- **Local file sharing** — Select files using the file picker or drag and drop them into the application.
- **Incoming transfer approval** — Review incoming transfer requests and approve or reject them.
- **Device pairing** — Handle pairing requests and authorize devices before granting access.
- **Custom device names** — Assign recognizable names to paired devices, including phones and tablets whose browsers don't expose their actual device names.
- **Trusted device management** — View paired devices and remove saved authorizations.
- **Transfer status** — View the receiving server's status and relevant transfer information.
- **Appearance customization** — Configure available themes, accent colors, interface styles, and transparency options.
- **Native Linux interface** — Built with Qt Quick and KDE Kirigami.

Features and workflows may change as development continues.

## Screenshots

Screenshots will be added as the interface and workflows mature.

## Requirements

CachyDrop is primarily developed for Linux, with CachyOS and Arch Linux as development targets.

The build requires:

- A C++ compiler with C++20 support
- CMake
- Qt 6, including Qt Quick and QML support
- Qt Quick Controls
- Qt Shader Tools
- KDE Kirigami
- `qrencode`
- `libsodium`
- Other dependencies required by the current CMake configuration

The exact dependency requirements may change as the project evolves.

## Build from source

### 1. Install dependencies

On CachyOS or Arch Linux, install the common build dependencies:

```bash
sudo pacman -S --needed \
  base-devel \
  git \
  cmake \
  pkgconf \
  qt6-base \
  qt6-declarative \
  qt6-shadertools \
  kirigami \
  qrencode \
  libsodium
```

### 2. Clone the repository

```bash
git clone https://github.com/ryeonnaderi/CachyDrop.git
cd CachyDrop
```

### 3. Configure the project

```bash
cmake -S . -B build
```

CMake will select an available build generator. The current development setup uses Unix Makefiles.

### 4. Build CachyDrop

```bash
cmake --build build
```

### 5. Run CachyDrop

```bash
./build/cachydrop
```

If the build fails, review the CMake output for missing dependencies or configuration errors. If the application fails to start, check the terminal output for missing Qt modules, QML import errors, or runtime dependencies.

## Using CachyDrop

### Connect a phone

1. Launch CachyDrop on your Linux computer.
2. Start the receiving service if it isn't already running.
3. Connect your phone to the same local network as your computer.
4. Open the connection address or scan the QR code displayed by CachyDrop.
5. Follow the pairing workflow presented by the application.
6. Approve the pairing request on your computer when appropriate.

Make sure your computer and phone can communicate over the local network. Firewall settings may prevent connections.

### Name and manage devices

When a device requests pairing, you can assign it a recognizable name in the pairing approval dialog. This is especially useful for iPhones, iPads, and other browsers that provide only a generic device name.

Open **Paired Devices** to view authorized devices. Removing a device revokes its saved authorization within the application.

### Transfer files

Use the file picker or drag and drop files into the application to prepare them for transfer. Follow the connection and transfer workflow shown by CachyDrop.

When receiving files, review the sender and file information before approving a transfer. Reject unexpected requests.

## Technology stack

- **C++20** — Application logic
- **Qt 6** — Core application framework
- **Qt Quick and QML** — User interface
- **Qt Quick Controls** — Interface controls
- **KDE Kirigami** — Desktop interface components
- **CMake** — Build system
- **libsodium** — Cryptographic primitives where implemented and used
- **qrencode** — QR-code functionality

## Project structure

The project separates the QML interface from the C++ networking and transfer logic.

Important components include:

- `src/qml/Main.qml` — Main interface, navigation, pairing dialogs, device management, and appearance settings.
- `ReceiverServer` — Receiving-server lifecycle and server-side state.
- `RequestRouter` — HTTP endpoint routing.
- `PairingManager` — Pairing requests and trusted-device management.
- `CryptoManager` — Cryptographic identity and related cryptographic operations.
- `CMakeLists.txt` — Build configuration and dependencies.
- `tests/` — Automated tests.

File names and component responsibilities may change as the project evolves.

## Security and privacy

CachyDrop is intended for local file sharing, with device pairing and explicit approval workflows to help users control access.

However, **local-network communication, device pairing, and the presence of a cryptographic library do not by themselves guarantee secure file transfers**.

Until the relevant protections have been implemented and independently verified:

- Transfer only files appropriate for an unverified local-network application.
- Connect only to devices and networks you trust.
- Review incoming requests before approving them.
- Do not assume pairing or session tokens encrypt file contents.
- Do not treat a QR code or a public key alone as proof of a device's identity.
- Keep in mind that local-network traffic may be observable by other parties on the network.

Security work includes authenticated key exchange, secure private-key storage, authenticated encryption of transferred files, robust session authorization, network exposure controls, and validation of uploaded filenames, file sizes, and destination paths.

These are development goals, not claims that every protection is already implemented.

## Development roadmap

- [ ] Complete and review cryptographic identity management.
- [ ] Implement authenticated key exchange between paired devices.
- [ ] Implement authenticated encryption for file transfers.
- [ ] Review pairing, session tokens, and authorization checks.
- [ ] Audit upload validation and file-path handling.
- [ ] Improve transfer progress, completion feedback, and error reporting.
- [ ] Expand automated tests for pairing, authorization, and transfers.
- [ ] Improve installation and packaging for supported Linux distributions.
- [ ] Add screenshots and more complete user documentation.

## Contributing

Contributions, bug reports, and suggestions are welcome.

When reporting a bug, include:

- Your Linux distribution and version.
- Your Qt version, if known.
- Steps to reproduce the issue.
- Relevant terminal output or error messages.
- Whether the issue affects pairing, sending, receiving, or the interface.

When contributing code, preserve existing authorization checks, validate external input carefully, and avoid weakening transfer security.

## License

A project license has not yet been specified. A license and corresponding `LICENSE` file will be added once the project's licensing terms have been decided.

Until then, do not assume that the source code is licensed for unrestricted redistribution, modification, or commercial use.

## Acknowledgments

CachyDrop uses Qt, Qt Quick, KDE Kirigami, and libsodium to support its desktop interface and development of networking and cryptographic functionality.

- [Qt](https://www.qt.io/)
- [KDE Kirigami](https://develop.kde.org/frameworks/kirigami/)
- [libsodium](https://doc.libsodium.org/)
- [CachyDrop source code](https://github.com/ryeonnaderi/CachyDrop)
