# vedit — Architettura

> Stato: **approvata** (2026-09-24) e implementata per la Fase 0. Le modifiche emerse durante l'implementazione sono
> nel registro delle decisioni (§15, D-17 in poi) e nella sezione "Esiti delle verifiche" (§18).
> Riferimenti: `SPEC-editor-video.md` (vincolante), `docs/FILE_FORMAT.md` (formato `.vproj`).
> Le decisioni prese in autonomia (regola 9 della sezione 9) sono numerate `D-xx` nella sezione 15.
> I punti marcati **[verifica]** sono comportamenti di MLT/Qt/FFmpeg da confermare con un piccolo programma di prova
> prima di costruirci sopra (regola 5).

---

## 1. Ambiente di riferimento rilevato

| Voce | Valore |
|---|---|
| Sistema | CachyOS (Arch), kernel 7.2, sessione **Wayland** (Hyprland), XWayland presente |
| CPU / RAM | Intel Core Ultra 5 226V, 8 core, 15 GiB |
| GPU | Intel Arc 140V (Lunar Lake), driver `xe`; Mesa 26.2 (OpenGL 4.6, Vulkan 1.4 ANV) |
| Video HW | `intel-media-driver` (VA-API), `vpl-gpu-rt` (QSV); FFmpeg 9.0 con VA-API, QSV, Vulkan, NVENC, AMF |
| Toolchain | GCC 16.2, Clang/LLVM 22 (clang-format, clang-tidy), CMake 4.4, Ninja 1.13 |
| Qt | 6.11.2 (base, declarative, multimedia, shadertools, svg, 5compat, tools, wayland, quick3d) |
| Audio | PipeWire 1.6 (+ pulse, jack), SDL2 fornito da `sdl2-compat` |
| Portale | `color-scheme` disponibile (scuro); **`accent-color` NON disponibile** (vedi §8.3) |

Questa macchina copre bene il percorso "GPU Intel moderna". Gli altri percorsi della sezione 1bis si provano così:
software puro (`LIBGL_ALWAYS_SOFTWARE=1`, `QT_QUICK_BACKEND=software`), Vulkan disattivato, VA-API disattivato,
safe mode. NVIDIA, AMD e le GPU vecchie non sono disponibili qui: la matrice in `docs/GPU_COMPATIBILITY.md` distinguerà
sempre ciò che è stato **provato** da ciò che è solo **progettato**.

---

## 2. Vista d'insieme

```
                        ┌─────────────────────────────── processo vedit ───────────────────────────────┐
                        │                                                                              │
 utente ──▶ QML (ui/qml) ──▶ Controller (ui/controllers) ──▶ Comandi (core/commands) ──▶ Modello (core)  │
                        │        ▲            ▲                  QUndoStack                 │          │
                        │        │            │                                             │ ChangeSet│
                        │   Modelli Qt   Theme (theme/)                                     ▼          │
                        │  (ui/models) ◀────────────────────────────────────────── notifiche di modifica│
                        │        ▲                                                          │          │
                        │        │ frame/anteprime                                          ▼          │
                        │   PreviewItem ◀── FrameSink ◀── Engine (engine/, thread proprio) ◀─┘          │
                        │                                   │  proiezione del modello in un grafo MLT  │
                        │                                   ├─ playback: consumer sdl2_audio (PipeWire) │
                        │                                   ├─ analysis: miniature, waveform (pool)     │
                        │                                   └─ salvataggio automatico (thread I/O)      │
                        └───────────────┬───────────────────────────┬──────────────────┬───────────────┘
                                        │ JSON su stdin/stdout      │                  │
                                        ▼                           ▼                  ▼
                               vedit-gpuprobe              vedit-render           vedit-ai (Fase 6)
                             (capacità GPU/codec,      (export, proxy, frame     (whisper, onnxruntime…
                              crash isolato)            di test; crash isolato)   crash isolato)
```

Regole di dipendenza (verificate dal build, un target CMake per modulo):

```
ui ──▶ engine ──▶ fx ──▶ core
 │        └──────────────▶ core
 ├──▶ theme
 ├──▶ ai (interfaccia) ──▶ core
 └──▶ core
core: solo Qt6::Core + Qt6::Gui (Gui serve solo per QUndoStack/QUndoCommand). Niente MLT, niente QML.
fx:   kernel degli effetti (CPU, poi GPU), nessuna dipendenza da MLT.
```

---

## 3. Struttura del repository e target di build

La radice del repository è **questa cartella** (corrisponde a `vedit/` nella specifica). Aggiunte rispetto all'albero
della sezione 2 della specifica, motivate in §15: `third_party/`, `cmake/`, `tools/`, `src/fx/`.

```
./
├── CMakeLists.txt, CMakePresets.json, .clang-format, .clang-tidy, PKGBUILD, README.md, LICENSE
├── cmake/                     # moduli CMake (warning, sanitizer, dev-sandbox, qt helpers)
├── docs/                      # come da specifica
├── third_party/
│   └── material-color-utilities/   # porting C++ vendored (Apache-2.0), con eventuali patch documentate
├── src/
│   ├── app/                   # main di vedit, bootstrap, preferenze, safe mode, single instance
│   ├── core/
│   │   ├── time/              # Rational, RationalTime, TimeRange
│   │   ├── project/           # Project, Media, Sequence, Track, Clip, Transition, Marker, Group
│   │   ├── effects/           # Effect, Param, Keyframe, Easing, valutazione delle animazioni
│   │   ├── edit/              # TimelineEditor (regole CapCut: traccia magnetica, tracce automatiche), SnapEngine
│   │   ├── commands/          # un QUndoCommand per ogni operazione
│   │   └── serialization/     # JSON <-> modello, migrazioni
│   ├── document/              # bozza aperta (progetto + undo + salvataggio continuo + lock), cartella delle bozze
│   ├── fx/                    # registro effetti/transizioni, manifest, kernel CPU (poi GPU)
│   ├── engine/
│   │   ├── mlt/               # proiezione modello -> grafo MLT, servizi MLT propri (vedit.*)
│   │   ├── playback/          # consumer, seek, scrubbing, FrameSink
│   │   ├── render/            # export (+ main di vedit-render)
│   │   ├── proxy/             # coda proxy
│   │   ├── analysis/          # probe media, miniature, waveform, fingerprint
│   │   └── gpu/               # capacità, catene di fallback (+ main di vedit-gpuprobe)
│   ├── theme/                 # M3: schemi, sorgenti del seme, token, ThemeManager
│   ├── ai/                    # IAiTask + plugin (Fase 6)
│   ├── ui/
│   │   ├── controllers/       # QObject esposti a QML
│   │   ├── models/            # QAbstractItemModel per timeline, media, librerie
│   │   ├── items/             # QQuickItem C++: PreviewItem, ThumbnailStrip, WaveformItem
│   │   └── qml/               # moduli QML: Vedit.UI (schermate: Main, Gallery), style/ = Vedit.Style,
│   │                          #   components/ = Vedit.Components (D-18)
│   └── assets/                # icone SVG originali, font, preset
├── resources/                 # librerie locali (transizioni, effetti, sticker, template)
├── tools/probes/              # programmi di prova (regola 5): MLT, aspetto del desktop (-DVEDIT_BUILD_PROBES=ON)
└── tests/ (unit/, integration/, render/)
```

| Target | Tipo | Dipende da |
|---|---|---|
| `vedit_core` | libreria statica | Qt6::Core, Qt6::Gui |
| `vedit_document` | libreria statica | vedit_core (bozze, salvataggio continuo, lock: FILE_FORMAT §6, §9) |
| `vedit_fx` | libreria statica | vedit_core |
| `vedit_engine` | libreria statica | vedit_core, vedit_fx, MLT++ 7, FFmpeg (libavformat/libavcodec/libavutil per probe e capacità) |
| `vedit_theme` | libreria statica | Qt6::Gui, Qt6::DBus, material-color-utilities |
| `vedit_ui` | libreria statica + moduli QML | tutto sopra, Qt6::Quick, Qt6::QuickControls2 |
| `vedit` | eseguibile | vedit_ui |
| `vedit-render` | eseguibile | vedit_engine (+ Qt6::Gui per il testo, piattaforma `offscreen`) |
| `vedit-gpuprobe` | eseguibile | Qt6::Gui, FFmpeg, loader Vulkan tramite Qt |
| test | eseguibili QtTest | il modulo sotto test |

Build: C++20, `-Wall -Wextra -Wpedantic -Werror` sul nostro codice (non su `third_party/`), ASan+UBSan in `Debug`,
preset CMake `dev` (RelWithDebInfo), `debug` (sanitizer), `release`. `ccache` usato se presente.

---

## 4. Core (modello dati puro)

### 4.1 Tempo
- `Rational { int64 num; int64 den; }` normalizzato (den > 0, gcd = 1). Aritmetica esatta con controllo di overflow
  (`__int128` per i prodotti intermedi).
- `RationalTime { int64 value; Rational rate; }`: `value` fotogrammi (o campioni) alla frequenza `rate`.
  Nessun `double` per i tempi. Conversione tra frequenze esatta quando possibile, altrimenti con arrotondamento
  esplicito scelto dal chiamante (`Round::Nearest` metà-al-pari, `Floor`, `Ceil`).
- `TimeRange { RationalTime start; RationalTime duration; }`.
- **Regola di griglia (D-04):** tutti i tempi di una sequenza (posizioni, durate, `sourceIn`, keyframe, marker, transizioni)
  sono sulla griglia di fotogrammi del progetto (`project.frameRate`). I media conservano la loro frequenza nativa solo
  nei metadati. Conseguenza: la proiezione su MLT (profilo = fps del progetto) è esatta; l'unica conversione con
  arrotondamento è il comando "Cambia frame rate", annullabile.

### 4.2 Entità
Tutte le entità hanno un `Id` stabile (UUID, tipizzato per categoria: `ClipId`, `TrackId`…). I comandi fanno sempre
riferimento agli id, mai a puntatori, così undo/redo restano validi anche dopo cancellazioni e ricreazioni.

```
Project
 ├─ settings: frameRate, sampleRate, canali audio, spazio colore, canvas predefinito, "formato dalla prima clip"
 ├─ mediaPool: Media[] + cartelle
 ├─ sequences: Sequence[]  (la principale + quelle annidate delle compound clip)
 └─ mainSequenceId
Sequence: canvas (w, h, preset di formato), magneticMain, visualTracks[], audioTracks[], markers[], groups[]
Track: kind (video | text | sticker | effect | adjustment | audio), lock, mute, solo, hidden, height, clips[], transitions[]
Clip: id, kind, start, duration, enabled, linkId, effects[], animations, transform, opacity, blendMode, masks[],
      markers[] + payload per tipo (media | text | sticker | color | effect | adjustment | compound | subtitle)
Media: path, fingerprint, info (metadati del probe), preferenze proxy
Effect: type, typeVersion, enabled, params{nome -> Param}
Param: valore statico o lista di Keyframe (t, v, interpolazione linear|hold|bezier, easing)
```

Il dettaglio di ogni campo è in `docs/FILE_FORMAT.md`: il formato ricalca il modello uno a uno.

- `visualTracks[0]` è sempre la **traccia principale**; gli indici crescono verso l'alto (ordine di composizione).
  Le tracce audio sono in una lista separata (nessun ordine di composizione). Testo, sticker, effetti e regolazioni
  sono tracce visive: il loro indice decide cosa coprono (un livello di regolazione agisce su tutto ciò che sta sotto).
- Parametri di effetti e trasformazioni sono **indipendenti dalla risoluzione** (frazioni del canvas), così cambiare formato
  o qualità dell'anteprima non altera il risultato.
- Tempo dei keyframe (D-05): per le clip media è **tempo della sorgente** (resta agganciato al contenuto quando si
  taglia, si cambia velocità o si inverte); per le clip generate (testo, sticker, colore, effetto, regolazione) è relativo
  all'inizio della clip. Le animazioni predefinite (ingresso, uscita, loop) sono agganciate ai bordi della clip.

### 4.3 Modifiche, notifiche, invarianti
- Il modello si modifica **solo** tramite l'API di mutazione primitiva (`ProjectMutator`), accessibile ai comandi.
  Ogni primitiva registra cosa ha toccato in un `ChangeSet` (tracce sporche, intervallo di tempo sporco, media, impostazioni).
- Ogni esecuzione di `redo()`/`undo()` è una transazione: alla fine `Project` emette **un solo** segnale
  `changed(const ChangeSet&)`. Lo ascoltano i modelli UI (aggiornamenti mirati), l'engine (proiezione incrementale)
  e il salvataggio automatico.
- `Sequence::checkInvariants()` (sempre attivo nei test, in Debug dopo ogni comando):
  clip della stessa traccia senza sovrapposizioni, tranne quella esatta di una transizione in modalità `overlap`;
  nessun vuoto sulla traccia principale se magnetica; transizioni solo tra clip adiacenti della stessa traccia;
  id unici; riferimenti validi.

### 4.4 Comandi e undo
- Un `QUndoStack` per progetto. **Ogni** modifica è un `QUndoCommand`, con testo tradotto per la cronologia visibile
  e per la snackbar ("Clip eliminata — Annulla").
- Le operazioni composte (per esempio l'eliminazione con ripple sulla traccia magnetica: rimuovi, compatta, sistema
  le transizioni, elimina le tracce rimaste vuote) sono **macro-comandi** di primitive, costruiti da `TimelineEditor`.
- Modifiche continue (slider, trascinamenti): `SetParamCommand::mergeWith` fonde i comandi che hanno lo stesso
  bersaglio **e** lo stesso `gestureId` (un id per ogni pressione e rilascio del mouse), quindi due gesti separati
  restano due passi di undo.
- La snackbar "Annulla" esegue `undo()` solo se in cima allo stack c'è ancora quel comando (controllo sull'indice).

### 4.5 TimelineEditor (le regole "come CapCut" vivono nel core, testabili)
- Traccia principale magnetica: inserimento, spostamento, trim ed eliminazione compattano sempre; niente vuoti accidentali.
- Trascinando una clip sopra un'altra non la si sovrascrive: si crea automaticamente una traccia sopra
  (nello stesso comando). Le tracce rimaste vuote vengono rimosse nello stesso comando.
- "+" dalle librerie: inserisce al playhead nella traccia giusta (per tipo), creandola se serve.
- Split, taglio a sinistra/destra del playhead, trim, ripple, roll, slip, slide: funzioni pure
  "stato + parametri → macro-comando".
- `SnapEngine`: bordi delle clip, playhead, marker e beat, con soglia in pixel convertita in tempo dallo zoom.
- Transizioni (D-06): di default sono **centrate sul taglio** e usano materiale oltre i punti di taglio.
  Se il materiale non basta, i fotogrammi mancanti vengono coperti con un **freeze** e compare un avviso non bloccante
  con due alternative a un clic: "Sovrapponi" (modalità `overlap`, la timeline si accorcia) e "Accorcia" (durata ridotta
  al materiale disponibile). Così la sincronizzazione con musica e sovrapposizioni non si rompe mai di nascosto.

---

## 5. Engine (MLT)

### 5.1 Principio
L'engine è una **proiezione** del core: riceve i `ChangeSet` e aggiorna il grafo MLT. La proiezione gira sul thread UI
(D-23): le operazioni lente (apertura dei file, decodifica, rendering) restano fuori, nei thread di MLT e in un pool. Non legge mai lo stato da MLT per prendere decisioni. `rebuildAll()`
ricostruisce l'intero grafo da zero a partire dal modello: viene usato all'apertura del progetto, come verifica nei test
(grafo incrementale e grafo ricostruito devono produrre gli stessi fotogrammi) e come recupero in caso di errore.

### 5.2 Corrispondenza core → MLT

| Core | MLT |
|---|---|
| Sequence | `Mlt::Profile` (canvas, fps del progetto) + `Mlt::Tractor` |
| Sfondo del canvas | traccia 0 del tractor: producer `color` (colore di fondo) |
| Traccia visiva k | `Mlt::Playlist` alla traccia k+1 + transizione di composizione `vedit.composite` (a_track 0, b_track k+1), sempre attiva, in ordine dal basso verso l'alto |
| Traccia audio / audio delle tracce visive | stessa playlist; transizione `mix` (somma) verso la traccia 0 per ogni traccia che ha audio |
| Media | `Mlt::Chain` (producer `avformat`, o `qimage`/`pixbuf` per le immagini) condiviso da tutte le clip di quel media |
| Clip media | `chain.cut(in, out)` nella playlist; spazi vuoti = `blank` |
| Velocità costante | producer `timewarp:<velocità>:<file>` (uno per media e velocità, nella stessa cache); `warp_pitch=1` mantiene l'intonazione (**verificato**, `phase2_probe`) |
| Clip invertita | `timewarp` a velocità negativa. All'export dal file originale; nell'anteprima da un proxy invertito (`vedit-render --backwards`, all-intra ≤720p) letto in avanti, perché leggere all'indietro un GOP lungo costa ~119 ms a fotogramma contro 10 (D-37); fino ad allora avviso "preparazione…" |
| Effetti della clip | `Mlt::Filter` attaccati al cut, nell'ordine del modello |
| Trasformazione, maschere, sfondo della clip | filtro proprio `vedit.transform` (produce un fotogramma RGBA del canvas con alfa); modalità di fusione e opacità passate come proprietà del frame e lette da `vedit.composite` |
| Keyframe | filtri propri: la lista di keyframe (JSON) viene valutata dalla **stessa** funzione del core; filtri MLT/frei0r: stringa animata MLT, campionata fotogramma per fotogramma quando l'easing non esiste in MLT |
| Transizione sulla traccia | playlist "transizioni" sopra la playlist delle clip della stessa traccia: nella finestra della transizione (centrata sul taglio) un piccolo tractor con le due clip e `vedit.transition`; il materiale mancante oltre il taglio è un fotogramma fermo (`repeat`) (D-35) |
| Transizione in ingresso/uscita di una clip sovrapposta | filtro `vedit.transition` sulla clip, con A = trasparente |
| Testo e sottotitoli | producer proprio `vedit.text` (QPainter/QTextLayout su QImage, CPU), disegnato una volta alla creazione sul thread della proiezione (D-39) |
| Sticker | producer `avformat`/`qimage` (PNG, GIF, WebP); Lottie da valutare in Fase 5 |
| Colore | producer `color` |
| Traccia effetti / livello di regolazione | filtro piantato sul campo (`Mlt::Field::plant_filter`) sulla traccia 0 **dopo** le composizioni delle tracce sottostanti e **prima** di quelle sopra, con in/out della clip **[verifica]** |
| Compound clip | tractor annidato usato come producer |
| Volume, dissolvenze audio, pan | filtro proprio `vedit.gain` sul cut (volume, dissolvenze, pan; posizioni del media, D-38) e un altro sulla playlist per il volume della traccia; misura dei picchi per il mixer (per traccia e master) |

Servizi propri (`vedit.composite`, `vedit.transform`, `vedit.transition`, `vedit.text`, `vedit.gain`, …) sono registrati
nel repository MLT all'avvio con `Mlt::Repository::register_service` (**verificato** con `tools/probes/mlt_probe.cpp`), sia in `vedit` sia in
`vedit-render`, da un unico codice (`engine/mlt/services`). Il loro calcolo delega ai kernel di `vedit_fx`.

### 5.3 Riproduzione e anteprima
- Consumer MLT `sdl2_audio` (audio via SDL2 verso PipeWire/PulseAudio, detta anche il ritmo del video). I fotogrammi
  arrivano con l'evento `consumer-frame-show` sul thread di MLT e finiscono in un `FrameSink`, uno slot "ultimo frame"
  senza lock bloccanti. `real_time = 1`: un thread di rendering in anticipo che scarta i frame in ritardo (D-24); il
  parallelismo è dentro i decoder FFmpeg e nei nostri servizi (kernel a fette). Frame persi contati per l'indicatore.
- `PreviewItem` (QQuickItem C++) sceglie la strategia in base al backend della scena:
  - **RHI (Vulkan, OpenGL, GLES):** `QQuickRhiItem` (Qt ≥ 6.7) con texture persistente aggiornata a ogni frame;
    shader compilati con `qt6-shadertools` (compatibili GLSL 100 ES / 120). In Fase 0 i frame arrivano già in RGBA;
    la conversione YUV→RGB su GPU è un'ottimizzazione successiva, con test di parità.
  - **Software:** `QSGImageNode` con `QImage` (nessuno shader richiesto).
- Qualità dell'anteprima (piena, 1/2, 1/4): il consumer lavora alla risoluzione ridotta, quindi tutta la pipeline MLT
  calcola meno pixel. Possibile grazie ai parametri indipendenti dalla risoluzione.
- Skimming: l'engine separa la **posizione mostrata** dal **playhead**. Al passaggio del mouse mostra il fotogramma sotto
  il cursore e, quando il mouse esce, torna al playhead. Nel pannello media usa un `FrameGrabber` separato
  (producer dedicato + cache LRU) su un worker, senza toccare la timeline.
- Scrubbing audio: proprietà `scrub_audio` del consumer (accettata dal consumer; comportamento durante il trascinamento
  da verificare quando esisterà la timeline, Fase 1).
- **Esiti della Fase 0** (programmi di prova e test): il profilo va impostato *prima* di creare i producer (le durate
  sono calcolate con i fps del profilo); tutti gli oggetti MLT vanno distrutti prima di `Mlt::Factory::close()`;
  al `play()` serve un `purge()` del consumer (altrimenti fotogrammi a velocità 0 già letti in anticipo bloccano
  `sdl2_audio`, race condition); un frame MLT ancora vivo dopo la chiusura del consumer impedisce di liberarlo, quindi
  il `FrameSink` riceve una copia dei pixel (D-19).
- Modifiche durante la riproduzione: la proiezione aggiorna le playlist con il tractor bloccato (`lock`/`unlock`),
  poi `purge` del consumer e ri-seek alla posizione corrente.
- Avvio veloce: `Mlt::Factory::init` (carica tutti i moduli) gira su un thread in background mentre si mostra la schermata
  iniziale, che non ha bisogno di MLT.

### 5.4 Export e rendering fuori processo
- `vedit-render` riceve una **copia congelata del progetto** (`.vproj` in un file temporaneo) e un job JSON (formato,
  codec, risoluzione, intervallo). Ricostruisce il grafo con lo stesso codice dell'engine: l'export è esattamente ciò
  che dice il modello. Usa il consumer `avformat` e scrive su stdout righe JSON di progresso, avvisi ed errori.
- In un processo separato un crash dell'export non chiude l'editor, e si può continuare a montare mentre esporta.
- Encoder hardware: `vcodec=h264_vaapi` / `hevc_nvenc` / `*_qsv` / `*_vulkan` con il dispositivo rilevato. Se l'encoder
  fallisce (a qualsiasi punto) il processo padre rilancia il job con l'encoder software equivalente
  (x264/x265/SVT-AV1/libvpx) e avvisa con una snackbar.
- Stesso eseguibile per: generazione dei proxy (`--job proxy`), estrazione dei fotogrammi per i test di rendering
  (`--job frames`) e generazione delle miniature animate delle transizioni.
- Gira con `QT_QPA_PLATFORM=offscreen` (serve QGuiApplication per i font del testo).

### 5.5 Proxy, miniature, waveform, probe
- Probe dei media con libavformat (metadati completi: rotazione, HDR, VFR, spazio colore). Fingerprint campionato
  (vedi FILE_FORMAT §7).
- Proxy automatici per 4K, HEVC, 10 bit e VFR: H.264, altezza massima 720p (540p in modalità software), GOP corto senza
  B-frame per seek rapidi, frame rate costante. Encoder hardware se disponibile, altrimenti libx264 `veryfast`.
  Salvati in `~/.cache/vedit/proxy/<fingerprint>-<profilo>.mp4`. Interruttore globale "anteprima a qualità ridotta".
  L'export usa sempre i file originali.
- Miniature e waveform: job del `TaskManager` (pool di thread, priorità "visibile ora" > "prefetch"), ognuno con il
  proprio producer MLT (mai condiviso con la riproduzione). Cache su disco per fingerprint: strisce JPEG per livello
  di zoom; waveform come picchi min/max multi-risoluzione in un file binario.

---

## 6. Sistema di effetti e transizioni (`src/fx`)

- **Registro** caricato da manifest JSON in `resources/` (e dai pacchetti utente): id, versione, categoria, nomi
  tradotti, parametri (tipo, intervallo, default, animabile, "avanzato"), implementazione (`kernel` proprio, filtro MLT
  o filtro frei0r), miniatura. Il formato dei manifest sarà documentato in `docs/EFFECT_FORMAT.md` quando si
  implementano i primi effetti (Fase 2).
- **Kernel** propri: interfaccia `CpuKernel` (obbligatoria, multithread a fasce, RGBA 8 bit premoltiplicato)
  e `GpuKernel` (opzionale, GLSL compatibile OpenGL 2.1 / GLES 2.0, senza estensioni di un solo vendor).
- Priorità di implementazione: filtri MLT/frei0r esistenti dove danno un risultato corretto e deterministico; kernel
  propri per il resto (composizione, trasformazioni, maschere, transizioni della libreria).
- **Percorso GPU (Fase 5):** un thread GPU dedicato con contesto offscreen (EGL surfaceless o pbuffer) esegue i
  `GpuKernel`. Qualsiasi errore (compilazione shader, memoria, perdita del contesto) fa passare **quell'operazione**
  al kernel CPU, scrive nel log e mostra un avviso non bloccante. I test di rendering girano due volte (GPU e CPU
  forzata) e confrontano il PSNR. Non useremo Movit (GPU di MLT): richiede OpenGL 3, ha un percorso CPU diverso e non
  garantirebbe la parità richiesta dalla sezione 1bis (D-10).
- Colore: pipeline interna RGBA 8 bit, Rec.709. I media HDR vengono convertiti in SDR con tone mapping alla decodifica
  (filtri `avfilter` di MLT: `zscale`/`tonemap`, disponibili in FFmpeg). Limite dichiarato: niente export HDR in v1.

---

## 7. GPU: capacità, fallback, safe mode (`src/engine/gpu`)

### 7.1 Rilevamento in un processo separato
`vedit-gpuprobe` stampa un JSON con:
- API grafiche: Vulkan (dispositivi, tipo, driver, versione API, VRAM; via `QVulkanInstance`, nessun link diretto a
  libvulkan), OpenGL/GLES (versione, renderer, vendor, se è un rasterizzatore software come llvmpipe);
- video: per VA-API, NVDEC/NVENC (CUDA), QSV e Vulkan Video, prova **reale** di apertura dei dispositivi FFmpeg e di
  decodifica/codifica di un breve flusso generato al momento con gli encoder software. Così si misura cosa funziona
  davvero, non cosa è dichiarato;
- ambiente: Wayland/X11, macchina virtuale, sessione remota.

Due livelli: `--quick` (solo API grafiche, serve prima di creare la finestra) e `--full` (codec, in background dopo
l'avvio). Timeout 4 s; un crash o un blocco del probe vale come "API non utilizzabile".

### 7.2 Cache
`~/.cache/vedit/gpu-caps.json`, invalidata quando cambia l'impronta dei driver: versione del kernel, id PCI delle schede
(`/sys/class/drm`), dimensione e data dei file dei driver (`libGLX_nvidia*`, `libgallium*`, `libvulkan_*`,
`*_drv_video.so`), versione di Qt e del probe. Il calcolo dell'impronta costa pochi millisecondi. In condizioni normali
all'avvio si legge solo la cache.

### 7.3 Catene di fallback

| Sottosistema | Catena |
|---|---|
| UI (Qt RHI) | Vulkan → OpenGL (3.3+ core, poi 2.1 / GLES 2.0) → software (`QT_QUICK_BACKEND=software`) |
| Effetti | kernel GPU → kernel CPU multithread |
| Decodifica | VA-API / NVDEC / QSV / Vulkan → software |
| Encoding | NVENC / VA-API / QSV / Vulkan Video → x264 / x265 / SVT-AV1 / libvpx |
| AI | CUDA / ROCm / Vulkan (dove il backend lo supporta) → CPU |

Scelta del backend UI all'avvio, in ordine: preferenza forzata dall'utente → safe mode → cache del probe →
primo backend utilizzabile della catena. Se esiste solo un rasterizzatore software (llvmpipe/lavapipe) si usa OpenGL
su llvmpipe (provato: funziona, con effetti GPU disattivati); il backend software di Qt Quick resta l'ultimo anello.
La misura delle prestazioni in solo software con un vero progetto è rimandata alla Fase 1 (serve la timeline).

### 7.4 Safe mode e crash all'avvio
- `--safe-mode`: backend software, nessun kernel GPU, decodifica ed encoding solo software.
- All'avvio si scrive un marcatore "avvio in corso" in `~/.local/state/vedit/`, che viene rimosso dopo il primo frame
  disegnato più qualche secondo di stabilità. Se al lancio successivo il marcatore c'è ancora, si incrementa il
  contatore dei crash consecutivi; a **2** si entra in safe mode automaticamente e l'utente viene avvisato.
- Errore del grafo di scena a runtime (`sceneGraphError`, perdita del dispositivo): l'app salva, registra il backend
  come problematico e si riavvia sul backend successivo della catena. Il salvataggio continuo rende il riavvio
  senza perdite.
- Preferenze → Prestazioni: GPU e backend in uso, capacità trovate, opzioni per forzare ogni sottosistema, scelta della
  GPU sui sistemi ibridi (`DRI_PRIME` / variabili PRIME di NVIDIA per i processi figli), "Copia informazioni di sistema".

---

## 8. Tema Material 3 (`src/theme`)

### 8.1 Componenti C++
- `SchemeGenerator`: usa **material-color-utilities** (C++, vendored, Apache-2.0) per generare i ruoli colore M3
  completi, con le varianti TonalSpot, Vibrant, Expressive, Neutral, Fidelity, Content e Monochrome, i livelli di
  contrasto standard, medio e alto e le modalità chiaro e scuro. Se il porting C++ richiede Abseil, abbiamo due strade:
  una patch minima documentata oppure il pacchetto `abseil-cpp`, già installato (vedi §15, D-12).
- `SeedProvider`: sorgenti del colore seme, a scelta dell'utente (sezione 4 della specifica), ciascuna con
  "non disponibile" come esito possibile:
  1. colore d'accento di sistema: portale `org.freedesktop.appearance accent-color`, aggiornato dal vivo tramite
     `SettingChanged`. Se il portale non lo fornisce, si legge (solo lettura) l'accento di GNOME (`gsettings`
     `org.gnome.desktop.interface accent-color`) o di KDE (`kdeglobals` → `AccentColor`) (D-13);
  2. sfondo del desktop (GNOME, KDE, hyprpaper/swww dove interrogabili) → quantizzatore di material-color-utilities;
  3. copertina del progetto; 4. colore scelto a mano; 5. palette predefinita.
- `ThemeManager` (singleton QML `Theme`): espone i ruoli colore, la tipografia (scala M3 completa, font incluso), le forme,
  l'elevazione, gli stati (hover 8%, focus 10%, pressed 10%, dragged 16%), il motion (durate e curve di easing M3 come
  bezier per `easing.bezierCurve`) e la densità. Il cambio di schema è animato da un'unica animazione C++ che interpola
  tutti i colori, senza riavvio. Tema chiaro/scuro/automatico (segue `color-scheme` del portale; scuro di default).
  "Riduci animazioni": dal portale se disponibile, altrimenti dalla preferenza dell'app.

### 8.2 Componenti QML
- Modulo `Vedit.Style`: uno **stile Qt Quick Controls personalizzato** costruito su `QtQuick.Templates`, con **fallback
  sullo stile Material** di Qt per i controlli non ancora ridefiniti (**verificato**: l'import nel `qmldir` deve essere
  senza versione). I componenti M3 senza equivalente Qt stanno in un modulo separato `Vedit.Components` (D-18).
  Motivo dello stile proprio: lo stile Material di Qt
  espone solo pochi colori (accent, primary, background, foreground), mentre la specifica chiede tutti i ruoli M3 e i
  componenti M3 (FAB esteso, segmented button, chip, navigation rail, search bar, snackbar, side sheet…).
- Nessun colore, dimensione, raggio o durata scritti nei QML: solo `Theme.*` (verificabile con un controllo in CI
  che cerca letterali di colore o di durata nei `.qml`).
- Icone: Material Symbols (Apache-2.0) come font variabile (asse FILL per lo stato attivo); font UI: Inter o Roboto
  Flex (OFL), inclusi nell'app.
- Backend software: niente shader per la leggibilità; ombre e sfocature decorative disattivate automaticamente.
- `vedit --component-gallery`: tutti i componenti in chiaro, scuro e alto contrasto, con selettori di seme e variante.

### 8.3 Nota su questa macchina
Qui il portale fornisce `color-scheme` ma **non** `accent-color` (risposta `NotFound`, sessione Hyprland), mentre GNOME
ha `accent-color = 'blue'`. Con la catena di §8.1 il criterio della Fase 0 ("colore seme dal sistema") risulta
soddisfatto qui tramite l'accento GNOME. Se il portale lo rende disponibile, verrà preferito automaticamente.

---

## 9. Interfaccia (`src/ui`)

- **QML per la presentazione, C++ per logica e modelli.** I controller (`ProjectController`, `TimelineController`,
  `PlayerController`, `MediaController`, `ExportController`, `LibraryController`, `SearchController`…) sono QObject
  registrati come singleton o istanze di contesto. Espongono azioni che creano comandi, mai modifiche dirette al modello.
- **ActionRegistry**: ogni azione ha id, testo, icona, scorciatoia, stato attivo e contesto. Da qui derivano la barra
  strumenti contestuale (sezione 0bis, regola 3), il menu del tasto destro, i tooltip con scorciatoia, la ricerca
  universale Ctrl+K e le scorciatoie personalizzabili (preset "stile CapCut" e "stile Premiere").
- **Timeline** (obiettivo: 500+ clip reattive): un QML leggero per tracce e clip con **virtualizzazione** (il modello
  espone solo le clip nell'intervallo visibile più un margine); miniature e forme d'onda disegnate da QQuickItem C++
  (`ThumbnailStrip`, `WaveformItem`) con texture del grafo di scena; hit-test, snapping e drag calcolati in C++
  (`TimelineController` + `SnapEngine`). L'accessibilità resta possibile perché ogni clip è un elemento QML con
  `Accessible.name`.
- **Anteprima dal vivo** al passaggio del mouse su filtri, effetti e transizioni: un "override temporaneo" applicato
  dall'engine alla sola proiezione (non al modello, non all'undo). Clic = comando vero.
- **Stato sempre visibile**: il `TaskManager` pubblica tutte le operazioni lunghe (progresso, annulla) nel punto di avvio
  e nell'indicatore globale.
- **i18n**: stringhe sorgente in inglese con `qsTr()`/`tr()`, traduzioni italiano/inglese in `.ts` (Qt Linguist,
  `qt_add_translations`). Lingua dell'interfaccia di default = lingua di sistema.
- **Stato dopo la Fase 1**: controller `AppController` (singleton `App`: bozze, libreria musicale, analisi dei media,
  editor aperto) ed `EditorController` (un progetto aperto: ogni azione diventa un comando); modelli `DraftsModel`,
  `MediaPoolModel`, `AudioLibraryModel`, `TimelineModel` (virtualizzato per intervallo visibile, aggiornato con un
  diff); item `MediaThumbnail` e `WaveformView`. Lo snapping è una funzione di `EditorController` (`snap`,
  `snapRange`: bordi delle clip, playhead, inizio), non ancora un `SnapEngine` separato. L'`ActionRegistry` (menu
  contestuali, Ctrl+K, scorciatoie personalizzabili) non esiste ancora: le azioni della Fase 1 sono dichiarate in
  barra, menu della clip e `Shortcut` QML; arriva quando le azioni diventano molte (Fase 2).

---

## 10. Thread e processi

| Contesto | Cosa fa | Regole |
|---|---|---|
| Thread UI | QML, modello core, undo stack, controller | Mai I/O bloccante o decodifica. Serializzazione del progetto per l'autosave misurata (obiettivo < 5 ms tipico, < 30 ms con 500 clip) |
| Thread UI (engine) | proiezione core→MLT, controllo del consumer (D-23) | Solo modifiche di playlist con il tractor bloccato (microsecondi); i producer si aprono in un pool |
| Thread di MLT | decodifica e rendering dei frame (interni al consumer) | I nostri servizi MLT sono rientranti e senza stato globale |
| Thread del grafo di scena | upload delle texture dell'anteprima | Legge solo dal `FrameSink` |
| Pool `TaskManager` | probe, fingerprint, miniature, waveform, analisi | Priorità, progresso, annullamento cooperativo (`CancellationToken`) |
| Thread I/O | scrittura atomica dell'autosave, snapshot della cronologia | Un solo scrittore per progetto |
| `vedit-gpuprobe` | capacità GPU e codec | Timeout, crash isolato |
| `vedit-render` | export, proxy, frame di test, miniature delle transizioni | Righe JSON su stdout, annullabile (SIGTERM → chiusura pulita del file) |
| `vedit-ai` (Fase 6) | compiti AI | Plugin caricati a runtime; risultati convertiti in comandi sul thread UI |

---

## 11. Persistenza
Dettagli in `docs/FILE_FORMAT.md`. In sintesi:
- Tutto è una **bozza** in `~/.local/share/vedit/drafts/<id>/` con `project.vproj`, miniatura, metadati per la schermata
  iniziale, stato UI e cronologia. Un `.vproj` aperto da file viene salvato automaticamente al suo posto.
- Salvataggio continuo: dopo ogni comando, con attesa di 300 ms di inattività e al massimo ogni 2 s durante modifiche
  continue. Serializzazione sul thread UI, scrittura atomica (file temporaneo + fsync + rename + fsync della cartella)
  sul thread I/O. Indicatore "Salvato" nella barra superiore. Se il disco è pieno: banner persistente, nuovi tentativi,
  nessuna perdita dello stato in memoria.
- Cronologia delle versioni: snapshot compressi ogni 10 minuti di modifiche, alla chiusura, prima di una migrazione
  e prima di ogni ripristino (così anche il ripristino si può annullare).
- Recupero dopo crash: un file di lock con PID e boot id rileva una chiusura anomala; il progetto è già all'ultimo stato
  salvato (perdita massima ~2 s) e l'utente può aprire la cronologia.

---

## 12. AI locali (Fase 6–7, solo l'impianto)
- `IAiTask`: id, nome, requisiti (componenti e modelli), `estimate()`, `run(input, params, progress, cancel)` →
  `AiResult` che il thread UI traduce in **normali comandi** (clip, sottotitoli, keyframe, maschere modificabili).
- Backend come plugin (`libvedit-ai-whisper.so`, `-onnx.so`, …) compilati solo se l'SDK è presente e caricati a runtime:
  se la libreria manca, la funzione appare disattivata con il nome del pacchetto da installare. L'app si compila e si
  avvia sempre senza componenti AI.
- Gestore modelli nelle preferenze: download solo su richiesta esplicita, dimensione mostrata prima, checksum verificato,
  licenze in `docs/MODELS.md` controllate prima dell'integrazione.

---

## 13. Log, errori, percorsi
- `QLoggingCategory` per modulo (`vedit.core`, `vedit.engine`, `vedit.gpu`, `vedit.render`, …) → `~/.local/state/vedit/logs/`
  con rotazione e livello configurabile; i log di MLT e FFmpeg sono inoltrati nelle stesse categorie.
- Percorsi XDG tramite `QStandardPaths`: config `~/.config/vedit/`, dati `~/.local/share/vedit/` (bozze, preset, pacchetti,
  modelli), cache `~/.cache/vedit/` (proxy, miniature, waveform, capacità GPU), stato `~/.local/state/vedit/` (log,
  contatori di crash, recenti).
- **Sandbox di sviluppo (D-03):** nelle build di sviluppo (`VEDIT_DEV_SANDBOX=ON`, default fuori dal PKGBUILD) `vedit`,
  `vedit-render` e i test puntano `XDG_CONFIG/DATA/CACHE/STATE_HOME` in `build/dev-home/`. Così, durante lo sviluppo,
  né l'app né i test scrivono fuori da questa cartella, e restano confinate anche le cache di Mesa e fontconfig.
- Errori: mai crash silenziosi. File corrotti o codec non supportati diventano un messaggio chiaro e un segnaposto
  nella timeline; eccezioni catturate ai confini dei thread; handler di segnale che scrive il marcatore di crash
  e un backtrace nel log.

---

## 14. Test
- **Unit** (QtTest): Rational/RationalTime (proprietà e casi limite 23.976/29.97/59.94), modello e invarianti,
  **ogni** comando (redo → undo → redo, con confronto dello stato serializzato), `TimelineEditor` (scenari CapCut),
  easing e keyframe, serializzazione, migrazioni (fixture per ogni versione del formato), `SchemeGenerator` (semi noti
  → colori attesi), logica di scelta del backend (output del probe simulati).
- **Integrazione**: apri → modifica → salva → riapri → JSON canonico identico; proiezione incrementale uguale a quella
  ricostruita (hash dei frame); engine headless con consumer `null`.
- **Rendering**: progetti in `tests/render/` esportati da `vedit-render --job frames` e confrontati con i PNG attesi
  (PSNR ≥ soglia per test); eseguiti in CPU forzata e, quando ci sarà, in GPU.
- **Smoke test headless**: `vedit --smoke-test <progetto>` (avvia, carica, riproduce N frame, esce con codice 0) con
  `QT_QPA_PLATFORM=offscreen` + `QT_QUICK_BACKEND=software` + `LIBGL_ALWAYS_SOFTWARE=1`.
- I media di test si generano al momento con `ffmpeg -f lavfi` (testsrc2, sine) in una fixture CTest, dentro `build/`:
  niente file video binari nel repository.

---

## 15. Decisioni (registro)

| Id | Decisione | Motivo |
|---|---|---|
| D-01 | Radice del repo = questa cartella | Evita un livello inutile; equivale a `vedit/` della specifica |
| D-02 | Qt minimo 6.7 (non 6.6) | `QQuickRhiItem` per l'anteprima su tutti i backend RHI; Arch fornisce 6.11 |
| D-03 | Sandbox XDG nelle build di sviluppo | Regola "non modificare file fuori da questa cartella" anche per app e test |
| D-04 | Tutti i tempi di sequenza sulla griglia fps del progetto; fps unico per progetto | Proiezione esatta su MLT (un solo profilo); unico arrotondamento nel comando "Cambia frame rate" |
| D-05 | Keyframe in tempo sorgente per le clip media, in tempo clip per le generate | Le animazioni restano agganciate al contenuto dopo trim, velocità e inversione |
| D-06 | Transizioni centrate sul taglio, freeze automatico per il materiale mancante + alternative a un clic | Nessuna perdita di sincronizzazione silenziosa, nessuna finestra di dialogo |
| D-07 | Export, proxy e probe in processi separati | Un crash di driver o codec non chiude l'editor; export in background |
| D-08 | `vedit-render` legge una copia congelata del `.vproj` | Un'unica fonte di verità: stesso codice di proiezione per anteprima ed export |
| D-09 | Pipeline interna RGBA 8 bit Rec.709, HDR convertito in SDR in ingresso | Semplicità e percorso CPU veloce; export HDR fuori dalla v1 (dichiarato) |
| D-10 | Niente Movit; GPU tramite kernel propri GLSL 2.1/ES 2.0 | Parità CPU/GPU verificabile e compatibilità con GPU vecchie |
| D-11 | Stile Qt Quick Controls personalizzato con fallback Material | Lo stile Material di Qt non espone tutti i ruoli e i componenti M3 |
| D-12 | material-color-utilities vendored in `third_party/` (patch minime documentate) | Richiesto dalla specifica; nessuna dipendenza di rete in build |
| D-13 | Sorgenti di accento aggiuntive (GNOME gsettings, KDE kdeglobals) dopo il portale | Il portale non espone `accent-color` su molte sessioni (qui incluso) |
| D-14 | Identificatori e commenti del codice in inglese, documentazione in italiano | Convenzione Qt/C++; stringhe UI in inglese tradotte in italiano |
| D-15 | Archivio `.vpack` = tar non compresso | Scrittura e lettura semplici senza nuove librerie; i video non si comprimono comunque |
| D-16 | Timeline QML virtualizzata + item C++ per miniature e waveform | Prestazioni con 500+ clip senza rinunciare a tema e accessibilità |
| D-17 | Anteprima RHI con `<rhi/qrhi.h>` (modulo `Qt6::GuiPrivate`) | API QRhi "semi-pubblica" (compatibilità garantita solo tra versioni minori vicine): è la via documentata da Qt per `QQuickRhiItem`; l'avviso di CMake è silenziato consapevolmente |
| D-18 | Due moduli QML: `Vedit.Style` (controlli con nome Qt Quick Controls) e `Vedit.Components` (componenti solo-M3) | Il `qmldir` dello stile importa Material per il fallback e ne riesporta i tipi: importarlo direttamente rende ambiguo `Button` |
| D-19 | Il `FrameSink` riceve una copia dei pixel del frame | Un frame MLT vivo dopo la chiusura del consumer impedisce di liberarlo (verificato con ASan); costo ~1 ms per frame in 1080p; ottimizzazione a copia zero rimandata |
| D-20 | Test e smoke test con `TMPDIR` e XDG dentro `build/`; smoke test dell'app in CTest | Nessun file scritto fuori dalla cartella; verifica automatica di avvio e riproduzione headless (software, safe mode, galleria) |
| D-21 | Icone risolte tramite il file `.codepoints` invece delle legature | Un nome errato mostra un'icona di riserva invece di disegnare testo sopra l'interfaccia |
| D-22 | Font UI: Inter Variable (OFL) | Ottima leggibilità a piccole dimensioni e corsivo separato ben supportato da Qt (Roboto Flex usa un asse `slnt`) |
| D-23 | Proiezione core → MLT sul thread UI, producer aperti in un pool | Evita di copiare il progetto a ogni modifica verso un thread engine; le modifiche al grafo (playlist con il tractor bloccato) costano microsecondi, l'unica parte lenta (aprire un file, decine di ms) è in background e la clip appare quando è pronta |
| D-24 | Consumer dell'anteprima con `real_time = 1` invece di `-N` | Nei worker paralleli di MLT il controllo del flag `rendered` avviene fuori dal mutex (`worker_get_frame` in `mlt_consumer.c`): un risveglio perso blocca l'anteprima in pausa e fa andare in deadlock `mlt_consumer_stop()` (riprodotto da `tst_timelineplayer`). È anche il default di Shotcut |
| D-25 | Colori verso MLT convertiti in `#AARRGGBB` | È il formato di MLT per 8 cifre; il modello usa `#RRGGBBAA` (FILE_FORMAT). Un test controlla i pixel |
| D-26 | Le "cut" sostituite durante la riproduzione restano vive ~1 s | I frame già letti in anticipo dal consumer possono ancora riferirle (e i loro filtri) mentre vengono renderizzati |
| D-27 | Libreria `src/document` (Document, AutoSaver, DraftLock, DraftStore) separata dalla UI | Salvataggio continuo, lock e bozze testabili senza QML; la UI chiede solo "apri/crea/chiudi" |
| D-28 | Data di modifica scritta nel file al salvataggio, non nel modello | Cambiarla nel modello richiederebbe un comando (e un passo di undo) a ogni salvataggio; il confronto "contenuto invariato, non scrivere" ignora la data |
| D-29 | Eliminare una bozza la sposta nel cestino di sistema | Recuperabile dal file manager: nessuna finestra di conferma necessaria (regola 10) |
| D-30 | `vedit-render` non chiude la factory MLT all'uscita | `Factory::close()` scarica moduli, FFmpeg e x264 mentre le loro cache globali sono allocate: LeakSanitizer le segnala come perdite di "<unknown module>" (verificato: nessuna senza lo scaricamento). Il processo termina subito dopo |
| D-31 | Cronologia delle versioni (snapshot in `history/`) rimandata alla Fase 8 | È nella riga della Fase 8 della tabella di SPEC §8; in Fase 1 bastano salvataggio atomico, lock e recupero |
| D-32 | Probe dei media in `vedit-render --probe` (processo), miniature e waveform in thread del processo principale | Un file che manda in crash il demuxer viene scartato come "danneggiato" prima che MLT lo apra nell'editor; miniature e waveform decodificano solo file già passati dal probe |
| D-33 | Miniature e waveform con FFmpeg diretto (non MLT) | Seek al keyframe + decodifica fino al punto e scalatura con swscale: molto più rapido di un producer MLT per ogni fotogramma; rotazione e aspetto dei pixel applicati a mano (test contro i fotogrammi decodificati) |
| D-34 | Canvas e fps dalla prima clip nello stesso comando dell'inserimento (`insertMediaAdoptingFormat`) | Un solo passo di annulla riporta tutto com'era; fps agganciato allo standard più vicino (29,99 → 30; 120 → 60), canvas al massimo 4K |
| D-35 | Transizioni come piccoli tractor su una playlist "transizioni" per traccia, non `Playlist::mix()` | `mix` accorcia la playlist e sposta tutto ciò che segue: il modello (transizione centrata sul taglio, durata della timeline invariata) non corrisponderebbe al grafo. Il tractor copre esattamente la finestra; oltre la fine del materiale si ripete l'ultimo fotogramma (`repeat`, come CapCut). L'audio resta quello delle clip (taglio netto per ora) |
| D-36 | Anteprima dal vivo delle librerie solo nella proiezione (`TimelineProjection::Preview`) | Passare sopra un filtro o una transizione non tocca il modello né la cronologia di annulla; si aggiornano solo le tracce interessate |
| D-37 | Proxy invertiti generati da `vedit-render --backwards`, usati solo dall'anteprima | Leggere all'indietro è ~12× più lento (misurato su 1080p GOP lungo). L'opzione non si chiama `--reverse` perché `QGuiApplication` si prende `-reverse` dagli argomenti |
| D-38 | I filtri sui cut ricevono le posizioni del media: `vedit.gain` riceve il primo fotogramma della clip | Verificato: un filtro attaccato a un cut vede la posizione nel producer, non nella clip; le dissolvenze sono calcolate su (posizione − primo fotogramma) |
| D-39 | Il livello di testo si disegna alla creazione del producer, non nei thread di MLT | I font di Qt nei thread non-Qt lasciano dati FreeType per thread che Qt libera male all'uscita del thread (LeakSanitizer in `vedit-render`); il testo della Fase 2 è statico e il profilo ha dimensione fissa, quindi un solo disegno basta |
| D-40 | Pannello proprietà guidato da `ClipInspector` con valori per chiave (`values["opacity"]`, `set(chiave, valore)`) | Un solo punto C++ testabile per tutte le proprietà (e per "Applica a tutte", "Ripristina", copia/incolla attributi); il QML resta presentazione. Le chiamate di `set` di un trascinamento si fondono in un passo di annulla fino a `endGesture()` |
| D-41 | "Migliora automaticamente": regolazioni calcolate dalle miniature già in cache (statistiche: esposizione verso il grigio medio, contrasto, alte luci/ombre, bilanciamento "grey world", vividezza) e volume dai picchi della forma d'onda (−1 dBFS) | Nessuna decodifica extra, risultato immediato e modificabile (valori normali di "Regola"); correzioni parziali e limitate: un'immagine già corretta non cambia. La normalizzazione LUFS arriva in Fase 4 |
| D-42 | Librerie senza selezione: un filtro va sulla clip sullo schermo (che diventa selezionata), una transizione sul taglio della traccia principale più vicino al playhead, uno stile di testo crea un testo nuovo | Nessun "seleziona prima una clip" nel caso più comune (regola 10): il risultato si vede subito ed è annullabile. Con una clip selezionata: il taglio dopo di essa (o prima, se è l'ultima) |
| D-43 | Anteprima dal vivo delle transizioni: la proiezione le mostra (D-36) e l'interfaccia fa scorrere in ciclo i fotogrammi della finestra con lo skimming, senza muovere il playhead | Mostra il movimento nel player come CapCut senza riprodurre il progetto; le miniature della libreria sono disegnate con gli stessi kernel CPU su scene di esempio, in un pool di thread con cache |
| D-44 | Un'unica funzione del core per il tempo dei keyframe e dei marker di clip (`core/project/ClipTime.h`), usata dall'engine, dai marker e dalla timeline | D-05 vale anche con velocità e inversione (a 2× un keyframe al fotogramma 60 della sorgente cade al fotogramma 30 della clip); prima l'engine usava `sourceIn + fotogrammi trascorsi` e i marker di clip erano scostamenti dall'inizio: il contenuto e i keyframe si separavano |
| D-45 | Le chiavi di rendering dei filtri (`TransformSettings`, `MaskSettings`) contengono tutti i keyframe (tempi, valori, interpolazione, easing), non solo il loro numero | Altrimenti spostare o cambiare un keyframe lasciava in anteprima il filtro vecchio (l'export, ricostruito da zero, era corretto): verificato da `tst_projection::keyframeChangesUpdateTheProjection` |
| D-46 | Animazioni predefinite (ingresso, uscita, ciclo; 30 per tipo) calcolate in `vedit.transform` a partire dall'id del manifest `animations.json` | Agganciate ai bordi della clip (non al contenuto), si sommano alla trasformazione e ai keyframe; nessun keyframe generato nel modello: la durata si cambia con un valore |

---

## 16. Rischi e proposte (regola 8)
1. **Parità CPU/GPU per oltre 100 transizioni.** Il percorso CPU arriva sempre per primo; quello GPU solo dove serve
   davvero (sfocature, distorsioni, 3D). La specifica lo consente ("opzionalmente GPU").
2. **Tempo reale in modalità solo software** (3 tracce 1080p con filtri): realistico solo con proxy e anteprima a 1/2
   o 1/4, attivati automaticamente come chiede la sezione 1bis. Lo misureremo e riporteremo i numeri.
3. **Media VFR all'export**: MLT gestisce il VFR in modo imperfetto. Se i test lo confermano, all'import proporremo
   (un clic, in background) una copia "ottimizzata" a frame rate costante.
4. **Avvio < 2 s**: `Mlt::Factory::init` e la cache GPU fuori dal percorso critico; probe `--quick` solo al primo avvio.
5. **AI pesanti** (Demucs, LaMa, SAM, RIFE): fattibilità e licenze da verificare in Fase 6–7. Nota pratica: il pacchetto
   `piper` dei repo ufficiali **non** è il TTS (è un configuratore di mouse da gaming); il TTS Piper è nell'AUR.
6. **Asset (font, icone, material-color-utilities)** vanno scaricati una volta in Fase 0 (con `git clone` da GitHub).
   Prima del download ti dirò esattamente cosa, da dove e con quale licenza.

---

## 17. Piano della Fase 0 (incrementi, ognuno compila, passa i test e ha un commit) — completato
1. Scheletro: `git init`, CMake e preset, warning come errori, sanitizer, clang-format/clang-tidy, sandbox XDG,
   harness dei test, `README.md`, `docs/PROGRESS.md`.
2. `core/time`: Rational, RationalTime, TimeRange + test.
3. `core/project` + invarianti + test.
4. `core/commands` + `TimelineEditor` minimo (aggiungi media, inserisci, sposta, trim, split, elimina, parametro con
   merge) + test do/undo/redo.
5. Serializzazione v1 + infrastruttura di migrazione + test di andata e ritorno.
6. Tema: material-color-utilities vendored, `SchemeGenerator`, sorgenti del seme, `ThemeManager` + test.
7. GPU: `vedit-gpuprobe`, cache, scelta del backend, `--safe-mode`, contatore dei crash + test.
8. Engine minimo: init MLT in background, apertura di un file, `PreviewItem` (RHI e software), audio SDL2,
   play/pausa/seek in una finestra QML.
9. `Vedit.Style` (primo insieme di componenti M3) e `vedit --component-gallery`.
10. Verifica del criterio: compilazione senza warning, test verdi, video riprodotto con GPU e con
    `QT_QUICK_BACKEND=software` + `LIBGL_ALWAYS_SOFTWARE=1`, galleria in chiaro e scuro con seme di sistema;
    riepilogo onesto.

---

## 18. Esiti delle verifiche della Fase 0
| Punto | Esito |
|---|---|
| `Mlt::Repository::register_service` per servizi propri | ✅ un filtro registrato a runtime viene creato per nome e applicato |
| `Playlist::mix` | ✅ crea il mix; accorcia la playlist della durata del mix |
| Consumer `sdl2_audio` + `consumer-frame-show` | ✅ 30 fps in tempo reale (anche con `SDL_AUDIODRIVER=dummy`) |
| Avvio in pausa e poi play | ⚠️ race condition con `real_time < 0`: risolta con `purge()` al play |
| Durata dei producer e profilo | ⚠️ il profilo va fissato prima di creare il producer (125 invece di 150 fotogrammi) |
| Chiusura della factory MLT | ⚠️ gli oggetti MLT vanno distrutti prima di `Factory::close()` (altrimenti crash) |
| Frame trattenuti dopo la chiusura del consumer | ⚠️ il consumer non viene mai liberato: copia dei pixel (D-19) |
| Fallback dello stile Qt Quick Controls | ✅ con import senza versione nel `qmldir` |
| Portale `accent-color` su Hyprland | ❌ non disponibile: seme letto da GNOME `gsettings` (D-13) |
| Font WOFF2 in Qt | ✅ caricati (Inter corsivo e Material Symbols) |
| QRhi in piattaforma `offscreen` con OpenGL | ⚠️ nessun QRhi: l'anteprima usa automaticamente la superficie software (`GraphicsInfo.api`) |
| `timeremap`/`rbpitch`, `plant_filter` per i livelli di regolazione | da verificare quando servono (Fasi 2–3) |

---

## 19. Piano della Fase 1 (incrementi, ognuno compila, passa i test e ha un commit) — completato
1. `src/fx` (composizione CPU di riferimento) + servizio `vedit.composite` + `MediaProducerCache` +
   `TimelineProjection` con aggiornamenti incrementali; test "incrementale = ricostruito" su fotogrammi reali.
2. `TimelinePlayer`: riproduzione della timeline viva, modifiche durante la riproduzione, J/K/L, skimming.
3. Export: `Renderer`, `vedit-render` (processo), `RenderJob`; test con ffprobe e confronto dei fotogrammi.
4. `src/document`: bozze, salvataggio continuo atomico, lock e recupero, `DraftStore`.
5. Import: probe in processo separato, fingerprint, miniature e forme d'onda con cache; formato dalla prima clip.
6. Core: duplica, sposta su nuova traccia. Interfaccia: schermata iniziale, editor, timeline, export a una schermata;
   test dell'interfaccia reale (`tst_ui`) con i test di semplicità 1, 2, 8, 10.
7. Prestazioni (500 clip), traduzioni, documentazione, verifica del criterio.

## 20. Esiti delle verifiche della Fase 1
| Punto | Esito |
|---|---|
| Profilo e producer "loader" | ✅ il loader adatta il media al canvas con bordi trasparenti e applica la rotazione (`projection_probe`) |
| Modifica delle playlist durante la riproduzione | ✅ con il tractor bloccato, `purge` e nuovo seek; ASan pulito con modifiche ogni 30 ms in riproduzione |
| Consumer con `real_time < 0` (worker paralleli) | ❌ risveglio perso in `worker_get_frame`: anteprima bloccata in pausa e deadlock nello stop → `real_time = 1` (D-24) |
| Colori MLT a 8 cifre | ⚠️ sono `#AARRGGBB`: lo sfondo "nero" era blu trasparente → conversione esplicita (D-25) |
| Durata del tractor | ✅ = traccia più lunga; lo sfondo viene dimensionato sulla durata |
| Consumer `avformat` per l'export | ✅ `real_time = -1` (nessun fotogramma scartato), fine con `terminate_on_pause`, avanzamento con `position()` |
| `Mlt::Factory::close()` a fine processo | ⚠️ scarica FFmpeg/x264 con le loro cache globali ancora allocate → falsi "leak" di LeakSanitizer; `vedit-render` non la chiama (D-30) |
| Fps diversi all'export | ✅ la proiezione converte i tempi (entrambi i bordi) al rate del profilo: 6,5 s a 25 fps = 162 fotogrammi (arrotondamento pari) |
| `QFile::moveToTrash` | ⚠️ fallisce se la cartella dati XDG non esiste ancora: viene creata prima |
| Drag interno QML da un elemento in `Overlay.overlay` | ❌ l'overlay è invisibile senza popup aperti: nessun evento di drag → il "fantasma" sta nel `contentItem` della finestra |
| Playlist MLT: `insert_blank`/`remove` | ✅ non fondono gli spazi vuoti da sole (lettura di `mlt_playlist.c`): la patch per indice è sicura |
| 500 clip | ✅ una modifica (comando + patch del grafo + diff del modello) 3–6 ms; serializzazione per il salvataggio 6–19 ms (1 MiB di JSON); primo caricamento del grafo ~35 ms |
| Backend grafici | ✅ smoke test dell'editor su Vulkan, OpenGL (Wayland), software e safe mode |

## 21. Esiti delle verifiche della Fase 2
| Punto | Esito |
|---|---|
| `timewarp` a velocità costante | ✅ mappa i fotogrammi esattamente (fotogramma k → k·v); `warp_pitch=1` mantiene l'intonazione (`phase2_probe`) |
| `timewarp` a velocità −1 | ✅ riproduce all'indietro, ma decodifica 119 ms/fotogramma contro 10 in avanti → proxy invertito (D-37) |
| `Playlist::repeat` | ✅ ripete un fotogramma: fermo immagine per il riempimento delle transizioni |
| Loader alla proporzione della sorgente | ✅ nessun bordo: `vedit.transform` posiziona il fotogramma intero sul canvas |
| Tractor annidato come producer di una playlist | ✅ usato per le transizioni (D-35) |
| Posizioni viste da un filtro su un cut | ⚠️ quelle del media, non della clip (D-38) |
| Font di Qt nei thread di MLT | ⚠️ perdita per thread segnalata da LeakSanitizer all'uscita dei thread → testo disegnato sul thread della proiezione (D-39) |
