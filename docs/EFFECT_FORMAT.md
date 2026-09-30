# vedit — Formato dei pacchetti di asset (effetti, filtri, transizioni, stili di testo)

Versione 1 (Fase 2). Riferimento: SPEC §5.11 ("formato di effetto documentato… nuovi effetti si aggiungano come file in
`resources/` senza ricompilare"), ARCHITECTURE §6.

## 1. Pacchetto
Una cartella con un `pack.json` e dei file di elementi JSON:
```
resources/packs/vedit.core/
├── pack.json          # { "format": "vedit.pack", "formatVersion": 1, "id": "vedit.core", "version": 1,
│                      #   "name": { "en": …, "it": … }, "license": … }
├── effects.json       # effetti con parametri (pannello "Regola")
├── filters.json       # filtri (look di colore)
├── transitions.json   # transizioni
├── stickers.json      # sticker ed emoji, visualizzatori audio
├── stickers/          # immagini degli sticker (SVG, PNG, GIF)
└── text-styles.json   # stili di testo
```
Ogni file di elementi è `{ "categories": [ { "id", "name" } ], "items": [ … ] }`. I nomi sono oggetti
`{ "en": …, "it": … }` (i manifest sono dati: l'interfaccia sceglie la lingua). Il pacchetto `vedit.core` è incorporato
nell'applicazione (risorse Qt `:/vedit/packs/vedit.core`), quindi l'app funziona anche senza file installati. I
pacchetti dell'utente (stessa struttura, in `~/.local/share/vedit/packs/<id>/`) arrivano con la gestione dei
pacchetti (Fase 5).

Nel progetto un elemento si riferisce con un `AssetRef` `{ "pack": "vedit.core", "id": "filters/warm", "version": 1 }`
(FILE_FORMAT §5.9). Gli id non cambiano mai; una modifica incompatibile alza `version`.

## 2. Filtri (`filters.json`)
```json
{ "kind": "filter", "id": "filters/teal-orange", "version": 1, "category": "cinema",
  "name": { "en": "Teal & orange", "it": "Verde acqua e arancio" },
  "look": { "contrast": 0.2, "saturation": 0.1, "shadowTone": [-0.6, 0.2, 0.6], "highlightTone": [0.7, 0.25, -0.5],
            "splitAmount": 0.8, "vignette": -0.25 } }
```
`look` usa i parametri di colore del kernel `fx::ColorAdjust` (tutti facoltativi, 0 = invariato):
`exposure` (stop, −3…3), `brightness`, `contrast`, `highlights`, `shadows`, `whites`, `blacks`, `saturation` (−1 = bianco
e nero), `vibrance`, `temperature`, `tint` (−1…1), `fade` (0…1), `shadowTone`/`highlightTone` ([r, g, b] −1…1) con
`splitAmount`, `curve` (punti `[x, y]` 0…1). In più `vignette` (−1…1), `grain` (0…1), `sharpness` (0…1).
Un solo kernel li rende tutti: il colore diventa una LUT 3D 33³ applicata con interpolazione trilineare.

Nel progetto un filtro è un effetto `{ "type": "vedit.filter", "preset": AssetRef, "intensity": 0…1 }`: l'intensità
mescola l'immagine originale con quella filtrata.

## 3. Effetti con parametri (`effects.json`)
```json
{ "kind": "effect", "id": "vedit.adjust.basic", "version": 1, "name": { … },
  "params": [ { "name": "exposure", "min": -3, "max": 3, "default": 0, "label": { … }, "advanced": false } ] }
```
I parametri `advanced` stanno nella sezione "Avanzate" (chiusa) del pannello. `vedit.adjust.basic` ha gli stessi nomi del
`look` dei filtri. Nel progetto: `{ "type": "vedit.adjust.basic", "params": { "exposure": 0.3, … } }`.

## 4. Transizioni (`transitions.json`)
```json
{ "kind": "transition", "id": "transitions/slide-left", "version": 1, "category": "motion",
  "name": { … }, "kernel": "slideLeft", "defaultDuration": 0.5, "params": { "easing": "easeInOut" } }
```
`kernel` è una delle transizioni CPU di `fx::TransitionKind` (`dissolve`, `dipToBlack`, `dipToWhite`, `slide*`, `push*`,
`wipe*`, `iris`, `clock`, `zoomIn`); più elementi possono usare lo stesso kernel con parametri diversi.
Parametri comuni: `easing` (`linear`, `easeIn`, `easeOut`, `easeInOut`), `softness` (bordo morbido, 0…0.5).
Il percorso GPU facoltativo (Fase 5) aggiungerà un campo `gpu` con lo shader; il kernel CPU resta obbligatorio.

## 5. Stili di testo (`text-styles.json`)
```json
{ "kind": "textStyle", "id": "text/outline", "version": 1, "category": "basic", "name": { … },
  "style": { …stesso schema di "style" del testo, FILE_FORMAT §5.5… } }
```
Applicare uno stile copia `style` nella clip (il progetto resta autosufficiente) e registra `stylePreset`.

## 6. Dimensione attuale delle librerie (Fase 2)
32 filtri in 7 categorie, 18 transizioni in 3 categorie, 24 stili di testo in 5 categorie. La specifica chiede alla
fine almeno 60 filtri, 100 transizioni e 40 stili: le librerie crescono nelle fasi successive (transizioni 3D e
distorsioni con la Fase 5, effetti testo con la Fase 3).

## 7. Sticker (`stickers.json`, Fase 5)
```json
{ "id": "stickers/shapes/star", "version": 1, "category": "shapes", "name": { … }, "path": "stickers/shape_star.svg",
  "animated": false, "defaultDuration": 3.0 }
{ "id": "stickers/emoji/fire-emoji", "version": 1, "category": "emoji", "name": { … }, "emoji": "🔥" }
{ "id": "visualizers/bars_neon", "version": 1, "category": "visualizers", "name": { … },
  "visualizer": { "style": "bars", "barCount": 32, "primaryColor": "#00DCFF", "secondaryColor": "#FF55AA", … } }
```
`path` è relativo alla cartella del pacchetto (SVG disegnato alla risoluzione del canvas; PNG/WebP; GIF/WebP animati
con `"animated": true`). `emoji` usa il font emoji a colori del sistema. `visualizer` ha lo schema di FILE_FORMAT §5.5 e
viene copiato nella clip, dove resta modificabile. Il pacchetto `vedit.core` ha 90 sticker in 6 categorie (23 immagini,
63 emoji, 4 visualizzatori).

## 8. Effetti a ritmo (`effects.json`, categoria `rhythm`)
`vedit.beat.flash` (`amount` 0–1 verso il bianco), `vedit.beat.zoom` (`amount`: ingrandimento in più sul beat),
`vedit.beat.shake` (`amount`: pixel a 1080p); `decay` = secondi in cui l'impulso si spegne. I beat vengono dai marker
"Beat" (ARCHITECTURE D-51).
