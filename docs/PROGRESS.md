# vedit — Stato di avanzamento

Ultimo aggiornamento: 2026-09-25

## Fase corrente
**Fase 1 — MVP editor: in corso** (via dell'utente ricevuto il 2026-09-24). La Fase 0 è completata (sotto).

### Fase 1: fatto finora (un commit per incremento)
1. `src/fx`: kernel CPU di composizione "over" (alfa non premoltiplicato, arrotondamento esatto) + test.
   Servizio MLT `vedit.composite` (a fette su tutti i core). `MediaProducerCache` (un producer "loader" per media,
   apertura sincrona o in background). `TimelineProjection`: sequenza → tractor (sfondo, tracce visive, audio),
   aggiornamenti incrementali per traccia **identici** a una ricostruzione completa (test con fotogrammi reali).
2. `TimelinePlayer`: riproduce la sequenza viva e segue `Project::changed` anche durante la riproduzione (modifiche
   piccole = patch del grafo, strutturali = ricostruzione, nuovo canvas/fps = nuovo profilo), playhead separato dal
   fotogramma mostrato (skimming), J/K/L fino a 8x, stop agli estremi. Test anche sotto ASan con modifiche continue in
   riproduzione. Scoperte: race di MLT nei worker paralleli (D-24), colori MLT `#AARRGGBB` (D-25).

3. Export MP4 in `vedit-render` (processo separato, progetto congelato, avanzamento JSON, annullamento senza file
   parziali), stessa proiezione dell'anteprima anche a dimensioni/fps diversi; test con ffprobe e fotogrammi decodificati.
4. `src/document`: bozze con salvataggio continuo atomico (300 ms / max 2 s, thread di I/O, nuovi tentativi), lock con
   recupero dopo crash, `DraftStore` (elenco, crea, rinomina, duplica, cestino).
5. Import: probe in `vedit-render --probe` (un file che fa crashare il probe viene scartato), fingerprint campionato,
   miniature e waveform con FFmpeg in background con cache su disco, canvas e fps dalla prima clip (un solo comando).

### Fase 1: prossimi passi (in ordine)
1. UI dell'editor: schermata iniziale, layout, media pool con skimming e "+", anteprima con J/K/L, timeline
   virtualizzata (drag/trim/split/snapping/zoom/skimming), barra contestuale, snackbar "Annulla", export a una schermata.
2. Test di semplicità 1, 2, 8, 10; verifica del criterio; README/SHORTCUTS/PROGRESS; attesa del via.

### Fase 1: limiti noti finora
- Cronologia delle versioni: rimandata alla Fase 8 come da tabella SPEC §8 (D-31).
- Stima della dimensione dell'export: ipotesi del 70% del tetto di bitrate, da tarare su filmati reali.
- Encoder hardware all'export: Fase 8 (ora sempre libx264, disponibile ovunque).

## Fase 0 — Fondamenta: completata

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

## Limiti noti e cose non ancora fatte (onestà, regola 4)
- L'interfaccia è solo quella della Fase 0: finestra di riproduzione di un file e galleria. **Nessun editor ancora**
  (schermata iniziale, bozze, timeline, media pool sono la Fase 1).
- Tipi di clip testo, sottotitolo, sticker, effetto e regolazione: nel modello sono conservati come JSON (nessuna perdita
  di dati), la loro modifica e il loro rendering arrivano nelle fasi 2–6. Movimento di clip collegate (`linkId`) non ancora
  gestito da TimelineEditor (serve dalla Fase 2, "scollega/collega audio e video").
- Proiezione core → MLT (grafo della timeline), servizi MLT propri `vedit.*`, `vedit-render`, proxy, miniature,
  waveform: Fase 1.
- Preferenze → Prestazioni (interfaccia): Fase 8; i dati esistono (`GraphicsPreferences` in QSettings).
- Il `FrameSink` copia ogni fotogramma mostrato (~1 ms in 1080p): va bene con anteprime a risoluzione ridotta; un
  percorso a copia zero richiede un protocollo di rilascio dei frame prima di chiudere il consumer (D-19).
- Hardware video rilevato (VA-API/QSV) ma non ancora usato: decodifica hardware nel player e encoding in Fase 1/8.
- Matrice GPU: provata solo la GPU Intel di questa macchina (+ percorsi software); NVIDIA/AMD/VM progettati ma non provati
  (`docs/GPU_COMPATIBILITY.md`).
- `tst_theme` impiega ~80 s nella build Debug con sanitizer (140 schemi generati); ~1 s in RelWithDebInfo.

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
./build/vedit [file]                 # player della Fase 0
./build/vedit --component-gallery    # galleria M3
cmake -S . -B build -DVEDIT_BUILD_PROBES=ON   # programmi di prova in build/tools/
```
Scaricamenti usati per il vendoring (non versionati): `.downloads/` (~1 GB, soprattutto il repository delle icone);
si può cancellare, i file necessari sono già copiati in `third_party/` e `src/assets/`.

## Decisioni
Registro in `docs/ARCHITECTURE.md` §15 (D-01 … D-26) ed esiti delle verifiche in §18.
`tools/run-test.sh <build-dir> <test> [funzione]` esegue un singolo test con lo stesso ambiente di CTest (utile per
ripetere un test instabile: `for i in $(seq 20); do tools/run-test.sh build tst_timelineplayer || break; done`).
