# vedit — Stato di avanzamento

Ultimo aggiornamento: 2026-09-25 (Fase 2 completata)

## Fase corrente
**Fase 3 — Keyframe e composizione: in corso** (Fase 2 completata, storico sotto).

### Criterio di completamento della Fase 3 (SPEC §8)
| Requisito | Esito | Verifica |
|---|---|---|
| Animo un titolo con keyframe ed easing | In corso | Keyframe, curve ed easing nel modello e player |
| Compongo un green screen con maschera | ✅ | `vedit.chroma_key` + `vedit.mask` verificati in `tst_projection::greenScreenCompositionWithMaskInProjection` |
| Compila senza warning, test verdi | ✅ | 25/25 in `build` (RelWithDebInfo e Debug con sanitizers) |

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

### Prossimi passi (Fase 3 — Keyframe e composizione)
- P3.4: Compound clip e tracce di regolazione (`adjustment`) in `TimelineEditor` e `TimelineProjection`.
- P3.5: Animazioni predefinite e marker (sequence e clip) in timeline.
- P3.6: Controlli UI inspector (keyframe diamonds, curve easing selector, chroma key picker, maschere) e verifica criterio di completamento SPEC §8.

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
Registro in `docs/ARCHITECTURE.md` §15 (D-01 … D-39), esiti delle verifiche in §18 (Fase 0), §20 (Fase 1) e §21 (Fase 2).
`tools/run-test.sh <build-dir> <test> [funzione]` esegue un singolo test con lo stesso ambiente di CTest (utile per
ripetere un test instabile: `for i in $(seq 20); do tools/run-test.sh build tst_timelineplayer || break; done`).
