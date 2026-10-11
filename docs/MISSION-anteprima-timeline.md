# Velacut — Missione: anteprima fluida con effetti/transizioni + timeline sistemata come si deve

Sei Claude Code e lavori nel repository **Velacut** (editor video desktop Linux: C++20, Qt 6 Quick/QML, MLT 7, FFmpeg).
Questo prompt è il tuo incarico completo. Leggilo tutto prima di toccare il codice.

---

## 0. Regole di base (non negoziabili)

1. Rispetta **tutto** `CLAUDE.md` (sicurezza, niente `sudo`, niente comandi distruttivi, niente `git push`,
   piccoli incrementi, compila + test verdi dopo ogni incremento, Conventional Commits, `-Werror`, nessun colore/
   dimensione/durata hardcoded nei QML → solo `Theme`, ogni stringa visibile con `qsTr()`/`tr()` IT+EN,
   tempi con `RationalTime`, percorso CPU di riferimento sempre presente).
2. Leggi `docs/PROGRESS.md` e le sezioni §5 (Engine), §6 (fx), §9 (UI), §15 (Decisioni) di `docs/ARCHITECTURE.md`.
   **Non** serve rileggere tutta `SPEC-editor-video.md` per questo incarico: consultala solo per le sezioni che tocchi
   (§1bis prestazioni/GPU, §5.2 timeline, §5.11bis transizioni).
3. **Questo prompt vale come approvazione** delle decisioni architetturali elencate qui sotto. Registrale come nuove
   voci `D-xx` in `docs/ARCHITECTURE.md` §15 man mano che le implementi. Per decisioni architetturali **non** coperte
   qui, o per qualsiasi modifica al formato `.vproj` (`docs/FILE_FORMAT.md`), **fermati e chiedi**.
4. **Misura prima, poi ottimizza.** Ogni ottimizzazione deve avere un numero "prima" e un numero "dopo" ottenuti con
   lo stesso banco di prova (Fase 0). Niente "dovrebbe essere più veloce".
5. **Verifica invece di indovinare** (regola 5 di CLAUDE.md): dove questo prompt dice *[da verificare]*, scrivi prima
   un piccolo programma di prova in `tools/probes/` e osserva il comportamento reale di MLT/Qt.
6. Non riscrivere il progetto. Non cambiare l'architettura generale (core → proiezione MLT → consumer → FrameSink →
   QQuickRhiItem). Interventi mirati, uno alla volta.
7. Lavora le fasi **in ordine**, senza fermarti tra una fase e l'altra, salvo: (a) decisione non coperta da questo
   prompt, (b) un obiettivo numerico che risulta irraggiungibile (spiega perché e proponi l'alternativa),
   (c) un test esistente che non riesci a far tornare verde senza cambiare comportamento voluto.
8. Usa la lista attività (todo) per tracciare le fasi e i sotto-punti; aggiorna `docs/PROGRESS.md` alla fine.

---

## 1. Diagnosi già fatta (lettura statica del codice — parti da qui, non ricominciare da zero)

Legenda: **[certo]** = verificato leggendo il codice; **[da verificare]** = ipotesi forte, confermala con una misura/probe.

### 1.1 Perché l'anteprima lagga appena metti un blur o una transizione

**A. Gli effetti video girano su un solo core, con algoritmi lenti** [certo]
- `src/engine/mlt/Services.cpp:1104` `videoEffectGetImage` chiama `fx::renderVideoEffect` **senza** `runSliced`
  (al contrario di composite/transform/adjust/transition che sono a fasce). Stesso problema per
  `motionBlurGetImage` (`Services.cpp:420`, usa l'overload non a fasce di `applyMotionBlur`) e per `beatGetImage`.
- Il ramo `mix < 0.999` di `videoEffectGetImage` fa una copia dell'immagine e poi un ciclo per byte con `lround` e `%`,
  anch'esso single-thread.
- `src/fx/VideoEffect.cpp:171` `blur()`: 3 iterazioni × 2 direzioni, chiamata di lambda per ogni canale di ogni pixel,
  divisioni `i/4`, `i%4` nel caricamento della riga, e **passata verticale colonna per colonna** (accesso a stride
  dell'intera riga → cache miss continui). Il raggio è `h*0.03*amount` → a 1080p fino a ~32 px.
- `radialBlur()` (`VideoEffect.cpp:299`, Zoom/Spin blur): 12 campioni bilineari in `double` per pixel con
  `cos`/`sin` ricalcolati per ogni pixel e ogni campione.
- La struct `Copy` e molti kernel allocano `std::vector` grandi quanto il fotogramma **a ogni fotogramma**.
- Il consumer usa `real_time = 1` (un solo thread di rendering, scelta motivata da un bug di MLT, vedi
  `PreviewConsumer.cpp`): quindi tutto ciò che non è a fasce è seriale e blocca il fotogramma.

**B. Lo scheduling dell'anteprima si auto-sabota quando un fotogramma costa > ~120 ms** [certo nel codice, effetto da misurare]
- `TimelinePlayer::scrubSeek` / `skim` (`src/engine/playback/TimelinePlayer.cpp` ~righe 421 e 524): un "watchdog"
  azzera `m_seekInFlight` dopo 120 ms e lancia un nuovo `showFrame()`, che fa **`purge()`** del consumer.
  Se il fotogramma con blur/transizione impiega più di 120 ms, ogni nuovo seek butta via il rendering in corso:
  durante lo scrub l'anteprima **non mostra quasi nulla** finché non ti fermi (livelock).
- Ogni movimento di uno slider di effetto (`EffectSlider.qml` → `ClipInspector::setEffectParam`) crea un comando,
  `Project::changed` → `TimelinePlayer::onProjectChanged` → `afterProjectionChange()` → `showFrame()` → `purge` +
  `seek`. A 60 eventi/s con un fotogramma da 100+ ms l'anteprima resta ferma fino al rilascio. Nessun coalescing.
- `TimelinePlayer::skim` quando c'è già un seek in volo **scarta** la posizione nuova senza memorizzarla
  (`TimelinePlayer.cpp:531`): quando il mouse si ferma l'anteprima resta su un fotogramma vecchio. Stesso effetto sulle
  maniglie di trim (che usano `skim`). **Bug.**
- Ogni refresh dopo `purge`+`seek` ridecodifica la sorgente da zero (seek al keyframe + decodifica del GOP) anche se
  hai cambiato solo un parametro dell'effetto sullo stesso fotogramma. *[da verificare: se la cache immagine del
  producer `avformat` di MLT evita la ridecodifica sulla stessa posizione]*.

**C. Le transizioni costano 3 decodifiche e, sulla stessa sorgente, fanno "seek thrashing"** [certo nel codice, impatto da misurare]
- Le transizioni sono piccoli tractor su una playlist "transizioni" sopra la playlist delle clip (D-35). Nella
  finestra della transizione la **playlist delle clip sotto continua a essere renderizzata** (decodifica + effetti +
  transform) e poi viene coperta da un'immagine opaca: lavoro buttato. `compositeGetImage` (`Services.cpp:87`) chiede
  sempre prima l'immagine del livello inferiore.
- `TimelineProjection::transitionProducer` (`TimelineProjection.cpp:1329`) usa per i lati A e B **lo stesso**
  `placed.producer`, che `MediaProducerCache` condivide tra tutte le clip dello stesso media. Dopo uno "Split" le due
  clip vengono dallo stesso file: per ogni fotogramma il decoder salta avanti e indietro tra due posizioni lontane →
  seek + decodifica GOP a ogni fotogramma. *[da verificare con misura: probabilmente la causa n.1 del lag delle transizioni]*
- `GpuTransitions::render` (`src/engine/gpu/GpuTransitions.cpp`): mutex globale + `BlockingQueuedConnection` verso un
  thread GL + premoltiplicazione CPU single-thread di A e B + upload + draw + **`glReadPixels` sincrono** + flip delle
  righe. Il giro completo può essere **più lento** del kernel CPU a fasce. *[da verificare con misura]*
- `vedit.composite` fa sempre una fusione a schermo intero, anche quando il livello sopra è opaco e copre tutto
  (es. traccia principale sopra lo sfondo colore).

**D. Si renderizzano molti più pixel del necessario** [certo]
- `AppController::previewLimit()` limita l'anteprima solo su iGPU/software. Su GPU discreta l'anteprima lavora alla
  risoluzione **piena del canvas** (un progetto 4K = pipeline CPU a 4K), anche se il pannello è largo ~900 px.
- Non esiste un controllo "qualità anteprima" (piena / ½ / ¼) promesso in `ARCHITECTURE.md` §5.3.
- **I proxy automatici** (§5.5: 4K, HEVC, 10 bit, VFR → H.264 ≤720p GOP corto) **non sono implementati**: esistono solo
  i proxy invertiti. Il campo `proxy: "auto"|"off"` esiste già nel formato (`FILE_FORMAT.md` §5): nessuna modifica di
  formato necessaria.
- Nessuna decodifica hardware nel percorso di anteprima (producer `avformat` senza hwaccel).

**E. Documentazione e codice che raccontano cose non vere** [certo]
- `FINAL_STATUS.md` dichiara "zero-copy UMA con texture NV12/P010", "LRU cache 15-30 frame", "playhead disaccoppiato
  a 60/120 FPS": in realtà `HwVideoDecoder` e `BoundedLruCache` sono compilati in `velacut_gpu` ma **non sono usati
  dall'anteprima** (solo da `tst_hwdecoder`); il FrameSink riceve sempre RGBA copiati da MLT; il playhead è guidato da
  `player.position` aggiornato solo quando arriva un fotogramma.
- `FINAL_STATUS.md`, `SESSION_FINAL.md`, `SESSION_SUMMARY.md` sono marcati "superati" ma stanno ancora nella radice e
  confondono chi legge (agenti inclusi).

### 1.2 Problemi della timeline

**Bug / correttezza**
- `TimelineModel` espone `cuts`, `tracks`, `markers` come `QVariantList`: ogni volta che cambiano, i `Repeater` in
  `TimelineView.qml` (righe ~372, ~725, ~784) **distruggono e ricreano tutti i delegati**. `cuts` cambia anche solo
  scorrendo (dipende dall'intervallo visibile). Trascinando il bordo di una transizione selezionata,
  `inspector.set("transition.duration")` cambia `cuts` → il delegato che sta ricevendo il trascinamento viene
  distrutto a metà gesto. *[da verificare con un test `tst_ui`: probabilissimo]*
- `skim` che perde l'ultima posizione (vedi 1.1-B): la linea di skimming e l'anteprima non coincidono col cursore.
- `TimelinePlayer::timecode()` mescola secondi reali (`frame / 29.97`) con `frame % 30`: a 29.97/59.94 fps il timecode
  è sbagliato su timeline lunghe (es. fotogramma 17982 → "10:00.12"). Il righello usa ancora un'altra formula.
- Il "+" delle transizioni (cerchio da 22 px centrato sul taglio, `z: 5`) copre le maniglie di trim (10 px per lato)
  delle due clip adiacenti nella fascia centrale.

**Prestazioni**
- `rowTop(row)` è un ciclo O(n) chiamato in binding da ogni delegato → O(n²) a ogni cambiamento.
- Miniature (`MediaThumbnail`) e forma d'onda (`WaveformView`) sono `QQuickPaintedItem`: rasterizzazione CPU +
  upload della texture a ogni scroll/zoom per ogni clip visibile. Una sola striscia di miniature per media, a
  risoluzione fissa (l'architettura prevede strisce per livello di zoom).
- Ogni `TimelineClip` istanzia il proprio `Menu` contestuale con 3 `Repeater` (`TimelineClip.qml:268`).
- Righello fatto con `Repeater` di Item+Label ricreati a ogni cambio di zoom.
- Il playhead durante la riproduzione salta quando l'anteprima perde fotogrammi (segue `player.position`).
- `scrubDebounceTimer` 80 ms + rate-limit 33 ms + un solo seek in volo: lo scrub sembra "gommoso".

**UX (rispetto a CapCut, che è il riferimento dichiarato nella SPEC)**
- Trascinando una clip sulla traccia principale magnetica le altre clip non si spostano dal vivo: si vede solo dove
  "atterra" dopo il rilascio. Idem per il trim con ripple.
- Nessuno scorrimento automatico quando trascini una clip o una maniglia di trim verso il bordo della vista
  (c'è solo per lo scrub del playhead).
- Rotella: solo `angleDelta` (niente `pixelDelta` del touchpad → scorrimento a scatti), zoom solo a passi ×1.25,
  niente pinch.
- Niente selezione a rettangolo (noto in PROGRESS.md), niente spostamento di più clip selezionate insieme.

---

## 2. Piano di lavoro

### Fase 0 — Strumentazione e baseline (obbligatoria, prima di ogni ottimizzazione)

1. **Banco di prova headless** `tools/bench/preview_bench` (target CMake opzionale `-DVELACUT_BUILD_BENCH=ON`, oppure un
   test CTest etichettato `bench` escluso dal run normale). Genera i media con `ffmpeg -f lavfi` dentro `build/`
   (come fanno già i test). Scenari, ognuno a 1080p30 e 2160p30 di canvas:
   - S1: 1 clip H.264, nessun effetto (riferimento).
   - S2: 1 clip + `blur` amount 1.0.
   - S3: 1 clip + `zoomBlur` amount 1.0.
   - S4: 2 clip **dello stesso file** (split) + transizione dissolvenza 1 s.
   - S5: 2 clip di file diversi + transizione `blurDissolve`.
   - S6: S2 + una traccia overlay testo + filtro colore (scenario "realistico").
   Per ogni scenario misura con il grafo vero (`TimelineProjection` + consumer `null` o chiamate dirette
   `get_frame`/`get_image`): **ms/fotogramma medio e p95** in riproduzione sequenziale, **latenza di un seek singolo**
   (fotogramma casuale), e **latenza di "cambio parametro sullo stesso fotogramma"**.
2. **Tempi per servizio**: aggiungi una categoria di log `velacut.engine.perf` (disattivata di default) che registra il
   tempo di ogni `*GetImage` dei servizi `vedit.*` e della decodifica. Niente costo quando è spenta.
3. **HUD di debug nell'anteprima** (solo con una variabile d'ambiente, es. `VELACUT_PREVIEW_HUD=1`, o una voce nascosta
   nelle preferenze avanzate): fps effettivi, fotogrammi persi (`FrameSink::framesDropped`), ms dell'ultimo fotogramma,
   risoluzione di rendering. Testi con `qsTr`, stile da `Theme`.
4. Scrivi i numeri della baseline in `docs/PERFORMANCE.md` (nuovo file: scenari, macchina, numeri). Ogni fase
   successiva aggiunge una colonna "dopo".

### Fase 1 — Scheduling dell'anteprima (bug, non ottimizzazioni)

1. **Niente più purge a vuoto.** Riscrivi la logica seek/scrub/skim di `TimelinePlayer` attorno a una sola regola:
   *un solo rendering in volo, l'ultima richiesta vince, e un rendering in corso non viene mai buttato per una
   richiesta di anteprima*. Concretamente:
   - una sola variabile "richiesta pendente" condivisa da `scrubSeek`, `skim`, cambi di parametro;
   - quando arriva il fotogramma (`onFrameShown`) si spedisce la pendente, se c'è;
   - il watchdog non deve più fare `purge`+re-seek a 120 ms: usa un timeout adattivo (es. `max(1 s, 4 × tempo medio
     misurato dell'ultimo fotogramma)`) e solo come recupero;
   - `purge` resta obbligatorio dove serve davvero (cambio del grafo, play/pausa: vedi commenti esistenti su
     `sdl2_audio`), non per ogni seek di anteprima. *[da verificare con probe: come si comporta `sdl2_audio` con
     `refresh` senza `purge` quando la coda è vuota]*.
2. **Fix `skim`**: memorizza la posizione pendente invece di scartarla (stesso meccanismo del punto 1). Test:
   in `tst_timelineplayer`, una raffica di `skim()` con rendering lento termina mostrando **l'ultima** posizione.
3. **Coalescing delle modifiche dal vivo**: in `TimelinePlayer::onProjectChanged`, se un rendering è in volo, applica
   la proiezione ma rimanda il refresh al fotogramma consegnato (o al massimo uno ogni ~33 ms), sempre con lo stato
   più recente; alla fine del gesto (rilascio dello slider) il fotogramma finale deve essere esatto. Valuta anche di
   rimandare `TimelineProjection::update` stessa se è costosa (misura).
4. **Cache del fotogramma sorgente prima degli effetti** *[da verificare prima con probe se MLT lo fa già]*: un filtro
   leggero `vedit.source_cache` messo **per primo** su ogni cut che tiene gli ultimi N (2–4) fotogrammi RGBA decodificati
   per (producer, posizione, dimensione). Cambiare un parametro di un effetto su un fotogramma fermo deve costare
   solo l'effetto, non decodifica+seek. Memoria limitata e liberata alla distruzione del grafo.
5. Test di regressione per ciascun punto (unit/integrazione con un servizio di prova "lento" che dorme N ms).

### Fase 2 — Costo per fotogramma (CPU)

1. **Tutti i kernel a fasce**: `videoEffectGetImage`, `motionBlurGetImage` (usa l'overload a fasce che esiste già in
   `MotionBlur.cpp`), `beatGetImage`, il ciclo di `mix`. Per i kernel che leggono i vicini (remap, blur, distorsioni):
   copia sorgente una volta, fasce sulle righe di output.
2. **Blur veloce** (`fx::blur` e `fx::boxBlur`): box blur separabile con somme scorrevoli intere, passata orizzontale a
   fasce per riga, passata verticale a **blocchi di colonne contigue** (es. 16–32 px) o via trasposizione, buffer
   riusati. Per raggi grandi (≥ 8 px) consentito il percorso "riduci → sfoca → ingrandisci" (stesso risultato
   percettivo, costo ~1/16). Il risultato cambia leggermente anche nell'export: accettabile **solo** se PSNR ≥ 38 dB
   rispetto all'implementazione attuale sulle immagini di test; aggiorna i riferimenti dei test in modo consapevole e
   registra la decisione D-xx.
3. **`radialBlur`**: `float`, seni/coseni dei 12 campioni precalcolati fuori dal ciclo, a fasce, campionamento
   bilineare in fixed-point o float.
4. **Niente allocazioni per fotogramma** nei kernel caldi: buffer `thread_local` o `mlt_pool_alloc` riusati.
5. **Transizioni sulla stessa sorgente**: i lati A e B di una finestra di transizione devono usare producer distinti
   (es. chiave di `MediaProducerCache` con una "corsia" A/B, aperti in background come gli altri). Misura S4 prima/dopo.
6. **Non renderizzare ciò che è coperto**: `vedit.transform` marca il fotogramma con una proprietà (es.
   `vedit.opaque=1`) quando il risultato è certamente opaco e copre tutto il canvas (sorgente senza alfa, opacità 1,
   niente crop/rotazione/scala < copertura, niente maschere/chroma). `vedit.transition` la propaga se entrambi i lati
   sono opachi. `compositeGetImage` legge **prima** il livello B; se è opaco, a piena opacità e in modalità normale,
   **non chiama `mlt_frame_get_image` sul livello A** (i livelli sotto non vengono né decodificati né elaborati) e usa
   direttamente B. *[da verificare con probe: che saltare il get_image di A non rompa audio, cache o lifecycle dei
   frame MLT]*. Questo elimina sia il lavoro sotto le transizioni sia la fusione inutile con lo sfondo.
7. **GPU transitions**: misura nel banco S4/S5 il percorso GPU contro il CPU a fasce alla risoluzione dell'anteprima.
   Se il GPU è più lento, la scelta deve essere automatica: al self-check esistente aggiungi una misura dei due percorsi
   alla dimensione reale e usa il più veloce (per anteprima ed export separatamente). Premoltiplicazione a fasce.
   Non introdurre letture asincrone complicate in questa fase.
8. Ricontrolla con `tst_transitions`, `tst_fx`, `tst_kernels`, `tst_gputransitions` che la parità CPU/GPU e i
   riferimenti siano rispettati.

### Fase 3 — Pixel giusti: risoluzione adattiva, proxy, playhead fluido

1. **Risoluzione di rendering = ciò che si vede**: l'anteprima renderizza al massimo alla dimensione in pixel del
   riquadro di anteprima × devicePixelRatio (arrotondata a numeri pari), mai oltre il canvas. Lo schermo intero (F11)
   alza il limite. *[da verificare con probe: se impostare `width`/`height` del consumer — senza cambiare il profilo —
   fa lavorare tutta la catena alla dimensione ridotta, dato che i nostri servizi usano `*width/*height`; se sì, il
   cambio è a caldo senza ricostruire il grafo; se no, proponi l'alternativa prima di ricostruire il profilo a ogni
   ridimensionamento]*.
2. **Qualità anteprima** nel pannello dell'anteprima: `Auto` (default) / `Piena` / `½` / `¼`, salvata nelle
   preferenze (non nel progetto). In `Auto`: durante riproduzione, scrub e trascinamento di slider, se il tempo medio
   per fotogramma supera ~80% del budget (1/fps) scende di un gradino; da fermo, dopo ~250 ms di inattività, rende il
   fotogramma a piena qualità. Indicatore discreto della qualità attiva. Rispetta Theme/qsTr.
3. **Proxy automatici** (ARCHITECTURE §5.5, già previsti dal formato): coda in background che genera con
   `velacut-render` (nuovo `--job proxy`) un H.264 ≤720p, GOP corto senza B-frame, CFR, per media > 1080p, HEVC/AV1,
   10 bit o VFR; encoder hardware se disponibile, altrimenti `libx264 veryfast`. Percorso
   `~/.cache/velacut/proxy/<fingerprint>-<profilo>.mp4` (sandbox di sviluppo rispettata). L'anteprima usa il proxy
   quando è pronto (scambio a caldo come per i proxy invertiti), l'export **sempre** l'originale. Interruttore globale
   nelle preferenze e rispetto di `proxy: "off"` per media. Progresso visibile nel pannello media.
4. **Playhead fluido in riproduzione**: in QML interpola la posizione visiva tra un aggiornamento e l'altro
   (`FrameAnimation`, posizione stimata = ultima posizione + tempo trascorso × rate × fps), riallineandola alla
   posizione reale quando la deriva supera un fotogramma. Il timecode mostrato resta quello reale.
5. Aggiorna `docs/PERFORMANCE.md` con i numeri dopo le fasi 1–3.

### Fase 4 — Timeline sistemata come si deve

**4a. Bug**
1. `cuts`, `tracks`, `markers` diventano modelli veri (`QAbstractListModel` con ruoli e aggiornamento a diff, come già
   fa la lista delle clip), così i delegati sopravvivono alle modifiche. Test `tst_ui`: trascinare il bordo di una
   transizione per 20 movimenti consecutivi cambia la durata senza interrompere il gesto.
2. `timecode()` e etichette del righello: una sola funzione corretta basata su `RationalTime` (minuti:secondi.fotogrammi
   nel tempo reale), giusta anche a 23.976/29.97/59.94. Unit test con questi casi.
3. Il "+" delle transizioni non deve coprire le maniglie di trim: più piccolo, posizionato nella metà inferiore della
   traccia o visibile solo al passaggio vicino al taglio; area di click delle maniglie prioritaria. Test `tst_ui`.
4. Le posizioni Y delle tracce si calcolano una volta (array `rowTops` nel modello o proprietà calcolata una volta in
   QML), niente più `rowTop()` O(n) nei binding.

**4b. Prestazioni**
1. `MediaThumbnail` e `WaveformView` diventano item a nodi del grafo di scena (`QSGImageNode`/`QSGGeometryNode`):
   la striscia caricata **una volta** come texture, le tessere disegnate dalla GPU; lo scroll cambia solo coordinate.
   Mantieni il percorso del backend software (nodi immagine funzionano anche lì).
2. Miniature per livello di zoom (come da ARCHITECTURE §5.5): almeno 2–3 densità, scelte in base ai px per fotogramma.
3. Un **solo** `Menu` contestuale condiviso dalla timeline, aperto per la clip cliccata.
4. Righello disegnato da un unico item (C++ a nodi o `Shape`), non un `Repeater` di Item+Label.
5. Obiettivo: scroll orizzontale e zoom a 60 fps con 300 clip visibili/caricate sulla macchina di riferimento
   (misuralo con un test di stress o con l'HUD).

**4c. Interazione (stile CapCut)**
1. **Riflusso dal vivo sulla traccia principale magnetica**: mentre trascini una clip, le altre si spostano per mostrare
   dove andrà (animazione con durate `Theme`), con indicatore d'inserimento; il comando resta uno solo al rilascio.
   Lo stesso per il trim con ripple: le clip successive seguono il bordo durante il gesto.
2. **Auto-scroll** ai bordi durante trascinamento di clip e maniglie (riusa la logica di `scrubAutoScrollTimer`).
3. **Rotella e touchpad**: usa `pixelDelta` quando presente (scorrimento continuo), Ctrl+rotella e pinch con zoom
   continuo ancorato sotto il puntatore; Shift+rotella verticale tra le tracce (già c'è, mantienilo).
4. **Scrub reattivo**: con la Fase 1 fatta, riduci/elimina il debounce da 80 ms se le misure lo permettono; il
   fotogramma sotto il playhead deve comparire entro ~100 ms da quando ti fermi, anche con blur.
5. **Selezione a rettangolo** sullo spazio vuoto e **spostamento di più clip selezionate** insieme (un comando).
6. Frecce = ±1 fotogramma, Maiusc+frecce = ±1 secondo (se non già presenti; controlla `docs/SHORTCUTS.md` e
   aggiornalo).
7. Ogni nuova interazione: test in `tst_ui` + conteggio azioni in `docs/USABILITY.md` se cambia un percorso utente.

### Fase 5 — Solo se dopo le fasi 1–4 gli obiettivi non sono raggiunti

Proponi (non implementare senza chiedere) una delle due strade, con stima e misure a supporto:
- **cache di rendering in background** delle porzioni di timeline con effetti pesanti (fotogrammi già calcolati in RAM/
  disco, invalidati dall'intervallo del `ChangeSet`), come il "render in background" di Final Cut;
- **percorso GPU per i kernel più costosi** (blur, transizioni) con texture persistenti e lettura asincrona, sempre con
  parità PSNR rispetto al CPU di riferimento.

### Fase trasversale — Pulizia

1. Sposta `FINAL_STATUS.md`, `SESSION_FINAL.md`, `SESSION_SUMMARY.md` in `docs/archive/` con `git mv` (non cancellare).
   Correggi in quei file o in `PROGRESS.md` le affermazioni false sul "zero-copy UMA"/"LRU cache"/"playhead 120 FPS".
2. `HwVideoDecoder` + `BoundedLruCache` + percorso NV12/P010 di `VideoSurfaceRhi`: **non** cancellarli; scrivi in
   `PROGRESS.md` che non sono collegati all'anteprima e cosa servirebbe per collegarli. Non collegarli in questo
   incarico.
3. Aggiorna `ARCHITECTURE.md` (§5.3, §5.5, §9, §15) e `PROGRESS.md` (fatto, numeri, bug noti, prossimi passi).

---

## 3. Criteri di accettazione (misurati con il banco della Fase 0, macchina di riferimento di `ARCHITECTURE.md` §1)

| # | Criterio | Obiettivo |
|---|---|---|
| 1 | S2 (blur 1.0) a 1080p, ms/fotogramma | ≥ 5× più veloce della baseline **e** ≤ 33 ms alla risoluzione d'anteprima in `Auto` |
| 2 | S4 (transizione stessa sorgente) a 1080p | riproduzione in tempo reale a 30 fps, fotogrammi persi < 5% |
| 3 | S5/S6 a 1080p in riproduzione | ≥ 30 fps effettivi in `Auto` (può scendere a ½, non deve scattare) |
| 4 | Scrub con S2/S4 | un fotogramma aggiornato almeno ogni 100 ms durante il trascinamento; fotogramma esatto ≤ 150 ms dopo lo stop |
| 5 | Slider di un effetto trascinato su fotogramma fermo | anteprima che si aggiorna ≥ 10 volte/s; valore finale esatto al rilascio |
| 6 | Progetto 4K con media 4K HEVC (proxy pronto) | riproduzione fluida come il caso 1080p |
| 7 | Timeline con 300 clip | scroll/zoom senza scatti (≥ 55 fps nell'HUD), nessun delegato ricreato durante lo scroll |
| 8 | Export | identico a prima entro le soglie PSNR dei test esistenti (salvo blur, vedi Fase 2.2) |
| 9 | Build e test | `-Werror` pulito, CTest tutto verde in RelWithDebInfo **e** Debug (ASan/UBSan), modalità software funzionante |

Se un obiettivo non è raggiungibile, scrivi il numero ottenuto, il collo di bottiglia misurato e la proposta.

---

## 4. Cosa NON fare

- Non toccare il formato `.vproj` senza chiedere.
- Non introdurre dipendenze nuove di sistema (se servono, scrivi all'utente il comando esatto, come da CLAUDE.md).
- Non passare a `real_time > 1` o `< 0` nel consumer: c'è un deadlock documentato in `PreviewConsumer.cpp`.
- Non togliere il percorso CPU di riferimento di nessun effetto o transizione.
- Non mettere valori hardcoded nei QML, non lasciare stringhe senza `qsTr`.
- Non spacciare per fatto ciò che non hai misurato: niente "✅" in documentazione senza numero o test.
- Non fare refactoring estetici di file che non c'entrano.

---

## 5. Report finale (in chat e in `docs/PROGRESS.md`)

1. Tabella prima/dopo per ogni scenario del banco (ms/fotogramma medio, p95, fps, fotogrammi persi, latenze scrub/param).
2. Elenco dei commit con una riga ciascuno.
3. Cosa è stato verificato con probe e cosa si è scoperto (soprattutto i punti *[da verificare]* di questo prompt).
4. Problemi aperti e prossimi passi onesti.
