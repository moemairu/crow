# Crow

A lightweight, native Linux mod loader for **Guilty Gear -Strive-**.

Built with pure C and GTK4, Crow provides a simple, distraction-free interface for managing your GGST mods. No Java runtime, no Electron — just a fast, native desktop app that feels right at home on Linux.

## Features

- **One-click mod toggling** — Enable/disable mods with a switch. Crow renames `.pak` ↔ `.pak.disabled` instantly.
- **Smart .sig handling** — Automatically renames companion `.sig` files alongside their `.pak` counterparts.
- **XDG-compliant config** — Saves your GGST install path to `~/.config/crow/config.ini`.
- **Clean GTK4 UI** — Native `GtkHeaderBar`, `GtkListView` with switches, and an empty state when no mods are found.
- **Refresh on demand** — Rescan the `~mods` directory any time.

## Screenshots

*Coming soon*

## Building

### Dependencies

- **GTK4** (≥ 4.10)
- **CMake** (≥ 3.20)
- **pkg-config**
- A C17-capable compiler (GCC or Clang)

On Arch Linux:
```bash
sudo pacman -S gtk4 cmake base-devel
```

On Ubuntu/Debian (22.04+):
```bash
sudo apt install libgtk-4-dev cmake build-essential
```

On Fedora:
```bash
sudo dnf install gtk4-devel cmake gcc
```

### Compile

```bash
git clone https://github.com/moemairu/crow.git
cd crow
cmake -B build
cmake --build build
```

### Run

```bash
./build/crow
```

On first launch, Crow will ask you to select your GGST installation directory (e.g., `~/.steam/steam/steamapps/common/GUILTY GEAR STRIVE`).

## How It Works

GGST (Unreal Engine 4) loads mods from:

```
[GGST_INSTALL_DIR]/RED/Content/Paks/~mods/
```

- Files ending in `.pak` → **Enabled** mods
- Files ending in `.pak.disabled` → **Disabled** mods

Crow simply renames files between these two states. It also handles companion `.sig` signature files automatically.

## Project Structure

```
crow/
├── CMakeLists.txt
├── include/
│   ├── config.h          # XDG config load/save
│   ├── crow_mod.h        # Mod data model, scanner, toggle
│   └── crow_window.h     # Main application window
└── src/
    ├── main.c            # GtkApplication entry point
    ├── config.c          # GKeyFile-based config management
    ├── crow_mod.c        # GObject mod model + file operations
    └── crow_window.c     # UI: HeaderBar, ListView, factory, empty state
```

## License

MIT
