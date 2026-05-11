# Nearby Sharing for Linux

This directory contains:
- A Linux-facing Nearby Sharing library (`NearbySharingApi`)
- A sample CLI app (`nearby_sharing_app`)
- A Qt/QML tray sample app (`qml_tray_app`)

## Scope and Intended Use

This project is primarily intended to be used as a reusable library/API.

The sample applications are still supported and will continue to be supported because they are used in real day-to-day device sharing workflows.

## Current Status

The current implementation works with the reverse-engineered certificate manager currently used in this project.

Compatibility can still break if Google changes certificate manager behavior/protocol details. That component is closed source, so upstream changes can be difficult to inspect and adapt to quickly.

## Test Coverage and Session Notes

- Verified: single-file sharing flow.
- Not fully verified: multiple transfers in one app lifetime.
- Current practical testing pattern: restart the application before each new transfer.

For this README, a "session" means one process lifetime (app start to app exit).

After one transfer, some endpoints may close and internal state can reset/change. Multi-transfer handling in one live session is still under investigation.

## Known Issues

- Linux hotspot startup can be slow.
- Connecting to a hotspot started on another device can be slow on Linux.
- Android-initiated connection formation can be very slow.

The Android/Linux connection latency issue still needs deeper investigation. One possible cause is connection/negotiation behavior that Linux does not currently handle well.

## Wi-Fi Direct Status

Wi-Fi Direct is theoretically possible but not implemented yet.

Reason: NetworkManager does not natively support creating Wi-Fi Direct Group Owners in the way this project needs.

## Installation

### Pop!_OS / Ubuntu notes

- Pop!_OS 24.04 and Ubuntu 24.04 source builds are supported.
- The release bundle includes `libsdbus-c++.so.2`, so bundle installs do not require a manual `/usr/local` `sdbus-c++` install.
- Older BlueZ releases can lack some optional extended-advertising properties. In that case the app falls back to non-extended BLE behavior instead of failing at startup.
- `./sharing/linux/install_nearby_sharing_service.sh` now installs a matching `libsdbus-c++.so.2` next to `libnearby_sharing_api_shared.so` and patches the library runpath to prefer that colocated runtime copy.
- Source builds still need a compatible `sdbus-c++` development install discoverable via `pkg-config` or installed under `/usr` or `/usr/local`.

### 1. Install the shared library

From the repository root:

```bash
./sharing/linux/install_nearby_sharing_service.sh
```

This installs:
- `libnearby_sharing_api_shared.so`
- `libsdbus-c++.so.2`
- `sharing/linux/nearby_sharing_api.h`

### 2. Build and run the CLI sample app (optional)

From the repository root:

```bash
bazel build //sharing/linux:nearby_sharing_app
./bazel-bin/sharing/linux/nearby_sharing_app
# Optional custom device name
./bazel-bin/sharing/linux/nearby_sharing_app "MyDeviceName"
```

### 3. Build and run the tray sample app (optional)

From `sharing/linux/qml_tray_app`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNEARBY_PREFIX=/usr/local
cmake --build build -j
./build/nearby_qml_file_tray_app
```

If you are building on Pop!_OS / Ubuntu and do not already have the dev packages installed, use:

```bash
sudo apt install -y \
  bluez network-manager avahi-daemon avahi-utils \
  qt6-base-dev qt6-declarative-dev qt6-wayland \
  qt6-tools-dev qt6-tools-dev-tools libqrencode-dev
```

### 4. Install launcher entry (`.desktop`) for the tray app

From `sharing/linux/qml_tray_app`:

```bash
mkdir -p "$HOME/.local/share/applications"
install -m 0644 quick-share.desktop "$HOME/.local/share/applications/quick-share.desktop"
sed -i "s|^Exec=.*|Exec=$(pwd)/build/nearby_qml_file_tray_app|" "$HOME/.local/share/applications/quick-share.desktop"
sed -i "s|^Icon=.*|Icon=$(pwd)/nearby-linux-desktop.png|" "$HOME/.local/share/applications/quick-share.desktop"
update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
```

After this, search for `Quick Share` in your desktop launcher.

For the packaged release bundle, prefer the included `install_nearby_file_share.sh` instead of manually editing the desktop file. The installer validates runtime dependencies before copying files into place.

## Documentation

Technical deep dives are being moved from README content to the wiki.

- Wiki: https://github.com/kidfromjupiter/nearby/wiki

This README stays focused on status, installation, and known limitations.

## Demo Assets (Planned)

- Video demo of end-to-end sharing flow.
- GIF showing the sharing process.
