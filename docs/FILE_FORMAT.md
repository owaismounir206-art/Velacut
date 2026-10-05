# vedit — Formato del progetto `.vproj` (versione 1)

> Stato: **proposta, in attesa di approvazione.** Va approvata prima di implementare la serializzazione (sezione 3).
> Il formato ricalca uno a uno il modello del core descritto in `docs/ARCHITECTURE.md` §4.

---

## 1. Principi
- **JSON UTF-8 leggibile e adatto a git** (sezione 5.14): indentazione fissa (4 spazi, quella di
  `QJsonDocument::Indented`), chiavi in ordine alfabetico, fine riga LF, newline finale. Due salvataggi dello stesso
  modello producono byte identici.
- **Ordine degli array stabile** e con significato: media nell'ordine di import, tracce nell'ordine di composizione,
  clip per `start`, effetti nell'ordine di applicazione.
- **Nessun `double` per i tempi**: stringhe `RationalTime` (§3.2).
- **Indipendente dalla risoluzione**: posizioni, dimensioni e spessori sono frazioni del canvas o della sorgente.
- **Il file contiene solo dati di progetto.** Stato dell'interfaccia (zoom, playhead, pannelli), cache (miniature,
  waveform, proxy, trascrizioni grezze) e capacità della macchina stanno fuori (§6), così spostare il playhead non
  produce differenze nel file.
- **Niente dati persi**: effetti, transizioni e asset di tipo sconosciuto (per esempio un pacchetto disinstallato) sono
  conservati alla lettera e riscritti invariati. Nell'interfaccia appaiono come "mancanti".
- **Versionato**: `formatVersion` intero; ogni cambiamento dello schema, anche solo additivo, alza la versione e ha una
  migrazione (§8).

## 2. Identificazione
- Estensione `.vproj`, tipo MIME `application/x-vedit-project` (glob `*.vproj`; il riconoscimento per contenuto usa
  `"format": "vedit.project"`).
- Il file deve essere un oggetto JSON con `format == "vedit.project"` e `formatVersion` intero ≥ 1.

---

## 3. Tipi comuni

### 3.1 Id
UUID v4 in minuscolo senza graffe: `"3f2c9a8e-5b1d-4e57-9a61-0c7d2f4b8e10"`. Unici nel file; stabili tra un salvataggio
e l'altro (undo, copia/incolla e diff git li preservano).

### 3.2 RationalTime e Rational
- `Rational`: stringa `"num/den"` oppure `"num"` se `den == 1`. Esempi: `"30000/1001"`, `"25"`, `"48000"`.
- `RationalTime`: stringa `"<value>@<rate>"`, con `value` intero (anche negativo, dove ammesso) e `rate` un `Rational`.
  Esempi: `"150@30000/1001"` (150 fotogrammi a 29,97 fps = 5,005 s), `"96000@48000"` (2 s di audio).
- Regola di griglia: **tutti i tempi dentro una sequenza usano `settings.frameRate` del progetto**. Un tempo con un'altra
  frequenza viene convertito al caricamento (arrotondamento al fotogramma più vicino, metà al pari) e segnalato nel log.
- I tempi nei metadati dei media (`info`) usano la frequenza nativa del media.

### 3.3 Colore
`"#RRGGBBAA"` esadecimale, sRGB, alfa **non** premoltiplicato. Esempio: `"#FFFFFFFF"`.

### 3.4 Vettori e coordinate
- `Vec2`: `[x, y]` numeri.
- **Coordinate del canvas**: origine al centro del canvas, `x` in frazioni della **larghezza**, `y` in frazioni
  dell'**altezza**, asse y verso il basso. `[0, 0]` = centro, `[-0.5, -0.5]` = angolo in alto a sinistra.
  Cambiando formato (16:9 → 9:16) un elemento resta nella stessa posizione relativa.
- **Scala**: `1.0` = dimensione "adattata al canvas" secondo `fit` (§5.4).
- **Angoli** in gradi, positivi in senso orario.
- Dimensioni del testo in frazioni dell'altezza del canvas.

### 3.5 Parametri animabili (`Param`)
Un parametro è **o** un valore letterale **o** un oggetto con `keyframes`:
```json
"opacity": 0.8
"opacity": { "keyframes": [
    { "t": "0@30",  "v": 0.0, "interp": "bezier", "ease": "easeOut" },
    { "t": "15@30", "v": 1.0, "interp": "linear" },
    { "t": "45@30", "v": 1.0, "interp": "hold" }
] }
```
- Tipi di valore: numero, booleano (non animabile), stringa/enumerazione (non animabile), colore (animabile, interpolato
  in sRGB lineare), `Vec2` (animabile, per componente).
- `t`: `RationalTime`. Spazio del tempo:
  - clip **media** → tempo della **sorgente**, stesso spazio di `sourceIn` (l'animazione segue il contenuto);
  - clip **generate** (testo, sticker, colore, effetto, regolazione, sottotitolo, compound) → tempo dall'inizio della clip.
- animazione in ciclo con `duration` zero: il ciclo è la clip intera (è il Ken Burns delle foto,
  `animations/loop/ken_burns`, messo di default a ogni foto inserita).
- `interp` descrive il segmento che **parte** da quel keyframe: `"linear"`, `"hold"` (costante fino al keyframe successivo),
  `"bezier"`.
- `ease` (solo con `bezier`): nome di un preset (`"easeIn"`, `"easeOut"`, `"easeInOut"`, `"easeInBack"`, `"easeOutBack"`,
  `"easeInOutBack"`, `"easeOutBounce"`, `"easeOutElastic"`, …, elenco completo nella libreria di easing del core)
  oppure una curva personalizzata `[x1, y1, x2, y2]` come la `cubic-bezier` CSS (`x1`, `x2` ∈ [0, 1]).
- Prima del primo keyframe e dopo l'ultimo il valore resta costante. Keyframe ordinati per `t`, senza duplicati.

---

## 4. Struttura generale

```json
{
    "format": "vedit.project",
    "formatVersion": 1,
    "generator": { "app": "vedit", "version": "0.1.0" },
    "id": "…",
    "name": "Viaggio a Roma",
    "createdAt": "2026-09-24T17:40:12Z",
    "modifiedAt": "2026-09-24T18:02:55Z",
    "settings": { … },
    "mediaFolders": [ … ],
    "media": [ … ],
    "sequences": [ … ],
    "mainSequenceId": "…"
}
```

| Campo | Tipo | Note |
|---|---|---|
| `format` | stringa | sempre `"vedit.project"` |
| `formatVersion` | intero | questa specifica: `1` |
| `generator` | oggetto | app e versione che hanno scritto il file (informativo) |
| `id` | Id | identità del progetto (anche id della bozza) |
| `name` | stringa | nome mostrato nelle bozze |
| `createdAt`, `modifiedAt` | stringa ISO 8601 UTC | `modifiedAt` è l'ora del salvataggio, scritta nel file e non nel modello (D-28) |
| `settings` | oggetto | §4.1 |
| `mediaFolders` | array | cartelle del media pool: `{ "id", "name", "parentId" \| null }` |
| `media` | array | §5.1 |
| `sequences` | array | §5.2; la principale + quelle delle compound clip |
| `mainSequenceId` | Id | sequenza mostrata in timeline ed esportata |

### 4.1 `settings`
```json
"settings": {
    "frameRate": "30000/1001",
    "sampleRate": 48000,
    "audioChannels": 2,
    "colorSpace": "bt709",
    "formatFromFirstClip": true,
    "defaultCanvas": { "width": 1920, "height": 1080, "preset": "16:9" }
}
```
- `formatFromFirstClip`: `true` finché canvas e fps non sono stati fissati. La prima clip aggiunta li imposta
  (sezione 0bis, regola 1) e il campo diventa `false`; lo stesso accade se l'utente sceglie un formato a mano.
- `colorSpace`: in v1 solo `"bt709"` (vedi ARCHITECTURE D-09).
- Valori ammessi per `preset`: `"16:9"`, `"9:16"`, `"1:1"`, `"4:5"`, `"21:9"`, `"3:4"`, `"custom"`.

---

## 5. Oggetti

### 5.1 `media[]`
```json
{
    "id": "…",
    "kind": "video",
    "name": "IMG_1234.MOV",
    "path": "/home/owais/Video/IMG_1234.MOV",
    "relativePath": null,
    "fingerprint": { "algo": "sha256-sampled-v1", "value": "9c1e…", "size": 734003200 },
    "info": {
        "duration": "1834@30000/1001",
        "video": { "width": 3840, "height": 2160, "frameRate": "30000/1001", "vfr": false, "rotation": 90,
                   "codec": "hevc", "pixelFormat": "yuv420p10le", "primaries": "bt2020", "transfer": "arib-std-b67",
                   "hdr": true, "hasAlpha": false, "sar": "1" },
        "audio": { "codec": "aac", "sampleRate": 48000, "channels": 2 }
    },
    "proxy": "auto",
    "favorite": false,
    "folderId": null
}
```
| Campo | Note |
|---|---|
| `kind` | `"video"`, `"audio"`, `"image"`, `"imageSequence"` (in quel caso `path` è un pattern `…%05d.png` e `info.video.frameRate` è obbligatorio) |
| `path` | assoluto (POSIX) |
| `relativePath` | relativo alla cartella del `.vproj` per i progetti su file; `null` per le bozze. Usato per primo quando il progetto viene spostato con i media |
| `fingerprint` | §7; serve al ricollegamento dei media mancanti |
| `info` | cache dei metadati del probe; rifatta se il fingerprint cambia. Campi assenti = sconosciuti |
| `proxy` | `"auto"` (decide l'app), `"off"` (mai proxy per questo media) |

Proxy, miniature, waveform, beat, scene e trascrizioni **non** sono nel file: sono cache indicizzate per fingerprint
(§6). Le parti che l'utente conserva (marker dei beat, sottotitoli, tagli) diventano normali oggetti del progetto.

### 5.2 `sequences[]`
```json
{
    "id": "…",
    "name": "Principale",
    "canvas": { "width": 1080, "height": 1920, "preset": "9:16" },
    "defaultBackground": { "type": "blur", "amount": 0.6 },
    "magneticMain": true,
    "visualTracks": [ … ],
    "audioTracks": [ … ],
    "markers": [ … ],
    "groups": [ { "id": "…", "clipIds": [ "…", "…" ] } ]
}
```
- `visualTracks[0]` è la **traccia principale** (kind `"video"`); gli indici crescono verso l'alto (ordine di composizione).
- `defaultBackground`: sfondo del canvas per le clip della traccia principale che non lo definiscono (§5.4).
  Tipi: `{ "type": "color", "color": "#000000FF" }`, `{ "type": "blur", "amount": 0..1 }`,
  `{ "type": "image", "mediaId": "…" }`, `{ "type": "pattern", "asset": AssetRef }`.
- Durata della sequenza: **derivata** (fine dell'ultima clip), non salvata.
- `groups`: clip raggruppate che si selezionano e si spostano insieme.

### 5.3 Tracce (`visualTracks[]`, `audioTracks[]`)
```json
{
    "id": "…",
    "kind": "video",
    "name": null,
    "locked": false,
    "muted": false,
    "solo": false,
    "hidden": false,
    "height": 1.0,
    "captions": false,
    "gainDb": 0.0,
    "clips": [ … ],
    "transitions": [ … ]
}
```
| Campo | Note |
|---|---|
| `kind` | tracce visive: `"video"`, `"text"`, `"sticker"`, `"effect"`, `"adjustment"`; tracce audio: `"audio"` |
| `height` | fattore rispetto all'altezza standard della traccia (1.0) |
| `captions` | solo tracce `"text"`: `true` per una traccia di sottotitoli (contiene clip `subtitle`) |
| `muted` | per le tracce visive silenzia l'audio delle clip; `hidden` le nasconde nell'anteprima e nell'export |
| `gainDb` | volume dell'intera traccia (mixer), `Param` in dB. Aggiunto nella Fase 2 senza cambiare `formatVersion`: è facoltativo (assente = 0) e le versioni precedenti lo conservano come campo sconosciuto |

### 5.4 Clip: campi comuni
```json
{
    "id": "…",
    "kind": "media",
    "start": "0@30000/1001",
    "duration": "150@30000/1001",
    "name": null,
    "enabled": true,
    "linkId": null,
    "transform": {
        "position": [0.0, 0.0],
        "scale": [1.0, 1.0],
        "uniformScale": true,
        "rotation": 0.0,
        "flipH": false,
        "flipV": false,
        "crop": { "left": 0.0, "top": 0.0, "right": 0.0, "bottom": 0.0 },
        "fit": "contain"
    },
    "opacity": 1.0,
    "blendMode": "normal",
    "background": null,
    "effects": [ … ],
    "masks": [ … ],
    "animations": { "in": null, "out": null, "loop": null },
    "transitionIn": null,
    "transitionOut": null,
    "markers": [ … ]
}
```
| Campo | Note |
|---|---|
| `kind` | `"media"`, `"text"`, `"subtitle"`, `"sticker"`, `"color"`, `"effect"`, `"adjustment"`, `"compound"` (§5.5) |
| `start`, `duration` | posizione e durata **in timeline** (`duration > 0`) |
| `enabled` | `false` = clip disattivata (visibile in timeline, esclusa dal rendering) |
| `linkId` | clip con lo stesso `linkId` sono collegate (audio/video) e si muovono insieme |
| `transform.position`, `scale`, `rotation` | `Param` animabili (§3.5) |
| `transform.crop` | frazioni della sorgente tagliate su ogni lato (0..1), animabili |
| `transform.fit` | come la sorgente riempie il canvas a scala 1: `"contain"` (default), `"cover"`, `"stretch"`, `"none"` (pixel 1:1 rispetto a un canvas 1080p di riferimento) |
| `opacity` | `Param` 0..1 |
| `blendMode` | `"normal"`, `"lighten"`, `"screen"`, `"multiply"`, `"overlay"`, `"softLight"`, `"hardLight"`, `"difference"`, `"darken"`, `"color"`, `"luminosity"`, `"add"`, `"colorDodge"`, `"colorBurn"`, `"exclusion"`, `"hue"`, `"saturation"` |
| `background` | solo traccia principale: sfondo del canvas per questa clip (stessi tipi di §5.2); `null` = quello della sequenza |
| `animations` | preset agganciati ai bordi: `{ "type": AssetRef, "duration": RationalTime, "params": {…} }`; se `loop` è presente, `in` e `out` sono ignorati (animazione "combinata") |
| `transitionIn`, `transitionOut` | solo tracce sovrapposte: `{ "type": AssetRef, "duration": RationalTime, "params": {…} }` |
| `markers` | marker della clip (§5.8), tempi nello spazio dei keyframe della clip |

Campi che non si applicano a un tipo di clip (es. `transform` su una clip `effect`) possono essere omessi; se presenti
vengono ignorati e riscritti invariati.

### 5.5 Clip: campi specifici per tipo

**`media`**
```json
{
    "mediaId": "…",
    "streams": "av",
    "sourceIn": "300@30000/1001",
    "speed": 1.0,
    "preservePitch": true,
    "reversed": false,
    "audio": {
        "gainDb": 0.0,
        "muted": false,
        "pan": 0.0,
        "fadeIn": "0@30000/1001",
        "fadeOut": "15@30000/1001"
    }
}
```
- `streams`: `"av"` (video con audio), `"video"` (audio scollegato o estratto), `"audio"` (sulle tracce audio).
- `sourceIn`: primo fotogramma usato della sorgente, nel tempo del contenuto a velocità 1×, sulla griglia del progetto.
  Intervallo di sorgente usato = `[sourceIn, sourceIn + duration × speed)`, arrotondato al fotogramma dall'engine.
  Con `reversed: true` la riproduzione va dalla fine dell'intervallo verso `sourceIn`.
- `speed`: numero (velocità costante, 0.1–100) **oppure** curva:
  `{ "curve": { "preset": "bullet" | null, "points": [ [x, speed], … ] } }`, con `x` ∈ [0, 1] posizione relativa nella
  clip e `speed` > 0; la durata della sorgente consumata è l'integrale della curva.
- `audio.gainDb`: `Param` in dB (−60…+20, `muted` separato); `pan`: `Param` −1…+1; dissolvenze come durate.
- Immagini: `sourceIn` = `"0@<fps>"`, `speed` e `audio` ignorati.
- Media mancante: la clip resta com'è; l'interfaccia mostra "Media mancante" e l'export chiede di ricollegare.

**`text`**
```json
{
    "text": "Ciao Roma!",
    "style": {
        "font": { "family": "Inter", "weight": 700, "italic": false },
        "size": 0.06,
        "color": "#FFFFFFFF",
        "gradient": null,
        "stroke": { "color": "#000000FF", "width": 0.08 },
        "shadow": { "color": "#00000099", "offset": [0.0, 0.02], "blur": 0.03 },
        "background": null,
        "letterSpacing": 0.0,
        "lineHeight": 1.2,
        "align": "center",
        "underline": false
    },
    "spans": [ { "start": 5, "end": 9, "style": { "color": "#FFD54FFF" } } ],
    "stylePreset": null,
    "box": { "width": null },
    "path": null,
    "textEffect": null,
    "tts": null
}
```
- `size` in frazioni dell'altezza del canvas; `stroke.width` e `shadow.blur` in frazioni della dimensione del font.
- `color`, `stroke.color`, `size`, `letterSpacing` sono `Param` animabili.
- `gradient`: `{ "type": "linear", "angle": gradi, "stops": [ [pos 0..1, colore], … ] }`.
- `background`: `{ "color", "padding", "radius" }` (frazioni della dimensione del font).
- `spans`: stili parziali su intervalli di caratteri (indici in unità UTF-16, `end` escluso); si sovrappongono a `style`.
- `box.width`: `null` = larghezza automatica; numero = frazione della larghezza del canvas con a capo automatico.
- `path`: testo su percorso `{ "type": "arc", "bend": −1..1 }` oppure `{ "type": "bezier", "points": [...] }`.
- `stylePreset`, `textEffect`: `AssetRef` (§5.9) al preset di stile o all'effetto testo applicato.
- `tts`: voce generata collegata `{ "voice": "…", "rate": 1.0, "pitch": 0.0, "audioClipId": "…" }`.

**`subtitle`** (solo nelle tracce con `captions: true`): campi di `text`, senza `style` proprio (usa quello della
traccia, sovrascrivibile riga per riga con `styleOverride`), più le parole temporizzate per gli stili animati e il karaoke:
```json
{ "text": "ciao a tutti", "styleOverride": null,
  "words": [ { "w": "ciao", "t0": "0@30", "t1": "9@30" }, { "w": "a", "t0": "9@30", "t1": "12@30" } ] }
```
I tempi delle parole partono dall'inizio della clip (si spostano con lei; tagliando una riga ogni metà tiene le parole
dette nella sua parte). Se `words` manca o non corrisponde più al testo (riga modificata, importata da SRT/VTT), le parole
vengono distribuite sulla durata della riga in proporzione alla loro lunghezza.

La traccia `captions` aggiunge `captionStyle` (assente = stile predefinito: bianco con contorno, in basso):
```json
"captionStyle": { "style": { …come `style` dei testi… }, "preset": "neon-karaoke", "maxWordsPerLine": 3,
                  "position": 0.32, "highlight": "karaoke", "highlightColor": "#FFD600FF",
                  "animation": "pop", "uppercase": false }
```
- `preset`: id dello stile della libreria da cui viene (solo informativo, lo stile è tutto nei campi).
- `maxWordsPerLine`: 0 = la riga intera; N = la riga viene mostrata a gruppi di N parole, seguendo il parlato (0–20).
- `position`: posizione verticale del centro del testo, come frazione dell'altezza del canvas dal centro (−0,5…0,5).
- `highlight`: parola pronunciata `"none"`, `"color"` (colorata), `"scale"` (ingrandita), `"box"` (riquadro),
  `"karaoke"` (riempimento progressivo); `highlightColor` è il suo colore.
- `animation`: entrata di ogni riga o gruppo `"none"`, `"pop"`, `"fade"`, `"bounce"`; `uppercase`: tutto maiuscolo.

**`sticker`** (tracce `sticker`): esattamente una sorgente fra
- `"source": AssetRef` — uno sticker della libreria (immagine SVG/PNG/GIF del pacchetto, o emoji);
- `"mediaId": "…"` — un'immagine del progetto importata dall'utente (PNG, WebP, JPEG, GIF animata);
- `"emoji": "🔥"` — un'emoji disegnata con il font emoji a colori del sistema;
- `"visualizer": { … }` — un visualizzatore audio (insieme a `source` se viene dalla libreria), che reagisce all'audio
  della timeline sotto di esso: `style` (`"bars"`, `"spectrum"`, `"waveform"`, `"circle"`), `barCount` (4–128),
  `primaryColor`, `secondaryColor`, `sensitivity` (0,1–10), `smoothing` (0–1: media sugli ultimi 0,25 s), `mirror`,
  `roundness` (0–1), `thickness` (pixel a 1080p);
- `"graphic": { … }` — un elemento grafico animato, la cui animazione copre la clip: `kind` (`"counter"`, `"countdown"`,
  `"timer"`, `"progressBar"`, `"arrow"`, `"circle"`, `"underline"`, `"highlighter"`, `"check"`, `"cross"`), `color`,
  `color2` (contorno dei numeri o traccia della barra), `thickness` (0–1: dimensione del testo o spessore del tratto);
  per `counter` anche `from`, `to`, `decimals` (0–4), `prefix`, `suffix` (conta nel primo 80% della clip rallentando,
  poi resta sul valore finale); per i segni disegnati a mano `drawSeconds` (tempo per disegnarli).

Facoltativi: `tint` (colore che ricolora l'immagine mantenendone le ombre; assente = nessuno), `loop` (default `true`:
uno sticker animato ricomincia; `false` = resta sull'ultimo fotogramma), `speed` (0,1–10, default 1). Le chiavi
sconosciute sono conservate. Lo sticker è un livello grande quanto il canvas con l'immagine centrata: posizione e
dimensione sono quelle di `transform` (inserito al 35% del canvas; un visualizzatore al 100%).
```json
{ "kind": "sticker", "source": { "pack": "vedit.core", "id": "stickers/shapes/star", "version": 1 },
  "tint": "#0080ffff", "loop": false, "speed": 0.5 }
```

**Segnaposto** (qualsiasi clip visiva, chiave comune `placeholder`): la clip è uno spazio di un template in attesa dei
media dell'utente, `{ "label": "Inquadratura d'apertura", "kind": "any" | "video" | "photo" }`. Nei template della
libreria è una clip `color` grigia; "Sostituisci" la trasforma in una clip `media` con la stessa posizione, durata (un
video più corto la accorcia) e aspetto (trasformazione, effetti, maschere, animazioni, transizioni), senza `placeholder`.

**`color`**: `{ "color": Param colore }`.

**`effect`** (tracce `effect`): `{ "effect": Effect }`, applicato a tutto ciò che sta sotto, per la durata della clip.

**`adjustment`** (tracce `adjustment`): `{ "effects": [ Effect, … ] }`, livello di regolazione (§5.6).

**`compound`**: `{ "sequenceId": "…", "sourceIn": RationalTime, "activeAngle"?: int }`; la sequenza annidata è in `sequences[]`.
Per sequenze multicamera con più tracce video/angoli, `activeAngle` (0-indicizzato, default 0) indica la traccia video attiva / angolatura mostrata.
Non sono ammessi cicli (A contiene B che contiene A): il file verrebbe rifiutato come corrotto.

### 5.6 Effetti (`effects[]`)
```json
{
    "id": "…",
    "type": "vedit.adjust.basic",
    "typeVersion": 1,
    "enabled": true,
    "preset": null,
    "intensity": 1.0,
    "params": { "exposure": 0.2, "contrast": { "keyframes": [ … ] }, "temperature": 0.0 }
}
```
- `type`: id dal registro degli effetti (manifest in `resources/` o pacchetti utente). Spazi dei nomi: `vedit.*` (inclusi
  nell'app), `mlt:<servizio>` (filtro MLT/frei0r usato direttamente), `pack:<pacchetto>/<id>` (pacchetti installati).
- `typeVersion`: versione dei parametri dell'effetto; il manifest può dichiarare migrazioni dei parametri.
- `preset`: `AssetRef` per filtri e look pronti (es. un filtro della libreria basato su LUT); `intensity` 0..1 (animabile).
- `params`: nomi e tipi dal manifest; parametri assenti = valore di default del manifest.
- Tipo sconosciuto: l'oggetto viene conservato così com'è e l'effetto viene saltato nel rendering con un avviso.

### 5.7 Maschere (`masks[]`)
```json
{ "id": "…", "shape": "rectangle", "center": [0.0, 0.0], "size": [0.5, 0.3], "rotation": 0.0,
  "roundness": 0.2, "feather": 0.05, "invert": false, "points": null }
```
- `shape`: `"linear"`, `"mirror"`, `"circle"`, `"rectangle"`, `"heart"`, `"star"`, `"path"`.
- `center`, `size`, `rotation`, `roundness`, `feather` sono `Param`; coordinate nello spazio della **sorgente**
  (frazioni del fotogramma sorgente, origine al centro), così la maschera segue la clip quando la si trasforma.
- `points` (solo `"path"`): `[ { "p": Vec2, "in": Vec2, "out": Vec2 }, … ]` (punti bezier con maniglie relative);
  l'animazione del percorso usa keyframe sull'intero array (`Param` di tipo percorso).
- Più maschere si combinano in unione, nell'ordine.

### 5.8 Transizioni, marker
**Transizioni** (`track.transitions[]`, tra due clip adiacenti della stessa traccia):
```json
{
    "id": "…",
    "type": { "pack": "vedit.core", "id": "transitions/dissolve", "version": 1 },
    "from": "clipIdA",
    "to": "clipIdB",
    "duration": "15@30000/1001",
    "alignment": "center",
    "fillMissing": "freeze",
    "audioCrossfade": true,
    "params": { "easing": "easeInOut", "direction": "left" }
}
```
- `alignment`: `"center"` (centrata sul taglio, usa il materiale oltre i punti di taglio) oppure `"overlap"` (la clip `to`
  inizia `duration` prima della fine di `from`; è l'unica sovrapposizione ammessa sulla stessa traccia).
- `fillMissing`: con `"center"`, cosa fare se manca materiale: `"freeze"` (congela il primo/ultimo fotogramma)
  o `"none"` (ammesso solo se il materiale basta; il validatore lo verifica).
- `params`: dal manifest della transizione (direzione, easing, colore, morbidezza, intensità…).

**Marker** (`sequence.markers[]` e `clip.markers[]`):
```json
{ "id": "…", "t": "90@30000/1001", "duration": null, "name": "Drop", "color": "tertiary", "note": "",
  "kind": "user" }
```
- `color`: ruolo di colore del tema (`"primary"`, `"secondary"`, `"tertiary"`, `"error"`) oppure un colore `#RRGGBBAA`.
- `kind`: `"user"`, `"beat"` (da rilevamento beat accettato), `"chapter"` (capitoli YouTube).

### 5.9 Riferimenti ad asset (`AssetRef`)
`{ "pack": "vedit.core", "id": "transitions/dissolve", "version": 1 }`: elemento di una libreria (transizioni, effetti,
filtri, preset di testo, sticker, animazioni). `pack` = id del pacchetto (`"vedit.core"` per quelli inclusi), `id` = percorso
nel pacchetto, `version` = versione dell'asset al momento dell'uso. Se il pacchetto manca, il riferimento resta intatto
e l'interfaccia propone di reinstallarlo.

---

## 6. Dove stanno i file

### 6.1 Bozze (il caso normale)
```
~/.local/share/vedit/drafts/<projectId>/
├── project.vproj          # il progetto (unica fonte di verità)
├── draft.json             # cache per la schermata iniziale (sotto)
├── thumbnail.jpg          # miniatura della bozza (fotogramma in anteprima alla chiusura, larga al massimo 320 px)
├── state.json             # stato dell'interfaccia (sotto)
├── media/                 # immagini create dall'editor e usate dal progetto (fermi immagine), in PNG
├── history/               # cronologia delle versioni (§9.2, dalla Fase 8)
│   └── 2026-09-24T17-50-00Z.vproj.gz
└── lock                   # presente mentre la bozza è aperta (§9.3)
```
`draft.json` e `thumbnail.jpg` sono cache: se mancano o non sono coerenti si rigenerano da `project.vproj`.
```json
// draft.json (scritto a ogni salvataggio)
{ "name": "Vacanze", "modifiedAt": "2026-09-25T08:12:03Z", "formatVersion": 1,
  "duration": "195@30", "canvas": { "width": 1920, "height": 1080 } }
// state.json (scritto alla chiusura; campi sconosciuti ignorati)
{ "playhead": 100, "export": { "folder": "/home/…/Video", "quality": 1 } }
```
`playhead` è in fotogrammi del progetto; `export.quality`: 0 bassa, 1 consigliata, 2 alta. Il nome della cartella è
l'id del progetto. "Elimina" sposta la cartella nel cestino del sistema (recuperabile).

### 6.2 Progetti su file
"Salva con nome" scrive un `.vproj` dove sceglie l'utente (con `relativePath` compilato). Un `.vproj` aperto da file
viene salvato automaticamente al suo posto; stato UI, cronologia e lock stanno in
`~/.local/state/vedit/external/<sha1 del percorso assoluto>/` per non sporcare la cartella dell'utente.

### 6.3 Cache (mai nel progetto)
`~/.cache/vedit/media/<fingerprint>/` (miniature, waveform, beat, scene, trascrizioni) e `~/.cache/vedit/proxy/`.
Cancellarle non perde nulla: si rigenerano.

### 6.4 Archivio `.vpack` (esporta/importa progetto)
Tar POSIX (ustar) **non compresso**:
```
manifest.json      # { "format": "vedit.pack", "formatVersion": 1, "projectFormatVersion": 1,
                   #   "createdAt": "…", "files": [ { "path", "size", "sha256" } ] }
project.vproj      # con i percorsi dei media resi relativi: "media/<fingerprint-8>-<nome>"
media/…            # i media usati (solo quelli referenziati)
assets/…           # eventuali asset di pacchetti non inclusi nell'app
```
All'import l'archivio si estrae in una nuova bozza (id nuovo se esiste già una bozza con lo stesso id). I percorsi
dell'archivio sono validati (niente `..` né percorsi assoluti).

---

## 7. Fingerprint dei media (`sha256-sampled-v1`)
SHA-256 di: dimensione del file (uint64 little-endian) + primo MiB + 1 MiB a partire da `floor(size/2)` + ultimo MiB.
Per file ≤ 3 MiB: SHA-256 dell'intero file preceduto dalla dimensione. Costa pochi millisecondi anche su file grandi.

**Ricollegamento** all'apertura, per ogni media: `path` → `relativePath` rispetto alla cartella del progetto →
stesso nome nelle cartelle degli altri media già trovati → cartella scelta dall'utente (ricerca ricorsiva).
Un candidato è accettato solo se il fingerprint coincide; se coincide solo il nome, l'utente conferma. Il nuovo percorso
viene salvato; i media non trovati restano segnalati senza bloccare l'apertura.

---

## 8. Versioni e migrazioni
- `formatVersion` = 1 per questo documento. Ogni modifica dello schema alza la versione e aggiunge una funzione pura
  `migrate_vN_to_vN+1(QJsonObject) -> QJsonObject`; il caricatore le applica in catena.
- Ogni migrazione ha fixture di test (`tests/data/format/vN/*.vproj`) e test che verificano il risultato dopo la catena
  completa: **un progetto v1 deve aprirsi in ogni versione futura**.
- Prima di migrare, il file originale viene copiato nella cronologia (`history/…-pre-migration-v1.vproj.gz`); il file
  migrato viene scritto solo al primo salvataggio.
- File con `formatVersion` **maggiore** di quella supportata: non viene aperto né modificato; messaggio chiaro
  ("Creato con una versione più recente di vedit: aggiorna l'app per aprirlo").

## 9. Salvataggio, cronologia, recupero

### 9.1 Salvataggio continuo atomico
1. Dopo ogni comando (e ogni undo/redo): attesa di 300 ms di inattività, al massimo 2 s durante modifiche continue;
   inoltre alla chiusura del progetto o dell'app e alla perdita del focus.
2. Il modello viene serializzato sul thread UI in un buffer (JSON canonico); se il buffer è identico all'ultimo salvato
   non si scrive nulla.
3. Sul thread I/O: scrittura in un file temporaneo nella stessa cartella (`QSaveFile`: `project.vproj.XXXXXX`) →
   `fsync` del file → `rename` sopra `project.vproj` → `fsync` della cartella. Un'interruzione in qualsiasi momento lascia la versione precedente o quella
   nuova, mai un file parziale.
4. Errore di scrittura (disco pieno, permessi): banner persistente con la causa, nuovi tentativi con attesa crescente,
   stato in memoria intatto; nessuna perdita.

### 9.2 Cronologia delle versioni (dalla Fase 8, D-31)
- Snapshot gzip del JSON canonico in `history/`: ogni 10 minuti se ci sono modifiche, alla chiusura, prima di una
  migrazione e prima di ogni "Ripristina versione" (così anche il ripristino è annullabile).
- Conservazione: tutti gli snapshot delle ultime 2 ore; poi uno all'ora per 2 giorni; poi uno al giorno per 30 giorni;
  poi uno alla settimana. Tetto di spazio configurabile (default 200 MB per bozza), si eliminano per primi i più vecchi.
- Ripristinare una versione = un normale comando annullabile che sostituisce il contenuto del progetto.

### 9.3 Lock e recupero dopo crash
- All'apertura si crea `lock` con `{ "pid", "hostname", "bootId", "program", "openedAt" }` e lo si rimuove alla
  chiusura (`program` = nome del processo in `/proc/<pid>/comm`).
- Se all'apertura esiste un lock il cui processo non esiste più (o il `bootId` è diverso, o quel pid ora è un altro
  programma), la sessione precedente si è chiusa in modo anomalo: il progetto è già all'ultimo salvataggio (perdita massima ~2 s) e compare una snackbar
  "Progetto recuperato" con accesso alla cronologia.
- Se il processo esiste ancora (bozza aperta in un'altra istanza), la bozza non si apre una seconda volta. Fase 1:
  un messaggio lo dice; portare in primo piano l'altra finestra richiede l'istanza singola (Fase 8).

### 9.4 Validazione al caricamento
- JSON non valido o campi obbligatori mancanti: il progetto non si apre e viene proposto l'ultimo snapshot valido della
  cronologia (con data e ora). Mai un crash.
- Valori fuori intervallo o enumerazioni sconosciute: corretti al default più vicino, con avviso nel log e una
  snackbar riassuntiva.
- Invarianti (sovrapposizioni, riferimenti, cicli di compound): violazioni riparabili (es. transizione che punta a una
  clip inesistente → rimossa) riparate con avviso; violazioni non riparabili trattate come file corrotto.

---

## 10. Esempio completo (minimo)
```json
{
    "createdAt": "2026-09-24T17:40:12Z",
    "format": "vedit.project",
    "formatVersion": 1,
    "generator": { "app": "vedit", "version": "0.1.0" },
    "id": "5f0c1a2b-3c4d-4e5f-8a9b-0c1d2e3f4a5b",
    "mainSequenceId": "0b6f1d6e-7a8b-4c9d-8e0f-1a2b3c4d5e6f",
    "media": [
        {
            "favorite": false,
            "fingerprint": { "algo": "sha256-sampled-v1", "size": 52428800, "value": "9c1e5f…" },
            "folderId": null,
            "id": "a1b2c3d4-e5f6-4a7b-8c9d-0e1f2a3b4c5d",
            "info": {
                "audio": { "channels": 2, "codec": "aac", "sampleRate": 48000 },
                "duration": "900@30",
                "video": { "codec": "h264", "frameRate": "30", "hasAlpha": false, "hdr": false, "height": 1920,
                           "pixelFormat": "yuv420p", "rotation": 0, "sar": "1", "vfr": false, "width": 1080 }
            },
            "kind": "video",
            "name": "spiaggia.mp4",
            "path": "/home/owais/Video/spiaggia.mp4",
            "proxy": "auto",
            "relativePath": null
        }
    ],
    "mediaFolders": [],
    "modifiedAt": "2026-09-24T18:02:55Z",
    "name": "Spiaggia",
    "sequences": [
        {
            "audioTracks": [],
            "canvas": { "height": 1920, "preset": "9:16", "width": 1080 },
            "defaultBackground": { "amount": 0.6, "type": "blur" },
            "groups": [],
            "id": "0b6f1d6e-7a8b-4c9d-8e0f-1a2b3c4d5e6f",
            "magneticMain": true,
            "markers": [],
            "name": "Principale",
            "visualTracks": [
                {
                    "captions": false,
                    "clips": [
                        {
                            "audio": { "fadeIn": "0@30", "fadeOut": "15@30", "gainDb": 0.0, "muted": false, "pan": 0.0 },
                            "duration": "150@30",
                            "effects": [],
                            "enabled": true,
                            "id": "c0ffee00-1111-4222-8333-444455556666",
                            "kind": "media",
                            "mediaId": "a1b2c3d4-e5f6-4a7b-8c9d-0e1f2a3b4c5d",
                            "opacity": 1.0,
                            "preservePitch": true,
                            "reversed": false,
                            "sourceIn": "60@30",
                            "speed": 1.0,
                            "start": "0@30",
                            "streams": "av",
                            "transform": {
                                "crop": { "bottom": 0.0, "left": 0.0, "right": 0.0, "top": 0.0 },
                                "fit": "contain", "flipH": false, "flipV": false,
                                "position": [0.0, 0.0], "rotation": 0.0, "scale": [1.0, 1.0], "uniformScale": true
                            }
                        }
                    ],
                    "hidden": false,
                    "height": 1.0,
                    "id": "7d8e9f0a-1b2c-4d3e-8f4a-5b6c7d8e9f0a",
                    "kind": "video",
                    "locked": false,
                    "muted": false,
                    "name": null,
                    "solo": false,
                    "transitions": []
                }
            ]
        }
    ],
    "settings": {
        "audioChannels": 2,
        "colorSpace": "bt709",
        "defaultCanvas": { "height": 1920, "preset": "9:16", "width": 1080 },
        "formatFromFirstClip": false,
        "frameRate": "30",
        "sampleRate": 48000
    }
}
```
(Nell'esempio alcuni oggetti sono compattati su una riga per leggibilità; il file reale usa l'indentazione canonica.)


## Appendice B — Kit del marchio (`kit.json`)
Dati dell'applicazione, non del progetto (SPEC §5.13ter): ogni kit è una cartella in
`$XDG_DATA_HOME/vedit/brandkits/<uuid>/` con `kit.json` e le **copie** dei suoi file, così un kit continua a funzionare
se gli originali vengono spostati.
```json
{ "format": "vedit.brandkit", "formatVersion": 1, "name": "Canale",
  "colors": ["#FF5500", "#112233"],            // al massimo 16, compaiono per primi in ogni selettore di colore
  "fonts": ["Inter"],
  "logos": ["logos/logo.png"],                  // percorsi relativi alla cartella del kit
  "intro": "intro/apertura.mp4", "outro": "outro/chiusura.mp4",
  "music": ["music/sigla.mp3"] }
```
Il kit in uso è ricordato in `QSettings` (`brandkit/current`). Un kit eliminato viene spostato in `.trash-<uuid>` (per
"Annulla") ed eliminato davvero all'avvio successivo. Nel progetto i file del kit sono media come gli altri, con il percorso
della copia nel kit: eliminando un kit (o un suo file), i progetti che lo usavano trovano quel media mancante.
