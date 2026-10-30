# CachyDrop

**Simple, secure-minded file transfers between your computer and phone.**

CachyDrop is a desktop application designed to make transferring files between your Linux computer and mobile phone simple. It provides a local file-sharing interface with QR-code connection details, device pairing, transfer approval, and a customizable appearance.

The goal is to make local file sharing convenient while keeping the user in control of which devices connect and which incoming transfers are accepted.

> **Development status:** CachyDrop is under active development. Core interface navigation, device pairing controls, and file-transfer workflows are present. Transport encryption and other security properties still need verification before the application should be considered suitable for sensitive files.

## Features

- **QR-code connection** — Display connection information and a QR code to help connect your phone.
- **Local file sharing** — Select files or drag and drop them into the application to prepare them for transfer to your phone.
- **Incoming transfer approval** — Review incoming transfer requests and explicitly approve or reject them.
- **Device pairing** — Approve devices that request pairing and manage trusted devices.
- **Trusted device management** — View paired devices and revoke saved authorization by removing a device.
- **Transfer status** — View receiving-service status and information about files ready to share.
- **Appearance preferences** — Configure available theme, accent color, interface style, and transparency preferences.
- **Native desktop interface** — Built with Qt Quick and KDE Kirigami.

## Requirements

CachyDrop is developed for Linux and uses the following technologies:

- C++ and Qt
- Qt Quick (QML)
- Qt Quick Controls
- KDE Kirigami
- CMake
- A C++ compiler compatible with the project's Qt version

The exact Qt and KDE Frameworks versions depend on the project's build configuration.

## Build from source

### 1. Install dependencies

Install the development packages for CMake, a C++ compiler, Qt, Qt Quick, and KDE Kirigami using your Linux distribution's package manager.

On Arch Linux or CachyOS, you can start with:

```bash
sudo pacman -S --needed \
    base-devel \
    cmake \
    ninja \
    qt6-base \
    qt6-declarative \
    qt6-shadertools \
    kirigami
```

Depending on the project's `CMakeLists.txt`, additional Qt modules or KDE Frameworks packages may be required.

### 2. Clone the repository

Replace the placeholder below with the actual repository URL:

```bash
git clone https://github.com/ryeonnaderi/CachyDrop.git
cd CachyDrop
```

If you already have the source code, open a terminal in the project directory instead.

### 3. Configure and build

For a project that uses the existing `build` directory:

```bash
cmake -S . -B build
cmake --build build
```

### 4. Run CachyDrop

```bash
./build/cachydrop
```

If the application fails to start, check the terminal output for missing Qt modules, QML import errors, or other runtime diagnostics.

## How to use

### Send files from your computer to your phone

1. Launch CachyDrop.
2. Start the receiving service.
3. Connect your phone using the displayed QR code or connection address.
4. Select files using the file picker or drag and drop them into the transfer area.
5. Follow the connection and transfer workflow displayed by the application.

### Receive files from your phone

1. Start the receiving service on your computer.
2. Connect your phone to the displayed address.
3. Follow the pairing process if your phone needs authorization.
4. When an incoming transfer request appears, review the sender, filenames, and total size.
5. Approve the transfer only if you recognize and trust the sender. Otherwise, reject it.

### Manage paired devices

Open **Paired Devices** in the sidebar to view trusted devices. Remove a device to revoke its saved authorization.

### Customize the interface

Open **Appearance** to access the available theme, accent color, interface style, and transparency preferences.

Some appearance options may not yet be fully connected to actual visual changes.

## Security and privacy

CachyDrop is intended for local file sharing with explicit device pairing and transfer approval. These features help control access, but they do not by themselves guarantee secure communication.

Before using CachyDrop to transfer sensitive files, verify the following:

- **Transport encryption:** Confirm that file transfers and authentication use properly configured HTTPS/TLS or another secure transport.
- **Server identity:** Ensure the phone can authenticate the computer it connects to, rather than trusting an arbitrary device on the local network.
- **Authentication tokens:** Protect pairing and session tokens against disclosure, unauthorized reuse, and unnecessary exposure.
- **Network exposure:** Understand which network interfaces the receiving service listens on and which other devices can reach it.
- **Input validation:** Validate uploaded filenames, file sizes, paths, and request contents.
- **Transfer authorization:** Ensure that incoming transfers cannot proceed without valid authorization and approval.

**Important:** The presence of pairing, session tokens, or an approval dialog is not proof that file contents are encrypted in transit. Until transport security has been verified, use CachyDrop only with files appropriate for an unverified local-network transfer.

## Project structure

The project includes a Qt Quick/Kirigami user interface and a C++ receiving server. The exact source layout may evolve during development.

Relevant areas include:

- `src/qml/Main.qml` — Main window, sidebar navigation, transfer controls, device management, appearance settings, and approval dialogs.
- C++ server implementation — Local connection handling, pairing, transfer requests, authorization, and file-transfer operations.
- `CMakeLists.txt` — Build configuration.

## Contributing

Contributions, bug reports, and suggestions are welcome.

When reporting a bug, please include:

- Your Linux distribution and version.
- Your Qt version, if known.
- Steps to reproduce the issue.
- Relevant terminal output or error messages.
- Whether the issue affects sending files, receiving files, pairing, or the interface.

For code changes, keep transfer and pairing behavior intact, validate inputs carefully, and avoid weakening existing authorization checks.

## Roadmap

Potential areas for continued development include:

- [ ] Verify and implement secure transport encryption.
- [ ] Improve server-certificate trust and phone connection setup.
- [ ] Audit pairing and session-token handling.
- [ ] Connect appearance preferences to actual application styling.
- [ ] Improve transfer progress, errors, and completion feedback.
- [ ] Add automated tests for pairing, authorization, and file transfers.
- [ ] Add screenshots and more complete installation instructions.
- [ ] Provide packaged builds for supported Linux distributions.

## License

No license has been specified here. Add the project's chosen license and a corresponding `LICENSE` file before distributing the software publicly.

## Acknowledgments

CachyDrop is built with Qt Quick and KDE Kirigami, using their tools to create a native-feeling Linux desktop experience.
