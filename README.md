# CachyDrop

**Simple, secure-minded file transfers between your Linux computer and phone.**

CachyDrop is a Linux desktop application designed to make transferring files between your computer and mobile phone simple and convenient. It provides a native desktop interface with QR-code connection details, device pairing, transfer approval, and appearance customization.

Built with C++, Qt Quick, and KDE Kirigami, CachyDrop aims to make local file sharing straightforward while keeping users in control of connected devices and incoming transfers.

> **Development status:** CachyDrop is under active development. The desktop interface, receiving server, HTTP request routing, pairing workflows, and file-transfer functionality are being developed and refined. Public/private-key management and encrypted file transfers remain security development goals until their implementation and verification are complete.

## Features

- **QR-code connections** — Connect a phone using the connection information displayed by the application.
- **Local file sharing** — Select files or drag and drop them into the application to prepare them for transfer.
- **Incoming transfer approval** — Review incoming transfer requests and approve or reject them.
- **Device pairing** — Handle pairing requests and authorize devices.
- **Trusted device management** — View paired devices and remove saved authorizations.
- **Transfer status** — View receiving-server status, transfer requests, and files available for sharing.
- **HTTP request routing** — Handle HTTP endpoints through a dedicated `RequestRouter` component, separate from the receiving server's connection-handling logic.
- **Appearance customization** — Configure available theme, accent color, interface style, and transparency options.
- **Native desktop interface** — Built with Qt Quick and KDE Kirigami.

## Technology stack

- C++
- Qt 6
- Qt Quick and QML
- Qt Quick Controls
- KDE Kirigami
- CMake
- Unix Makefiles for the current configured build
- libsodium for cryptographic functionality where configured and used

## Requirements

CachyDrop is developed primarily for Linux, including Arch Linux and CachyOS.

You will generally need:

- A C++ compiler
- CMake
- Qt 6 development packages
- Qt Quick / Qt Declarative
- Qt Shader Tools
- KDE Kirigami
- Additional libraries required by the current `CMakeLists.txt`

### Install dependencies on Arch Linux or CachyOS

Start with:

```bash
sudo pacman -S --needed \
    base-devel \
    cmake \
    qt6-base \
    qt6-declarative \
    qt6-shadertools \
    kirigami
```

Install any additional dependencies required by the project's build configuration. If your current configuration requires libsodium, install it with:

```bash
sudo pacman -S --needed libsodium
```

The exact dependency list may change as development progresses.

## Build from source

### 1. Clone the repository

```bash
git clone https://github.com/ryeonnaderi/CachyDrop.git
cd CachyDrop
```

If you already have the source code, open a terminal in the project directory instead.

### 2. Configure the project

```bash
cmake -S . -B build
```

The current build configuration uses **Unix Makefiles**. CMake selects the default available generator when creating a new build directory.

### 3. Build CachyDrop

```bash
cmake --build build
```

### 4. Run CachyDrop

```bash
./build/cachydrop
```

If the application fails to build or start, check the terminal output for missing dependencies, CMake configuration errors, missing Qt modules, or QML import errors.

## How to use

### Send files from your computer to your phone

1. Launch CachyDrop.
2. Start the receiving service if it is not already running.
3. Connect your phone using the displayed QR code or connection address.
4. Select files using the file picker or drag and drop them into the transfer area.
5. Follow the connection and transfer workflow presented by the application.

### Receive files from your phone

1. Start the receiving service on your computer.
2. Connect your phone to the displayed address.
3. Complete device pairing if authorization is required.
4. Review the incoming transfer request and the available sender and file information.
5. Approve the transfer only if you recognize and trust the sender. Otherwise, reject it.

### Manage paired devices

Open **Paired Devices** in the sidebar to view authorized devices. Remove a device to revoke its saved authorization.

### Customize the interface

Open **Appearance** to access available theme, accent color, interface style, and transparency preferences. Some appearance options may still be under development.

## Project structure

CachyDrop separates its desktop interface from its C++ networking and transfer logic.

Relevant components include:

- `src/qml/Main.qml` — Main application interface, navigation, transfer controls, device management, and appearance settings.
- `ReceiverServer.h` / `ReceiverServer.cpp` — Receiving-server lifecycle, connection handling, and server-side state.
- `RequestRouter.h` / `RequestRouter.cpp` — HTTP endpoint routing for health checks, sessions, pairing, uploads, downloads, and transfer requests.
- `HttpRequestParser` — HTTP request parsing.
- `HttpUtils` — HTTP-related helper functionality.
- `TransferUtils` — File-transfer helper functionality.
- `CMakeLists.txt` — Build configuration and source dependencies.

The exact file layout may evolve as the project is refactored.

## Security and privacy

CachyDrop is designed around local file sharing, device pairing, and explicit approval of incoming transfers. These features help control access, but they do not automatically guarantee encrypted communication.

### Planned security improvements

- [ ] **Public/private-key management** — Generate and securely store a persistent private key while allowing the corresponding public key to be shared.
- [ ] **Authenticated key exchange** — Establish a shared secret between paired devices and protect against key-substitution attacks.
- [ ] **Encrypted file transfers** — Encrypt file contents before transmission and authenticate them before decryption.
- [ ] **Pairing and session security** — Protect authorization tokens and prevent unauthorized reuse.
- [ ] **Network exposure controls** — Review which network interfaces the receiving server listens on.
- [ ] **Input validation** — Validate uploaded filenames, file sizes, request data, and destination paths.
- [ ] **Transfer authorization** — Ensure transfers cannot bypass pairing, authorization, or approval checks.

These are planned improvements, not claims that every protection is currently implemented.

### Before transferring sensitive files

Until encryption and authentication have been implemented and verified, use CachyDrop only for files appropriate for an unverified local-network transfer.

- Connect only to devices and networks you trust.
- Review incoming transfer requests before approving them.
- Do not assume that pairing or session tokens encrypt file contents.
- Do not treat a QR code or a public key alone as proof of a device's identity.

Security should be evaluated against the actual implementation, not simply the presence of security-related features or libraries.

## Contributing

Contributions, bug reports, and suggestions are welcome.

When reporting an issue, please include:

- Your Linux distribution and version.
- Your Qt version, if known.
- Steps to reproduce the problem.
- Relevant terminal output or error messages.
- Whether the issue affects sending, receiving, pairing, or the interface.

When contributing code, preserve existing pairing and authorization checks, validate external input carefully, and avoid weakening transfer security.

## Roadmap

- [ ] Implement persistent public/private-key management.
- [ ] Implement authenticated key exchange between paired devices.
- [ ] Encrypt and authenticate transferred file contents.
- [ ] Review pairing, session-token, and authorization handling.
- [ ] Audit upload validation and file-path handling.
- [ ] Improve transfer progress, error reporting, and completion feedback.
- [ ] Connect appearance preferences to their intended visual behavior.
- [ ] Add automated tests for pairing, authorization, encryption, and file transfers.
- [ ] Add screenshots and more complete installation instructions.
- [ ] Provide packaged builds for supported Linux distributions.

## License

No license has been specified in this README. Add the project's chosen license and a corresponding `LICENSE` file before distributing the software publicly.

## Acknowledgments

CachyDrop is built with Qt Quick and KDE Kirigami to provide a native-feeling Linux desktop experience, with C++ handling the networking and file-transfer functionality.
