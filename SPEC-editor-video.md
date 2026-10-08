# SPECIFICA COMPLETA — Editor video desktop per Arch Linux (parità funzionale con CapCut Desktop)

> Questo documento è la specifica vincolante del progetto. Leggilo per intero prima di scrivere codice.
> Rileggilo, insieme a `docs/PROGRESS.md`, all'inizio di OGNI nuova sessione di lavoro.

---

## 0. Ruolo e obiettivo

Sei un ingegnere software senior esperto di video editing non lineare (NLE), C++, Qt e pipeline multimediali su Linux.
Il tuo compito è progettare e implementare **velacut**: un editor video desktop nativo per Arch Linux che offra
**tutte le funzionalità di CapCut Desktop**, con la stessa immediatezza d'uso, ma:

- completamente **offline**: ogni funzione "AI" gira in locale, nessun servizio cloud, nessun account;
- **open source**, con interfaccia e asset originali (niente loghi, icone, template o nomi commerciali di CapCut);
- ottimizzato per Linux: Wayland e X11, PipeWire;
- **compatibile con TUTTE le schede grafiche**: NVIDIA, AMD, Intel, vecchie e nuove, driver open e proprietari,
  macchine virtuali e perfino sistemi **senza accelerazione GPU**. La GPU è un acceleratore opzionale, mai un requisito
  (regole vincolanti nella sezione 1bis);
- **semplice quanto CapCut**: chi non ha mai montato un video deve riuscire a fare un video completo in pochi minuti
  (regole e test misurabili nella sezione 0bis, che ha la priorità su tutto il resto dell'interfaccia);
- interfaccia in stile **Material You (Material Design 3)** di Google, con colori dinamici (sezione 4);
- una libreria di **transizioni** ricca e curata quanto quella di CapCut (sezione 5.11bis).

Le poche funzioni di CapCut che dipendono per forza dal cloud (spazio cloud, collaborazione online, libreria stock online,
template della community) vanno sostituite con equivalenti locali descritti nella sezione 5.14.

Il risultato finale deve essere un'applicazione **usabile davvero**, non una demo: stabile, veloce, con undo/redo affidabile,
salvataggio automatico e recupero dopo crash.

---

## 0bis. Semplicità come CapCut (priorità assoluta)

La ragione per cui le persone usano CapCut non è il numero di funzioni: è che **chiunque, senza aver mai montato un video,
riesce a fare un video bello in pochi minuti**. velacut deve dare la stessa sensazione. Quando una scelta di design mette
in conflitto potenza e semplicità, **vince la semplicità** nell'interfaccia principale, e la potenza va in "Avanzate".

### Regole di semplicità (vincolanti)
1. **Aprire e montare subito.** "Nuovo progetto" apre l'editor all'istante, senza chiedere risoluzione, fps o cartelle.
   Il formato del canvas e il frame rate si impostano automaticamente dalla **prima clip** aggiunta (come CapCut),
   e si possono cambiare dopo con un clic sul selettore del formato.
2. **Niente "Salva".** Il progetto si salva da solo a ogni modifica (salvataggio incrementale, veloce, sicuro).
   I progetti sono **bozze** nella schermata iniziale, con miniatura, durata e data; si rinominano, duplicano ed eliminano
   da lì. "Salva con nome", "Esporta progetto" e simili esistono solo nel menu, per chi li cerca.
3. **Barra degli strumenti contestuale** sopra la timeline, che cambia in base alla selezione:
   - clip video: Dividi, Elimina, Duplica, Velocità, Volume, Animazione, Congela, Inverti, Specchia, Ruota, Ritaglia,
     Rimuovi sfondo, Migliora, Sostituisci;
   - clip audio: Dividi, Elimina, Volume, Dissolvenze, Velocità, Beat, Riduci rumore, Cambia voce, Separa voce/musica;
   - testo: Modifica, Stile, Animazione, Leggi ad alta voce (TTS), Duplica;
   - nessuna selezione: Dividi al playhead, Aggiungi testo, Aggiungi audio, Sottotitoli automatici.
   Le stesse azioni sono nel menu del tasto destro. Le funzioni più usate sono **sempre a un clic** dalla selezione.
4. **Aggiungere con un gesto.** Ogni elemento delle librerie (media, audio, testi, sticker, effetti, filtri, transizioni)
   ha un pulsante **"+"** che lo inserisce al playhead, nella traccia giusta; in alternativa si trascina.
   Le tracce si creano e si rimuovono da sole: l'utente non deve mai "creare una traccia", e le tracce vuote spariscono.
5. **Anteprima dal vivo di tutto.** Passando il mouse su un filtro, effetto, animazione, stile di testo o transizione,
   il player lo mostra **applicato alla clip selezionata**. Clic = applica. Clic di nuovo = rimuovi.
   Ogni pannello di proprietà ha l'intensità regolabile e **"Applica a tutte"**.
6. **Default intelligenti.** Transizioni da 0,5 s; testi centrati con stile leggibile e contorno; volume della musica
   automaticamente più basso quando c'è una voce; export con le impostazioni consigliate uguali al progetto;
   sottotitoli nella lingua rilevata; durata delle foto 3 s con leggero zoom (Ken Burns).
7. **Linguaggio semplice.** Nell'interfaccia principale niente gergo: "Velocità", non "time remapping"; "Qualità", non "bitrate";
   "Taglia", non "razor". I termini tecnici (codec, bitrate, GOP, spazio colore, profili) stanno solo nelle sezioni
   **"Avanzate"**, chiuse di default.
8. **Divulgazione progressiva.** Il pannello proprietà mostra prima 3–6 controlli essenziali; il resto è in sezioni
   espandibili. Ogni sezione ha **"Ripristina"**.
9. **Un clic per le cose difficili.** Pulsanti che fanno tutto da soli, con risultato sempre modificabile:
   "Migliora automaticamente" (colore + luce + audio), "Sottotitoli automatici", "Rimuovi sfondo", "Rimuovi pause",
   "Rimuovi parole di riempimento", "Adatta a 9:16", "Migliora voce", "Sincronizza al ritmo", "Stabilizza".
10. **Errori impossibili o reversibili.** Tutto si annulla; le azioni distruttive mostrano una snackbar "Annulla" invece di
    una finestra "Sei sicuro?". Le uniche conferme bloccanti sono eliminare un progetto e sovrascrivere un file esistente.
11. **Timeline indulgente.** Traccia principale magnetica, niente spazi vuoti accidentali; se una clip viene trascinata
    sopra un'altra, non la sovrascrive: si crea automaticamente una traccia sopra. Snapping attivo di default.
12. **Asse di anteprima (skimming).** Passando il mouse sulla timeline o su una clip del pannello media, il player mostra
    quel fotogramma senza spostare il playhead. Si disattiva con un pulsante.
13. **Prima esecuzione senza configurazione.** Nessuna procedura guidata obbligatoria. Progetto di esempio incluso,
    tour di 5 passi saltabile e suggerimenti contestuali la prima volta che si usa una funzione (mostrati una sola volta).
14. **Ricerca universale** (Ctrl+K): trova effetti, filtri, transizioni, testi, sticker, musica, comandi e impostazioni.
    Selezionando un risultato lo si applica o lo si apre.
15. **Stato sempre visibile.** Le operazioni lunghe (sottotitoli, proxy, export, AI) mostrano progresso nel punto in cui
    sono state avviate e in un piccolo indicatore globale; l'utente può continuare a montare nel frattempo.

### Test di semplicità (misurabili)
Scrivi in `docs/USABILITY.md` questi scenari con il numero massimo di azioni (clic, tasti o trascinamenti) consentito.
Alla fine di ogni fase ripeti gli scenari applicabili, **conta le azioni** e riporta il risultato. Se uno scenario
supera il limite, semplifica l'interfaccia prima di proseguire.

| Scenario | Limite |
|---|---|
| Dall'avvio al primo taglio di una clip (nuovo progetto, trascina clip, dividi) | 4 azioni |
| Aggiungere musica dalla libreria locale sotto il video | 2 azioni |
| Applicare un filtro a tutte le clip | 3 azioni |
| Aggiungere un testo, scriverlo e dargli un'animazione di ingresso | 5 azioni + digitazione |
| Generare i sottotitoli automatici con uno stile animato | 3 azioni |
| Mettere una transizione tra tutte le clip | 3 azioni |
| Trasformare un video 16:9 in 9:16 con il soggetto inquadrato | 2 azioni |
| Esportare con le impostazioni consigliate | 2 azioni |
| Video TikTok completo: 5 clip, musica sul ritmo, testo, sottotitoli, transizioni, filtro, export | 25 azioni |
| Nessuna funzione principale più in profondità di | 2 livelli di menu/pannello |

---

## 1. Stack tecnico (vincolante)

| Ambito | Scelta |
|---|---|
| Linguaggio | C++20 |
| UI | Qt 6 (≥ 6.6), Qt Quick/QML, stile Qt Quick Controls **Material** in variante **Material 3**, esteso con componenti propri; C++ per logica e modelli |
| Colori dinamici | libreria **material-color-utilities** (porting C++, inclusa nel repository come sottomodulo o vendored) per generare gli schemi Material 3 |
| Motore video | **MLT 7** (libmlt++), con i suoi filtri, transizioni e il consumer avformat |
| Decodifica/encoding | FFmpeg (via MLT): decodifica ed encoding **software sempre disponibili**; VA-API / NVENC / QSV / Vulkan Video solo come accelerazione opzionale |
| Rendering UI | Qt RHI con scelta automatica del backend (Vulkan → OpenGL → OpenGL ES 2 → software) |
| Rendering effetti | Implementazione **CPU di riferimento per ogni effetto e transizione** (MLT/frei0r/codice proprio); percorso GPU opzionale con shader GLSL compatibili con OpenGL 2.1 / GLES 2.0 |
| Audio | MLT + PipeWire (tramite SDL2 o il backend audio di Qt) |
| Build | CMake + Ninja |
| Test | QtTest + CTest; test di rendering con confronto di frame (hash/PSNR) |
| Versionamento | git, commit piccoli e descrittivi (Conventional Commits) |
| Packaging | PKGBUILD per Arch; opzionale AppImage |

**Dipendenze Arch (pacman):**
`qt6-base qt6-declarative qt6-multimedia qt6-shadertools qt6-svg qt6-5compat mlt ffmpeg sdl2 cmake ninja git frei0r-plugins ladspa rubberband`

**Dipendenze per le funzioni AI locali (sezione 5.12)** — da integrare come componenti opzionali, attivabili a runtime:
- `whisper.cpp` → trascrizione e sottotitoli automatici
- `onnxruntime` → rimozione sfondo, segmentazione persona, rilevamento volti
- `rnnoise` o DeepFilterNet → riduzione rumore / isolamento voce
- `aubio` → rilevamento beat
- `piper` → text-to-speech locale
- `rife-ncnn-vulkan` → interpolazione frame (slow motion fluido; richiede Vulkan, con fallback CPU obbligatorio)
- `vid.stab` (via FFmpeg/MLT) → stabilizzazione
- separazione voce/musica: Demucs (esportato in ONNX o porting C++) → estrazione stem
- stima della profondità: Depth Anything V2 Small (ONNX) → foto 3D / effetto parallasse
- segmentazione a un clic: MobileSAM o EfficientSAM (ONNX) → scontorno di oggetti selezionati con un clic
- inpainting: LaMa (ONNX) → rimozione di oggetti
- ricerca per contenuto: CLIP / SigLIP (ONNX) → cerca "spiaggia", "cane", "tramonto" nei propri media
- rilevamento pose e volti: modelli ONNX leggeri (es. YuNet, MoveNet) → sticker che seguono il viso, effetti corpo

Regole sulle dipendenze:
- Prima di integrare un modello AI, **verifica la sua licenza** (codice e pesi) e documentala in `docs/MODELS.md`.
  Solo licenze che permettono la redistribuzione e l'uso nell'app (Apache 2.0, MIT, BSD o simili); in caso di dubbio, chiedi.
- Se ti serve una libreria non elencata, **chiedi prima**, indicando il nome esatto del pacchetto (repo ufficiale o AUR).
- Le funzioni AI non devono mai essere necessarie per compilare o avviare l'app: se il componente manca, la funzione
  appare disattivata con un messaggio che spiega cosa installare.
- I modelli AI (pesi) si scaricano solo su richiesta esplicita dell'utente, da un gestore modelli nelle preferenze,
  con dimensione mostrata prima del download. Mai download silenziosi.

---

## 1bis. Compatibilità con tutte le schede grafiche (vincolante)

L'app deve avviarsi, montare ed esportare video su **qualsiasi** configurazione grafica. Nessuna funzione di base può
dipendere da un vendor, da un'API grafica specifica o dalla presenza di una GPU.

### Configurazioni da supportare
- **NVIDIA**: driver proprietario recente (`nvidia`, `nvidia-open`), rami legacy (470xx, 390xx dall'AUR) e **nouveau**.
- **AMD**: `amdgpu` (RADV/RadeonSI), GPU vecchie con driver `radeon` (r600, solo OpenGL, niente Vulkan).
- **Intel**: Xe/Arc, iGPU recenti (`iris`, ANV) e vecchie (`crocus`, `i915`, OpenGL 2.1–3.x).
- **Grafica ibrida** (laptop Intel/AMD + NVIDIA con PRIME offload): funziona sulla iGPU, usa la dGPU se l'utente lo sceglie.
- **Macchine virtuali e sistemi senza GPU**: virgl, VMware SVGA, `llvmpipe`/`softpipe` (rendering software Mesa).
- **Wayland e X11**, sessioni remote (VNC/RDP).

### Regole
1. **Percorso CPU completo e di riferimento.** Ogni effetto, transizione, filtro, regolazione colore, maschera e
   compositing ha un'implementazione CPU corretta. Il percorso GPU è un'ottimizzazione che deve produrre lo stesso
   risultato (verificato dai test di rendering con tolleranza PSNR).
2. **Rilevamento automatico delle capacità all'avvio** (in un processo separato, così un driver che va in crash non
   blocca l'app): API disponibili (Vulkan, versione OpenGL/GLES), vendor, driver, VRAM, decoder/encoder hardware
   (interrogati tramite VA-API, NVDEC/NVENC, QSV, Vulkan Video). Il risultato si salva in cache e si rivaluta
   quando cambia il driver.
3. **Catena di fallback**, dal migliore al più compatibile, per ciascun sottosistema:
   - UI (Qt RHI): Vulkan → OpenGL 3.3+ → OpenGL ES 2.0 / OpenGL 2.1 → software (`QT_QUICK_BACKEND=software`);
   - effetti: GPU (GLSL compatibile OpenGL 2.1 / GLES 2.0, niente estensioni esclusive di un vendor) → CPU multithread;
   - decodifica: hardware → software (FFmpeg);
   - encoding: NVENC / VA-API / QSV / Vulkan Video → x264 / x265 / SVT-AV1 / libvpx software;
   - AI: CUDA / ROCm / Vulkan (dove il backend lo supporta) → CPU.
4. **Fallback a runtime**: se un'operazione GPU fallisce (errore del driver, memoria esaurita, shader non compilabile),
   l'app passa al percorso CPU per quell'operazione, lo registra nel log e mostra un avviso non bloccante. Mai un crash,
   mai un frame nero.
5. **Modalità sicura**: avvio con `--safe-mode` (e automaticamente dopo due crash consecutivi all'avvio) che disattiva
   ogni accelerazione GPU e usa solo i percorsi software.
6. **Preferenze → Prestazioni**: mostra la GPU rilevata, il backend in uso e le capacità trovate; permette di forzare
   il backend della UI, attivare/disattivare accelerazione effetti, decodifica ed encoding hardware, scegliere la GPU
   su sistemi ibridi. Pulsante "Copia informazioni di sistema" per le segnalazioni di bug.
7. **Nessuna estensione proprietaria obbligatoria**: niente CUDA, OptiX, NVAPI o estensioni GL/Vulkan di un solo vendor
   nei percorsi di base. Il codice specifico di un vendor è ammesso solo dietro il rilevamento delle capacità e con fallback.
8. **Memoria video limitata**: rispetta la VRAM disponibile; con GPU da 1 GB o meno riduci automaticamente la risoluzione
   delle texture di anteprima e la cache GPU.
9. **Prestazioni in modalità software**: su un sistema senza GPU l'app resta usabile in 1080p usando proxy e
   anteprima a qualità ridotta attivati automaticamente.

### Verifica
- Test di rendering eseguiti due volte: con percorso GPU e con percorso CPU forzato; i risultati devono coincidere entro la tolleranza.
- Test di avvio e di export automatico in ambiente headless con `llvmpipe` (`LIBGL_ALWAYS_SOFTWARE=1`) e con `QT_QUICK_BACKEND=software`.
- Documenta in `docs/GPU_COMPATIBILITY.md` la matrice delle configurazioni, cosa è stato provato, e le variabili
  d'ambiente utili per il debug.

---

## 2. Architettura

```
velacut/
├── CMakeLists.txt
├── PKGBUILD
├── README.md
├── docs/
│   ├── ARCHITECTURE.md      # decisioni architetturali, diagrammi, flussi
│   ├── PROGRESS.md          # stato di avanzamento: fatto / in corso / da fare / bug noti
│   ├── FILE_FORMAT.md       # specifica del formato .vproj
│   ├── GPU_COMPATIBILITY.md # matrice GPU/driver, catene di fallback, variabili di debug
│   ├── DESIGN_SYSTEM.md     # token Material 3, schemi colore, componenti, motion
│   ├── USABILITY.md         # scenari dei test di semplicità e risultati per fase
│   ├── MODELS.md            # modelli AI, dimensioni, licenze, fonti
│   └── SHORTCUTS.md
├── src/
│   ├── app/                 # main, bootstrap, gestione finestre, preferenze
│   ├── core/                # modello dati PURO (nessuna dipendenza da MLT o QML)
│   │   ├── project/         # Project, Sequence, Track, Clip, Marker
│   │   ├── effects/         # Effect, Parameter, Keyframe, Easing
│   │   ├── commands/        # QUndoCommand per OGNI operazione
│   │   └── serialization/   # JSON <-> modello, migrazioni di versione
│   ├── engine/              # adattatore MLT: traduce il modello core in un grafo MLT
│   │   ├── playback/        # anteprima, seek, scrubbing
│   │   ├── render/          # export, code di rendering
│   │   ├── proxy/           # generazione proxy in background
│   │   ├── analysis/        # waveform, miniature, beat, scene detection
│   │   └── gpu/             # rilevamento capacità, catene di fallback, percorso GPU/CPU degli effetti
│   ├── theme/               # sistema Material 3: token, schemi colore dinamici, tipografia, forme, motion
│   ├── ai/                  # plugin AI locali, ognuno dietro un'interfaccia comune
│   ├── ui/
│   │   ├── controllers/     # QObject esposti a QML
│   │   ├── models/          # QAbstractItemModel per timeline, media, effetti
│   │   └── qml/             # componenti QML
│   └── assets/              # icone SVG originali, preset, LUT, font di default
├── resources/               # librerie locali: transizioni, effetti, sticker, template
└── tests/
    ├── unit/
    ├── integration/
    └── render/              # progetti di riferimento + frame attesi
```

### Principi architetturali obbligatori
1. **Il `core` è la fonte di verità.** L'engine MLT è una proiezione del modello, ricostruibile in qualsiasi momento.
   Mai leggere lo stato dall'engine per decidere cosa fare.
2. **Ogni modifica passa da un `QUndoCommand`.** Nessuna eccezione: spostamenti, trim, cambi di parametro,
   keyframe, testi. Le modifiche continue (es. trascinare uno slider) vanno fuse in un solo comando (`mergeWith`).
3. **Nessun lavoro pesante sul thread UI.** Decodifica, miniature, waveform, proxy, AI, export: tutto in worker thread
   o processi separati, con progresso e annullamento.
4. **Le funzioni AI sono plugin** che implementano un'interfaccia comune (`IAiTask`: input, parametri, progresso,
   annullamento, output nel modello come normali clip/keyframe/sottotitoli modificabili).
5. **Tempi in frame razionali**, mai in `double` di secondi: usa un tipo `RationalTime` (valore + frame rate)
   per evitare errori di arrotondamento su 23.976/29.97.
6. **Formato progetto versionato** con migrazioni: un progetto salvato con la v1 deve aprirsi nelle versioni future.

---

## 3. Modello dati

- **Project**: impostazioni (risoluzione, fps, sample rate, spazio colore), media pool, sequenze, preset utente.
- **Sequence**: elenco ordinato di tracce, durata, marker, formato (16:9, 9:16, 1:1, 4:5, 21:9, 3:4, personalizzato).
- **Track**: tipo (video, audio, testo, sticker, effetto, filtro/regolazione), lock, mute, solo, nascosta, altezza.
- **Clip**: riferimento al media, in/out sorgente, posizione in timeline, velocità (costante o curva), volume,
  trasformazioni, lista di effetti, animazioni, maschere, keyframe, collegamento audio/video.
- **Effect / Parameter / Keyframe**: ogni parametro animabile ha una lista di keyframe con interpolazione
  (lineare, bezier, hold) e preset di easing.
- **Compound clip**: una sequenza annidata usabile come clip singola.
- **Media**: percorso, hash, metadati (codec, durata, fps, risoluzione, rotazione, HDR), stato del proxy, analisi cache.

Documenta il formato completo in `docs/FILE_FORMAT.md` prima di implementare la serializzazione.

---

## 4. Interfaccia utente

### Layout (come CapCut Desktop, ma con design originale)
- **Barra superiore**: menu, nome progetto (modificabile con un clic), indicatore "Salvato", ricerca universale,
  pulsante Esporta ben visibile.
- **Pannello sinistro a schede**: Media · Audio · Testo · Sticker · Effetti · Transizioni · Filtri · Regolazioni · Template ·
  Sottotitoli · Kit del marchio. Ogni scheda ha categorie a chip, ricerca, Preferiti e Recenti.
- **Centro**: anteprima con controlli di riproduzione, **selettore del formato** (16:9, 9:16, 1:1, 4:5…) sotto il player,
  zoom dell'anteprima, guide, maniglie di trasformazione dirette sul video, schermo intero.
- **Pannello destro**: proprietà contestuali della selezione (Video · Audio · Velocità · Animazione · Regolazione · AI),
  con controlli essenziali in alto, "Avanzate" richiudibili e indicatori keyframe accanto a ogni parametro animabile.
- **Barra strumenti contestuale** tra player e timeline (sezione 0bis, regola 3), più i comandi globali: annulla/ripeti,
  magnete traccia principale, snapping, asse di anteprima, collegamento audio/video, zoom timeline.
- **Timeline in basso**: traccia principale magnetica, tracce sovrapposte create automaticamente, righello, playhead,
  zoom, forme d'onda, miniature, icone di transizione tra le clip, pulsante "Copertina" all'inizio della traccia principale.
- **Ridimensionamento**: tutti i pannelli si ridimensionano e si comprimono; layout ricordato per progetto.

### Stile: Material You (Material Design 3) — vincolante
L'intera interfaccia segue **Material Design 3** (m3.material.io), adattato a un'app desktop di editing.

**Sistema di design a token**
- Nessun colore, dimensione, raggio o durata di animazione scritti a mano nei componenti QML: tutto passa da un singleton
  `Theme` (in `src/theme/`) che espone i token Material 3.
- **Ruoli colore M3 completi**: primary, onPrimary, primaryContainer, onPrimaryContainer, secondary…, tertiary…,
  error…, surface, onSurface, surfaceVariant, onSurfaceVariant, surfaceContainerLowest/Low/–/High/Highest,
  surfaceDim, surfaceBright, outline, outlineVariant, inverseSurface, inversePrimary, scrim, shadow.
- **Tipografia M3**: scala completa (display, headline, title, body, label; ciascuno large/medium/small),
  font predefinito con buona leggibilità (es. Roboto Flex o Inter, inclusi nell'app), dimensioni in punti indipendenti dal DPI.
- **Forme M3**: token di raggio (none, extra-small 4, small 8, medium 12, large 16, extra-large 28, full).
- **Elevazione M3**: livelli 0–5 resi principalmente con colori di superficie tonali, ombre leggere solo dove servono.
- **Stati di interazione M3**: state layer per hover (8%), focus (10%), pressed (10%), dragged (16%); ripple sui pulsanti.

**Colori dinamici (il cuore di Material You)**
- Schema generato con material-color-utilities (spazio colore HCT, varianti TonalSpot, Vibrant, Expressive, Neutral,
  Fidelity, Content, Monochrome) a partire da un **colore seme**.
- Sorgenti del colore seme, a scelta dell'utente:
  1. **colore d'accento del sistema** letto tramite `xdg-desktop-portal` (impostazione `org.freedesktop.appearance`
     `accent-color`, supportata da KDE Plasma e GNOME), aggiornato in tempo reale se l'utente lo cambia;
  2. **sfondo del desktop** (colore dominante estratto con il quantizzatore di material-color-utilities), dove leggibile;
  3. **copertina del progetto corrente** (miniatura del primo frame o immagine scelta);
  4. colore scelto a mano, con anteprima dal vivo;
  5. palette di fallback predefinita.
- Tema **chiaro, scuro e automatico** (segue `color-scheme` del portale); tema scuro predefinito, come si addice a un editor video.
- Livelli di contrasto M3: standard, medio, alto.
- Il cambio di schema avviene con transizione animata dei colori, senza riavvio.
- L'anteprima video e le miniature non vengono mai tinte dal tema: il colore del video resta fedele.

**Componenti M3 da usare** (stile Material di Qt Quick Controls dove esiste, altrimenti componenti propri fedeli alla specifica M3)
- **Navigation rail** per le schede del pannello sinistro (Media, Audio, Testo, Sticker, Effetti, Transizioni, Filtri,
  Regolazioni, Template, Sottotitoli), con indicatore attivo a pillola e icone.
- **Top app bar** con nome progetto, formato e azioni; il pulsante **Esporta** è un **FAB esteso** o un filled button prominente.
- Pulsanti: filled, filled tonal, outlined, text, elevated, icon button (standard, filled, tonal, outlined); **segmented button**
  per scelte esclusive (es. formato 16:9 / 9:16 / 1:1).
- **Card** per gli elementi delle librerie (effetti, transizioni, template), con anteprima animata al passaggio del mouse.
- **Chip** (filter, assist, input) per categorie e filtri di ricerca; **search bar** M3 nelle librerie.
- **Slider** M3 con value indicator; **switch**, checkbox, radio M3.
- Text field **filled** e **outlined** con label, supporting text ed errori.
- **Dialog**, **bottom sheet/side sheet**, **menu**, **tooltip** (plain e rich), **snackbar** per notifiche e annullamento
  ("Clip eliminata — Annulla"), **progress indicator** lineari e circolari M3, **badge**.
- La timeline resta un componente specializzato, ma usa i token del tema (colori di superficie per le tracce, primary
  per selezione e playhead, colori tonali per distinguere i tipi di clip).

**Motion M3**
- Curve di easing M3: emphasized, emphasized decelerate/accelerate, standard, standard decelerate/accelerate.
- Durate dai token M3 (short 50–200 ms, medium 250–400 ms, long 450–600 ms).
- Transizioni tra pannelli con i pattern M3 (container transform, shared axis, fade through).
- Rispetta "riduci animazioni" del sistema (via portale) disattivando le animazioni non essenziali.

**Adattamento al desktop**
- Layout responsivo con le classi di finestra M3 (compact < 600, medium 600–839, expanded 840–1199, large 1200–1599,
  extra-large ≥ 1600 dp): sotto una certa larghezza i pannelli laterali diventano sheet a scomparsa.
- Densità regolabile (predefinita / compatta), perché un editor ha molti controlli.
- Accessibilità M3: contrasto minimo 4.5:1 per il testo e 3:1 per i componenti, target cliccabili adeguati,
  navigazione completa da tastiera con focus visibile, nomi accessibili per screen reader (Qt Accessibility / AT-SPI),
  nessuna informazione trasmessa solo dal colore.
- **Icone**: Material Symbols (licenza Apache 2.0) in versione variabile (outlined/rounded, riempimento per lo stato attivo),
  incluse nell'app come font o SVG.
- Il tema deve funzionare anche con il backend di rendering software (sezione 1bis): niente effetti che richiedono shader
  per essere leggibili; ombre e sfocature decorative si disattivano automaticamente in modalità software.

Documenta token, schemi e componenti in `docs/DESIGN_SYSTEM.md`, con una pagina di galleria dei componenti
(`velacut --component-gallery`) per verificarli tutti in chiaro, scuro e alto contrasto.

### Requisiti UX
- Supporto HiDPI e scaling frazionario su Wayland.
- Drag & drop ovunque: da file manager al media pool, dal media pool alla timeline, da un pannello effetti alla clip.
- Anteprima al passaggio del mouse per effetti, transizioni, filtri, animazioni e testi.
- Tooltip su ogni controllo, con scorciatoia da tastiera.
- Scorciatoie completamente personalizzabili; preset "stile CapCut" e "stile Premiere".
- Lingue: italiano e inglese dall'inizio (Qt Linguist, nessuna stringa hardcoded).
- Schermata iniziale con un grande pulsante "Nuovo progetto" (nessuna domanda: si apre subito l'editor), bozze con
  miniatura, e accesso rapido a Montaggio automatico, Da copione a video, Slideshow, Registra schermo e Template.
- Messaggi di errore comprensibili, mai crash silenziosi.

---

## 5. Funzionalità complete (parità con CapCut Desktop)

### 5.1 Progetto e media
- Bozze con **salvataggio continuo** (sezione 0bis, regola 2), cronologia delle versioni del progetto (ripristina una
  versione di 10 minuti fa, di ieri…), **recupero dopo crash**; salva con nome, apri da file e duplica dal menu.
- Canvas e frame rate impostati automaticamente dalla prima clip; cambiabili in qualsiasi momento.
- Import: video (MP4, MOV, MKV, WebM, AVI, MTS/M2TS, MXF), audio (MP3, WAV, FLAC, AAC, OGG, OPUS, M4A),
  immagini (PNG, JPG, WebP, GIF animate, SVG, HEIC se disponibile), sequenze di immagini, sottotitoli (SRT, VTT, ASS).
- Supporto a video verticali, rotazione da metadati, frame rate variabile (conversione a costante in proxy), HDR (tone mapping in anteprima).
- Media pool con miniature, filtri per tipo, ricerca, ordinamento, cartelle, preferiti, indicatore "usato in timeline",
  skimming al passaggio del mouse, selezione di un intervallo (in/out) prima di aggiungere alla timeline.
- **Ricerca per contenuto** (modulo AI opzionale): scrivi "spiaggia", "cane", "persona che ride" e trovi i punti dei tuoi
  video e le foto corrispondenti; l'indicizzazione avviene in background.
- Ricollegamento media mancanti (per nome e hash), consolidamento del progetto in una cartella con tutti i media.
- Proxy automatici per 4K/HEVC/10-bit con interruttore globale "anteprima a qualità ridotta".
- Registrazione voce fuori campo direttamente in timeline.
- Cattura dello schermo (via PipeWire/xdg-desktop-portal) e webcam come sorgenti, anche insieme (webcam in un riquadro
  sopra lo schermo), con **zoom automatico** sulle zone dove avvengono i clic e evidenziazione del cursore.
- **Teleprompter** durante la registrazione con webcam o voce: testo scorrevole con velocità regolabile, specchiabile.
- Import dal telefono: cartella condivisa o dispositivo MTP rilevato, con anteprima e import selettivo.

### 5.2 Timeline ed editing
- Traccia principale **magnetica** (le clip si compattano automaticamente, disattivabile) + tracce libere sovrapposte illimitate.
- Selezione singola, multipla, a rettangolo; selezione di tutto a destra/sinistra del playhead.
- Split (S), elimina (Canc), elimina con ripple, **taglia a sinistra/destra del playhead** (Q/W).
- Trim dai bordi, ripple trim, roll, slip, slide.
- Copia/incolla clip, **copia/incolla attributi** (effetti, filtri, regolazioni, animazioni) tra clip.
- Duplica, sostituisci clip mantenendo effetti e durata, congela fotogramma, inverti clip.
- Raggruppa/separa, compound clip (crea, apri, scomponi).
- Scollega/collega audio e video; estrai audio.
- Snapping su bordi, playhead, marker e beat; attivabile/disattivabile al volo.
- Marker su timeline e su clip, con colore, nome e note; navigazione tra marker.
- Zoom timeline (Ctrl+rotella, adatta alla finestra), scroll fluido, miniature e waveform adattive allo zoom.
- Blocco, mute, solo, nascondi traccia; altezza traccia regolabile.
- Rilevamento automatico delle scene con split automatico.
- **Rimozione automatica delle pause/silenzi** dal parlato (soglia regolabile, anteprima prima di applicare).
- Adattamento automatico della durata di una traccia audio al video.
- **Editing basato sul testo**: dalla trascrizione del parlato, selezioni e cancelli parole o frasi nel testo e il video
  viene tagliato di conseguenza; puoi anche riordinare frasi trascinandole.
- **Rimozione delle parole di riempimento** ("ehm", "uhm", "cioè", "tipo", "allora", "um", "like"…, lista modificabile)
  e delle ripetizioni, con anteprima dei tagli e scelta di cosa tenere.
- **Multicamera**: sincronizzazione di più clip tramite l'audio (o timecode), vista multi-angolo e cambio di inquadratura
  in tempo reale durante la riproduzione con i tasti numerici.
- Sincronizzazione automatica di un audio esterno (microfono separato) con il video tramite forma d'onda.
- Sostituzione rapida: trascina una clip dal pannello media sopra una clip in timeline tenendo premuto Alt per sostituirla
  mantenendo durata, effetti e animazioni.
- Undo/redo illimitato con cronologia visibile.

### 5.3 Anteprima e riproduzione
- Play/pausa, J/K/L con velocità multiple, frame per frame, vai a inizio/fine, loop di una regione.
- Scrubbing audio durante il trascinamento del playhead.
- Anteprima a schermo intero, qualità anteprima (piena, 1/2, 1/4), indicatore di frame persi.
- Manipolazione diretta sul canvas: sposta, scala, ruota, ritaglia con maniglie; guide di allineamento e snapping al centro/bordi.
- Area di sicurezza per social (overlay TikTok/Reels/Shorts attivabile).
- Confronto prima/dopo per filtri e regolazioni.

### 5.4 Trasformazioni e composizione
- Posizione, scala (uniforme e non), rotazione, opacità, ritaglio, specchia orizzontale/verticale.
- **Sfondo del canvas**: colore, sfocatura della clip stessa, immagine, pattern.
- Modalità di fusione: normale, schiarisci, scherma, moltiplica, sovrapponi, luce soffusa, luce intensa, differenza, scurisci, colore, luminosità, ecc.
- **Maschere**: lineare, specchio, cerchio, rettangolo (con angoli arrotondati), cuore, stella, disegno libero (bezier);
  sfumatura, inversione, animabili con keyframe.
- **Chroma key** con selettore colore, intensità, bordo, rimozione dello spill.
- Picture-in-picture, schermo diviso con layout predefiniti.
- Adattamento automatico al formato (**auto reframe**): segue il soggetto quando si passa da 16:9 a 9:16.
- **Motion tracking**: traccia un'area e aggancia testo/sticker/effetti al movimento.
- Stabilizzazione con livelli di intensità.
- **Scontorno a un clic**: clicchi su un oggetto o una persona e viene isolato (segmentazione tipo SAM), con pennello
  per aggiungere/togliere e propagazione automatica sui fotogrammi successivi; contorno luminoso o bordo applicabili.
- **Foto 3D**: trasforma una foto in un movimento di camera con parallasse usando la stima della profondità.
- **Rimozione oggetti** (inpainting) per foto e riprese con camera fissa; per riprese in movimento, dichiara i limiti
  e proponi l'alternativa migliore invece di produrre un risultato scadente.
- Effetti di movimento: **motion blur** regolabile, **camera shake**, zoom lento automatico su foto (Ken Burns).

### 5.5 Velocità
- Velocità costante 0.1x–100x, con mantenimento dell'intonazione audio (rubberband).
- **Curve di velocità** con preset (montaggio, eroe, proiettile, salto, flash in, flash out) e editor della curva personalizzabile.
- **Slow motion fluido** tramite interpolazione dei fotogrammi: RIFE se installato e con Vulkan disponibile; altrimenti
  interpolazione a flusso ottico su CPU (`minterpolate` di FFmpeg); come ultima risorsa frame blending.
- Clip al contrario, freeze frame.

### 5.6 Animazioni e keyframe
- Keyframe su ogni parametro numerico: trasformazioni, opacità, volume, filtri, regolazioni, maschere, effetti.
- Editor dei keyframe in timeline e nel pannello proprietà; editor delle curve con preset di easing.
- **Animazioni predefinite** per clip, testi e sticker: di ingresso, di uscita e combinate (almeno 30 per categoria),
  con durata regolabile e anteprima.

### 5.7 Testo
- Testo base con font di sistema e font importati, dimensione, colore, gradiente, contorno, ombra, sfondo, spaziatura, interlinea, allineamento, grassetto/corsivo/sottolineato.
- **Preset di stile testo** (almeno 40) e **effetti testo** (neon, glitch, 3D, contorno doppio, ecc.).
- Animazioni testo di ingresso/uscita/loop, incluse animazioni per lettera e per parola (macchina da scrivere, rimbalzo, onda...).
- Testo curvo e su percorso; testo in box a capo automatico.
- **Text-to-speech** locale (Piper) con più voci italiane e inglesi, velocità e tono regolabili.
- Titoli e terzi inferiori (lower third) predefiniti.
- **Template di testo animati** (almeno 50): titoli, citazioni, "iscriviti", call to action, liste, date, luoghi.
- **Nuvolette e fumetti**, callout con freccia che punta a un oggetto (agganciabile al motion tracking).
- **Elementi grafici animati**: contatori numerici, timer e conto alla rovescia, barra di avanzamento del video,
  frecce e cerchi disegnati a mano, evidenziatore.
- Modifica del testo direttamente sul canvas (doppio clic sul testo nel player) oltre che nel pannello.
- Emoji a colori nei testi (font emoji di sistema).

### 5.8 Sottotitoli
- **Sottotitoli automatici** da parlato (whisper.cpp), con scelta della lingua e del modello; supporto a più lingue.
- Sottotitoli da testo (incolla un copione e allinealo all'audio).
- **Riconoscimento testi delle canzoni** in formato karaoke (evidenziazione parola per parola).
- Editor dei sottotitoli: modifica testo, dividi/unisci righe, sposta tempi, trova e sostituisci, stile globale e per riga.
- Evidenziazione automatica delle parole chiave, emoji automatiche opzionali.
- Sottotitoli bilingue (traduzione locale solo se è installato un modello di traduzione; altrimenti funzione disattivata).
- Import/export SRT, VTT, ASS; sottotitoli "impressi" nel video o come traccia separata all'export.
- **Stili di sottotitoli social** (almeno 30): parola per parola con la parola attiva evidenziata o ingrandita,
  "pop" a ritmo, blocchi colorati, stile karaoke; numero massimo di parole per riga regolabile; posizione sicura
  rispetto alle interfacce di TikTok/Reels/Shorts.
- **Capitoli automatici** per YouTube: dalla trascrizione, proposta di capitoli con titolo e timestamp, copiabili e
  trasformabili in marker.

### 5.9 Audio
- Volume per clip e per traccia, fade in/out, keyframe del volume, normalizzazione del loudness (LUFS, target per social).
- Mixer con VU meter per traccia e master.
- **Riduzione del rumore** e **isolamento della voce** (RNNoise/DeepFilterNet).
- **Ducking automatico**: abbassa la musica quando c'è il parlato.
- **Rilevamento beat** (aubio) con marker automatici e snapping; montaggio automatico sul ritmo.
- **Modificatore di voce** (robot, chipmunk, profonda, eco, radio, megafono...) ed effetti audio (EQ, compressore, riverbero, delay, pitch).
- Separazione audio da video, estrazione audio da file video del media pool.
- Libreria locale di musica ed effetti sonori (cartella di asset + import dell'utente), con anteprima e ricerca.
- Registrazione voce fuori campo con conto alla rovescia e monitor del livello.
- **Separazione voce/musica** (Demucs): da una clip ottieni tracce separate di voce, musica, batteria, basso, altro.
- **Migliora voce** a un clic: riduzione rumore + EQ per il parlato + compressione + normalizzazione.
- **Visualizzatori audio**: spettro, onda, cerchio pulsante, barre, reattivi alla musica, con colori del kit del marchio.
- **Effetti che reagiscono al ritmo**: zoom, flash, scossa e cambio filtro sincronizzati ai beat rilevati.
- Musica di sottofondo che si adatta da sola alla durata del video (taglio sul beat con dissolvenza finale naturale).
- Effetti sonori con ricerca per parola (whoosh, clic, applauso, notifica…) e **suono automatico sulle transizioni** (opzionale).

### 5.10 Colore, filtri e regolazioni
- **Filtri** preimpostati a categorie (almeno 60), intensità regolabile, anteprima al passaggio del mouse.
- **Regolazioni base**: esposizione, luminosità, contrasto, alte luci, ombre, bianchi, neri, saturazione, vividezza, temperatura, tinta, nitidezza, chiarezza, vignettatura, grana.
- **HSL** per 8 gamme di colore; **curve** RGB e per canale; **ruote colore** (ombre, mezzitoni, alte luci).
- **LUT** .cube importabili, con intensità.
- **Livello di regolazione** (adjustment layer) su traccia dedicata, che influenza tutte le tracce sotto.
- Bilanciamento del bianco automatico e correzione automatica del colore.
- **Abbina colore**: scegli una clip di riferimento e le altre ne copiano l'aspetto con un clic.
- **Rimozione dello sfarfallio** (deflicker) per luci artificiali e timelapse.
- Scope: istogramma, forma d'onda, vettorscopio (in un pannello opzionale).
- Salvataggio dei preset di regolazione dell'utente.

### 5.11 Effetti video
- **Effetti video** a categorie (almeno 80): glitch, sfocature, bagliore, particelle, retro/VHS, zoom, scossa, flash, strobo, specchio, caleidoscopio, pixel, luci, bordi, vignette animate...
- **Effetti corpo/persona** che usano la segmentazione (contorno luminoso, ombra, clone, sfondo sostituito) — attivi solo con il modulo AI installato.
- **Sticker ed effetti che seguono il viso** (occhiali, cappelli, emoji sul volto, sfocatura automatica dei volti per la privacy).
- Effetti applicabili alla singola clip o come traccia effetto su un intervallo di tempo.
- Implementazione: preferisci filtri MLT/frei0r esistenti; per il resto definisci gli effetti in un **formato di effetto
  documentato** (manifest JSON + implementazione CPU + shader GLSL opzionale compatibile OpenGL 2.1 / GLES 2.0),
  così che nuovi effetti si aggiungano come file in `resources/` senza ricompilare. Il percorso CPU è obbligatorio (sezione 1bis).

### 5.11bis Transizioni (area prioritaria)
Le transizioni sono una delle cose che rendono CapCut immediato: devono essere tante, belle, veloci da applicare e
funzionare su qualsiasi scheda grafica.

**Libreria (almeno 100 transizioni, in categorie):**
- **Base**: dissolvenza incrociata, dissolvenza al nero, al bianco, a un colore, dissolvenza additiva, dissolvenza per luminanza (luma wipe con immagini maschera).
- **Movimento**: slide e push nelle 4 direzioni e diagonali, whip pan (panoramica a frusta con motion blur), zoom in/out, zoom attraverso, rotazione, spin, rimbalzo, scorrimento a pagina.
- **Wipe e forme**: wipe lineari e diagonali, a orologio, iris cerchio/rombo/stella/cuore, a bande, a veneziana, a scacchiera, a mosaico, sipario, spaccatura centrale.
- **Camera**: flash della fotocamera, otturatore, messa a fuoco/sfocatura, lens flare, esposizione, dolly zoom.
- **Glitch e digitali**: glitch RGB, schermo rotto, pixel/mosaico, onda digitale, disturbo TV, VHS, interferenza, dati corrotti.
- **Luce e colore**: fuga di luce (light leak), bagliore, bruciatura della pellicola, arcobaleno, inversione colori, saturazione esplosiva.
- **Distorsioni**: ondulazione, liquido, vortice, onda d'urto, rifrazione, lente, fumo, inchiostro.
- **3D**: cubo, flip, porta, pagina che si gira, carosello, piegatura, rotazione 3D di piano (con rendering CPU in proiezione prospettica se manca la GPU).
- **Mascherate e creative**: transizione con maschera che segue una forma animata, transizione con pennellata, strappo della carta, split screen animato, transizioni a tema (social, vlog, sport, gaming).
- **Match cut assistiti**: transizione che allinea automaticamente il soggetto tra due clip (usando il tracking, se disponibile).

**Comportamento in timeline (come CapCut):**
- Icona di transizione tra ogni coppia di clip adiacenti; clic per aprire la libreria, trascinamento dalla libreria sul punto di taglio.
- **Anteprima al passaggio del mouse** nella libreria (miniatura animata generata in locale, con cache) e anteprima dal vivo nel player.
- Durata regolabile trascinando i bordi della transizione o da slider (da 0.1 s alla durata massima consentita dalle clip).
- **Gestione automatica dei fotogrammi di sovrapposizione**: se le clip non hanno abbastanza materiale oltre i punti di
  taglio, offri le opzioni sovrapposizione / estensione con freeze frame / accorciamento, con avviso chiaro.
- **Applica a tutte** le giunzioni della traccia, **transizione casuale**, **rimuovi tutte**.
- Parametri per transizione: direzione, easing (con i preset di curva della sezione 5.6), colore, morbidezza del bordo,
  intensità, audio crossfade collegato (attivabile).
- Transizioni **audio** dedicate: crossfade a potenza costante, a guadagno costante, taglio con fade veloce.
- **Preferiti** e **usati di recente** nella libreria; ricerca per nome e categoria con chip M3.
- Transizioni **personalizzate** salvabili come preset e importabili come pacchetti (stesso formato degli effetti).

**Requisiti tecnici:**
- Ogni transizione ha implementazione CPU e, opzionalmente, GPU, con risultato identico entro tolleranza (test di rendering su ciascuna).
- Le transizioni si possono usare anche su tracce sovrapposte (ingresso/uscita di una clip sopra un'altra), non solo sulla traccia principale.
- Riproduzione in tempo reale delle transizioni base anche in modalità software a risoluzione di anteprima ridotta.
- Le miniature animate della libreria si generano in background al primo avvio e si aggiornano quando si aggiungono pacchetti.

### 5.12 Funzioni AI (tutte locali)
- Sottotitoli automatici e riconoscimento testi canzoni (whisper.cpp).
- **Rimozione dello sfondo** senza green screen (segmentazione persona via ONNX Runtime), con affinamento dei bordi.
- **Auto reframe** con tracciamento del soggetto.
- Riduzione rumore / isolamento voce.
- Rilevamento beat, scene detection, rimozione silenzi.
- Text-to-speech (Piper).
- Slow motion con interpolazione (RIFE).
- **Ritocco viso** (levigatura pelle, schiarimento, occhi) basato su rilevamento volti, intensità regolabile.
- **Miglioramento qualità video**: denoise e upscale (con modello opzionale), correzione automatica di luce e colore.
- **Taglio automatico dei momenti salienti** da un video lungo (basato su audio, scene e parlato), con risultato modificabile.
- **Da video lungo a clip brevi**: da un podcast o una diretta propone 3–10 clip verticali con reframe, sottotitoli
  animati e titolo, ognuna aperta come progetto modificabile.
- Separazione voce/musica, scontorno a un clic, foto 3D, rimozione oggetti, ricerca per contenuto, sticker che seguono
  il viso, editing basato sul testo e rimozione delle parole di riempimento (descritti nelle sezioni relative).
- Ogni funzione AI: stima del tempo, barra di progresso, annullamento, risultato **sempre modificabile** (mai un video "cotto" irreversibile).
- Uso della GPU quando disponibile (Vulkan/CUDA/ROCm tramite i rispettivi backend), **fallback obbligatorio su CPU**
  per ogni funzione AI; con GPU senza Vulkan (es. driver `radeon`, nouveau vecchi) si usa la CPU senza errori.
- RIFE (ncnn-vulkan) richiede Vulkan: senza Vulkan lo slow motion fluido ripiega su interpolazione a flusso ottico CPU
  (filtro `minterpolate` di FFmpeg) e poi su frame blending.

### 5.13 Sticker, template e asset
- Sticker da libreria locale (PNG/APNG/WebP/GIF/Lottie se possibile), emoji, forme vettoriali animabili.
- Import di sticker e sovrapposizioni dell'utente.
- **Template di progetto**: un template è un progetto con segnaposto sostituibili (media, testi); l'utente sceglie un template,
  sostituisce i media e ottiene il video. Salvataggio dei propri progetti come template.
- Gestore degli asset: installa/rimuovi pacchetti di asset (effetti, transizioni, LUT, sticker, template) da cartelle o archivi .zip.

### 5.13bis Creazione rapida (video pronti in pochi clic)
Strumenti accessibili dalla schermata iniziale e dal menu "Crea". Ognuno produce un **normale progetto modificabile**,
non un video finito e bloccato.
- **Montaggio automatico**: selezioni clip, foto e (facoltativo) una musica; scegli uno stile (vlog, viaggio, sport,
  cinematico, festa, meme, prodotto) e la durata (15 s, 30 s, 60 s, libera). L'app sceglie i momenti migliori
  (movimento, volti, qualità, niente fotogrammi mossi o bui), li monta sul ritmo, aggiunge transizioni, filtro e titolo.
  Pulsante "Rimescola" per ottenere un'altra versione.
- **Da copione a video**: incolli un testo; l'app lo divide in scene, genera la voce (TTS locale), crea sottotitoli animati,
  propone per ogni scena i media dell'utente più adatti (ricerca per contenuto) o un segnaposto colorato con testo,
  aggiunge musica con ducking.
- **Slideshow dalle foto**: selezioni le foto, scegli musica e stile; movimento Ken Burns o foto 3D, cambi sul ritmo.
- **Da video lungo a clip brevi** (sezione 5.12).
- **Registra**: schermo, webcam, schermo + webcam, solo voce, con teleprompter (sezione 5.1).
- **Template** (sezione 5.13).

### 5.13ter Copertina e kit del marchio
- **Copertina del video**: scegli un fotogramma o un'immagine, aggiungi testi, sticker e filtri con gli stessi strumenti
  dell'editor. Viene esportata come immagine (JPG/PNG, anche in 1280×720 per YouTube) e incorporata come copertina nel file MP4.
- **Kit del marchio**: loghi, palette di colori, font, stili di testo, intro e outro, watermark e musiche preferite salvati
  una volta e applicabili a qualsiasi progetto con un clic; più kit (es. canale personale e lavoro).
  I colori del kit compaiono per primi in ogni selettore di colore.

### 5.14 Sostituti locali delle funzioni cloud di CapCut
- Spazio cloud → consolidamento del progetto in una cartella portatile + esporta/importa progetto come archivio `.vpack`.
- Libreria stock online → libreria locale di asset + import rapido dalla cartella dell'utente.
- Template della community → template locali e pacchetti importabili.
- Collaborazione → nessuna; il formato del progetto è JSON leggibile e compatibile con git.

### 5.15 Esportazione
- Formati: MP4 (H.264, H.265), MOV (ProRes via FFmpeg), WebM (VP9, AV1), GIF, sequenza di immagini, solo audio (MP3, WAV, AAC, FLAC).
- Risoluzione da 480p a 4K (e personalizzata), fps (24, 25, 30, 50, 60 e personalizzati), bitrate (consigliato/qualità/personalizzato, CBR/VBR).
- **Encoding hardware** VA-API (AMD/Intel), NVENC (NVIDIA), QSV (Intel) e Vulkan Video dove supportato, con rilevamento
  automatico dei codec e profili realmente supportati dalla scheda e **fallback software** (x264, x265, SVT-AV1, libvpx)
  sempre disponibile. Se l'encoder hardware fallisce a metà export, l'export riparte in software e l'utente viene avvisato.
- Preset per piattaforme: YouTube, TikTok, Instagram Reels/Post, YouTube Shorts, X; con dimensione file stimata.
- Esporta solo una regione (tra punti in/out), esporta fotogramma corrente come immagine, esporta sottotitoli separati.
- Coda di rendering con più export in sequenza; rendering in background senza bloccare l'editing.
- Verifica automatica dello spazio su disco prima dell'export.
- **Finestra di export a una schermata**: nome file, cartella, risoluzione, fps e qualità (Bassa / Consigliata / Alta)
  in vista semplice; tutto il resto in "Avanzate". Mostra durata e dimensione stimate prima di iniziare.
- **Esporta in più formati insieme**: dallo stesso progetto 16:9, 9:16 e 1:1 in un solo passaggio, usando l'auto reframe;
  ogni versione si può rivedere e ritoccare prima dell'export.
- **Dimensione massima del file**: scegli "sotto 16 / 25 / 50 / 100 MB" (o un valore a piacere) e l'app calcola
  risoluzione e bitrate per rispettarla (encoding a due passaggi), con preset per WhatsApp, Telegram, Discord ed email.
- A fine export: pulsanti "Apri cartella", "Riproduci", "Copia percorso", e una notifica di sistema se l'app è in secondo piano.
- Export veloce senza ricodifica quando possibile (solo tagli su clip con lo stesso codec).

### 5.16 Preferenze e sistema
- Cartelle predefinite, cache (dimensione, pulizia), proxy, qualità anteprima, accelerazione hardware, lingua, tema, scorciatoie, gestore modelli AI e asset.
- Log su file in `~/.local/state/velacut/` con livello configurabile; rispetto delle specifiche XDG per config, cache e dati.
- Integrazione desktop: file `.desktop`, icona, associazione del tipo MIME `.vproj`, apertura file da riga di comando.

---

## 6. Requisiti non funzionali

- **Prestazioni** (su un PC di fascia media, 1080p30 H.264):
  - avvio dell'app < 2 s;
  - riproduzione in tempo reale di 3 tracce video con filtri base senza frame persi (con proxy se necessario);
  - seek < 150 ms; scrubbing fluido;
  - miniature e waveform generate in background senza rallentare l'interfaccia;
  - progetti con 500+ clip ancora reattivi.
- **Stabilità**: nessun crash su file corrotti o codec non supportati (messaggio chiaro); salvataggio continuo atomico
  (scrittura su file temporaneo + rinomina, mai un progetto corrotto se l'app si chiude a metà); file di recupero dopo crash.
- **Reattività percepita**: ogni clic ha una risposta visiva entro 100 ms; aprire una bozza < 1 s per progetti tipici;
  l'anteprima dal vivo di filtri ed effetti al passaggio del mouse appare entro 150 ms.
- **Memoria**: cache di frame e miniature con limite configurabile.
- **Compatibilità**: Wayland e X11, qualsiasi GPU o nessuna GPU secondo la sezione 1bis, PipeWire e PulseAudio.
  I requisiti di prestazioni sopra valgono con accelerazione; in modalità software l'app deve restare usabile con proxy.
- **Licenze**: rispetta le licenze di MLT (LGPL), FFmpeg e delle librerie AI; documentale in `README.md`. Nessun asset o codice copiato da CapCut.

---

## 7. Qualità del codice e test

- Warning del compilatore trattati come errori (`-Wall -Wextra -Wpedantic -Werror`), sanitizer (ASan/UBSan) nella build di debug.
- `clang-format` e `clang-tidy` configurati nel repository.
- Test unitari per: modello core, ogni QUndoCommand (do/undo/redo), serializzazione e migrazioni, RationalTime, keyframe/easing.
- Test di integrazione: apertura progetto → modifica → salvataggio → riapertura identica.
- Test di rendering: progetti di riferimento in `tests/render/` esportati e confrontati con frame attesi.
- Ogni bug corretto riceve un test che lo riproduce.

---

## 8. Fasi di sviluppo

Ogni fase ha un **criterio di completamento**. Non passare alla fase successiva finché il criterio non è verificato e io non ho dato il via.
Ogni fase, dalla 1 in poi, deve inoltre superare i **test di semplicità** applicabili della sezione 0bis, con il conteggio
delle azioni riportato in `docs/USABILITY.md`.

| Fase | Contenuto | Criterio di completamento |
|---|---|---|
| **0 — Fondamenta** | Scheletro CMake, struttura cartelle, core con RationalTime, Project/Track/Clip, undo stack, serializzazione, test, integrazione MLT minima (apri un file e riproducilo in una finestra QML), **rilevamento capacità GPU con catene di fallback e `--safe-mode`**, **sistema di tema Material 3** (token, colori dinamici, galleria componenti) | Compila senza warning, test verdi, un video si riproduce nell'anteprima sia con GPU sia con `QT_QUICK_BACKEND=software` + `LIBGL_ALWAYS_SOFTWARE=1`; la galleria componenti mostra il tema M3 in chiaro/scuro con colore seme dal sistema |
| **1 — MVP editor** | Schermata iniziale con bozze, nuovo progetto istantaneo con canvas dalla prima clip, salvataggio continuo, media pool con skimming e "+", timeline con traccia magnetica e tracce automatiche, trim/split/ripple, snapping, barra strumenti contestuale, asse di anteprima, player J/K/L, snackbar "Annulla", export MP4 a una schermata | Importo 3 clip, le taglio e riordino, esporto un MP4 corretto, chiudo e riapro la bozza identica senza aver mai premuto "Salva"; i primi 2 test di semplicità sono rispettati |
| **2 — Editing essenziale** | Trasformazioni con maniglie sul canvas, sfondo canvas, selettore del formato, testo base e preset con modifica sul canvas, audio base (volume, fade, mixer), velocità costante, freeze/reverse, transizioni base, filtri e regolazioni base con anteprima dal vivo e "Applica a tutte", copia/incolla attributi, "Migliora automaticamente", ricerca universale Ctrl+K | Realizzo un video verticale 9:16 con testi, musica, transizioni e filtri rispettando i test di semplicità applicabili |
| **3 — Keyframe e composizione** | Keyframe su tutti i parametri, editor curve, animazioni predefinite, maschere, blend mode, chroma key, compound clip, livello di regolazione, marker | Animo un titolo con keyframe ed easing e compongo un green screen con maschera |
| **4 — Colore e audio avanzati** | HSL, curve, ruote colore, LUT, scope, abbina colore, deflicker; EQ/compressore/effetti voce, "Migliora voce", loudness, ducking, registrazione voce con teleprompter, registrazione schermo + webcam, sincronizzazione audio esterno, multicamera | Correggo colore con LUT e curve, il mix audio rispetta −14 LUFS, e monto un'intervista a due camere sincronizzate dall'audio |
| **5 — Libreria creativa** | Effetti video (formato documentato CPU + GPU), **libreria completa di oltre 100 transizioni** (sezione 5.11bis), animazioni testo per lettera, template di testo, nuvolette ed elementi grafici animati, sticker, visualizzatori audio ed effetti sul ritmo, curve di velocità, motion blur, template con segnaposto, copertina, kit del marchio, slideshow dalle foto, gestore asset | Uso un template, sostituisco i media e ottengo un video completo; ogni transizione supera il test di rendering CPU/GPU |
| **6 — AI locali** | Interfaccia IAiTask, gestore modelli con licenze in `docs/MODELS.md`, sottotitoli automatici + editor + stili social, capitoli automatici, editing basato sul testo, rimozione parole di riempimento, TTS, rimozione silenzi, beat detection, riduzione rumore, separazione voce/musica, rimozione sfondo, auto reframe, stabilizzazione, slow motion (RIFE + fallback CPU), scene detection | Genero sottotitoli animati parola per parola, taglio un video cancellando frasi dalla trascrizione e rimuovo lo sfondo di una clip senza green screen |
| **7 — AI avanzate e creazione rapida** | Motion tracking, scontorno a un clic, foto 3D, rimozione oggetti, ritocco viso, sticker sul viso, miglioramento qualità, effetti corpo, ricerca per contenuto, highlight e "da video lungo a clip brevi", **montaggio automatico**, **da copione a video**, sottotitoli bilingue, karaoke | Da 20 clip e una canzone ottengo con il montaggio automatico un video sul ritmo in meno di 5 azioni, e lo modifico liberamente |
| **8 — Rifinitura e rilascio** | Encoding hardware con fallback, coda di rendering, preset piattaforme, export multi-formato e con dimensione massima, cronologia versioni, tour iniziale e suggerimenti contestuali, preferenze complete, i18n, profiling e ottimizzazione, verifica della matrice GPU (`docs/GPU_COMPATIBILITY.md`), rifinitura Material 3 e accessibilità, PKGBUILD, documentazione utente | `makepkg -si` installa l'app, che rispetta i requisiti di prestazioni della sezione 6, funziona in modalità software completa e supera **tutti** i test di semplicità |

---

## 9. Modo di lavorare (obbligatorio)

1. **Prima di scrivere codice**: leggi questa specifica, poi scrivi `docs/ARCHITECTURE.md` (classi principali, flussi dati,
   come il core si mappa su MLT, threading) e `docs/FILE_FORMAT.md`. Presentameli e **aspetta la mia approvazione**.
2. **Lavora a piccoli incrementi**. Dopo ogni incremento: compila, esegui i test, avvia l'app se la modifica tocca la UI,
   correggi finché tutto funziona, poi fai commit.
3. **Il progetto deve sempre compilare** e i test devono sempre passare alla fine di ogni incremento.
4. **Niente finzioni**: niente stub vuoti, niente `TODO` spacciati per funzionalità, niente pulsanti che non fanno nulla.
   Se una cosa non è finita, scrivilo in `docs/PROGRESS.md`.
5. **Verifica invece di indovinare**: se un'API di MLT, Qt o FFmpeg non si comporta come previsto, scrivi un piccolo
   programma di prova, osserva il comportamento reale e poi procedi.
6. **Aggiorna `docs/PROGRESS.md`** alla fine di ogni sessione: cosa è fatto, cosa è in corso, prossimi passi, bug noti,
   decisioni prese e motivazioni. Deve bastare a te stesso per ripartire in una sessione nuova senza altra memoria.
7. **Alla fine di ogni fase**: aggiorna README e SHORTCUTS, fai un riepilogo onesto (cosa funziona, cosa no, limiti noti),
   verifica il criterio di completamento e **aspetta il mio via**.
8. **Se una funzionalità di questa specifica risulta irrealizzabile o troppo costosa** in locale, dimmelo con una proposta
   alternativa invece di ometterla in silenzio.
9. Fai domande solo quando una decisione cambia davvero il risultato; per il resto scegli l'opzione più ragionevole
   e annotala in `docs/ARCHITECTURE.md`.
10. **Pensa come CapCut.** Per ogni funzione, prima di implementarla, chiediti: qual è il modo con meno passaggi per farla?
    Serve davvero una finestra di dialogo o basta un controllo nel pannello? Il default è già quello giusto per l'80% delle
    persone? Se l'interfaccia che hai progettato richiede di leggere istruzioni per essere usata, semplificala.

---

## 10. Build e installazione attese

```bash
# sviluppo
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
./build/velacut

# installazione come pacchetto Arch
makepkg -si
```

---

**Inizia ora dalla Fase 0: leggi tutta la specifica e proponi `docs/ARCHITECTURE.md` e `docs/FILE_FORMAT.md`. Non scrivere altro codice prima della mia approvazione.**
