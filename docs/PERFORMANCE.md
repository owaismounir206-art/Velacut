# velacut — Prestazioni dell'anteprima

Numeri misurati, non stime: ogni ottimizzazione dell'anteprima ha un "prima" e un "dopo" ottenuti con lo stesso banco
di prova. Le colonne "dopo" si aggiungono fase per fase (incarico in `docs/MISSION-anteprima-timeline.md`).

## Macchina delle misure

| Voce | Valore |
|---|---|
| CPU | AMD Ryzen 5 PRO 8500GE (6 core / 12 thread, 35 W) |
| GPU | Radeon 740M integrata (memoria condivisa), Mesa, OpenGL 4.6 |
| RAM | 21 GiB |
| Sistema | Arch/CachyOS, kernel 7.2.8, Qt 6.11.2, MLT 7.42, FFmpeg 9.0.2 |
| Build | RelWithDebInfo (`-O2 -g`) |

Nota: è la macchina su cui si lavora oggi. `ARCHITECTURE.md` §1 descrive la macchina della Fase 0 (Intel Core Ultra 5
226V, Arc 140V), non più quella delle misure.

## Banco di prova

`tools/bench/preview_bench.cpp`, target facoltativo (`cmake -DVELACUT_BUILD_BENCH=ON`, poi
`./build/tools/preview_bench [--size 1080|2160|all] [--scenario S1,…] [--cpu-transitions] [--no-player]`). Con XDG
dentro la build (come i test):

```bash
cd build && XDG_CACHE_HOME=$PWD/bench-home/cache XDG_DATA_HOME=$PWD/bench-home/data \
  XDG_CONFIG_HOME=$PWD/bench-home/config XDG_STATE_HOME=$PWD/bench-home/state \
  ./tools/preview_bench --size 1080 --media-dir bench-media
```

**Media** generati una volta con ffmpeg in `build/bench-media/`: 10 s, 30 fps, H.264 `veryfast` CRF 20, GOP 2 s con
B-frame, grana animata (`noise=alls=10:allf=t`: la decodifica costa come un file di una fotocamera, non come un
motivo di prova piatto). Il canvas ha la risoluzione dei media (1080p su 1080p, 2160p su 2160p).

**Scenari**

| | Timeline | Intervallo misurato |
|---|---|---|
| S1 | 1 clip H.264, nessun effetto (riferimento) | fotogrammi 60–119 |
| S2 | S1 + effetto `blur` (Sfocatura forte) con amount 1.0 | 60–119 |
| S3 | S1 + `zoomBlur` con amount 1.0 | 60–119 |
| S4 | 1 clip divisa a 5 s (due clip dello **stesso file**) + dissolvenza 1 s | la finestra della transizione, 135–164 |
| S5 | 2 clip di file diversi + `blurDissolve` 1 s | la finestra, 285–314 |
| S6 | S2 + un testo sopra per tutta la durata + filtro colore "Vivido" | 60–119 |

**Misure "pipeline"** (grafo vero: `TimelineProjection` + `get_frame`/`get_image` sul thread del banco, come l'unico
thread di rendering del consumer con `real_time = 1`):
- *Seq.*: ms per fotogramma in riproduzione sequenziale sull'intervallo (media e 95° percentile);
- *Seek*: media di 6 seek a fotogrammi casuali (stesso seme a ogni esecuzione);
- *Param*: cambio dell'amount dell'effetto con un comando + `TimelineProjection::update` + rendering dello stesso
  fotogramma (media/p95 di 6).

**Misure "player"** (il `TimelinePlayer` dell'app con il suo consumer `sdl2_audio`, audio SDL `dummy` che va in tempo
reale):
- *Play fps* e *Dropped*: fotogrammi consegnati al `FrameSink` al secondo durante la riproduzione dell'intervallo, e la
  quota di quelli riprodotti che non è stata mostrata (saltata perché in ritardo);
- *Scrub max gap*: trascinamento simulato del playhead (60 eventi/s per 1,5 s, avanti e indietro nell'intervallo):
  intervallo più lungo senza un fotogramma nuovo durante il trascinamento; *Scrub settle*: dal rilascio
  (`commitSeek`) al fotogramma esatto mostrato;
- *Slider*: trascinamento simulato del cursore dell'effetto (60 comandi/s uniti come quelli dell'ispettore, 1,5 s,
  fotogramma fermo): fotogrammi mostrati al secondo durante il gesto, e dall'ultimo comando all'ultimo fotogramma
  consegnato (*settle*).

Altri strumenti: `QT_LOGGING_RULES="velacut.engine.perf.debug=true"` scrive il tempo di ogni servizio `vedit.*`
(lavoro proprio) e della sorgente letta da `vedit.transform` (decodifica + effetti a monte); `VELACUT_PREVIEW_HUD=1`
mostra sull'anteprima fps, fotogrammi persi, ms per fotogramma e risoluzione di rendering.

## Baseline (prima di ogni intervento, commit `5cecbf7`)

Transizioni con il percorso GPU attivo, come nell'app (dove la GPU è permessa).

### 1080p

| Scenario | Canvas | Frame | Seq. mean ms | Seq. p95 ms | Seek ms | Param ms (mean/p95) | Play fps | Dropped | Scrub max gap ms | Scrub settle ms | Slider frames/s | Slider settle ms |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| S1 1 clip H.264, no effect | 1920×1080 | 1920×1080 | 14 | 16 | 253 | – | 30 | 3 % | 702 | 311 | – | – |
| S2 1 clip + blur 1.0 | 1920×1080 | 1920×1080 | 108 | 113 | 337 | 103 / 104 | 8.3 | 68 % | 1440 | 706 | 0.0 | 363 |
| S3 1 clip + zoom blur 1.0 | 1920×1080 | 1920×1080 | 544 | 548 | 775 | 540 / 541 | 1.5 | 81 % | 1440 | 618 | 0.0 | 755 |
| S4 split clip (same file) + 1 s dissolve | 1920×1080 | 1920×1080 | 41 | 43 | 248 | – | 20 | 27 % | 616 | 533 | – | – |
| S5 2 files + 1 s blur dissolve | 1920×1080 | 1920×1080 | 41 | 44 | 289 | – | 19 | 27 % | 494 | 721 | – | – |
| S6 blur 1.0 + text overlay + colour filter | 1920×1080 | 1920×1080 | 121 | 126 | 349 | 117 / 120 | 6.8 | 73 % | 1442 | 400 | 0.0 | 428 |

Transizioni con il percorso CPU (`--cpu-transitions`):

| Scenario | Canvas | Frame | Seq. mean ms | Seq. p95 ms | Seek ms | Param ms (mean/p95) | Play fps | Dropped | Scrub max gap ms | Scrub settle ms | Slider frames/s | Slider settle ms |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| S4 split clip (same file) + 1 s dissolve | 1920×1080 | 1920×1080 | 40 | 43 | 247 | – | 19 | 30 % | 555 | 573 | – | – |
| S5 2 files + 1 s blur dissolve | 1920×1080 | 1920×1080 | 97 | 103 | 293 | – | 7.1 | 70 % | 801 | 508 | – | – |

### 2160p

| Scenario | Canvas | Frame | Seq. mean ms | Seq. p95 ms | Seek ms | Param ms (mean/p95) | Play fps | Dropped | Scrub max gap ms | Scrub settle ms | Slider frames/s | Slider settle ms |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| S1 1 clip H.264, no effect | 3840×2160 | 3840×2160 | 54 | 60 | 899 | – | 15 | 47 % | 1441 | 1133 | – | – |
| S2 1 clip + blur 1.0 | 3840×2160 | 3840×2160 | 462 | 470 | 1276 | 465 / 475 | 1.8 | 81 % | 1441 | 2087 | 0.0 | 1609 |
| S3 1 clip + zoom blur 1.0 | 3840×2160 | 3840×2160 | 2206 | 2276 | 2999 | 2173 / 2193 | 0.1 | 98 % | 2546 | 4255 | 0.0 | none |
| S4 split clip (same file) + 1 s dissolve | 3840×2160 | 3840×2160 | 176 | 218 | 925 | – | 3.9 | 77 % | 1442 | 334 | – | – |
| S5 2 files + 1 s blur dissolve | 3840×2160 | 3840×2160 | 165 | 181 | 1201 | – | 4.4 | 77 % | 1441 | 691 | – | – |
| S6 blur 1.0 + text overlay + colour filter | 3840×2160 | 3840×2160 | 511 | 535 | 1352 | 485 / 499 | 1.6 | 81 % | 1441 | 2415 | 0.0 | 1806 |

### Cosa dicono i numeri

- **Scrub**: già senza effetti (S1) passano fino a 0,7 s senza un fotogramma nuovo; con blur (S2) per tutto il
  trascinamento (1,44 s = la durata del gesto) non arriva **nessun** fotogramma. È il livelock previsto dalla diagnosi:
  il watchdog a 120 ms rilancia `showFrame()`, che fa `purge()` e butta via il rendering in corso.
- **Slider**: con un effetto da 100+ ms per fotogramma l'anteprima non si aggiorna mai durante il gesto (0
  fotogrammi/s): ogni comando fa `purge` + seek.
- **Seek**: un seek casuale costa 250–350 ms a 1080p e 0,9–1,3 s a 2160p anche senza effetti: decodifica del GOP
  (2 s) dal keyframe. È il costo che i proxy a GOP corto devono togliere.
- **Effetti**: blur 108 ms, zoom blur 544 ms a 1080p (×4 a 2160p): kernel su un solo core.
- **S4 contro S5**: la transizione sulla stessa sorgente non costa più di quella su due file nella misura sequenziale
  (41 ms entrambe): il "seek thrashing" ipotizzato nella diagnosi non si vede qui (da capire in Fase 2.5).
- **GPU contro CPU per le transizioni** (1080p): dissolvenza 41 contro 40 ms (pari), blur dissolve 41 contro 97 ms
  (la GPU vince): l'ipotesi "GPU più lenta della CPU" non è confermata per questi due casi.
- **Riproduzione**: a 1080p solo S1 tiene 30 fps; S4/S5 scendono a ~20 fps (27 % persi); a 2160p nemmeno S1 (15 fps).
