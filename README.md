# vedit

Editor video desktop per Linux (Arch), completamente offline, con interfaccia Material 3.
Specifica: `SPEC-editor-video.md`. Architettura: `docs/ARCHITECTURE.md`. Stato: `docs/PROGRESS.md`.

> Stato attuale: **Fase 0 (fondamenta) completata** — riproduzione di un video e galleria dei componenti; l'editor
> arriva con la Fase 1. Dettagli in `docs/PROGRESS.md`.

## Dipendenze (Arch)
```bash
sudo pacman -S --needed qt6-base qt6-declarative qt6-multimedia qt6-shadertools qt6-svg qt6-5compat qt6-tools \
    mlt ffmpeg sdl2 cmake ninja git frei0r-plugins ladspa rubberband vulkan-headers
```

## Uso (Fase 0)
```bash
./build/vedit video.mp4              # apre e riproduce un video (Spazio, ←/→, Home/Fine, Ctrl+O)
./build/vedit --component-gallery    # galleria dei componenti Material 3
./build/vedit --safe-mode            # nessuna accelerazione GPU
QT_QUICK_BACKEND=software LIBGL_ALWAYS_SOFTWARE=1 ./build/vedit video.mp4
```
Lingua: quella del sistema (italiano o inglese). Documentazione: `docs/ARCHITECTURE.md`, `docs/FILE_FORMAT.md`,
`docs/DESIGN_SYSTEM.md`, `docs/GPU_COMPATIBILITY.md`, `docs/SHORTCUTS.md`, `docs/USABILITY.md`.

## Compilazione e test
```bash
cmake --preset dev            # build RelWithDebInfo in ./build
cmake --build build
ctest --test-dir build --output-on-failure
cmake --preset debug && cmake --build build-debug   # Debug con ASan/UBSan
```
Nelle build di sviluppo (`VEDIT_DEV_SANDBOX=ON`, default) app e test usano cartelle XDG dentro la build
(`build/dev-home/`, `build/test-home/`) e non scrivono nella home dell'utente.

## Licenze
- vedit: GPL-3.0-or-later (`LICENSE`).
- `third_party/material-color-utilities`: Apache-2.0 (patch in `PATCHES.md`).
- Font Inter (`src/assets/fonts`): SIL Open Font License 1.1.
- Material Symbols (`src/assets/icons`): Apache-2.0.
- Librerie di sistema usate a runtime: MLT (LGPL-2.1+), FFmpeg (LGPL/GPL secondo la build), Qt 6 (LGPL-3),
  frei0r (GPL-2+), rubberband (GPL-2+), SDL2 (zlib).

Nessun asset o codice di CapCut è incluso.
