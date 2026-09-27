# vedit — Stato di avanzamento

Ultimo aggiornamento: 2026-09-25 (Fase 3 completata)

## Fase corrente
**Fase 3 — Keyframe e composizione: completata, in attesa del via dell'utente per la Fase 4.**
Nota: la Fase 3 è iniziata senza un via registrato dell'utente dopo la Fase 2 (la cui chiusura aveva lacune, recuperate:
vedi sotto); l'utente ha poi chiesto di proseguire ("continua pure il lavoro").

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

## Fase 4 — Colore e audio avanzati (in corso)
1. **Kernel colore e LUT (`P4.1`)**: `fx::Grade` con bilanciamento del bianco per canale, ruote colore lift/gamma/gain (RGB offset + livello), selettore HSL per 8 gamme con transizioni morbide tra tinte adiacenti, curve monotone cubiche (senza overshoot) RGB e master; parser e campionatore trilineare/lineare per LUT `.cube` 3D/1D (`fx::CubeLut`).
2. **Proiezione colore e deflicker (`P4.2`)**: composizione look + grade + LUT in una singola 33³ `ColorLut` per MLT senza overhead a runtime; proiezione degli effetti `vedit.grade`, `vedit.lut` (con cache per modifica file) e `vedit.deflicker` (`avfilter.deflicker`) anche sui livelli di regolazione (`tst_projection::gradeEffectInProjection`, `cubeLutInProjection`, `deflickerInProjection`).
3. **Interfaccia colore e visualizzatori scope (`P4.3`)**:
   - Controlli completi nel pannello proprietà (scheda Regola): "Bilanciamento automatico" e "Abbina colore" al fotogramma del playhead, selettore file LUT `.cube` 3D/1D con cursore intensità, deflicker (toggle, modalità e dimensione finestra), ruote colore interattive lift/gamma/gain con disco cromatico 2D e cursore livello, regolatore HSL a 8 gamme colore con chip cromatici (tinta, saturazione, luminosità), editor grafico di curve monotone cubiche (Master RGB, Rosso, Verde, Blu) con aggiunta/trascinamento/rimozione punti.
   - Video Scopes dal vivo (`ScopeView` in QQuickPaintedItem e overlay `ScopePanel` nell'anteprima): Istogramma (RGB e luminanza), Waveform (linee IRE 0, 7.5, 50, 100 con intensità di segnale) e Vettorscopio (reticolo 75%, bersagli colore R/Mg/B/Cy/G/Yl, asse skin-tone "I").
   - Test di integrazione `tst_ui::colorGradingAndScopes`: verifica apertura/chiusura scope, modifica parametri `grade` e `deflicker`, verifica effetti e reset. 25/25 test CTest verdi.

### Prossimi passi (Fase 4 — Colore e audio avanzati)
Dalla SPEC §8: HSL, curve, ruote colore, LUT, scope, abbina colore, deflicker; EQ/compressore/effetti voce, "Migliora
voce", loudness, ducking, registrazione voce con teleprompter, registrazione schermo + webcam, sincronizzazione audio
esterno, multicamera. Criterio: correggo colore con LUT e curve, il mix audio rispetta −14 LUFS, e monto
un'intervista a due camere sincronizzate dall'audio.

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
