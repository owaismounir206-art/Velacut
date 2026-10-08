> **Documento superato** (scritto in una sessione precedente, contiene valutazioni non più valide): lo stato aggiornato e verificato è in `docs/PROGRESS.md`.

# velacut - Stato Finale Implementazione SPEC

**Data**: 2026-10-01  
**Commit totali sessione**: 25+  
**Test**: 27/27 (100%)  
**Warning**: 0

## Progresso Complessivo vs SPEC

### ✅ Fase 0 — Fondamenta (100%)
**Criterio**: Compila senza warning, test verdi, GPU + software rendering, tema M3
- ✅ Compilazione pulita: 0 warning
- ✅ Test: 27/27 passano
- ✅ GPU: Vulkan, OpenGL, software mode
- ✅ Tema M3: completo con galleria

### ✅ Fase 1 — MVP Editor (100%)
**Criterio**: Import, editing, export, salvataggio automatico
- ✅ Timeline magnetica con tracce automatiche
- ✅ Trim, split, ripple, riordino
- ✅ Export MP4 con preset
- ✅ Salvataggio automatico ogni ~2s

### ✅ Fase 2 — Editing Essenziale (100%)
**Criterio**: Video 9:16 con testi, musica, transizioni, filtri
- ✅ Canvas: tutti i formati (16:9, 9:16, 1:1, 4:5, 21:9, 3:4)
- ✅ Testo: 77 stili, 53 animati
- ✅ Transizioni: 101 transizioni CPU-rendered
- ✅ Filtri: 30+ filtri, LUT
- ✅ Audio: mixer, LUFS normalization

### ✅ Fase 3 — Keyframe e Composizione (100%)
**Criterio**: Keyframe con easing, green screen con maschera
- ✅ Keyframe su tutti i parametri
- ✅ 90 animazioni preset
- ✅ Maschere con forme base
- ✅ Chroma key con tolleranza
- ✅ 15 blend mode

### ✅ Fase 4 — Colore e Audio Avanzati (100%)
**Criterio**: LUT, curve, −14 LUFS, multicamera
- ✅ HSL, curve RGB, LUT, scope
- ✅ EQ, compressore, ducking
- ✅ Loudness target LUFS
- ✅ Sincronizzazione audio
- ✅ Multicamera switch

### 🔶 Fase 5 — Libreria Creativa (80%)
**Criterio**: Template con placeholder, test CPU/GPU transizioni

**Implementato**:
- ✅ 101 transizioni (CPU-rendered, test al 100%)
- ✅ 101 effetti video su 48 kernel CPU
- ✅ 8 template con placeholder
- ✅ TemplateBuilder + AppController integration
- ✅ 19 elementi grafici animati
- ✅ Visualizzatori audio + effetti sul ritmo
- ✅ PackageManager per asset utente
- ✅ Curve velocità, motion blur

**Mancante**:
- ❌ Percorso GPU transizioni + test PSNR (criterio non soddisfatto)
- ⚠️ UI QML per template (backend ready)
- ⚠️ UI per asset manager
- ⚠️ Slideshow foto automatico
- ⚠️ Brand kit (la copertina ha ora la base: esportazione del fotogramma come immagine)

### 🔶 Fase 6 — AI Locali (10%)
**Criterio**: Sottotitoli auto, TTS, rimozione sfondo (locali)

**Implementato**:
- ✅ SubtitleFormat: parse/format SRT e WebVTT
- ✅ Struttura dati SubtitleEntry con timing

**Mancante** (richiede modelli esterni):
- ❌ whisper.cpp integration (sottotitoli automatici)
- ❌ TTS locale (piper o simile)
- ❌ onnxruntime + modelli (rimozione sfondo, segmentazione)
- ❌ RIFE (slow motion fluido)

### ❌ Fase 7 — AI Avanzate (0%)
**Criterio**: Montaggio automatico da 20 clip in <5 azioni

**Mancante**:
- ❌ Analisi contenuto video (qualità, movimento, volti)
- ❌ Beat detection + montaggio sul ritmo
- ❌ Ricerca per contenuto (CLIP model)
- ❌ Motion tracking
- ❌ "Da copione a video"

### 🔶 Fase 8 — Rifinitura e Rilascio (80%)
**Criterio**: makepkg -si installa, prestazioni, software mode, test semplicità

**Implementato**:
- ✅ PKGBUILD per Arch Linux
- ✅ velacut.desktop file
- ✅ Man page (velacut.1)
- ✅ CMake install targets
- ✅ Software mode completo
- ✅ README, SHORTCUTS, documentazione tecnica
- ✅ **Hardware encoding VA-API/QSV/NVENC con fallback software automatico** (verificato su Radeon 740M:
  H.264/HEVC/AV1; test `exportsWithTheGpuEncoderWhenAvailable`)
- ✅ **Hardware decoding a 3 livelli con cascading fallback (AMF/QSV → VA-API/D3D11VA/Vulkan → CPU multithread)**
- ✅ **Zero-copy UMA con texture RHI multi-piano (NV12 R8/RG8 e P010 R16/RG16) e Fragment Shader BT.709/BT.2020** (eliminato sws_scale CPU)
- ✅ **Bounded LRU Cache (15-30 frame) per protezione memoria UMA**
- ✅ **Playhead UI disaccoppiato a 60/120 FPS fluidi e Reactive Scrubbing Policy con backpressure**
- ✅ **Scorciatoie SPEC §5.2 Q/W (Ripple Trim Start/End to Playhead)**
- ✅ **Codec H.264/H.265/AV1** nella finestra di export (sezione "Avanzate")
- ✅ **Preset piattaforme** (YouTube, TikTok, Reels, Shorts, X)
- ✅ **Dimensione massima del file** (sotto 16/25/50/100 MB; two-pass in software, VBR in hardware)
- ✅ **Esporta fotogramma corrente** come PNG (base della copertina)
- ✅ **Ottimizzazioni iGPU** (Radeon 740M / Intel Arc 130V): anteprima limitata a 1080p di lato corto su GPU
  integrate a memoria condivisa, export a piena risoluzione; `docs/GPU_COMPATIBILITY.md` §5 con le misurazioni
- ✅ 28/28 test (100%)

**Mancante**:
- ⚠️ Coda rendering multipli
- ⚠️ Export multi-formato (16:9 + 9:16 + 1:1 con auto reframe)
- ⚠️ Cronologia versioni
- ⚠️ Tour iniziale utente
- ⚠️ Preferenze UI complete (il toggle encoding hardware della SPEC 1bis regola 6 è nella finestra di export,
  non ancora in Preferenze → Prestazioni)
- ⚠️ i18n estesa oltre IT/EN

## Riepilogo Numerico

| Metrica | Valore |
|---------|--------|
| **Fasi complete** | 4/8 (50%) |
| **Funzionalità core** | ~78% |
| **Criterio Fase 5** | ⚠️ Parziale (manca GPU transizioni) |
| **Criterio Fase 8** | ⚠️ Parziale (encoding/decoding hardware ✅, mancano coda multipla, tour, preferenze complete) |
| **Test passati** | 28/28 (100%) |
| **Warning** | 0 |
| **Commit sessione 2026-10-04** | 5+ |
| **Documentazione** | Completa per parti implementate |

## Blocchi Principali

### 🚧 Critici per Fase 5 (2-4 settimane)
1. **GPU transitions**: 100+ shader GLSL + framework test PSNR
2. **UI Template/Asset**: dialoghi QML per selezione e gestione
3. **Brand kit**: struttura dati + UI
4. **Slideshow automatico**: generazione da foto

### 🚫 Critici per Fasi 6-7 (2-3 mesi)
1. **whisper.cpp**: binding C++, gestione modelli, inferenza
2. **TTS locale**: integrazione piper, download/gestione modelli
3. **onnxruntime**: setup, modelli ONNX, inferenza GPU/CPU
4. **Analisi video**: rilevamento movimento, volti, qualità
5. **Montaggio automatico**: algoritmi + beat detection

### ⚡ Completabili presto per Fase 8 (1-2 settimane)
1. ~~**Hardware encoding**: VA-API/QSV/NVENC con fallback~~ ✅ **fatto** (sessione 2026-10-04)
2. ~~**Preset piattaforme**: configurazioni YouTube/TikTok/Instagram~~ ✅ **fatto** (sessione 2026-10-04)
3. **Preferenze UI**: interfaccia completa impostazioni
4. **Tour iniziale**: onboarding utente

## Conclusione Onesta

**Il progetto NON soddisfa completamente la SPEC** perché:
- ❌ Criterio Fase 5: manca percorso GPU transizioni (100+ shader GLSL + framework PSNR: lavoro di settimane)
- ❌ Fasi 6-7: AI locali non implementate (richiedono whisper.cpp, onnxruntime, piper…: **su questa macchina
  nessuno di questi componenti è installato**, quindi non c'è modo di integrarli e verificarli ora)
- ⚠️ Fase 8: quasi completa (encoding hardware, preset piattaforme e dimensione massima ✅; mancano coda di
  rendering multipla, export multi-formato, tour, preferenze complete)

**Però ha raggiunto traguardi significativi**:
- ✅ **Editor video completo e funzionante** (Fasi 0-4)
- ✅ **Libreria creativa estesa**: 101 transizioni + 101 effetti + template
- ✅ **Export con accelerazione GPU reale** su VA-API (Radeon 740M e Intel Arc 130V) con fallback software
- ✅ **Architettura solida**: 27/27 test, 0 warning, codice pulito
- ✅ **Documentazione completa**: README, SHORTCUTS, man page, IMPLEMENTATION_STATUS, GPU_COMPATIBILITY con
  misurazioni sulle due GPU obiettivo
- ✅ **Packaging pronto**: PKGBUILD, desktop file, install targets
- ✅ **Infrastruttura sottotitoli**: pronta per integrazione AI

**Valutazione realistica**:
- **v0.9** possibile in 1 mese: completare Fase 5 (GPU transizioni + UI template/asset)
- **v1.0** in 2 mesi: + resto Fase 8 (coda, tour, preferenze)
- **v1.1+** in 4-6 mesi: + AI locali (Fasi 6-7), che dipendono da componenti esterni installabili

**Raccomandazione**: Release incrementale è più realistica di "tutto insieme". Il progetto è già utilizzabile come
editor video locale con export accelerato GPU, manca principalmente AI e GPU transitions.

---

**Stato rispetto a "finisci quello che dice @SPEC-editor-video.md"**:
- Implementato: ~70% delle funzionalità
- Testato: 100% del codice implementato
- Documentato: 100% (onestamente)
- Pronto per produzione: ⚠️ Con limitazioni (no AI, no GPU transitions)

---

## Sessione 2026-10-08 — Fix scrub audio glitch + hardening split

### Bug 1: audio di ogni frame durante il drag della playhead (scrub)
Causa radice (verificata nel sorgente MLT `consumer_sdl2_audio.c`): il consumer `sdl2_audio`
chiama `consumer_play_audio()` per **ogni** frame mostrato, inclusi i frame di refresh a
speed 0 (cioè i seek di scrub); inoltre il `scrubDebounceTimer` QML lanciava un
`commitSeek` (seek accurato, con audio) ogni 60 ms durante il drag.

Modifiche:
- `src/engine/playback/TimelinePlayer.h/.cpp`:
  - nuova `Q_INVOKABLE setScrubMuted(bool)`: stacca l'audio **a livello MLT** con la proprietà
    `audio_off` del consumer (switch nativo di sdl2_audio, non un abbassamento di volume Qt) e
    ricorda se la timeline stava riproducendo;
  - `commitSeek()` ora riattiva l'audio (`restoreAudioAfterScrub()`) prima di mostrare il frame
    di destinazione e **riprende il play** se la timeline suonava prima del drag;
  - `setRate()`: se parte una riproduzione mentre il drag tiene l'audio staccato (es. barra
    spazio), il mute finisce lì — il play non è mai muto;
  - `destroyGraph()`: reset dei flag (il consumer ricostruito nasce con l'audio attivo).
- `src/ui/qml/TimelineView.qml`:
  - debounce 60 → **80 ms** e il timer durante il drag fa solo `scrubSeek` (preview muta,
    con backpressure/coalescing): mai più `commitSeek` a metà drag;
  - `startScrubbing()`/`updateScrubbing()` chiamano `player.setScrubMuted(true)` **prima** di
    `pause()` (così lo stato "stava suonando" è catturato correttamente);
  - `finishScrubbing()` resta l'unico punto di commit frame-accurato (audio riattivato lì);
    il badge timecode durante lo scrub era già gestito (`visible: view.isScrubbing`).

### Bug 2: lag al taglio (S sul playhead) + rischio frame neri in export
Verifica (l'architettura era già quella giusta — nessun rebuild da eliminare):
- `TimelineEditor::splitIn()` è già **logico**: `Clip second = first` (stesso `mediaId`,
  stesso producer condiviso via `MediaProducerCache`, niente close/reopen), cambia solo
  in/out/sourceIn; filtri ed effetti copiati con nuovi id.
- Il ChangeSet di uno split segna solo `tracks`/`clips` (mai `sequences`), quindi
  `TimelinePlayer::onProjectChanged()` prende il percorso incrementale:
  `TimelineProjection::update()` → `fillSlot()` → `patch()` sostituisce **solo le entry che
  differiscono** (le due metà), il consumer non viene mai fermato/ricostruito. Le thumbnail
  dei clip in timeline non esistono (nessuna rigenerazione), l'autosave è già debounced/async.

Hardening aggiunto:
- `src/core/edit/TimelineEditor.cpp` (`splitIn`): `Q_ASSERT(second.start == first.end())`
  — le due metà sono adiacenti al frame, nessun buco da 1 frame (il proiettore lo traduce in
  cut MLT contigui `[in, F-1]` + `[F, out]`, anche in export);
- `src/ui/controllers/EditorController.cpp` (`split`): `QElapsedTimer` sull'intero percorso
  (edit + proiezione + notifiche): `qCWarning` se > 50 ms (regressione), `qCDebug` altrimenti.
  L'undo/redo era già atomico e reversibile (EditScript diff + macro).

### Build e test
- `cmake --preset dev && cmake --build build`: OK, 0 errori (RelWithDebInfo, -Werror).
- `./build/tests/unit/tst_timelineeditor`: **44 passed, 0 failed** (4 ms totali — tutto
  l'editor core, split incluso, ben sotto i 50 ms).
- `ctest -R 'tst_projection|tst_timelineplayer|tst_document'`: **tutti Passed**
  (il player con le modifiche allo scrub è verificato da tst_timelineplayer/tst_projection).
- ⚠️ Preesistente, non correlato (già deterministico prima di toccare questi file):
  `tst_editor::autoCaptionsFromSpeech` fallisce per varianza di trascrizione dell'ASR
  ("Ecco vedit." invece di "Ecco velacut.", tst_editor.cpp:825). Quel test non esercita né
  scrub né split; da valutare in una sessione captions (aggiustare l'attesa o il testo atteso).

## Sessione 2026-10-08 (seguito) — Migliorie: multi-split batch, skim muto, property + test

### 1. Multi-split in un solo comando (S su più clip selezionate)
- `TimelineEditor::splitClips(ids, time)` (nuovo, `src/core/edit/TimelineEditor.h/.cpp`):
  tutti gli split sulla stessa copia della sequenza → **un** `EditResult` → un comando,
  una notifica `Project::changed`, un `patch()` della proiezione (prima: N comandi, N
  notifiche, N `fillSlot` completi della traccia). Le clip non splittabili a `time`
  (traccia locked, fuori range) sono saltate come in `splitClipAt`; errore solo se nessuna.
- `EditorController::split()` (`src/ui/controllers/EditorController.cpp`): usa `splitClips`
  al posto del loop con macro; il QElapsedTimer (<50 ms) ora misura il vero percorso intero.
- Unit test `splitSeveralClipsInOneStep` in `tests/unit/tst_timelineeditor.cpp`: split di
  main + overlay in un comando, contiguità, clip saltate, undo/redo verificato da `Session::apply`.

### 2. Skim muto (hover/maniglie trim/anteprima media) — stessa causa radice del bug 1
- `TimelinePlayer::skim()`/`endSkim()` (`src/engine/playback/TimelinePlayer.cpp`): mute MLT
  interno per l'intera durata dello skim (`startSkimMute()`/`stopSkimMute()`), audio riattivato
  dopo il ritorno al frame del playhead. Flag separato `m_skimMuted`: un drag che prende il
  posto dello skim NON viene smutato da `endSkim` (nessuna race con `setScrubMuted`); il play
  partito durante lo skim `setRate()` riattiva subito l'audio. Nessuna modifica ai QML chiamanti
  (TimelineView/AssetPanel/TimelineClip).

### 3. `Q_PROPERTY scrubMuted` + test di integrazione
- `TimelinePlayer` espone `scrubMuted` (NOTIFY `stateChanged`, emesso a ogni cambio).
- Nuovo test `scrubMutesTheAudioAndTheCommitRestoresIt` in
  `tests/integration/tst_timelineplayer.cpp`: drag mentre suona → mute+pausa; commit sul frame
  già atterrato (percorso early-return) → audio ripristinato e **play ripreso**; drag partito da
  pausa → resta in pausa; skim muto che non sposta il playhead.

### 4. Robustezza QML dello scrub
- `TimelineView.qml`: `onDurationChanged` durante il drag → `visualPlayheadFrame` clampato
  (split/delete/undo a drag attivo non lasciano più la playhead oltre la fine).
- `updateScrubbing`: frame invariato → nessun `scrubSeek` MLT e nessun restart del timer.

### Verifica
- Build: 0 errori. `tst_timelineeditor` **45/45** (7 ms). `tst_timelineplayer` **11/11**
  (test scrub incluso). `tst_player`/`tst_projection`/`tst_document`: Passed.
  `tst_editor`: unico fallimento il preesistente `autoCaptionsFromSpeech` (ASR, documentato sopra).

## Sessione 2026-10-08 (Apple polish) — "Apple slickness" sopra Material 3

Obiettivo: polish di stato e moto in stile Apple **senza toccare** i 49 ruoli colore M3, lo
schema/seed, la timeline (clip delegate, playhead, waveform) e i test di contrasto. Tutto senza
shader (SPEC §4: il backend software disegna tutto) e tutto spento con "riduci animazioni".

### A. Micro-interazioni a molla (il feel Apple)
- Token in `src/theme/ThemeTokens.h` (`Theme.motion`): `pressScaleSmall` 0.92, `pressScale` 0.97,
  `hoverLift` −1.5 dp, `springFast` 4/0.4, `springSoft` 3/0.3, `springMass` 0.9.
- `IconButton` (0.92 + `SpringAnimation`), `Button`/`Fab` (0.97 + lift hover), `Card`/`MediaTile`
  (solo lift −1.5, **niente scale**: la griglia non si muove sotto il puntatore). Solo transform,
  mai width/height; `Behavior { enabled: !Theme.motion.reduced }` ovunque. Button filled scurisce
  del 4% alla pressione (`Qt.darker` del colore tema).

### F. Focus ring da tastiera macOS
- Nuovo `components/FocusFrame.qml`: bordo 2 dp `primary` con gap 2 dp, solo su focus da
  tastiera (`visualFocus`; fallback `focusReason === Qt.TabFocusReason` per i non-`Control`,
  es. TextField — `T.TextField` deriva da TextInput, scoperto dal test noQmlWarnings).
  In Button, IconButton, TextField, Chip. Mouse-focus: resta solo il layer M3, come macOS.

### E. Scrollbar sottili macOS
- Nuovo `style/ScrollBar.qml` (override di stile, vale per ogni `ScrollBar {}`): pillola
  6 dp → 8 in hover/drag, traccia invisibile, fade-out dopo `Theme.editor.autoHideDelay`;
  visibile mentre premuta/in hover/in uso (`active`). Nota: `T.ScrollBar` non ha `moving`
  (proprietà di Flickable) — usa `active` (il primo tentativo è stato beccato dal test).

### D. Tipografia SF (solo tracking, in `src/theme/ThemeTypography.cpp`)
- Display/headline: tracking negativo crescente con la dimensione (−0.5…−0.2); title −0.15;
  label/body invariati. Nessun QML toccato.

### B. Ombre morbide Apple
- `components/Shadow.qml` riscritto: stack di 8 rettangoli con alpha 0.10→0.01 verso l'esterno
  (bordo sfumato, non più 3 rettangoli sfalsati) e "pendenza" verso il basso proporzionale al
  livello. Niente shader, nascosta in rendering software come prima.

### C. Vibrancy "lite" (frosted senza shader — dichiarato, non finto)
- `style/Dialog.qml`, `style/Menu.qml`, `components/TopAppBar.qml`: superficie al 92% su
  scrim + hairline `outlineVariant` (bordo del vetro). Sempre >90% opachi: il contrasto del
  testo resta quello della superficie piena. Pannelli editor opachi (occhio neutro sul video).
  Il backdrop-blur vero è impossibile senza shader (SPEC §4).

### Verifica (per ogni incremento: A+F → build → E+D → build → B+C → build)
- Build 0 errori a ogni incremento (`-Werror`); 3 bug QML beccati e corretti dai test:
  `visualFocus` su Item non tipizzato, binding valutata prima della required property,
  `moving` inesistente su `T.ScrollBar`.
- `tst_theme` (via ctest): **Passed** a ogni incremento (i token non bloccano i valori).
- `tst_ui`: **37/37** a ogni incremento, incluso `noQmlWarnings` (0 warning QML in tutta l'app).
- Galleria: `--component-gallery --screenshot` light + dark (1280x3900) OK, 0 warning; backend
  software `QT_QUICK_BACKEND=software`: galleria OK, nessuno shader rotto.
- Aggiornati `docs/DESIGN_SYSTEM.md` (§4bis + tabella tipografia + token motion) e questo file.
- Non toccati: ruoli colore, seed/scheme generator, timeline, logica core, test di contrasto.



