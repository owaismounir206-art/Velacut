# Consegna: timeline di Velacut come quella di CapCut

Documento per chi riprende il lavoro sulla timeline (es. GLM 5.3 su Cline). Scritto il 2026-10-11, alla fine della
Fase 4b della missione `docs/MISSION-anteprima-timeline.md`. **Leggilo tutto prima di toccare il codice.**
La colonna "Stato" delle tabelle viene aggiornata da chi completa una voce: controlla `git log` per sapere cosa è già
stato fatto dopo la stesura.

---

## 0. Regole che non si discutono (da `CLAUDE.md`, valgono anche per te)

- Lavora **solo** dentro `/home/owais/Velacut`. Mai `sudo`, mai installare pacchetti, mai `git push`, mai
  `rm -rf` / `git reset --hard` / `git clean` / `git push --force`. Niente `curl … | sh`.
- La cartella `Velacut/` non tracciata dentro il repo e `.claude/settings.local.json` **non vanno mai committati**
  (niente `git add -A` / `git add .`: aggiungi i file per nome).
- **Piccoli incrementi**: dopo ognuno `cmake --build build`, test, avvio dell'app se tocchi la UI, poi commit
  Conventional Commits (`feat(timeline): …`, `fix(timeline): …`). Il progetto compila sempre e i test passano sempre.
- **Niente finzioni**: niente pulsanti che non fanno nulla, niente stub, niente `TODO` spacciati per funzioni.
  Quello che non finisci va in `docs/PROGRESS.md`.
- Il `core` (`src/core`) non dipende da MLT né da QML. **Ogni modifica al progetto passa da un `QUndoCommand`** e un
  gesto dell'utente (un trascinamento) produce **un solo** comando al rilascio, mai uno per movimento.
- Tempi con `RationalTime` nel core; in QML la timeline lavora in fotogrammi interi del progetto.
- **QML: nessun colore, dimensione, raggio o durata scritti a mano.** Tutto dal singleton `Theme`, definito in C++ in
  `src/theme/ThemeTokens.h` (classi `ThemeEditor` → `Theme.editor.*` per la timeline, `ThemeMotion` →
  `Theme.motion.short1…long4` per le animazioni, che diventano 0 con "riduci movimento"; i ritardi che non sono
  animazioni stanno in `ThemeEditor`). Se ti serve un valore nuovo, aggiungi `Q_PROPERTY` + membro con un commento.
  Anche gli `interval:` dei `Timer` sono durate: vanno nel tema.
- **Nessuna stringa visibile hardcoded**: sempre `qsTr()` (QML) / `tr()` (C++). Dopo aver aggiunto stringhe:
  `cmake --build build --target update_translations`, poi apri `i18n/velacut_it.ts` e traduci in italiano ogni voce
  `type="unfinished"` (togli l'attributo). Le stringhe sorgente sono in inglese.
- `-Wall -Wextra -Wpedantic -Werror`: un warning rompe la build.
- **Non cambiare il formato `.vproj`** (`docs/FILE_FORMAT.md`) senza chiedere all'utente. Se una funzione lo richiede
  (es. gruppi di clip, altezza delle tracce salvata), fermati e chiedi.
- Le decisioni non ovvie vanno in `docs/ARCHITECTURE.md` §15 come `D-xx` (l'ultima è D-66: continua da D-67).
- Se un'API di Qt/MLT non fa quello che pensi, scrivi un programmino in `tools/probes/` e osserva; non indovinare.
- **Pensa come CapCut**: meno passaggi possibile, niente finestre di dialogo se basta un gesto, default giusti.

## 1. Comandi

```bash
cmake --build build                                   # build (RelWithDebInfo, bench e probe abilitati)
ctest --test-dir build -j6 --output-on-failure        # tutti i test (~5 min, 32 test)
./build/velacut                                       # app
QT_QUICK_BACKEND=software LIBGL_ALWAYS_SOFTWARE=1 ./build/velacut   # modalità software: DEVE funzionare

# un solo test di tst_ui (interfaccia guidata con mouse/tastiera finti, offscreen)
cd build/tests/integration && H=/home/owais/Velacut/build/test-home/tst_ui; \
  QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy TMPDIR=$H/tmp \
  XDG_CACHE_HOME=$H/cache XDG_DATA_HOME=$H/data XDG_CONFIG_HOME=$H/config XDG_STATE_HOME=$H/state \
  ./tst_ui nomeDelTest
# aggiungi VELACUT_UI_SHOTS=/percorso/cartella per avere uno screenshot per passo (utile per vedere cosa succede)

# i log di Qt finiscono in journald se l'output è in pipe: forza stderr
QT_FORCE_STDERR_LOGGING=1 ./build/velacut 2>&1 | less
```

Build Debug con ASan/UBSan (da usare prima di chiudere un lavoro grosso):
`cmake --preset debug && cmake --build build-debug && ctest --test-dir build-debug -j6 --output-on-failure`.

## 2. Mappa del codice della timeline

| File | Cosa contiene |
|---|---|
| `src/ui/qml/TimelineView.qml` | La timeline intera: righello, intestazioni tracce, `Flickable flick` con `canvas`, playhead, skimming, scrub (`startScrubbing/updateScrubbing/finishScrubbing`, `scrubDebounceTimer`, `scrubAutoScrollTimer`), zoom (`setZoom(value, anchorX)`, `zoomBy`, `zoomToFit`), `rowTop/rowAt/rowHeight` (righe), `frameAt(x)`, `snapGuide`, `dropMarker` (rilascio da media pool), menu contestuale unico `clipMenu` / `openClipMenu()`, i "+" delle transizioni (`cut`) |
| `src/ui/qml/TimelineClip.qml` | Delegato di una clip: corpo colorato per tipo, miniature (`MediaThumbnail`), forma d'onda (`WaveformView`), etichetta nome+durata, keyframe, marker di clip. `MouseArea mover` (spostamento: `moveFrame/moveRow`, snap con `editor.snapRange`, al rilascio `editor.moveClip`), componente `TrimHandle` (trim: `editStart/editEnd`, al rilascio `editor.trimClip`) |
| `src/ui/qml/ContextToolbar.qml` | Barra sopra la timeline: annulla/ripeti, azioni sulla selezione, magnete, snapping, skimming, zoom |
| `src/ui/qml/EditorScreen.qml` | Scorciatoie da tastiera (`Shortcut { … }`) |
| `src/ui/controllers/EditorController.{h,cpp}` | API per il QML (`Q_INVOKABLE`): `moveClip`, `trimClip`, `split`, `rippleTrimLeft/Right`, `deleteSelection`, `rippleDeleteSelection`, `select(id, additive)`, `selectAll`, `snap`, `snapRange`, `setMagneticMain`, … |
| `src/ui/models/TimelineModel.*` | Modelli per il QML: clip, `trackModel`, `cutModel`, `markerModel` (`KeyedListModel`: i delegati sopravvivono alle modifiche, non ricrearli) |
| `src/core/commands/*` | I comandi annullabili (`Edit`, spostamento, trim, ripple…). Le regole della traccia magnetica stanno qui, non nel QML |
| `src/ui/items/MediaThumbnail.*`, `WaveformView.*`, `ImageTiles.*`, `TimelineRuler.*` | Item C++ a nodi del grafo di scena (miniature, onde, righello) |
| `tests/integration/tst_ui.cpp` | Test che guidano la UI vera. **Ogni nuova interazione ha un test qui** |
| `docs/SHORTCUTS.md`, `docs/USABILITY.md` | Da aggiornare se cambi scorciatoie o il numero di azioni di un percorso |

Trappole già scoperte (non ripeterle):
- Backend software: in un nodo immagine la texture va assegnata **prima** di `setFiltering`, e una texture va
  cancellata solo dopo aver riassegnato i nodi figli (vedi `ImageTiles.cpp`). Altrimenti crash solo in modalità software.
- Niente `Repeater` di centinaia di Item per cose disegnabili (righello, tacche): usa un item C++ a nodi.
- Niente funzioni O(n) nei binding per riga/clip (usa l'array `rowTops`).
- Un solo `Menu` condiviso: non mettere un `Menu` dentro il delegato della clip.

## 3. Cosa sistemare subito (Fase 4c della missione)

Riferimento completo: `docs/MISSION-anteprima-timeline.md` §2 "Fase 4c". Ogni voce: test in `tst_ui` + eventuale
aggiornamento di `docs/USABILITY.md` / `docs/SHORTCUTS.md`.

| # | Cosa | Come | Stato |
|---|---|---|---|
| 4c.1 | **Riflusso dal vivo sulla traccia principale magnetica** | Durante il drag di una clip della traccia principale, calcola in QML l'indice d'inserimento (dal centro della clip trascinata rispetto ai centri delle altre) e sposta visivamente le altre clip (`x` animato con `Behavior` e durata `Theme.motion.*`), con una barra verticale d'inserimento. **Nessun comando durante il gesto**: al rilascio un solo `editor.moveClip`. Lo stesso per il trim con ripple: durante il gesto le clip successive seguono il bordo (offset visivo), al rilascio un comando | fatta (commit `d179c6d`, test `tst_ui::liveReflowOnTheMainTrack`) |
| 4c.2 | **Auto-scroll ai bordi** durante drag di clip e maniglie di trim | Riusa la logica di `scrubAutoScrollTimer` (velocità proporzionale a quanto il puntatore è oltre il bordo); il fotogramma sotto il puntatore va ricalcolato mentre `contentX` cambia anche se il mouse è fermo | fatta (commit `d179c6d`, test `tst_ui::dragScrollsAtTheEdges`) |
| 4c.3 | **Rotella/touchpad**: `pixelDelta` per lo scroll continuo, Ctrl+rotella e pinch con zoom continuo sotto il puntatore, Maiusc+rotella verticale | `WheelHandler` separati per modificatore + `PinchHandler` in `TimelineView.qml` | fatta (commit `4e408ca`, test `tst_ui::arrowsAndWheel`) |
| 4c.4 | **Scrub reattivo**: ridurre/eliminare il debounce da 80 ms (`scrubDebounceTimer`) | Il motore ora tiene un solo render in volo e l'ultima richiesta vince (commit ae87b94), quindi il debounce serve solo per l'audio. Misura con `tools/preview_bench` (scenario scrub) prima/dopo. L'`interval` va comunque nel tema | senza misura (commit `d179c6d`, debounce rimosso; manca benchmark comparativo dedicato) |
| 4c.5 | **Selezione a rettangolo** sullo spazio vuoto + **spostamento di più clip** insieme | Rettangolo disegnato nel `canvas` (colori `Theme.color.primary` con opacità dal tema); seleziona le clip che interseca (Ctrl = aggiunge). Spostare una clip selezionata sposta tutte le selezionate con lo stesso delta: serve un comando nel core che sposti più clip in **un solo** `QUndoCommand` (es. `EditorController::moveSelection(deltaFrames, deltaRows)`) | da fare (core/controller pronti: `TimelineEditor::moveClips`, `EditorController::moveSelection`/`selectArea` in commit `da72652`; mancano rettangolo QML nel canvas e drag multi-clip) |
| 4c.6 | Frecce ±1 fotogramma, Maiusc+frecce ±1 secondo | `EditorScreen.qml` | fatta (commit `4e408ca`, test `tst_ui::arrowsAndWheel`) |

> [!WARNING]
> **Test instabile da indagare**: `tst_ui` (in particolare `timelineGestures`) è risultato instabile sotto carico elevato o con più istanze di test in parallelo, pur passando sempre da solo e in esecuzione sequenziale. Da indagare e stabilizzare (possibile sensibilità ai tempi dell'animazione `Behavior on x` o a concorrenza grafica offscreen).


## 4. Cosa aggiungerei per arrivare a CapCut (in ordine di valore per l'utente)

Prima di implementare una voce: controlla che non esista già (`grep` in `EditorController.h` e nei QML), e per quelle
segnate **[formato]** chiedi all'utente perché toccano il `.vproj`.

### 4.1 Gesti di base che in CapCut "si sentono"
1. **Indicatore d'inserimento e "fantasma"** quando trascini dal media pool alla timeline: oggi c'è `dropMarker`; in
   CapCut la clip fantasma ha la sua larghezza reale (durata × zoom) e mostra dove finisce. Sulla traccia principale
   magnetica mostra il riflusso (come 4c.1).
2. **Trascinare una clip sopra/sotto le tracce crea una traccia nuova** con una linea orizzontale che mostra dove
   nascerà (esiste `Theme.editor.newTrackZone` in alto: estendilo anche *tra* due tracce).
3. **Snapping visibile**: la linea guida (`snapGuide`) c'è; aggiungi un piccolo "scatto" percepibile (la clip si ferma
   sul punto per qualche pixel prima di lasciarlo — è già il comportamento di `snap` con soglia; verifica che la soglia
   sia in pixel costanti a ogni zoom) e lo snap **al playhead** e ai **marker/beat** (SPEC §5.2).
4. **Doppio clic su una clip** = seleziona e apre il pannello proprietà sulla scheda giusta (video/audio/testo).
5. **Alt+trascina** una clip = duplica e sposta la copia (un comando).
6. **Copia / Incolla clip** (Ctrl+C / Ctrl+V al playhead, sulla stessa traccia o sopra se occupata). Oggi esistono solo
   copia/incolla *attributi* (Ctrl+Alt+C/V). Un comando di incolla nel core.
7. **Seleziona tutto a destra / a sinistra del playhead** (SPEC §5.2), utile prima di spostare in blocco.

### 4.2 Aspetto delle clip (come CapCut)
1. **Badge sulla clip**: velocità ("2.0x", icona se curva), invertita, muta, filtro/effetti applicati, animazione.
   Piccole icone in alto a destra, dal tema (`Theme.editor.smallIconSize`), visibili solo se la clip è larga abbastanza.
2. **Forma d'onda sotto le miniature delle clip video** della traccia principale (striscia bassa, come CapCut), con la
   linea del volume. `WaveformView` esiste già: riusalo nel delegato video.
3. **Maniglie di dissolvenza audio** (fade in/out) negli angoli superiori della clip audio selezionata: trascinandole
   cambi `fadeIn/fadeOut` (se le proprietà esistono già nella clip; altrimenti **[formato]**). La rampa si disegna
   sopra la forma d'onda.
4. **Trascinare la linea del volume** su/giù sulla clip audio (con tooltip in dB), un comando al rilascio.
5. **Durata sempre leggibile**: quando la clip è stretta, nome nascosto e solo durata; durante il trim un tooltip con
   durata nuova e delta (già c'è: `trimLabel`).
6. **Clip selezionata** con bordo spesso bianco e maniglie di trim "a parentesi" ben visibili (in CapCut le maniglie
   sono bianche con una tacca); quando non selezionata nessuna maniglia, ma il cursore cambia comunque sui bordi.

### 4.3 Tracce
1. **Altezza traccia regolabile** trascinando il bordo inferiore dell'intestazione (SPEC §5.2) **[formato]** se va
   salvata nel progetto; altrimenti in `QSettings` come preferenza di vista (scelta consigliata: niente cambio formato).
2. **Traccia principale più alta** delle altre e tracce testo/effetti/sticker più basse (CapCut fa così): token di tema
   diversi per tipo di traccia.
3. **Rimozione automatica delle tracce vuote** quando l'ultima clip esce (se non è già così; verifica).
4. **Solo** traccia audio (esiste `setTrackSolo` nel controller: manca il pulsante nell'intestazione?).

### 4.4 Editing avanzato (SPEC §5.2, mancano nel controller)
1. **Scollega / estrai audio** di una clip video (l'audio diventa una clip su una traccia audio sotto, collegata o no).
   Probabilmente **[formato]** per il collegamento: chiedi.
2. **Slip** (Alt+trascina dentro la clip = sposta il contenuto senza cambiare posizione e durata), **roll** (trascina il
   taglio tra due clip = una si allunga e l'altra si accorcia), **slide**. In CapCut il più usato è il roll sul taglio
   della traccia principale.
3. **Raggruppa / separa** clip (spostarle insieme senza clip composta) **[formato]**.

### 4.5 Navigazione
1. **Zoom con lo slider** che resta ancorato al playhead (non all'inizio): controlla `ContextToolbar.qml` → `setZoom`
   con `anchorX` = posizione del playhead nella vista.
2. **Durante la riproduzione la vista segue il playhead a pagine** (esiste) — aggiungi la modalità "playhead fisso al
   centro" solo se l'utente la chiede.
3. **Minimappa / barra di scorrimento** in basso con la forma delle clip (CapCut non ce l'ha: bassa priorità).

## 5. Come verificare che una voce è finita

1. `cmake --build build` senza warning.
2. Il nuovo test in `tst_ui` passa, e passano tutti gli altri (`ctest --test-dir build -j6 --output-on-failure`).
3. Avvio dell'app normale **e** in modalità software: il gesto funziona, niente crash, niente messaggi nel log.
4. Nessun colore/dimensione/durata nuovi fuori da `Theme`; nessuna stringa fuori da `qsTr`; traduzioni italiane
   compilate in `i18n/velacut_it.ts`.
5. Un solo comando di annulla per gesto (verifica con Ctrl+Z nel test: un Ctrl+Z annulla tutto il gesto).
6. Commit piccolo, poi aggiorna la colonna "Stato" qui sopra e `docs/PROGRESS.md`.
