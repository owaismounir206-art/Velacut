# vedit — Stato di avanzamento

Ultimo aggiornamento: 2026-09-24

## Fase corrente
**Fase 0 — Fondamenta**, passo preliminare: documenti di architettura e formato **in attesa di approvazione**.
Nessun codice scritto (per regola: niente codice prima dell'approvazione di `docs/ARCHITECTURE.md` e `docs/FILE_FORMAT.md`).

## Fatto
- `.claude/settings.json`: permessi per i comandi sicuri (allow), conferma per quelli rischiosi (ask), blocchi (deny).
- `CLAUDE.md`: istruzioni di sessione, regole di sicurezza, sintesi della sezione 9.
- Specifica rinominata in `SPEC-editor-video.md` (era `SPEC-editor-video(1).md`).
- Controllo delle dipendenze (solo lettura), vedi sotto.
- Bozze di `docs/ARCHITECTURE.md` e `docs/FILE_FORMAT.md`.

## In corso
- Revisione di ARCHITECTURE e FILE_FORMAT da parte dell'utente.

## Prossimi passi (dopo l'approvazione)
1. L'utente installa le dipendenze mancanti (comando sotto).
2. Programmi di prova MLT per i punti marcati [verifica] in ARCHITECTURE (register_service, Playlist::mix, timeremap,
   plant_filter, scrub_audio, fallback di stile QML).
3. Incrementi della Fase 0 come da ARCHITECTURE §17, partendo da `git init` e dallo scheletro CMake.

## Dipendenze (controllo del 2026-09-24)
- Presenti: qt6-base/declarative/multimedia/shadertools/svg/5compat 6.11.2, ffmpeg 9.0.1, sdl2 (via sdl2-compat),
  cmake 4.4.3, ninja 1.13.2, git 2.55, rubberband 4.0; inoltre qt6-tools, qt6-wayland, vulkan-headers, clang 22,
  gcc 16, abseil-cpp, aubio, rnnoise, vid.stab, opencv.
- **Mancanti, obbligatorie:** `mlt`, `frei0r-plugins`, `ladspa` → `sudo pacman -S --needed mlt frei0r-plugins ladspa`
- Facoltative utili per lo sviluppo: `ccache` (ricompilazioni più veloci), `libva-utils` (`vainfo`, diagnostica VA-API).
- AI (Fase 6, da non installare ora): `whisper-cpp`, `onnxruntime-cpu`, `ncnn` sono nei repo; Piper TTS e
  rife-ncnn-vulkan solo AUR. Attenzione: il pacchetto `piper` dei repo è un configuratore di mouse, non il TTS.

## Ambiente
Intel Core Ultra 5 226V, Arc 140V (xe), Mesa 26.2, Hyprland (Wayland). Portale: `color-scheme` sì, `accent-color` no
(GNOME `accent-color` = 'blue' leggibile via gsettings). Dettagli in ARCHITECTURE §1 e §8.3.

## Decisioni
Registro in `docs/ARCHITECTURE.md` §15 (D-01 … D-16).

## Bug noti
Nessuno (niente codice ancora).
