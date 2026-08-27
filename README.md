# The Goonies Ports 🏴‍☠️

The Goonies Ports is a custom Homebrew application for the Nintendo Switch designed to browse, download, and install game ports directly to your console. Built with a sleek, native-feeling user interface powered by the Borealis engine.

## ✨ Features

- 🎮 **Direct Downloads & Installs:** Download and automatically extract game ports directly to your SD card (`/switch/`).
- 🗂️ **Dynamic Categories:** Games are automatically grouped by categories (with real-time counters) pulled directly from the cloud catalog.
- 🎵 **Background Music:** Enjoy immersive background music while browsing the store.
- 🌍 **Bilingual Support:** Fully translated native interface in both Spanish (Castellano) and English.
- ⚡ **Background Processing:** Safe background extraction that prevents the console from sleeping during large downloads.
- 🎨 **Native Switch UI:** Clean, dark-mode interface perfectly integrated with the Switch ecosystem, using native button hints and navigation.

## 🚀 Installation

1. Download the latest `TheGooniesPorts.nro` from the [Releases](#) page.
2. Copy the `.nro` file to the `/switch/TheGooniesPorts/` folder on your Nintendo Switch SD card.
3. Launch it via the Homebrew Menu. *(Note: Full memory access/Title Takeover is highly recommended for large extractions).*

## 🛠️ Build Instructions

To build this project from source, you will need to set up the standard Switch homebrew development environment.

### Requirements:
- [devkitPro](https://devkitpro.org/) with `devkitA64` and `libnx`
- [Borealis](https://github.com/natinusala/borealis) (Included in the `vendor/borealis` submodule/folder)
- Standard Switch portlibs (`sdl2`, `curl`, etc.)

### Compilation:
```bash
# Clone the repository
git clone https://github.com/GoodmanBCN10/The-Goonies-Ports.git
cd The-Goonies-Ports

# Build the project
make -j8
```
The compiled `TheGooniesPorts.nro` will be placed in the `build/` directory.

## 📦 Catalog System

The application fetches its game list from a remote `catalog.json` file hosted on GitHub. It uses a robust downloading manager that handles chunked zip files and direct `.nro` injections.

## 📄 License

This project is created for the homebrew community. Please respect the licenses of the individual ports and the Borealis engine used in this project.
