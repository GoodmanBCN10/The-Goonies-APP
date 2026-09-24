<div align=center>

<img src="extras/banner.png" alt="Banner" width="30%">

</div>
<h1 align=center>After Burner Climax — Nintendo Switch port</h1>

A wrapper/native port of the Android version of **After Burner Climax** v0.1.7 (SEGA /
FishingCactus). It loads the original game binary (`libacb.so`) together with the
FMOD runtime it ships (`libfmod.so` + `libfmodstudio.so`), provides a minimal
Android (NativeActivity) environment around them, and runs the game natively on
the Switch.

### Install

You need, in `/switch/abc/` on your SD card:

1. **`abc_nx.nro`** — this port (build it, see below).
2. **`libacb.so`**, **`libfmod.so`** and **`libfmodstudio.so`** — extract from the
   APK at `lib/arm64-v8a/`.
3. The data file **`main.<version>.com.sega.afterburnerclimax.obb`**, placed
   in an **`obb/`** subfolder.

```
/switch/abc/
├── abc_nx.nro
├── libacb.so
├── libfmod.so
├── libfmodstudio.so
└── obb/
    └── main.19110101.com.sega.afterburnerclimax.obb
```

A `config.txt` is created on first run. `config.txt`, `userdata.txt` (your
settings/progress) and saves live in `/switch/abc/`.

> This will not work in applet/album mode. Launch via a **title override**
> (hold R on an installed game) or a forwarder.

### How to build

You're going to need devkitA64 and the following devkitPro packages:

* `switch-mesa`
* `switch-libdrm_nouveau`

Set `DEVKITPRO` in your environment and run `make`; it produces `abc_nx.nro`.

### config.txt

* `screen_width` / `screen_height` — render resolution; `-1` auto-picks
  1280×720 handheld / 1920×1080 docked.
* `show_fps` — `1` draws an on-screen FPS counter in the top-left corner.

### Credits

* fgsfds for the max_nx wrapper this is based on.
* TheOfficialFloW / Andy Nguyen for the original so-loader lineage.

### Support

If you enjoy my work and want to support me :

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/D1D1P2MOG)

### Legal

No affiliation with SEGA or FishingCactus. "After Burner Climax" is a trademark of
its respective owners. No game assets or original program code are included in
this repository — you must provide your own legally-obtained copy. Licensed under
the MIT License unless noted otherwise.
