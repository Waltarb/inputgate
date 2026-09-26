# Inputgate

Inputgate is a fork of [Input Leap](https://github.com/input-leap/input-leap)
that shares one keyboard and mouse across computers over the network, with a
focus on making **clipboard sharing and file transfer work on Linux/Wayland**.

> **Status:** early development. Right now Inputgate behaves the same as
> upstream Input Leap; the features below are being worked on.

## Goals

- [ ] **Wayland clipboard sharing.** Text, HTML and images synced between
      Windows and Wayland desktops, using the `ext-data-control-v1` protocol
      (with `wlr-data-control` as a fallback). Targets KDE Plasma 6 and
      wlroots-based compositors first.
- [ ] **File transfer.** Copy files on one machine and paste them on the other,
      sent over the existing encrypted connection.
- [ ] Drag and drop of files between screens (later).

Primary test setup: a Windows 11 server and a KDE Plasma 6 (Wayland) client.

## Building

### Linux (Arch / CachyOS)

```sh
sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-tools libei libportal \
    libxkbcommon wayland wayland-protocols plasma-wayland-protocols openssl \
    libx11 libxtst libxinerama libxrandr libxi gtest
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DQT_DEFAULT_MAJOR_VERSION=6
cmake --build build
```

### Windows

See the upstream build instructions in [README.inputleap.md](README.inputleap.md).

## Relationship to Input Leap

Inputgate is based on Input Leap, which descends from Barrier and Synergy 1.x.
All credit for the core application goes to those projects. The original
Input Leap README is kept as [README.inputleap.md](README.inputleap.md).

## License

Inputgate is licensed under the **GNU General Public License v2.0**, the same
license as Input Leap. See [LICENSE](LICENSE).
