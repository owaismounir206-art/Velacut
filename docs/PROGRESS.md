# vedit — Stato di avanzamento

Ultimo aggiornamento: 2026-10-01 (Fase 5 in corso: P5.1–P5.6 completati; iniziato P5.7 con template e placeholder)

## Fase corrente
**Fase 5 — Libreria creativa: in corso (P5.1–P5.6 completati; P5.7 iniziato con template e placeholder; restano 
slideshow, kit del marchio e copertina, gestore asset, percorso GPU delle transizioni).**

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
  * **Ancora da fare per P5.7**: interfaccia QML (scheda Template con anteprima e "Usa"), "Sostituisci" con Alt+trascina, 
    "Salva come template", slideshow dalle foto.
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
   - ✅ `templates.json` nel pacchetto core con 8 template pronti in 8 categorie (vlog, travel, party, business, sport, 
     memories, social, cinematic).
   - ✅ Ogni template ha slot (placeholder) con etichette localizzate, tipo richiesto (video/photo/any), durate, 
     transizioni, filtri, testi e sticker.
   - ✅ `TemplateBuilder` (`core/project/TemplateBuilder.h`/`.cpp`) costruisce una sequenza da un manifest di template.
   - ✅ Supporto placeholder già presente nel core (`Clip::placeholder`, serializzazione in `ProjectJson`) e nell'editor
     (`TimelineEditor::insertPlaceholder`, `replaceClipMedia` per sostituire con media reali).
   - ✅ `fx::Library` carica i template con categorie e preset localizzati.
   - ✅ `AppController::newProjectFromTemplate()` crea un progetto da un template.
   - ✅ `AssetLibraryModel` supporta il tipo `Templates` per esporre i template al QML.
   - ✅ Test: `tst_kernels::coreLibraryLoads` verifica ≥8 template in ≥8 categorie.
   - **Ancora da fare**: 
     * Interfaccia QML per la scheda Template (anteprima miniature, "Usa questo template")
     * "Sostituisci" con Alt+trascina sulla timeline per rimpiazzare i placeholder
     * "Salva come template" per salvare il progetto corrente come template personalizzato
     * Slideshow automatico dalle foto con Ken Burns e transizioni

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

### Prossimi passi (Fase 5 — Libreria creativa)
3. P5.7 (continua): interfaccia per i template (scheda Template con anteprima, "Usa questo template" che crea un
   progetto con placeholder), "Sostituisci" anche con Alt+trascina nella timeline, "Salva come template", slideshow
   dalle foto con Ken Burns e transizioni.
4. P5.8: kit del marchio (più kit: palette, font, loghi, stili, intro/outro, watermark, musiche; colori del kit primi nei
   selettori) e copertina (fotogramma o immagine + testi/sticker, export JPG/PNG, incorporata nell'MP4).
5. P5.9: gestore asset (installa/rimuovi pacchetti da cartelle o .zip).
6. P5.10: percorso GPU facoltativo delle transizioni (GLSL compatibile GL 2.1/GLES 2.0, offscreen) con test di
   rendering CPU/GPU entro tolleranza PSNR.
7. P5.11: criterio della Fase 5 ("Uso un template, sostituisco i media e ottengo un video completo; ogni transizione
   supera il test di rendering CPU/GPU"), README, SHORTCUTS, USABILITY.

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
