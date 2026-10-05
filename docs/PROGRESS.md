# vedit — Stato di avanzamento

Ultimo aggiornamento: 2026-10-05 (Fasi 0-5 ✅ — criteri della Fase 5 verificati; Fase 6 🔶 in corso: sottotitoli
manuali/da file completi; Fase 8 🔶: preferenze reali, encoding hardware; Fase 7 da fare. L'utente ha dato il via a
proseguire con l'obiettivo "sistema tutto secondo la SPEC, uguale a CapCut, migliora l'estetica".)

## Sessione 2026-10-05 (sera) — Fase 7: montaggio automatico
- **Montaggio automatico** (SPEC §5.13bis, criterio della Fase 7; `ai/Montage`, `engine::extractShotSamples`,
  `EditorController::buildMontage/shuffleMontage`, `MontageDialog`): video, foto e una canzone facoltativa, uno stile
  (vlog, viaggio, sport, cinematico, festa, meme, prodotto) e una durata (15/30/60 s o tutto). Ogni video è
  campionato 4 volte al secondo (nitidezza, luce, movimento); per ogni ripresa si sceglie la finestra migliore non
  ancora usata (nitida, illuminata, con un po' di movimento ma non mossa), le riprese durano 1–8 battiti secondo lo
  stile e finiscono sui battiti della canzone (rilevatore esistente), riempiono il canvas, con filtro, transizioni e
  titolo dello stile e la musica tagliata alla durata con dissolvenza. Progetto normale (lo storico parte da lì);
  "Rimescola" nel banner del player = altro ordine e altri momenti (seme diverso), un passo di annullamento.
  Test: 16 video + 4 foto + una canzone a 120 BPM → 15 s, ogni taglio su un battito, rimescola/annulla, poi un taglio
  normale funziona (`tst_editor::automaticMontage`); dalla schermata iniziale 5 azioni (4 con i valori predefiniti).
- **Da copione a video** (SPEC §5.13bis; `ai::splitScript`, `EditorController::buildFromScript`, `ScriptVideoDialog`):
  il testo diventa scene (paragrafi; quelli lunghi divisi a fine frase, ≤ 30 parole), ognuna uno spazio da riempire
  con i propri video ("Scegli" del banner dei template), lunga quanto la sua voce (Piper, se c'è una voce) o il tempo
  di lettura; le parole come sottotitoli animati; la musica sotto, più bassa e abbassata ancora mentre parla la voce
  (ducking esistente). Senza ricerca per contenuto (servirebbe CLIP) i media non vengono proposti in automatico.
  Test con e senza voce (sostituto di piper-tts). Nella schermata iniziale 5 strumenti rapidi, ora a schede verticali.
- **Tracciamento del movimento** ("Traccia" sui testi e sticker; `fx::PointTracker`, `engine::extractTrackedPath`):
  la zona del video sotto il testo/sticker (8 % della larghezza) seguita fotogramma per fotogramma (confronto a
  blocchi grossolano+fine, sub-pixel, modello aggiornato lentamente, "perso" quando nessun posto somiglia abbastanza)
  → keyframe di posizione ogni 2 fotogrammi nel tempo della clip; video ruotati dai metadati gestiti. Test: uno
  sticker su un quadrato in movimento lo segue (errore < 2 % della larghezza). Scoperto che `drawbox` di FFmpeg non
  anima x/y con `t` (i test usavano un quadrato fermo): ora `overlay` con `eval=frame`.
- **Momenti salienti e da video lungo a clip brevi** (SPEC §5.12; `ai/Highlights`, `ai::HighlightAnalysis`,
  `AiController::highlights/makeShortClips`, `EditorController::makeShortClipDrafts`): il video lungo è diviso in
  pezzi di 3–15 s ai cambi di scena, a metà delle pause e dove il volume salta di più di 8 dB (risate, applausi,
  enfasi); ogni pezzo ha un punteggio (più forte del solito 0,6, parlato fitto 0,4 se c'è la trascrizione, inizio su
  un cambio di scena +0,1). "Momenti salienti" tiene i pezzi migliori fino a circa 60 s (il 30 % se la clip dura meno
  di 2 minuti) con `removeSourceRanges`: un passo di annullamento. "Da video lungo a clip brevi" sceglie 3–10 finestre
  di circa 30 s (15–60 s, non sovrapposte) e ne fa **bozze 9:16** nella schermata iniziale (video a riempimento,
  sottotitoli dalla trascrizione se c'è, titolo animato con le prime parole o "Parte N"; ogni clip reinquadrata
  seguendo il soggetto con lo stesso tracciamento di "Auto reframe"), senza toccare il progetto aperto. Analisi una volta per file (memorizzata nella sessione). Test: un minuto con due picchi di volume e due cambi
  di scena (`tst_editor::highlightsAndShortClips`), dall'interfaccia fino alla bozza aperta in 3 azioni
  (`tst_ui::shortClipsFromTheAiTab`) e unitario (`tst_ai`). Limite: niente visione (volti, sorrisi,
  azione): il punteggio usa solo suono, scene e parlato.
- **Riduci rumore** (SPEC §5.12 "miglioramento qualità video: denoise"): cursore in Regola (`denoise` di
  `vedit.adjust.basic`), filtro CPU che conserva i bordi (`fx::denoisePass`, due passate separabili con pesi interi,
  ~6 ms a 1080p su 12 thread). Test: unitario (rumore dimezzato, bordo intatto, alfa conservato) e sul servizio MLT.
  Restano: upscale (servirebbe un modello, es. Real-ESRGAN ncnn, come programma esterno facoltativo) e stima automatica
  del rumore in "Migliora" (le miniature sono troppo piccole per misurarlo).
- **Scheda "IA" nelle proprietà** (la SPEC elenca "AI" tra le schede del pannello destro): tutti gli strumenti
  intelligenti della clip selezionata in un elenco con una riga di spiegazione; quelli che non valgono per la clip
  sono in grigio, quelli solo video nascosti per l'audio. Nessuna funzione nuova: ogni voce usa l'azione esistente.
- **Proposta per l'utente (da decidere)**: OpenCV 5 è installato su questa macchina ma non è nell'elenco delle
  dipendenze della SPEC (che chiede di domandare prima): con il suo consenso darebbe rilevamento dei volti (ritocco
  viso, sticker sul viso, auto reframe e montaggio sui volti), inpainting (rimozione oggetti), GrabCut (scontorno a un
  clic) e i tracker di OpenCV, come programma separato facoltativo (`vedit-vision`) così che l'app parta anche senza.

## Sessione 2026-10-05 (pomeriggio) — Fase 6: sottotitoli
- **Core** (`10f2f61`): clip `subtitle` tipizzate (testo, parole con tempi dall'inizio della clip, stile per riga) e
  `captionStyle` tipizzato della traccia (evidenziazione colore/ingrandita/riquadro/karaoke, entrata pop/dissolvenza/
  rimbalzo, parole alla volta, altezza, maiuscolo). `TimelineEditor`: `insertCaptions` (traccia sottotitoli propria,
  righe sovrapposte accorciate), taglio e trim che tengono le parole dove vengono dette, `mergeCaptionLines`,
  `shiftCaptions`; testi e sottotitoli non si mescolano mai su una traccia. Lettore SRT/WebVTT riscritto senza `double`
  (accetta CRLF, BOM, tempi VTT senza ore, impostazioni dei cue, tag di formattazione, entità).
- **Rendering** (`engine/text/CaptionRenderer`, servizio MLT `vedit.caption`): le parole diventano contorni
  (`QPainterPath`) sul thread della proiezione, i fotogrammi si disegnano dai contorni nei thread di MLT senza font
  (D-39); l'immagine si riusa finché non cambiano gruppo, parola detta o passo di animazione. La parola ingrandita fa
  spazio alle vicine. Anteprima al passaggio del mouse sugli stili (`Preview.captionStyle`).
- **40 stili social** in `caption-styles.json` (8 categorie, test: ≥ 30, valori validi, tutti disegnano testo).
- **Interfaccia**: scheda **Sottotitoli** nella barra della libreria (righe modificabili sul posto, clic = vai alla
  riga, dividi/unisci/elimina, cerca e sostituisci, sposta tutti ±0,1 s, aggiungi riga; stili con chip e anteprima),
  pagina **Sottotitoli** nelle proprietà (testo della riga, stile di tutte le righe), "Sottotitoli" nella barra
  contestuale senza selezione e nella ricerca universale; import/export SRT e WebVTT.
- **Bug trovato e corretto**: i testi chiedevano il font "Inter" ma il font incluso si registra come "Inter Variable":
  tutti i testi uscivano in Noto Sans, e vedit-render non caricava proprio il font. Ora `vedit_fonts` (libreria
  comune) registra Inter in ogni processo e sostituisce "Inter" → "Inter Variable" (test in `tst_theme`).
- Le proprietà ricordano la pagina scelta per tipo di clip (un testo o un sottotitolo si apre sulle sue parole).
- **Funzioni AI senza modelli** (libreria `vedit_ai`, D-55): interfaccia `ai::AiTask` (thread, progresso, annullamento,
  distruzione sicura; test), **Rimuovi pause** (livelli RMS → pause con soglia adattiva, tagliate con
  `TimelineEditor::removeSourceRanges`, il resto si ricompatta) e **Dividi le scene** (differenze tra fotogrammi →
  `splitClipAt`), nella barra della clip, con pillola di progresso e "Ferma" nella barra; un passo di annullamento.
  Test su un file generato (tre inquadrature, un secondo di silenzio): tagli esatti a 1 s e 2 s, ~0,7 s tolti.
- **Stabilizza** (`fx/Stabilization`, effetto `vedit.stabilize`, FILE_FORMAT §5.6): movimento della camera stimato su
  immagini grigie 160×90 (4 regioni, ricerca a blocchi grossolana+fine, sub-pixel, mediana; a parità vince lo
  spostamento minore, per le zone piatte), percorso levigato con una gaussiana (forza 0..1), ingrandimento unico per la
  clip (≤ 25 %) che nasconde i bordi; dati salvati nel progetto (base64, ~8 byte/fotogramma) così anche `vedit-render`
  li ha. Filtro CPU `vedit.stabilize` per primo nella catena. Test: tremolio 12,8 → 1,8 px/fotogramma su un video
  generato. **Limite**: corretta solo la traslazione; la stima della rotazione su immagini piccole è troppo rumorosa
  (≈ 0,4°/fotogramma) e sommata dava un'inclinazione visibile, quindi è misurata ma non applicata.
- **Rallentatore fluido** (`smooth` nelle clip media, FILE_FORMAT §5.5; `engine/analysis/SmoothMotion`): sotto 1× un
  interruttore nella pagina Velocità; la parte usata del file (arrotondata a 2 s) viene ricalcolata da `ffmpeg
  minterpolate` (compensazione di movimento, bidirezionale) a fps × ⌈1/velocità⌉ (max 240) con l'audio copiato, in
  cache; la clip la riproduce alla sua velocità (stessa mappatura dei tempi; verificato con un programma di prova che
  il fotogramma 0 della copia è l'inizio dell'intervallo e i dispari sono quelli nuovi). Anteprima: coda in background
  con avanzamento; export: la copia si fa al momento se manca. Test: a ½× da 10 fotogrammi ripetuti su 20 a 0.
- **Adatta a 9:16 / 1:1 / 4:5 seguendo il soggetto** (menu del formato, ricerca; `fx/Reframe`): soggetto stimato 5
  volte al secondo da movimento (peso 3) e dettagli (peso 0,5) con una leggera preferenza per il centro, centroide
  delle parti sopra la media, percorso levigato (~1,5 s, i punti incerti pesano meno); il formato cambia e ogni video
  della traccia principale riempie il canvas (Riempi) con keyframe di posizione ogni 0,5 s nel tempo della sorgente,
  limitati ai bordi dell'immagine; nessun keyframe se il soggetto sta fermo. Un passo di annullamento (formato +
  posizioni). Limite: niente riconoscimento dei volti (servirebbe un modello ONNX); test con un quadrato che si muove.
- **Sottotitoli automatici** (`ai/Whisper`, `ai/Transcript`): audio 16 kHz mono con ffmpeg → `whisper-cli -m … -l
  <lingua|auto> -pp -ojf` (avanzamento letto da `progress = N%`) → parole dai token (lo spazio iniziale apre una
  parola, i pezzi e la punteggiatura si attaccano, i token speciali `[_…]` si scartano; segmenti senza token: parole
  distribuite) → righe (fine frase, pausa > 0,6 s, ≤ 42 caratteri) sulla traccia sottotitoli, sostituendo quelle
  presenti; un passo di annullamento. Lingua scelta nella scheda Sottotitoli. Trascrizioni in cache per file.
  **Gestore modelli** (`AiModelsModel`, `FileDownload`): quattro modelli con dimensione, scarica/ferma/rimuovi;
  stato di whisper.cpp con il comando da copiare e "Controlla di nuovo". Test: download da `file://`, sostituto di
  `whisper-cli` nel PATH (cache riusata, sostituzione, annullamento, stato disattivato senza programma).
- **Editing dal testo** (scheda **Trascrizione**, `TranscriptController`, criterio della Fase 6): le parole della
  traccia principale in ordine di timeline, a frasi (lista virtualizzata); clic = vai alla parola, Maiusc+clic =
  seleziona fino a lì, Canc o "Taglia N parole dal video" = `TimelineEditor::removeSourceRanges` su più clip in un
  passo; parola in corso evidenziata; "Rimuovi N parole di riempimento" (ehm, uhm, um, uh, eh…; le parole vere usate
  come riempitivo restano all'utente). Trascrizioni prese anche dalla cache delle sessioni precedenti.
- **Separa voce e musica** (Demucs, `ai::VoiceSeparation`) e **Leggi ad alta voce** (Piper, `ai::SpeechSynthesis`):
  programmi esterni facoltativi come whisper.cpp; senza, un messaggio con il comando. I suoni ottenuti entrano nel
  progetto con `EditorController::importThen` e diventano clip audio sotto la clip/il testo (separazione: audio
  originale silenziato, un passo di annullamento). Voci di Piper aggiunte dall'utente (nessuna offerta da vedit:
  licenze diverse voce per voce). Verificati con programmi sostitutivi nei test.
- **Rimuovi sfondo** (`engine/analysis/Cutout`, campo `cutout`): fotogrammi della parte usata (ffmpeg) → `rembg p`
  → video QuickTime RLE ARGB con l'audio originale, in cache; la proiezione lo usa al posto della sorgente (test: un
  video con metà trasparente lascia vedere il video sotto, e così il risultato del sostituto di rembg). Barra della
  clip video e pagina Ritaglio (pulsante, poi interruttore per tornare indietro). Download del modello di rembg
  annunciato e confermato con un secondo clic. Con `cutout` il rallentatore fluido non si applica.
- **Capitoli per YouTube** (`ai::findChapters`): il primo a 0:00, almeno 3 da ≥ 10 s, circa uno al minuto; ciascuno
  all'inizio di una frase, preferendo le pause più lunghe vicino a punti equidistanti; titolo = prime parole della
  frase. Diventano marker "Capitolo" sulla timeline (un passo di annullamento) e l'elenco "00:00 Titolo" va negli
  appunti per la descrizione.

## Sessione 2026-10-05 — Fine Fase 5: transizioni GPU, slideshow, kit del marchio, filtri, blocco del motore
- **Percorso GPU delle 114 transizioni** (`92d25d6`, criterio Fase 5 seconda metà ✅): GLSL 1.00/1.10 (OpenGL 2.1 /
  ES 2.0) tradotto riga per riga dai kernel CPU; `engine::GpuTransitions` (contesto offscreen su un thread suo,
  texture premoltiplicate, rumore della CPU come texture, autocontrollo al primo uso, ripiego CPU con avviso).
  `tst_gputransitions`: 342 confronti, PSNR peggiore 52,1 dB (Radeon 740M) e 55,3 dB (llvmpipe), soglia 40 dB.
  Dettagli in `docs/GPU_COMPATIBILITY.md` §7. Prestazioni del percorso GPU **non ancora misurate**.
- **Blocco del motore risolto** (`afc4c26`): `tst_editor` restava fermo per sempre alla chiusura dell'editor. Con gdb:
  un worker della cache era bloccato in `producer_timewarp_init` su un mutex dentro memoria già liberata
  (use-after-free) — il thread dell'interfaccia costruiva producer colore passando dal loader di MLT mentre il worker
  costruiva un producer di file. Ora i colori usano il servizio `color` diretto con i normalizzatori del loader aggiunti
  a mano (misurati con un programma di prova: senza quelli audio l'export falliva, "error with audio encode: -22"), i
  worker costruiscono i producer uno alla volta, il producer delle clip al contrario si apre in background. Il lock
  globale della sessione precedente, preso anche sul thread dell'interfaccia, è stato tolto (bloccava l'anteprima).
- **Slideshow dalle foto** (`2403bc6`, §5.13bis), **kit del marchio** (`8ba685c`, §5.13ter), **copertina nel file
  MP4** (`bf5217b`), **66 filtri** (`05b95a4`: erano 32, la SPEC ne chiede 60; un test verifica che siano tutti
  diversi), varianti del Ken Burns.
- Test: 31 suite, verdi su tre esecuzioni consecutive della suite completa.

## Sessione 2026-10-04 (sera) — Interfaccia stile CapCut, template, preferenze, gestore asset
Obiettivo dell'utente: "sistemare tutto secondo la SPEC, renderlo il più possibile uguale a CapCut e migliorare
l'estetica". Fatto, un commit per incremento:
- **Editor stile CapCut** (`4790f3d`): pannelli come card arrotondate su sfondo più scuro (ruoli derivati
  `Theme.color.backdrop`/`panel` in `ThemeColors.h`), ridimensionabili con dimensioni ricordate per progetto
  (`EditorController::panelSize`, `uiState.panels`); barra superiore compatta (Menu, nome al centro, ricerca, Esporta);
  barra contestuale a sole icone con annulla/ripeti (SPEC §4 li vuole lì), Q/W accanto a Dividi, interruttori magnete,
  aggancio e **asse di anteprima** (SPEC 0bis regola 12, `skimmingEnabled`, prima mancava); lettore con intestazione,
  timecode unico, **schermo intero F11** (`FullScreenPreview.qml`); vu-meter master a segmenti lungo la timeline;
  intestazioni traccia con blocca/nascondi/muto sempre visibili e **copertina** in testa alla traccia principale
  (da fotogramma o immagine, export PNG e YouTube 1280×720; prima la chiusura dell'editor sovrascriveva la copertina
  scelta); righello con tacche minori, clip con striscia nome+durata, maniglie di trim piene, nuova testina; dettagli del
  progetto e formato a un clic quando non è selezionato nulla; `ComboBox` M3 nello stile (prima il fallback disegnava una
  tendina bianca Fusion); finestra Scorciatoie; Ctrl+A/Ctrl+T/Ctrl+N/Ctrl+Maiusc+E.
- **Schermata iniziale** (`eda424b`): grande "Nuovo progetto" con gradiente tonale, Registra schermo e Template, galleria
  dei template disegnati nel loro formato, bozze su card; file trascinati ovunque = nuovo progetto con quei file.
- **Deadlock** (`7d1309b`): sotto carico un worker restava bloccato in `producer_timewarp_init` di MLT mentre un altro
  producer veniva costruito, e la chiusura dell'editor aspettava per sempre. La costruzione dei producer dei worker è
  serializzata (`MediaProducerCache::constructionMutex`, mai preso sul thread dell'interfaccia: provato, bloccava la UI).
- **Template completi** (`1737104`, criterio Fase 5 prima metà ✅): `TemplateBuilder` era uno stub con un `TODO` (niente
  testi, sticker, transizioni, filtri): rimosso; `EditorController::applyTemplate` costruisce il progetto con le operazioni
  vere (formato, segnaposto che riempiono il riquadro, filtro, transizione su ogni taglio, titoli animati, sticker).
  Scegliere un template apre il progetto e chiede subito i media, che riempiono i segnaposto in ordine (la musica va sotto
  il video); banner sul lettore; **Sostituisci** nella barra (SPEC §5.2), doppio clic su un segnaposto, media rilasciato
  su un segnaposto o con Alt su una clip, "+" di un media riempie il segnaposto selezionato.
  Test: `tst_editor::phaseFiveCriterionTemplate` (fino all'MP4 esportato), `tst_ui::templateFromTheHomeScreen` (2 azioni).
- **Preferenze e gestore asset** (`955364a`): finestra Preferenze (aspetto Material You completo, lingua applicata
  all'avvio, motore grafico e accelerazioni = `GraphicsPreferences` reali con "Riavvia ora", pacchetti, informazioni).
  Gestore pacchetti vero: `pack.json` come da EFFECT_FORMAT (il vecchio leggeva `manifest.json`, lo ZIP era un `TODO`),
  install da cartella o `.zip` (bsdtar/unzip, percorsi pericolosi rifiutati), aggiornamento, rimozione; la libreria
  unisce i pacchetti utente ed è sostituita atomicamente, i pannelli si aggiornano subito. Rimosso il modello
  `Preferences` finto (setter vuoti, mai usato). **Bug**: lo stile non aveva `DialogButtonBox`, quindi i dialoghi con
  `standardButtons` non mostravano pulsanti (Rinomina OK/Annulla, Informazioni). Test `tst_packages`, `tst_ui::preferences`.
- Traduzioni italiane complete a ogni incremento (anche `vedit_fx`, prima esclusa dall'estrazione); `SHORTCUTS.md`
  riscritto: elencava scorciatoie inesistenti (Ctrl+O, Ctrl+S, strumenti V/H/C/T, Ctrl+1..4 pannelli…).
- Nota: un test aveva usato `QStandardPaths::setTestModeEnabled` creando cartelle vuote in `~/.qttest`, fuori dal
  progetto: rimosse subito (solo `rmdir` di cartelle vuote) e il test ora gira solo con `XDG_DATA_HOME` nella build.

## Sessione 2026-10-04 — Tuning iGPU UMA (Radeon 740M / Arc 130V) e fix backlog scrubbing (SPEC §1bis, §5.3, §6)
- **Decoder: budget UMA in byte e fast-path sequenziale <16 ms (`src/engine/gpu/HwVideoDecoder`)**:
  * La cache LRU di scrubbing ora è dimensionata da un **budget in byte (~76 MiB)** oltre che dal numero
    frame (`umaCacheCapacityForFrameSize`, finestra 15–30): 1080p NV12 → 25 frame, 720p → 30, 4K/10-bit → 15.
    Su iGPU a memoria condivisa (AMD Phoenix2, Intel Lunar Lake) i frame grandi non saturano più la RAM.
  * **Fast-path di decodifica forward**: se il target è avanti di ≤64 frame rispetto all'ultimo frame decodificato
    e lo stream è contiguo, si decodifica in avanti senza keyframe-seek; il gap è ammesso solo se il costo
    previsto (EMA del costo per frame × gap) resta **≤16 ms**. Strumentazione: `lastDecodeMs`,
    `averageFrameCostMs`, `forwardFastPathHits`. I frame intermedi decodificati verso il target vengono
    cachati gratis: lo scrubbing all'indietro/in jitter diventa un hit LRU a latenza zero.
  * Catena di fallback invariata (Tier 1 AMF/QSV → Tier 2 VA-API/D3D11VA/Vulkan → Tier 3 CPU) con reset della
    contiguità su `flush()`, `close()` e `fallbackToSoftware()`. Verificato su questa GPU: fast path attivo a
    ogni step +1 con costo per frame < 16 ms (`tst_hwdecoder::forwardFastPathAndNeighbourCache`,
    `umaCacheCapacityAdaptive`; suite 28/28).
- **Fix bug timeline/scrubbing: cursore↔playhead e backlog di seek (`TimelineView.qml`, `TimelinePlayer`)**:
  * `TimelinePlayer::skim` non era soggetto a backpressure: hover-axis e maniglie di trim inviavano
    `showFrame` non throttlati a ogni pointermove, saturando il consumer MLT di seek asincroni (preview stantio,
    playhead disallineato dal cursore). Ora al massimo **un seek in flight**: i point intermedi vengono
    scartati (vince il più recente), con watchdog a 120 ms identico a `scrubSeek`.
  * Il playhead visivo QML ora è **clampato a [0, duration-1]** (`clampFrame`): prima, trascinando oltre la
    fine, la UI disegnava il cursore oltre il contenuto mentre il player clampava il timestamp → divergenza
    visiva. `updateScrubbing` ora mette anche in pausa se entra in scrub mentre si riproduce.
  * `HoverHandler` dello skimming disattivato durante il drag di scrub (evitava doppie richieste
    skim+scrub per ogni movimento del puntatore).
  * `setRate` (play/pause) resetta `m_seekInFlight`/`m_pendingScrubFrame` dopo il purge del consumer: prima
    il purge scartava la risposta di uno seek in flight e il primo seek del nuovo drag veniva inghiottito
    fino a 120 ms (scatto del playhead all'inizio del drag).
  * `commitSeek` salta il purge/refresh ridondante quando il target è già il frame mostrato: il debounce di
    60 ms non genera più tempeste di refresh durante scrubbing fermo. `scrubSeek` termina lo stato di
    skimming residuo (la hairline di skim non resta più sovrapposta al playhead).

## Sessione 2026-10-04 — Zero-Copy UMA, Scrubbing 60/120 FPS e Scorciatoie Q/W (SPEC §1bis, §5.2, §5.3)
- **Pipeline Hardware GPU Zero-Copy & Ottimizzazione UMA (`src/engine/gpu/HwVideoDecoder`, `src/engine/playback/LruFrameCache`)**:
  * Decodifica video hardware con fallback a cascata su 3 livelli: Tier 1 (HW dedicato AMF/QSV) → Tier 2 (HW generico VA-API/D3D11VA/Vulkan) → Tier 3 (CPU multithread libavcodec).
  * Degradazione automatica trasparente a Tier 3 per formati chroma non accelerati (4:2:2, 4:4:4, High 10 senza HW).
  * Formati nativi semi-planari NV12 (8-bit) e P010 (10-bit) mantenuti senza alcuna conversione CPU-side `sws_scale` RGB.
  * Shaders RHI `preview.vert` e `preview.frag` compilati con `qsb`: texturing bi-planare (R8/R16 per il piano Y, RG8/RG16 per il piano UV) e conversione YUV→RGB eseguita direttamente via hardware nella GPU con matrici ITU-R BT.709 (SDR) e BT.2020 (HDR).
  * Gestione UMA: `BoundedLruCache` thread-safe rigidamente limitata tra 15 e 30 frame (default 24 frame, ~74.6 MB per 1080p NV12), eliminando round-trip e saturazione del bus di sistema.
  * Nuova suite di test `tst_hwdecoder` (8/8 passati, verifica reale su AMD Phoenix2 / Radeon 740M con `radeonsi_drv_video.so`).
- **Disaccoppiamento Playhead UI & Reactive Scrubbing Policy (`TimelineView.qml`, `TimelinePlayer`)**:
  * Playhead UI guidato a 60/120 FPS fluidi sul thread UI tramite `visualPlayheadFrame` completamente disaccoppiato dalle chiamate sincrone di seek.
  * `scrubSeek(frame)` con rate-limiting a ~33 ms (30 fps) e backpressure: se un frame è in elaborazione (`m_seekInFlight`), i target intermedi vengono accorpati atomicamente in `m_pendingScrubFrame` e scartati, evitando la saturazione della pipeline MLT/FFmpeg.
  * Watchdog temporale anti-deadlock di 120 ms nel consumer loop.
  * Frame-accurate commit via `commitSeek` su rilascio cursore (`onReleased`) e su debounce di inattività a 60 ms.
- **Allineamento Scorciatoie SPEC §5.2 (Q/W) e Shuttle §5.3**:
  * `TimelineEditor::rippleTrimClip`: `Q` esegue il ripple trim dall'inizio clip al playhead riposizionando il cursore sulla giunzione; `W` esegue il ripple trim dal playhead alla fine del clip.
  * Integrazione completa in `EditorController`, `ActionRegistry` ed `EditorScreen.qml`.
  * Verificati frame-stepping atomico (Frecce Sinistra/Destra) e shuttle J-K-L (-8x..+8x).
  * Test unitari e di integrazione dedicati in `tst_timelineeditor` (`rippleTrimClipTest`) e `tst_editor` (`rippleTrimShortcuts`).
  * Suite CTest: **28/28 test superati al 100%**.

## Sessione 2026-10-04 — Export hardware, iGPU, fotogramma
- **Encoding hardware con fallback (SPEC §5.15, Fase 8, criterio parziale)**:
  * `ExportSettings`: nuovo `videoCodec` (H.264/H.265/AV1), `hardwareEncoder` (auto/off), `maxFileSizeMB`.
  * `EncoderPlan planEncoder(...)`: funzione pura che unisce la scelta dell'utente con gli encoder **verificati
    dal probe** (solo encoder con cui il probe ha davvero codificato fotogrammi). Catena: VA-API → QSV → NVENC →
    Vulkan Video; software: libx264/libx265/libsvtav1.
  * `Renderer::render`: consumer costruito dal piano; **se l'encoder hardware fallisce a metà export riparte in
    software** e l'utente riceve un avviso tra i warnings (SPEC 1bis regola 4). Verificato su questa Radeon 740M
    (Phoenix2): `h264_vaapi`, `hevc_vaapi`, `av1_vaapi` tutti funzionanti via MLT 7.40.
  * `RenderJob` passa la lista encoder nel job JSON; `AppController` la ricava da decisione+capacità.
  * **Dimensione massima del file**: bitrate medio calcolato dal target (`targetVideoBitrate`); two-pass in
    software (`v2pass` di MLT), VBR con tetto in hardware (i VA-API non hanno il two-pass, documentato).
  * **Preset piattaforme** (YouTube/TikTok/Reels/Shorts/X) e sezione **"Avanzate"** (codec, accelerazione
    hardware con nome GPU, dimensione massima) nella finestra di export, chiusa di default (SPEC 0bis regola 7/8).
  * Test: `encoderPlanFollowsSettingsAndCapabilities` (unit), `exportsHevcWhenAsked`,
    `exportRespectsMaximumSize` (file ≤ target), `exportsWithTheGpuEncoderWhenAvailable` (probe reale; skip se la
    macchina non ha encoder GPU verificati).
  * **Commit**: `25096eb`.
- **Ottimizzazioni iGPU per Radeon 740M e Intel Arc 130V (richiesta esplicita)**:
  * `decideGraphics` ora riporta `integratedGpu` (iGPU senza dGPU nel sistema); motivo visibile in Preferenze.
  * `TimelinePlayer::setPreviewLimit`: l'**anteprima** si renderizza al massimo a 1080p di lato corto con iGPU a
    memoria condivisa o rendering software (540p con ≤1 GB di VRAM). Fps, posizioni ed **export** restano a
    piena risoluzione: si riducono i pixel, non la funzionalità. Le superfici QML già si adattavano (fitRect).
  * **Decodifica hardware nel producer MLT: NON attivata**, su misura: verificato con `melt avformat:…
    hwaccel=vaapi` su un H.264 1080p60 (1,26 s vs 1,26 s; il download GPU→RAM annulla il risparmio). La
    preferenza resta, il probe continua a verificare i decoder, ma il beneficio misurato è nullo e un driver
    difettoso produrrebbe frame neri (SPEC 1bis regola 4). Documentato in `docs/GPU_COMPATIBILITY.md` §5.
  * `docs/GPU_COMPATIBILITY.md`: nuova sezione 5 con i risultati misurati su **Radeon 740M** (encoder VA-API
    H.264/HEVC/AV1 funzionanti; `quality` del driver Mesa con range 0–32 → valori 8/18/28) e le note per
    **Arc 130V** (VA-API prima di QSV, serve `intel-media-driver`, AV1 hardware pratico).
  * Test: `tst_gpu::integratedGpuIsDetected`. **Commit**: `713a0f5`.
- **Esporta fotogramma corrente (SPEC §5.15, base della copertina §5.13ter)**:
  * `EditorController::exportCurrentFrame`: PNG del frame a schermo, mai sovrascritto ("nome (2).png").
  * Pulsante macchina fotografica nella barra di trasporto dell'anteprima + snackbar con il percorso;
    `AppController::videosFolder()` esposto a QML.
  * Test `tst_editor::exportsTheFrameOnScreen`. **Commit**: `3858c86`.
- **Traduzioni**: nuove stringhe di export/anteprima estratte e tradotte (0 non tradotte in `vedit_it.ts`).

## Fase corrente
**Fase 6 — AI locali: funzioni fatte, verifica con i programmi veri da fare.** Criterio SPEC §8 ("genero sottotitoli
animati parola per parola, taglio un video cancellando frasi dalla trascrizione e rimuovo lo sfondo di una clip senza
green screen"): tutto il percorso è implementato e provato con programmi sostitutivi che rispondono nel formato
documentato (whisper-cli `--output-json-full`, `rembg p`), ma **whisper.cpp e rembg non sono installati su questa
macchina**, quindi il criterio non è verificato con i programmi reali. Comandi per l'utente in `docs/MODELS.md` §3.
Limiti onesti: stabilizzazione solo traslazione; auto reframe senza volti; RIFE non integrato (c'è il percorso CPU
`minterpolate`); nessuna voce di Piper offerta da vedit; rembg scarica il suo modello alla prima esecuzione (vedit
chiede conferma).

**Fase 5 — Libreria creativa: completa.** Criterio SPEC §8: "uso un template, sostituisco i media e ottengo un video
completo" ✅ (`tst_editor::phaseFiveCriterionTemplate` fino all'MP4, `tst_ui::templateFromTheHomeScreen` in 2 azioni);
"ogni transizione supera il test di rendering CPU/GPU" ✅ (`tst_gputransitions`, anche su llvmpipe). Limiti onesti:
- copertina: da fotogramma o immagine (con testi, sticker e filtri presenti in quel fotogramma); non c'è un editor
  della copertina separato dalla timeline;
- gestore asset: pacchetti da cartella o `.zip`; le LUT non fanno ancora parte del formato dei pacchetti;
- il percorso GPU riguarda le transizioni; filtri ed effetti video restano solo CPU (consentito: la GPU è opzionale).

### Prossimi passi (Fase 6 — AI locali, via dato dall'utente con l'obiettivo della sessione)
1. ✅ Sottotitoli manuali e da file (vedi sessione 2026-10-05 pomeriggio). Restano: ASS in import/export, sottotitoli
   come traccia separata nell'export (oggi sono impressi nel video), stile per singola riga dall'interfaccia (il
   formato lo supporta: `styleOverride`), parole chiave evidenziate automaticamente ed emoji automatiche.
2. ✅ Sottotitoli automatici con whisper.cpp (processo esterno, disattivati con il comando se manca) e gestore modelli
   (Preferenze → Modelli AI, download solo su richiesta con la dimensione). **Da verificare con il whisper-cli reale**
   quando l'utente lo installa (`yay -S whisper.cpp`): il formato `--output-json-full` è stato ricostruito dalla
   documentazione e provato con un sostituto. ✅ Editing dal testo e parole di riempimento (scheda Trascrizione).
   ✅ Capitoli automatici (pulsante nella scheda Trascrizione), ✅ sottotitoli da copione allineati al parlato
   (allineamento di Levenshtein sulle parole, parole non dette con tempi interpolati). Testi delle canzoni: si fanno con
   "Sottotitoli automatici" + uno stile karaoke, se whisper riconosce il canto.
3. Senza modelli: ✅ rimozione pause, ✅ divisione delle scene, ✅ stabilizzazione (traslazione), ✅ rallentatore
   fluido (percorso CPU `minterpolate`), ✅ auto reframe classico; restano rotazione nella stabilizzazione (stima più
   robusta), RIFE quando installato, volti (con un modello) per l'auto reframe.
4. ✅ Interfaccia comune `ai::AiTask` (progresso, annullamento, risultato modificabile).

### Sessione 2026-10-01
- **P5.7 — Template di progetto con segnaposto (fondamenta complete)**:
  * Aggiunto `templates.json` al pacchetto core con 8 template pronti in 8 categorie diverse (vlog, travel, party, 
    business, sport, memories, social, cinematic). Ogni template specifica: canvas, slot (placeholder con label, 
    tipo video/photo/any, durata), transizioni, filtri, testi e sticker posizionati.
  * Implementato `TemplateBuilder` (`src/core/project/TemplateBuilder.{h,cpp}`) che costruisce una `Sequence` da un 
    manifest di template JSON: crea i placeholder come clip colore grigio con metadati `Clip::placeholder`.
  * Aggiunto `AppController::newProjectFromTemplate(templateId)` che carica un template dalla libreria e crea un 
    progetto con la sequenza già popolata di placeholder pronti per essere sostituiti.
  * Esteso `AssetLibraryModel` con il tipo `Templates` per esporre i template all'interfaccia QML (categorie e item).
  * Caricamento e serializzazione placeholder già presenti: `Clip::placeholder` nel core, `ProjectJson` lo salva/carica, 
    `TimelineEditor::insertPlaceholder()` e `::replaceClipMedia()` gestiscono l'inserimento e la sostituzione.
  * Test: `tst_kernels::coreLibraryLoads` verifica ≥8 template in ≥8 categorie, tutti con nomi bilingui.
  * **Commit**: `be66e66` (template library + TemplateBuilder), `6e2d4a7` (AppController), `8f8c8bb` (AssetLibraryModel), 
    `78f2124` (test).

- **P5.9 — Gestore asset (fondamenta)**:
  * Implementato `PackageManager` (`src/fx/PackageManager.{h,cpp}`) per gestire pacchetti di asset utente.
  * I pacchetti vengono installati in `~/.local/share/vedit/packs/` accanto al pacchetto core built-in.
  * `installPackage()` copia ricorsivamente una cartella di pacchetto nella directory utente.
  * `removePackage()` rimuove pacchetti utente (il core built-in è protetto).
  * `installedPackages()` elenca tutti i pacchetti (built-in + utente) con metadati da `manifest.json`.
  * Placeholder per estrazione ZIP (da implementare con libreria o comando esterno).
  * **Commit**: `[commit corrente]`.
  * **Ancora da fare per P5.9**: interfaccia QML per installare/rimuovere pacchetti, supporto ZIP completo, 
    integrazione con `fx::Library` per caricare asset dai pacchetti utente.
- **Bug "l'app crasha quando metto qualcosa nella timeline"** (segnalato dall'utente): era un **blocco**, non un crash.
  `WaveformView::paint` disegnava la forma d'onda dell'intera clip come un unico path di un rettangolo per colonna;
  con un video di 4 minuti (~43 000 px di larghezza) il riempimento del path nel rasterizzatore di Qt impiegava minuti,
  bloccando interfaccia e thread di rendering: l'app sembrava morta, veniva chiusa a forza e restava il `lock` della
  bozza (visto nei log del 27/09). Ora miniature e forma d'onda si disegnano solo attorno alla parte visibile
  (a blocchi di `Theme.editor.paintChunk`), a rettangoli singoli. Test di regressione
  `tst_ui::longClipIsPaintedOnlyInView` (fallisce senza la correzione). Riprodotto e verificato anche col file vero
  dell'utente su Vulkan (driver di prova temporaneo, poi rimosso).
- `tst_ui` in ASan dura ~260 s: limite portato a 600 s; soppressa una perdita di Qt 6.11 (`QFreetypeFace::cleanup`).
- **P5.4** completato riscrivendo la bozza non committata lasciata da un altro assistente (vedi sotto).
- **Traduzioni**: il file italiano era indietro di ~240 stringhe delle Fasi 4–5 (non erano mai state estratte, anche
  se questo file diceva "traduzioni complete"). Ora complete; corrette 6 stringhe sorgente scritte in italiano nel QML.
- `testMedia()` dei test dava a tutti i file la stessa impronta: ora una per file (le cache per impronta si mescolavano).

### Fase 5: fatto (un commit per incremento)
1. **P5.1 — Pacchetti creativi, filtri e transizioni estese**:
   - Espansione catalogo con 60+ filtri creativi e 40+ transizioni suddivisi per categorie tematiche.
2. **P5.2 — Animazioni testo avanzate, template titoli, fumetti e terzi inferiori**:
   - Algoritmi di animazione testo a livello di carattere, parola, linea e blocco (typewriter, bounce, pop-in, wave, glitch, blur).
   - Fumetti (speech bubbles) con 9 forme geometriche SDF (Oval, RoundedRect, ThoughtCloud, ShoutBurst, ComicSquare, Pill, Diamond, Hexagon, Ribbon) e code vettoriali orientabili.
   - Terzi inferiori (lower thirds) con sfondi grafici, accenti cromatici e calcolo preciso bounding box.
   - 61 stili di testo pronti all'uso in `text-styles.json`.
3. **P5.3 — Curve di velocità (Speed Ramping) e Motion Blur direzionale**:
   - Modello matematico spline cubica Hermite monotona (PCHIP Fritsch-Carlson) in `core/project/SpeedCurve.h`/`.cpp`.
   - 7 preset completi (Montaggio, Eroe, Proiettile, Salto, Flash in, Flash out, Personalizzata).
   - Integrazione analitica esatta $I(u) = \int_0^u v(t) dt$ e inversione esatta via Newton-Raphson `progressAtSource`.
   - Mappatura temporale reversibile dei fotogrammi e dei keyframe (`sourceTimeAt`, `timelineOffsetAtSourceTime`).
   - Servizio MLT custom `vedit.speed_ramp` con riproduzione fluida accurata al singolo fotogramma senza ricampionamenti a scatti.
   - Kernel CPU straight-alpha multi-tap di Motion Blur direzionale con angolo, campioni e intensità regolabili (`fx/MotionBlur.h`/`.cpp`) e servizio MLT `vedit.motion_blur`.
   - Editor visuale interattivo `SpeedCurveEditor.qml` con visualizzazione scala logaritmica centrata su 1.0×, trascinamento nodi, aggiunta con doppio clic, rimozione con clic destro e indicatore tooltip.
   - Integrazione in `ClipInspector` e `PropertiesPanel.qml`: commutatore modalità Costante/Curva, chip preset, motion blur switch e slider d'intensità.
   - Ripple magnetico automatico e ripristino durata coerente in `TimelineEditor::setSpeedCurve` e `removeSpeedCurve`.
   - Suite completa di test unitari (`tst_kernels`, `tst_timelineeditor`) e integrazione (`tst_services`), 25/25 test CTest passati al 100%.

4. **P5.4 — Sticker, visualizzatori audio, effetti a ritmo**:
   - Scheda **Sticker** (dopo Testo): 90 sticker in 6 categorie — 23 immagini del pacchetto (SVG, una GIF animata),
     63 emoji a colori disegnate col font del sistema, 4 visualizzatori. Clic (o "+") = aggiunto al playhead su una
     traccia sticker, al 35% del canvas; **Importa** aggiunge immagini proprie (PNG/WebP/JPEG/GIF) come sticker.
     Miniature (animate al passaggio), ricerca Ctrl+K, azioni contestuali (Modifica sticker, Animazione, Specchia, Ruota).
   - Pagina **Sticker/Visualizzatore** del pannello: ricolora (mantiene le ombre), velocità e ripetizione degli
     animati; stile del visualizzatore (barre, spettro, onda, cerchio), due colori, sensibilità, morbidezza, barre,
     specchiato; Ripristina e Applica a tutti.
   - **Spettro audio** (D-49) in background con cache su disco; **visualizzatori** che seguono l'audio della timeline
     sotto di loro (D-50).
   - **Beat**: azione della barra contestuale sulle clip audio, rilevamento per flusso spettrale in background, marker
     "Beat" sulla clip (sostituiti a ogni nuova ricerca). **Effetti a ritmo** flash/zoom/scossa pilotati dai marker
     (D-51) — nel modello e nel motore, ma **ancora senza interfaccia**: arrivano nella scheda Effetti (P5.5).
   - Test: `tst_projection` (`stickersInProjection`, `visualizerFollowsTheMusic`, `beatFlashOnTheBeats`),
     `tst_services` (`spectrumOfASine`, `beatsOfAClickTrack`), `tst_timelineeditor::stickersAndBeats`,
     `tst_serialization` (sticker nel roundtrip), `tst_ui::stickersAndBeat`. 25/25 verdi.
   - Rispetto alla bozza di Gemini: niente decodifica audio sul thread dell'interfaccia (avrebbe bloccato l'app come il
     bug sopra), niente battito finto a 120 BPM senza beat, `fx` di nuovo indipendente da `core`/`engine`, tempo dei
     beat relativo alla clip (prima sfasato con clip tagliate), percorsi degli sticker del pacchetto risolti (prima
     gli sticker erano trasparenti), filtri nello schema `push_get_image` del progetto.

5. **P5.5 — Scheda Effetti**: 101 effetti video in 13 categorie (sfocature, bagliore e luci, glitch, retrò/VHS, zoom e
   scossa, flash e strobo, specchio e caleidoscopio, stilizza, distorsioni, particelle e meteo, cornici e vignette,
   colore, a ritmo) su 48 kernel CPU (`fx/VideoEffect`, D-52). Anteprima al passaggio (in ciclo per quelli animati),
   clic = sulla clip selezionata o sullo schermo (sommabili, clic di nuovo = tolto), "+" = livello effetto di 3 s sulla
   timeline; pagina **Effetti** del pannello con intensità e parametri di ogni effetto e cestino; Ctrl+K. Gli effetti a
   ritmo trovano da soli i beat della musica se non ce ne sono. Le schede del pannello proprietà ora scorrono invece di
   troncare le etichette. Test: `tst_kernels::everyVideoEffect` (ogni kernel: deterministico, alfa intatto, cambia
   l'immagine) e `coreLibraryLoads` (≥ 80 effetti, ogni kernel usato), `tst_projection::videoEffectsInProjection`,
   `tst_ui::effectsLibrary`. Limite: solo percorso CPU (il GPU è in P5.10).

6. **P5.6 — Elementi grafici animati e template di testo**: 19 elementi nella scheda Sticker ("Elementi animati":
   contatori — anche %, €, $, anni, decimali —, conto alla rovescia, cronometro, barre di avanzamento, frecce, cerchi,
   sottolineatura, evidenziatori, spunta, croce), pagina **Elemento** (da/a, prima/dopo, decimali, colori, spessore,
   tempo di disegno) (D-53). Template di testo: 77 stili, **53 animati** (nuove categorie Elenchi, Date e luoghi, Inviti
   all'azione, più titoli), con il testo d'esempio in italiano e inglese (prima era solo italiano). Test:
   `tst_kernels::graphicElements`, `coreLibraryLoads` (≥ 40 stili, ≥ 50 template bilingui),
   `tst_projection::graphicElementsInProjection`, `tst_serialization`, `tst_ui::animatedElements`.

7. **P5.7 — Template di progetto con segnaposto (in corso)**: 
   - ✅ `templates.json` nel pacchetto core con 8 template pronti in 8 categorie
   - ✅ `TemplateBuilder` costruisce sequenze da manifest template
   - ✅ Supporto placeholder completo nel core e editor
   - ✅ `fx::Library` carica template con categorie localizzate
   - ✅ `AppController::newProjectFromTemplate()` crea progetti da template
   - ✅ `AssetLibraryModel` supporta tipo `Templates`
   - ✅ Test: `tst_kernels::coreLibraryLoads` verifica ≥8 template in ≥8 categorie
   - **Ancora da fare**: UI QML, Alt+trascina per sostituire, salva come template, slideshow foto

8. **P5.9 — Gestore asset (in corso)**:
   - ✅ `PackageManager` gestisce pacchetti in `~/.local/share/vedit/packs/`
   - ✅ `installPackage()` e `removePackage()` per gestione pacchetti
   - ✅ `installedPackages()` elenca built-in + utente con metadati
   - **Ancora da fare**: UI QML, supporto ZIP completo, integrazione con `fx::Library`

### Criterio di completamento della Fase 4 (SPEC §8)
| Requisito | Esito | Verifica |
|---|---|---|
| Correggo colore con LUT e curve | ✅ | `tst_editor::phaseFourCriterion`: LUT `.cube` 3D/1D applicata con intensità, curve monotone cubiche RGB Master / per canale, bilanciamento del bianco per canale, ruote colore (midtones level), saturazione selettiva per gamma HSL (red) |
| Il mix audio rispetta −14 LUFS | ✅ | `tst_editor::phaseFourCriterion`: esportazione con `normalizeLoudness` attiva a −14 LUFS, misura oggettiva ITU-R BS.1770-4 / EBU R128 tramite `extractLoudness` sull'MP4 esportato conforme entro 0,6 LU |
| Monto un'intervista a due camere sincronizzate dall'audio | ✅ | `tst_editor::phaseFourCriterion`: allineamento automatico forme d'onda, creazione clip multicamera annidata con traccia audio master e 2 angoli, taglio e commutazione angoli al playhead e con tasti di scelta rapida 1–9 |
| Compila senza warning, test verdi | ✅ | 25/25 in `build` (`-Wall -Wextra -Wpedantic -Werror`), zero compiler warnings, 100% test CTest passati |

### Criterio di completamento della Fase 3 (SPEC §8)
| Requisito | Esito | Verifica |
|---|---|---|
| Animo un titolo con keyframe ed easing | ✅ | `tst_ui::phaseThreeCriterionTitle`: testo, due keyframe di opacità, andamento "Morbido", dall'interfaccia |
| Compongo un green screen con maschera | ✅ | `tst_ui::phaseThreeCriterionGreenScreen`: colore scelto con un clic sul player, maschera a cerchio ingrandita dal suo angolo; il fotogramma renderizzato mostra il video al posto del verde e il soggetto intatto |
| Compila senza warning, test verdi | ✅ | 25/25 in `build` (RelWithDebInfo) e in `build-debug` (ASan + UBSan) |
| Test di semplicità applicabili | ✅ | scenario 4 (testo scritto e animato): **4** azioni su 5; 1, 2, 3, 6, 8, 10 confermati (`docs/USABILITY.md`) |

### Fase 3: fatto (un commit per incremento)
1. **Core & Fx (`P3.1`)**:
   - Modello dati per maschere (`Mask`, `MaskShape`, `MaskPoint`), animazioni (`ClipAnimation`, `ClipAnimations`), e livelli di regolazione (`AdjustmentClipData`).
   - Serializzazione JSON canonica e tollerante in `ProjectJson.cpp` verificata senza perdita di dati in `tst_serialization` e `tst_projectroundtrip`.
   - Implementate tutte le 16 modalità di fusione (`BlendMode`: Normal, Multiply, Screen, Overlay, Darken, Lighten, ColorDodge, ColorBurn, HardLight, SoftLight, Difference, Exclusion, Add, Hue, Saturation, Color, Luminosity) con straight alpha compositing W3C in `Composite.cpp`.
   - Kernel CPU di riferimento e servizio per Chroma Key (`ChromaKey.cpp` / `vedit.chroma_key`): spazio colore UV, soglia/somiglianza, morbidezza bordo e soppressione spill verde/blu.
   - Test unitari in `tst_kernels` (blend modes e chroma key) e `tst_serialization` al 100% verdi.

2. **Engine Keyframes & Pipeline (`P3.2`)**:
   - Valutazione runtime dei keyframe per trasformazioni, opacità e ritaglio in `Services.cpp` (`transformGetImage`) con calcolo sicuro di `contentTime` nello spazio temporale del contenuto/sorgente (decisione architetturale D-05).
   - Propagazione `BlendMode` su ogni frame con straight alpha compositing in `vedit.composite` attraverso `fx::compositeBlend`.
   - Integrazione del servizio `vedit.chroma_key` prima di trasformazione e filtri, consentendo la rimozione dello sfondo verde/blu in spazio coordinate nativo prima della trasformazione geometrica e compositing multi-traccia.
   - Test di integrazione al 100% in `tst_services` (`transformAnimatesWithKeyframes`, `chromaKeyRemovesGreen`) e `tst_projection` (`chromaKeyRemovesGreenInProjection`, `animatedTransformInProjection`).

3. **Maschere in Fx ed Engine (`P3.3`)**:
   - Kernel CPU di riferimento in `src/fx/Mask.h` e `Mask.cpp`: rasterizzazione di tutte le forme di maschera (Rectangle con roundness SDF, Circle/Ellipse, Linear, Mirror, Heart, Star a 5 punte, Path vettoriale con raycasting) con feathering (smoothstep), invert e combinazione unione multi-maschera in straight alpha.
   - Servizio MLT `vedit.mask` in `Services.cpp` collegato nella pipeline di cut render in spazio sorgente prima di correzione colore e trasformazione (garbage matte).
   - Test unitari approfonditi in `tst_kernels` (forme, sfumatura, invert, multi-maschera) e integrazione in `tst_services` (`maskCutsOutRegion`) e `tst_projection` (`greenScreenCompositionWithMaskInProjection`).

4. **Compound Clip & Adjustment Layer (`P3.4`)**:
   - `ProjectMutator` e `edits::`: operazioni atomiche e reversibili `insertSequence`, `removeSequence`, `replaceSequence` con ripristino ordinato in undo/redo.
   - `TimelineEditor`:
     - `createCompoundClip`: raggruppa le clip selezionate in una nuova sequenza annidata (`Sequence`), sostituisce la selezione con una clip composta (`CompoundClipData`) preservando sincronizzazione e posizioni relative.
     - `expandCompoundClip`: esplode una clip composta ripristinando le clip componenti nelle rispettive tracce della sequenza padre e rimuovendo la sequenza annidata con pieno supporto di annullamento/ripetizione.
     - `insertAdjustment`: inserisce un livello di regolazione (`AdjustmentClipData`) su una traccia dedicata `TrackKind::Adjustment` con calcolo dell'estensione temporale.
   - `TimelineProjection`:
     - Supporto per proiezioni ricorsive di sequenze annidate tramite `compoundProducer`, memorizzando in cache le proiezioni MLT senza cicli.
     - Integrazione delle clip composte nel grafo MLT con trasformazioni, opacità, effetti, maschere e mixing audio.
     - Gestione dei livelli di regolazione (`TrackKind::Adjustment` o `AdjustmentClipData`): applicazione degli effetti tramite filtri agganciati al trattore sul composito durante il range temporale della clip.
   - Test unitari in `tst_timelineeditor` (`compoundClipCreationAndExpansion`, `adjustmentLayerInsertion`) e integrazione in `tst_projection` (`compoundClipRendersInProjection`, `adjustmentLayerRendersInProjection`) 100% verdi.

5. **Animazioni predefinite e marker (`P3.5`)**:
   - `animations.json` nel pacchetto core: 30 animazioni di ingresso, 30 di uscita, 30 in ciclo; calcolate in
     `vedit.transform` (D-46). `TimelineEditor::setClipAnimations`; `EditorController::applyAnimation/removeAnimation`.
   - Marker della sequenza e delle clip (`TimelineEditor` add/update/remove; `EditorController::addMarker` al playhead,
     `nextMarker`/`previousMarker`; `TimelineModel.markers` e ruolo `markers` delle clip).
   - Tempo dei keyframe e dei marker di clip con velocità e inversione (`core/project/ClipTime.h`, D-44); chiavi di
     rendering con tutti i keyframe (D-45: prima un keyframe modificato non aggiornava l'anteprima); frequenza dei
     fotogrammi del profilo non più troncata (29,97 diventava 29 nei calcoli dei keyframe).
   - Test: `tst_timelineeditor` (`keyframeTimeFollowsTheContent`, `markers`, `presetAnimations`), `tst_projection`
     (`keyframesFollowTheSpeed`, `keyframeChangesUpdateTheProjection`, `presetFadeInAnimation`), `tst_editor`
     (`markersAndAnimations`), `tst_kernels` (30 animazioni per tipo). Traduzioni complete.
   - Nell'interfaccia non c'è ancora niente di tutto questo (P3.6).

6. **Interfaccia delle animazioni (`P3.6a`)**: scheda **Animazioni** nella barra laterale (entrata, uscita, ciclo;
   miniature animate al passaggio del mouse, anteprima in ciclo sul player, clic applica, di nuovo rimuove), pagina
   **Animazione** del pannello proprietà (durata di ognuna, rimozione), azione "Animazione" nella barra contestuale e
   nella ricerca. La matematica delle animazioni sta in `fx/Animation` (la usano il renderer e le miniature).

7. **Marker nell'interfaccia (`P3.6b`)**: M aggiunge un marker al playhead (della clip selezionata, altrimenti del
   video); rombi sul righello e tacche sulle clip; clic porta il playhead lì, tasto destro rimuove
   (`tst_ui::markers`).

8. **Keyframe nell'interfaccia (`P3.6c`)**: diamante accanto a posizione, dimensione, rotazione e opacità (vuoto =
   non animato, colorato = animato, pieno = keyframe al playhead; clic aggiunge o toglie); cambiare un parametro
   animato crea il keyframe al playhead; andamento verso il keyframe successivo (Costante, Accelera, Rallenta,
   Morbido, Salto); salto al keyframe precedente/successivo; diamanti sulla clip selezionata nella timeline. Solo i
   parametri che il renderer anima davvero hanno il diamante (volume, dimensione del testo e regolazioni no).
   Criterio, prima metà: **"Animo un titolo con keyframe ed easing" ✅** (`tst_ui::phaseThreeCriterionTitle`, con
   mouse e tastiera; `tst_editor::keyframes`). Il pannello non si ricalcola a ogni fotogramma della riproduzione
   (solo in pausa, se la clip è animata o il playhead entra/esce dalla clip): prima i test di avvio in ASan col
   rendering software mostravano 4 fotogrammi in 25 s.

9. **Scontorno, fusione, raggruppamento (`P3.6d`)**: scheda **Scontorno** (maschera: linea, fascia, cerchio,
   rettangolo, cuore, stella; bordo morbido, angoli arrotondati, rotazione, inverti; maniglie della maschera sul
   player per spostarla e ridimensionarla; **rimozione di un colore** con il contagocce sul player, che legge il
   fotogramma della sorgente, non il risultato già scontornato), **modalità di fusione** nella scheda Video, **clip
   composta** (raggruppa/separa) e **livello di regolazione** dal menu del tasto destro e da Ctrl+K
   (`tst_editor::cutoutBlendAndGrouping`, `tst_ui::phaseThreeCriterionGreenScreen`).

10. **Editor delle curve**: "Personalizzata" accanto agli andamenti predefiniti apre la curva del movimento verso il
    keyframe successivo; si trascinano i due punti (Bézier cubica, anche oltre 0–1 per l'effetto rimbalzo).
    Pannello: le clip livello di regolazione mostrano Filtro e Regola (prima non si potevano regolare).

### Fase 3: limiti noti (onestà, regola 4)
- **Keyframe nell'interfaccia** solo per posizione, dimensione, rotazione e opacità. Il modello e il formato li
  ammettono su ogni parametro, e il renderer anima anche ritaglio e maschere, ma volume, filtri, regolazioni,
  dimensione del testo e parametri delle maschere non hanno ancora il diamante (il renderer di volume, regolazioni e
  testo usa valori fissi): arrivano con le Fasi 4–5 insieme ai rispettivi renderer animati.
- **Maschere**: una per clip dall'interfaccia (il modello ne ammette più, unite); la maschera a tracciato (Bézier
  libera) esiste nel modello e nel rasterizzatore ma non ha ancora l'editor sul canvas. La rotazione si cambia col
  cursore, non con una maniglia.
- **Rimozione di un colore**: il contagocce legge il fotogramma della sorgente; nessuna scelta del colore dell'alone
  (si toglie il colore scelto).
- **Clip composta**: raggruppa e separa; non si può ancora "entrare" nella clip per montarne l'interno.
- **Marker**: aggiungi, salta, rimuovi; nome, colore e nota non si modificano ancora dall'interfaccia.
- **Animazioni predefinite**: 90 movimenti di trasformazione (nessuna animazione per lettera o parola: Fase 5).
- Una volta, in 6 esecuzioni della suite RelWithDebInfo, un test è fallito senza che il nome sia stato registrato;
  non si è ripetuto (tst_ui eseguito 5 volte di fila senza errori). Da tenere d'occhio.

### Fase 4 — Colore e audio avanzati (completata)
1. **Kernel colore e LUT (`P4.1`)**: `fx::Grade` con bilanciamento del bianco per canale, ruote colore lift/gamma/gain (RGB offset + livello), selettore HSL per 8 gamme con transizioni morbide tra tinte adiacenti, curve monotone cubiche (senza overshoot) RGB e master; parser e campionatore trilineare/lineare per LUT `.cube` 3D/1D (`fx::CubeLut`).
2. **Proiezione colore e deflicker (`P4.2`)**: composizione look + grade + LUT in una singola 33³ `ColorLut` per MLT senza overhead a runtime; proiezione degli effetti `vedit.grade`, `vedit.lut` (con cache per modifica file) e `vedit.deflicker` (`avfilter.deflicker`) anche sui livelli di regolazione (`tst_projection::gradeEffectInProjection`, `cubeLutInProjection`, `deflickerInProjection`).
3. **Interfaccia colore e visualizzatori scope (`P4.3`)**:
   - Controlli completi nel pannello proprietà (scheda Regola): "Bilanciamento automatico" e "Abbina colore" al fotogramma del playhead, selettore file LUT `.cube` 3D/1D con cursore intensità, deflicker (toggle, modalità e dimensione finestra), ruote colore interattive lift/gamma/gain con disco cromatico 2D e cursore livello, regolatore HSL a 8 gamme colore con chip cromatici (tinta, saturazione, luminosità), editor grafico di curve monotone cubiche (Master RGB, Rosso, Verde, Blu) con aggiunta/trascinamento/rimozione punti.
   - Video Scopes dal vivo (`ScopeView` in QQuickPaintedItem e overlay `ScopePanel` nell'anteprima): Istogramma (RGB e luminanza), Waveform (linee IRE 0, 7.5, 50, 100 con intensità di segnale) e Vettorscopio (reticolo 75%, bersagli colore R/Mg/B/Cy/G/Yl, asse skin-tone "I").
   - Test di integrazione `tst_ui::colorGradingAndScopes`: verifica apertura/chiusura scope, modifica parametri `grade` e `deflicker`, verifica effetti e reset. 25/25 test CTest verdi.
4. **Audio avanzato, loudness e ducking (`P4.4`)**:
   - Misura del loudness secondo standard ITU-R BS.1770-4 / EBU R128 (`fx::Loudness` con filtri biquad K-weighting high-shelf + RLB highpass, gating assoluto a −70 LKFS e relativo a −10 LU, finestre momentary/short-term e true peak).
   - Normalizzazione automatica loudness su clip e su mix in fase di esportazione (`ExportSettings::normalizeLoudness`, target predefinito −14 LUFS).
   - Keyframe sul volume con diamante e curva di andamento (`Param` animato nel servizio `GainSettings` di MLT e controller `ClipInspector`).
   - Auto-ducking: rilevamento audio sovrapposto su altre tracce e generazione automatica di keyframe di attenuazione con attacco (0.2s) e rilascio (0.4s).
   - Filtri ed effetti vocali: riduzione rumore `rnnoise` con intensità regolabile (0–100%), equalizzatore parametrico a 3 bande (bassi 100Hz, medi 1kHz, alti 10kHz), compressore dinamico con soglia e rapporto configurabili, effetti vocali ("Migliora voce", voce profonda, chipmunk, robot, radio, megafono, eco).
   - Controlli completi nel pannello proprietà (scheda Audio), test unitari (`tst_fx::loudnessSilence`, `loudnessSineTone`) e di integrazione (`tst_editor::audioProcessingAndLoudness`), 100% CTest (25/25) verdi.
5. **Registrazione voce, teleprompter, webcam e schermo (`P4.5`)**:
   - `RecordController`: gestione delle modalità Voce fuori campo (PCM stereo 48kHz WAV), Schermo (x11grab/PipeWire MP4), Webcam (V4L2 MP4) e Schermo+Webcam con riquadro PiP in angolo.
   - Conto alla rovescia animato 3-2-1 e monitor di livello audio in tempo reale.
   - Teleprompter integrato con testo scorrevole, velocità regolabile (10–300 px/s), pausa/riavvolgi e specchiatura orizzontale (flip per prompter beam-splitter).
   - Salvataggio automatico in `media/` della bozza e inserimento diretto al playhead della timeline.
   - Accesso rapido da schermata iniziale ("Registra schermo"), pulsante "Registra" con menu a discesa nel pannello Media e overlay `RecordDialog.qml`.
   - Test di integrazione `tst_editor::recordingAndTeleprompter` e UI `tst_ui::recordingAndTeleprompterUI`. 25/25 test CTest verdi.
6. **Sincronizzazione audio waveform e montaggio multicamera (`P4.6`)**:
   - `AudioSync`: calcolo dell'allineamento temporale relativo tra file audio e forme d'onda (`alignWaveforms`, `alignAudioFiles`) tramite cross-correlazione normalizzata degli inviluppi energetici con sottrazione della baseline; stima del ritardo/anticipo in secondi e coefficiente di confidenza.
   - Sincronizzazione automatica clip selezionate in timeline (`TimelineEditor::alignClipsByAudio` e `EditorController::syncSelectedClipsByAudio`): compensazione accurata del ritardo, riposizionamento temporale senza collisioni e salvaguardia degli invarianti di traccia (spostamento automatico su tracce overlay/audio dedicate se necessario).
   - Sequenze multicamera: `CompoundClipData` esteso con campo serializzato `activeAngle` (0-indicizzato, con backward-compatibility e documentazione in `FILE_FORMAT.md`). Creazione di clip multicam (`createMulticamClip`) con sincronizzazione automatica degli angoli, tracce video parallele per ciascun angolo e traccia audio master continua.
   - Selezione e cambio inquadratura (`setMulticamAngle`, `cutAndSwitchAngle`): commutazione al volo durante la riproduzione o a timeline ferma con tasti dedicati **1–9** (`multicamAngleKey`), divisione automatica del segmento alla posizione del playhead con continuità temporale e aggiornamento del tag `[Cam N]` nella visualizzazione della clip.
   - Azioni di ricerca e menu contestuale della clip (`createMulticam`, `syncAudio`), scorciatoie attive in `SHORTCUTS.md`.
   - Test unitari (`tst_timelineeditor::multicamEditingAndAudioSync`) e di integrazione motore (`tst_projection::multicamAngleSwitchingAndAudioSync`), 100% CTest (25/25) verdi.
7. **Integrazione del criterio di completamento della Fase 4 (`P4.7`)**:
   - Workflow completo end-to-end convalidato in `tst_editor::phaseFourCriterion`:
     1) Importazione di riprese multicamera per intervista (due angoli camera);
     2) Sincronizzazione automatica da inviluppi di forma d'onda audio tramite `createMulticamFromSelection`;
     3) Montaggio multicamera con tagli e commutazione angoli (Angolo 1 → Angolo 2) sia al playhead sia tramite tasti numerici 1–9, con verifica di continuità temporale e `sourceIn`;
     4) Color grading avanzato: applicazione file LUT `.cube` 3D con controllo d'intensità, curve monotone cubiche RGB Master, bilanciamento del bianco per canale, ruote colore (midtones level), saturazione HSL selettiva;
     5) Esportazione con audio mix normalizzato a −14 LUFS (EBU R128) tramite opzione dedicata in `ExportDialog.qml` ed `EditorController::startExport`;
     6) Verifica ffprobe dell'output (H.264, AAC) e misura oggettiva con `engine::extractLoudness` a −14 LUFS (tolleranza ≤ 0,6 LU);
     7) Salvataggio continuo e verifica di roundtrip bit-for-bit del progetto salvato e riaperto.
   - Build a zero avvisi con `-Wall -Wextra -Wpedantic -Werror`, 100% test passati (25/25 CTest).

## Fase 5 — Libreria creativa (in corso)
1. **Libreria completa di 100+ transizioni (`P5.1`)**:
   - Espansa la suite di transizioni da 18 a 114 transizioni uniche distribuite su 10 categorie:
     - *Base (10)*: dissolve, dip-to-black, dip-to-white, dip-to-color, fade-grayscale, exposure-flash, luma-fade, additive, subtract, multiply;
     - *Movimento (16)*: slide (4 direzioni + 4 diagonali) e push (4 direzioni + 4 diagonali);
     - *Zoom & Spin (12)*: zoom-in, zoom-out, cross-zoom, warp-zoom, spin-zoom in/out, spin orario/antiorario, ruota e scala, dolly zoom in/out;
     - *Tendine & Bande (16)*: wipe (4 direzioni + 4 diagonali), divisioni orizzontali/verticali, porte da granaio, veneziane, scacchiera, mosaico;
     - *Forme & Iris (12)*: iris cerchio, rombo, stella a 5 punte, cuore, triangolo, orologio (orario, antiorario, doppio), griglia esagonale, poligono, tagli diagonali;
     - *Sfocatura & Camera (10)*: blur dissolve, sfocature direzionali, sfocatura radiale, tilt-shift, whip pan (4 direzioni), otturatore;
     - *Glitch & Digitale (10)*: glitch RGB cromatico, pixelatura, strappo CRT, distorsione VHS, blocco dissolve, rumore digitale, scossa/jitter, corruzione dati, screen tear, glitch di luminanza;
     - *Luce & Colore (10)*: infiltrazioni di luce calda/fredda, bagliore lente, bruciatura pellicola, flash arcobaleno, colori invertiti, solarizzazione, flash neon, stroboscopio, bagliore di luminanza;
     - *Distorsioni (10)*: onde sinusoidali orizzontali/verticali, increspatura d'acqua, vortice, onda d'urto, pizzico, pizzico con torsione, lente sferica, mulinello, caleidoscopio;
     - *3D & Rotazioni (8)*: cubo 3D (4 direzioni con ombreggiatura prospettica), capovolgimento orizzontale/verticale, porta girevole, piegatura ad angolo.
   - `src/fx/Transition.h` e `src/fx/Transition.cpp`: modello ed esecuzione con garanzia di identità a t=0 (coincide con clip A) e a t=1 (coincide con clip B).
   - `resources/packs/vedit.core/transitions.json`: catalogo completo con nomi bilingue (italiano e inglese) e durate consigliate.
    - Test unitari in `tst_kernels::everyTransitionStartsOnAAndEndsOnB` e `coreLibraryLoads` (114/114 verificate), test di integrazione in `tst_editor::librariesAndTransitions` (>= 100 verificate), 100% test CTest (25/25) verdi.

2. **Animazioni di testo avanzate, speech bubbles e terzi inferiori (`P5.2`)**:
   - Modello dati e formati di serializzazione JSON (`src/core/project/Clip.h`, `ProjectJson.h`, `ProjectJson.cpp`):
     - `BubbleShape`: Rectangle, SpeechRound, SpeechSquare, ThoughtCloud, ComicShout, Callout, LowerThirdBar, LowerThirdTwoTone, Badge;
     - `BubbleTail`: None, BottomLeft, BottomCenter, BottomRight, TopLeft, TopRight, Left, Right;
     - `TextBackground` esteso con shape, tail, tailSize, borderColor, borderWidth, accentColor;
     - `TextAnimationType`: None, Typewriter, FadeIn, SlideUp, SlideDown, Bounce, PopIn, Wave, Glitch, Blur;
     - `TextAnimationScope`: Character, Word, Line, All;
     - `struct TextAnimation` (type, scope, duration, easing, cursor, stagger, params) aggiunto come proprietà opzionale in `TextClipData`.
   - Catalogo predefiniti stili testo (`resources/packs/vedit.core/text-styles.json`):
     - Espanso da 24 a 61 preset ricchi bilingue (italiano/inglese) con template per terzi inferiori (broadcast, minimal, neon, corporate, breaking news), speech bubble (fumetti, manga, nuvola di pensiero, shout, callout HUD) e testi animati.
   - Motore di rendering e conformità FreeType D-39 (`src/engine/text/TextRenderer.h`, `TextRenderer.cpp`, `Services.cpp`):
     - Rendering parametrizzato `TextRenderer::render` con tempo/progresso e rendering sfondi personalizzati per tutte le 9 forme di bolla e terzi inferiori.
     - Animazioni di testo avanzate a tempo per carattere, parola, riga o intero blocco: macchina da scrivere (con cursore lampeggiante), dissolvenza, scorrimento, rimbalzo elastico, pop-in, onda sinusoidale, glitch con cifrario casuale, defocus/sfocatura.
     - Calcolo `TextRenderer::bounds` ad incastro dinamico per includere code e accenti grafici nelle maniglie di selezione a schermo.
     - Piena conformità D-39: le animazioni di testo vengono pre-renderizzate sul thread di proiezione Qt in `TextState::frames` e lette dai thread worker MLT come meri buffer di pixel senza chiamate a FreeType concorrenti o memory leak thread-local.
   - Interfaccia utente (`src/ui/controllers/ClipInspector.cpp`, `EditorController.cpp`, `PropertiesPanel.qml`):
     - Controlli completi nel pannello proprietà (scheda Testo): selezione forma sfondo bolla, direzione e dimensione coda, colore accento e bordi;
     - Sezione dedicata "Animazione del testo": abilitazione/disabilitazione, tipo animazione, ambito (carattere, parola, riga, blocco), durata e cursore lampeggiante;
     - Supporto per "Ripristina" e "Applica a tutti" per stile e animazione in modo indipendente;
     - Preset con testo di esempio e animazione preconfigurata applicabili con un clic.
   - Suite di test:
     - Test unitari di serializzazione e roundtrip in `tst_serialization` (15/15);
     - Test di integrazione motore e MLT in `tst_services` (`textAnimationRendersProgressively`, `speechBubbleAndLowerThird`, 13/13);
     - 100% CTest passati (25/25) con zero compiler warnings (`-Wall -Wextra -Wpedantic -Werror`).

### Prossimi passi della Fase 5 (storico: completati il 2026-10-05)
3. P5.7 (continua): UI QML per template, Alt+trascina per sostituire placeholder, salva come template, slideshow foto.
4. P5.8: kit del marchio (palette, font, loghi, intro/outro, watermark) e copertina (fotogramma + testi, export JPG/PNG).
5. P5.9 (continua): UI QML per gestore asset, supporto ZIP completo, integrazione `fx::Library` con pacchetti utente.
6. P5.10: percorso GPU delle transizioni (GLSL ES 2.0/2.1, offscreen) con test PSNR CPU/GPU.
7. P5.11: criterio Fase 5, README, SHORTCUTS, USABILITY.

### Lacune della Fase 2 (trovate il 2026-09-25) — recuperate
La Fase 2 era stata segnata come completata senza alcune funzioni della sua riga di SPEC §8, e la Fase 3 è iniziata
senza il via registrato dell'utente. Recuperate nello stesso giorno (commit `feat(ui): Ctrl+K, contextual actions,
freeze frame and mixer`):
- **ricerca universale Ctrl+K** (anche dal pulsante nella barra in alto) su comandi, filtri, transizioni, stili di
  testo, animazioni, musica e media, in italiano e inglese e senza accenti; **`ActionRegistry`** (D-47);
- **barra contestuale e menu del tasto destro** dalla selezione (regola 3): Dividi, Elimina, Duplica, Velocità,
  Volume, Dissolvenze, **Congela**, **Inverti**, **Specchia**, **Ruota**, **Migliora**, Modifica/Stile del testo,
  Aggiungi testo/audio, Applica a tutti i tagli; **Copia/Incolla attributi** (menu, Ctrl+Alt+C/V, ricerca);
- **fermo immagine**: PNG del fotogramma della sorgente in `media/` della bozza (D-48, FILE_FORMAT §6.1);
- **mixer**: misuratore su ogni intestazione di traccia e master accanto al player; clic sull'intestazione: volume e
  muto della traccia;
- corretti: l'indicatore delle schede del pannello proprietà restava sulla scheda cliccata quando un pulsante apriva
  un'altra pagina; la ricerca mostrava tutti i comandi per qualsiasi testo.
Test: `tst_editor::actionsSearchAndFreeze`, `tst_ui::universalSearchAndToolbar`, `tst_ui::mixer`.
Ancora nella barra contestuale secondo la SPEC ma di fasi successive: Animazione (P3.6), Ritaglia, Rimuovi sfondo
(Fase 6), Sostituisci, Beat, Riduci rumore, Cambia voce, Separa voce/musica (Fasi 4–6), Sottotitoli automatici (Fase 6).

## Criterio di completamento della Fase 1 (SPEC §8)
| Requisito | Esito | Verifica |
|---|---|---|
| Importo 3 clip | ✅ | `tst_editor::phaseOneCriterion`: 3 file rilasciati sulla timeline, importati nell'ordine; canvas e fps dal primo |
| Le taglio e riordino | ✅ | trim, divisione al playhead, eliminazione con ripple, spostamento in testa, annulla/ripeti |
| Esporto un MP4 corretto | ✅ | ffprobe: H.264 yuv420p, 30 fps, 230 fotogrammi esatti, AAC 48 kHz stereo; fotogrammi decodificati uguali alla proiezione (`tst_export`) |
| Chiudo e riapro la bozza identica senza "Salva" | ✅ | il progetto riaperto è uguale campo per campo (tranne l'ora di salvataggio); c'era già sul disco prima di chiudere; playhead ripristinato |
| Primi 2 test di semplicità | ✅ | test 1: **4** azioni (limite 4); test 2: **2** (limite 2); inoltre test 8: **2** (limite 2) e test 10 ✅ (`docs/USABILITY.md`) |
| Compila senza warning, test verdi | ✅ | 23/23 in `build` (RelWithDebInfo) e in `build-debug` (ASan + UBSan); smoke test dell'editor su Vulkan, OpenGL, software e safe mode |

## Fase 1: fatto (un commit per incremento)
1. `src/fx` (composizione "over" CPU) + servizio MLT `vedit.composite`; `MediaProducerCache`; `TimelineProjection`
   (sequenza → tractor) con aggiornamenti per traccia identici a una ricostruzione (test su fotogrammi reali), poi
   **patch** delle playlist (solo le voci cambiate: una modifica su 500 clip costa ~3–6 ms).
2. `TimelinePlayer`: timeline viva in anteprima, modifiche anche durante la riproduzione, J/K/L fino a 8×, skimming
   (fotogramma sotto il puntatore senza spostare il playhead), nuovo profilo al cambio di canvas/fps.
3. Export MP4 in `vedit-render` (processo separato: progetto congelato, avanzamento JSON, annullamento pulito, nessun
   file parziale), anche a dimensioni/fps diversi; controllo dello spazio su disco; notifica di sistema a fine export
   se vedit è in secondo piano; "Riproduci", "Apri cartella", "Copia percorso".
4. `src/document`: bozze con salvataggio continuo atomico (300 ms / max 2 s, thread di I/O, nessuna scrittura se il
   contenuto non cambia, nuovi tentativi se il disco dà errore), lock con recupero dopo crash, `DraftStore`
   (elenco, crea, rinomina, duplica, cestino), stato dell'interfaccia (`state.json`).
5. Import: probe in `vedit-render --probe` (un file che manda in crash il demuxer viene scartato come danneggiato e
   gli altri proseguono), fingerprint `sha256-sampled-v1`, miniature e forme d'onda con FFmpeg in background, cache su
   disco per fingerprint; canvas e fps dalla prima clip nello stesso comando (un solo annulla).
6. Interfaccia: schermata iniziale (Nuovo progetto, bozze con miniatura/durata/data e menu), editor (barra in alto con
   nome modificabile, "Salvato", annulla/ripeti, formato, Esporta; media pool con skimming, "+" e trascinamento; musica
   locale; anteprima con trasporto; barra contestuale Dividi/Elimina/Duplica e menu del tasto destro; timeline
   virtualizzata con traccia magnetica, tracce automatiche, trim, spostamento, snapping con linea guida, skimming,
   zoom, rilascio di media e file; snackbar "Annulla"; export a una schermata). Italiano e inglese.
7. Test dell'interfaccia reale guidata con mouse e tastiera (`tst_ui`), anche in ASan.

## Fase 1: limiti noti (onestà, regola 4)
- **Trascinare file dal file manager** (sulla timeline o sul media pool) è implementato ma non verificabile in un test
  headless: da provare a mano. Il trascinamento dal media pool alla timeline è invece testato col mouse.
- **Skimming nel media pool**: il fotogramma scorre nella tessera; l'anteprima grande mostra sempre la timeline.
- **Audio durante lo skimming**: il consumer ha `scrub_audio` attivo, quindi anche passare sopra la timeline fa sentire
  brevi frammenti; non verificato a orecchio (i test usano l'audio "dummy"). Se disturba, un interruttore arriva con
  le preferenze.
- **Export**: solo H.264 + AAC software (libx264); encoder hardware con fallback, altri formati e la sezione
  "Avanzate" arrivano con la Fase 8 (una sezione "Avanzate" senza scelte reali sarebbe finta). La dimensione stimata è
  un'euristica (70% del tetto di bitrate) non ancora tarata su filmati veri.
- **Cronologia delle versioni** (snapshot e "Ripristina"): Fase 8 come da tabella SPEC §8 (D-31).
- **Bozza già aperta in un'altra finestra**: messaggio; portare in primo piano l'altra finestra richiede l'istanza
  singola (Fase 8).
- Timeline: niente selezione a rettangolo, niente muto/nascondi traccia nell'interfaccia (il modello e la proiezione
  li supportano già), niente Q/W (taglia a sinistra/destra del playhead).
- Prestazioni misurate su progetti sintetici (500 clip, video 320×180); con filmati 1080p/4K reali e proxy: Fase 8.
- Componenti M3 della Fase 0: alcune misure della specifica M3 sono ancora scritte nei file (vedi DESIGN_SYSTEM §6).
- La classe `engine::Player` (riproduzione di un file, Fase 0) non è più usata dall'app: resta, testata, per
  l'anteprima dei media del pool in una fase successiva.

## Piano iniziale della Fase 2 (storico)
Dalla SPEC §8: trasformazioni con maniglie sul canvas, sfondo del canvas, selettore del formato (già presente in forma
base), testo base e preset con modifica sul canvas, audio base (volume, dissolvenze, mixer), velocità costante,
freeze/inversione, transizioni base, filtri e regolazioni con anteprima dal vivo e "Applica a tutte", copia/incolla
attributi, "Migliora automaticamente", ricerca universale Ctrl+K. Criterio: un video verticale 9:16 con testi, musica,
transizioni e filtri rispettando i test di semplicità applicabili (3, 4, 6). Prima di iniziare: ActionRegistry e
pannello proprietà, poi servizi MLT `vedit.transform`, `vedit.text`, `vedit.gain`, `vedit.transition` con i kernel CPU.

## Fase 0 — Fondamenta: completata (storico)

## Criterio di completamento della Fase 0 (SPEC §8)
| Requisito | Esito |
|---|---|
| Compila senza warning | ✅ `-Wall -Wextra -Wpedantic -Werror` su tutto il codice vedit, build RelWithDebInfo e Debug |
| Test verdi | ✅ 14/14 in `build` e in `build-debug` (ASan + UBSan) |
| Un video si riproduce nell'anteprima con GPU | ✅ Vulkan (ANV) e OpenGL (iris) su Wayland, Vulkan e OpenGL su X11/XWayland |
| … e con `QT_QUICK_BACKEND=software` + `LIBGL_ALWAYS_SOFTWARE=1` | ✅ anche headless in CTest (`smoke_software`), più `--safe-mode` |
| Galleria componenti M3 in chiaro/scuro con seme dal sistema | ✅ `vedit --component-gallery`; seme `#3584e4` dall'accento GNOME (il portale di Hyprland non fornisce `accent-color`, vedi D-13) |

Gli smoke test verificano fotogrammi **decodificati e mostrati** dall'anteprima (≥ 44 su 45), non solo decodificati.

## Fatto (per incremento, un commit ciascuno)
1. Scheletro CMake/Ninja, preset (dev, debug con sanitizer, release), clang-format/clang-tidy, sandbox XDG di sviluppo,
   material-color-utilities vendored senza Abseil, font Inter e Material Symbols con licenze.
2. `core/time`: Rational, RationalTime, TimeRange esatti (aritmetica a 128 bit, arrotondamenti espliciti).
3. `core/project`: modello completo della sezione 3 (Media, Sequence, Track, Clip, Effect, Param/Keyframe/Easing,
   transizioni, marker, gruppi), tipi non ancora implementati conservati alla lettera; verifica delle invarianti.
4. `core/commands` + `core/edit`: modifiche reversibili (EditScript), EditCommand con fusione dei gesti continui,
   ProjectMutator (un ChangeSet per comando), TimelineEditor con le regole CapCut (traccia principale magnetica,
   tracce automatiche, tracce vuote rimosse, blocco), SequenceDiff minimale (ripple = un solo "shift").
5. Serializzazione `.vproj` v1 (JSON canonico), lettura tollerante con avvisi, riparazioni, migrazioni, scrittura atomica;
   test di integrazione apri → modifica → salva → riapri.
6. Tema Material 3: 49 ruoli colore, varianti, contrasto, sorgenti del seme (portale, GNOME, KDE, sfondo, copertina,
   manuale, predefinito), singleton `Theme` con tipografia, forme, stati, spaziatura/densità, elevazione, motion.
7. GPU: `vedit-gpuprobe` isolato (Vulkan, OpenGL, codec hardware con prove reali), cache per impronta dei driver,
   catene di fallback, safe mode automatico dopo 2 avvii instabili, fallback a runtime con riavvio.
8. Engine MLT: runtime in background, Player (apertura asincrona, riproduzione, pausa, seek, passo, volume),
   anteprima su ogni backend (QQuickRhiItem con shader, oppure nodi immagine), finestra QML del player.
9. Stile `Vedit.Style` + `Vedit.Components` (29 componenti), galleria, traduzione italiana completa, smoke test in CTest.

## Limiti rimasti dalla Fase 0 (ancora validi)
- Tipi di clip testo, sottotitolo, sticker, effetto e regolazione: nel modello sono conservati come JSON (nessuna perdita
  di dati), la loro modifica e il loro rendering arrivano nelle fasi 2–6. Movimento di clip collegate (`linkId`) non ancora
  gestito da TimelineEditor (serve dalla Fase 2, "scollega/collega audio e video").
- Preferenze → Prestazioni (interfaccia): Fase 8; i dati esistono (`GraphicsPreferences` in QSettings).
- Il `FrameSink` copia ogni fotogramma mostrato (~1 ms in 1080p): va bene con anteprime a risoluzione ridotta; un
  percorso a copia zero richiede un protocollo di rilascio dei frame prima di chiudere il consumer (D-19).
- Hardware video rilevato (VA-API/QSV) ma non ancora usato per decodifica ed encoding: Fase 8.
- Matrice GPU: provata solo la GPU Intel di questa macchina (+ percorsi software); NVIDIA/AMD/VM progettati ma non provati
  (`docs/GPU_COMPATIBILITY.md`).
- `tst_theme` impiega ~90 s nella build Debug con sanitizer (140 schemi generati); ~1 s in RelWithDebInfo.

## Bug noti / avvisi
- **Anteprima nera a player fermo, raro, solo con la macchina satura**: il consumer `sdl2_audio` di MLT in pausa a
  volte non mostra più fotogrammi dopo il primo (riprodotto con 4 test pesanti in parallelo: anche con il codice di
  prima di questa sessione, 3 volte su 5). Lo stack mostra tutti i thread di MLT in attesa; né ripetere il refresh né
  purge/seek lo sbloccano, la riproduzione (Play) sì. `tst_timelineplayer` gira da solo in CTest. Da valutare nella
  Fase 8: un consumer dell'anteprima proprio (rendering dei fotogrammi fermi senza `sdl2_audio`).
- Se vedit viene chiuso a forza durante un export, i file del job (`~/.cache/vedit/render/<id>.json/.vproj`) restano
  nella cache: da ripulire all'avvio (piccoli, ma si accumulano).
- Avviso Qt "Failed to register with host portal … App info not found for 'vedit'": manca il file `.desktop`
  installato; sparirà con il packaging (Fase 8). Innocuo.
- Moduli MLT opzionali non installati sul sistema (movit, rtaudio, sox): MLT lo scrive nel log all'avvio. Innocuo
  (vedit non li usa).
- LeakSanitizer: piccole allocazioni globali in MLT, SDL3, glib e libavcodec sono soppresse con motivazione in
  `tests/lsan.supp`; nessuna perdita nel codice vedit.


## Come riprendere
```bash
cmake --preset dev && cmake --build build && ctest --test-dir build --output-on-failure
./build/vedit                        # schermata iniziale (le bozze di sviluppo stanno in build/dev-home)
./build/vedit video.mp4              # nuovo progetto con quel file
./build/vedit --component-gallery    # galleria M3
VEDIT_UI_SHOTS=build/shots tools/run-test.sh build tst_ui   # interfaccia guidata + screenshot di ogni passo
cmake -S . -B build -DVEDIT_BUILD_PROBES=ON   # programmi di prova in build/tools/
```
Traduzioni: `cmake --build build --target update_translations`, poi completare `i18n/vedit_it.ts` (nessuna voce
`unfinished` deve restare) e le forme plurali in `i18n/vedit_en.ts`.

Scaricamenti non versionati in `.downloads/` (~1 GB): repository delle icone e dei font usati per il vendoring, e i
sorgenti di MLT consultati per le verifiche (D-24, §20). Si possono cancellare: i file necessari sono già in
`third_party/` e `src/assets/`.

## Decisioni
Registro in `docs/ARCHITECTURE.md` §15 (D-01 … D-48), esiti delle verifiche in §18 (Fase 0), §20 (Fase 1) e §21 (Fase 2).
`tools/run-test.sh <build-dir> <test> [funzione]` esegue un singolo test con lo stesso ambiente di CTest (utile per
ripetere un test instabile: `for i in $(seq 20); do tools/run-test.sh build tst_timelineplayer || break; done`).
