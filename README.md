# Inputgate

Inputgate is a fork of [Input Leap](https://github.com/input-leap/input-leap)
that shares one keyboard and mouse across computers over the network, with a
focus on making **clipboard sharing and file transfer work on Linux/Wayland**.

> **Status:** early development. The features below are implemented and pass
> automated tests, but have not been tested on real hardware yet.

## Features

- **Wayland clipboard sharing.** Text, HTML and images are synced between
  Windows and Wayland desktops, including the primary (middle click)
  selection. It uses the `ext-data-control-v1` protocol, with
  `wlr-data-control` as a fallback, so it works without focus or portal
  prompts on **KDE Plasma 6** and wlroots based compositors (Sway, Hyprland,
  ...). GNOME doesn't support these protocols, so clipboard sharing is not
  available there yet.
- **File transfer.** Copy files or folders in Explorer or Dolphin, move the
  mouse to the other computer and paste. The files travel with the clipboard
  over the existing (TLS encrypted) connection and are saved in
  `Downloads/Inputgate/<date>_<time>/` on the receiving side, then put on
  the clipboard so pasting copies them where you want.
- **PNG images on Windows.** Images copied on Linux (usually PNG only) can be
  pasted into Windows apps that read the `PNG` clipboard format (browsers,
  Office, most image editors).
- Drag and drop of files between screens is planned for later.

Everything else works the same as Input Leap: install Inputgate on every
computer, run one as the server (the computer whose keyboard and mouse you
use) and the others as clients.

## Installing

### Arch Linux / CachyOS

```sh
git clone --recursive https://github.com/Waltarb/inputgate.git
cd inputgate/dist/arch
makepkg -si
```

This builds the package from the checkout (running the unit tests) and
replaces an installed `input-leap` package. To update later, `git pull`
and run `makepkg -si` again.

### Windows

Download the installer from the latest successful
[Inputgate builds](https://github.com/Waltarb/inputgate/actions/workflows/inputgate.yml)
run (artifact `inputgate-windows-installer`). It upgrades an existing Input Leap
installation.

## Settings

These environment variables are read by the Inputgate client and server:

| Variable | Default | Meaning |
|---|---|---|
| `INPUTGATE_MAX_FILE_MB` | `90` | Largest total size of copied files that will be sent. |
| `INPUTGATE_RECEIVE_DIR` | `Downloads/Inputgate` | Folder that received files are saved in. |
| `INPUTGATE_NO_WAYLAND_CLIPBOARD` | unset | Set to disable Wayland clipboard sharing. |
| `INPUTGATE_NO_LOG_FILE` | unset | Set to stop writing the log file (`%ProgramData%\Inputgate\` on Windows, `~/.local/state/inputgate/` on Linux). |
| `INPUTGATE_DATA_CONTROL` | unset | Set to `wlr` to use `wlr-data-control` even if `ext-data-control-v1` is available. |

The server also has a clipboard size limit (100 MB by default) in
*Configure Server → Advanced*. Copied files larger than either limit are
not sent; text and images still are.

## Building

### Linux (Arch / CachyOS)

```sh
sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-tools libei libportal \
    libxkbcommon wayland openssl avahi libx11 libxext libxtst libxinerama libxrandr libxi
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DQT_DEFAULT_MAJOR_VERSION=6 \
    -DINPUTLEAP_BUILD_LIBEI=ON
cmake --build build
build/bin/unittests
```

### Windows

See the upstream build instructions in [README.inputleap.md](README.inputleap.md),
or the `windows` job in [.github/workflows/inputgate.yml](.github/workflows/inputgate.yml).

## Relationship to Input Leap

Inputgate is based on Input Leap, which descends from Barrier and Synergy 1.x.
All credit for the core application goes to those projects. The original
Input Leap README is kept as [README.inputleap.md](README.inputleap.md).

The Wayland protocol files in `src/lib/platform/wayland-protocols/` come from
[wayland-protocols](https://gitlab.freedesktop.org/wayland/wayland-protocols)
and [wlr-protocols](https://gitlab.freedesktop.org/wlroots/wlr-protocols) and
keep their own (MIT style) licenses.

## License

Inputgate is licensed under the **GNU General Public License v2.0**, the same
license as Input Leap. See [LICENSE](LICENSE).
