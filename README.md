# vedit

Editor video desktop per Linux (Arch), completamente offline, con interfaccia Material 3.
Specifica: `SPEC-editor-video.md`. Architettura: `docs/ARCHITECTURE.md`. Stato: `docs/PROGRESS.md`.

> Stato attuale: **Fase 3 (Keyframe e composizione) completata**, in attesa del via per la Fase 4.
> Keyframe con andamento (diamanti accanto ai parametri e sulla clip), 90 animazioni predefinite (entrata, uscita,
> ciclo), maschere con maniglie sul player, rimozione di un colore (green screen) col contagocce, modalità di fusione,
> clip composte, livelli di regolazione, marker. Dalla Fase 2: trasformazioni sul canvas, testi e stili, filtri,
> regolazioni, transizioni con anteprima dal vivo, velocità, inversione, fermo immagine, mixer, ricerca universale
> **Ctrl+K**. Dettagli e limiti in `docs/PROGRESS.md`.

## Dipendenze (Arch)
```bash
sudo pacman -S --needed qt6-base qt6-declarative qt6-multimedia qt6-shadertools qt6-svg qt6-5compat qt6-tools \
    mlt ffmpeg sdl2 cmake ninja git frei0r-plugins ladspa rubberband vulkan-headers
```

## Uso
```bash
./build/vedit                        # schermata iniziale: "Nuovo progetto" o una bozza
./build/vedit video.mp4 foto.jpg     # nuovo progetto con quei file già nella timeline
./build/vedit --safe-mode            # nessuna accelerazione GPU
QT_QUICK_BACKEND=software LIBGL_ALWAYS_SOFTWARE=1 ./build/vedit
./build/vedit --component-gallery    # galleria dei componenti Material 3
```
Nell'editor: importa (o trascina) video, foto e musica; "+" su un media lo mette al playhead; trascina le clip per
spostarle, i bordi per accorciarle; `S` divide, `Canc` elimina, `Ctrl+Z` annulla; **Esporta** in alto a destra.
A sinistra le librerie (Testo, Transizioni, Filtri, Animazioni: passa sopra per vedere, clic per applicare), a destra
le proprietà della clip selezionata; `Ctrl+K` cerca qualsiasi comando, filtro, transizione, testo, animazione o brano.
Non c'è un pulsante "Salva": ogni modifica è sul disco entro ~2 secondi. Scorciatoie: `docs/SHORTCUTS.md`.

Lingua: quella del sistema (italiano o inglese). Documentazione: `docs/ARCHITECTURE.md`, `docs/FILE_FORMAT.md`,
`docs/DESIGN_SYSTEM.md`, `docs/GPU_COMPATIBILITY.md`, `docs/SHORTCUTS.md`, `docs/USABILITY.md`.

Dove stanno i dati: bozze in `~/.local/share/vedit/drafts/`, cache di miniature e forme d'onda in
`~/.cache/vedit/media/`, video esportati di default in `~/Video` (nelle build di sviluppo tutto resta dentro
`build/dev-home/`).

## Compilazione e test
```bash
cmake --preset dev            # build RelWithDebInfo in ./build
cmake --build build
ctest --test-dir build --output-on-failure
cmake --preset debug && cmake --build build-debug   # Debug con ASan/UBSan
tools/run-test.sh build tst_ui                      # un solo test con l'ambiente di CTest
VEDIT_UI_SHOTS=build/shots tools/run-test.sh build tst_ui   # anche gli screenshot di ogni passo
```
Nelle build di sviluppo (`VEDIT_DEV_SANDBOX=ON`, default) app e test usano cartelle XDG dentro la build
(`build/dev-home/`, `build/test-home/`) e non scrivono nella home dell'utente.

## Licenze
- vedit: GPL-3.0-or-later (`LICENSE`).
- `third_party/material-color-utilities`: Apache-2.0 (patch in `PATCHES.md`).
- Font Inter (`src/assets/fonts`): SIL Open Font License 1.1.
- Material Symbols (`src/assets/icons`): Apache-2.0.
- Librerie di sistema usate a runtime: MLT (LGPL-2.1+), FFmpeg (LGPL/GPL secondo la build; l'export usa libx264, GPL),
  Qt 6 (LGPL-3),
  frei0r (GPL-2+), rubberband (GPL-2+), SDL2 (zlib).

Nessun asset o codice di CapCut è incluso.
