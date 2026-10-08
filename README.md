# velacut — Editor Video Locale

Video editor desktop nativo per Linux, progettato per essere veloce, semplice e completamente locale (nessun cloud, nessun account richiesto).

## Stato del Progetto

**Versione**: pre-alpha, in sviluppo attivo. Stato dettagliato e verificato in `docs/PROGRESS.md`.
**Fasi complete**: 0–5 (fondamenta, MVP, editing essenziale, keyframe e composizione, colore e audio avanzati,
libreria creativa). **Fase 6** (AI locali): funzioni fatte; quelle che usano programmi esterni (whisper.cpp, rembg,
Demucs, Piper) sono provate con programmi sostitutivi e restano da verificare con quelli veri (`docs/MODELS.md`).
**Fase 8** (rifinitura) in parte: encoding hardware, preset piattaforme, preferenze, PKGBUILD. **Fase 7**: da fare.

### Cosa c'è
- **Interfaccia in stile CapCut** con Material You: pannelli ridimensionabili, schermata iniziale con progetti, template
  e strumenti rapidi, barra contestuale a icone, timeline magnetica, copertina, anteprima a schermo intero (F11).
- **Montaggio**: dividi, elimina a sinistra/destra (Q/W), ripple, trim, duplica, sostituisci, congela, inverti,
  clip composte, multicamera, marker, entrata/uscita, aggancio, asse di anteprima.
- **Libreria creativa**: 114 transizioni (CPU e GPU, verificate identiche), 66 filtri, 101 effetti video, 77 stili di
  testo (53 animati), 93 animazioni, 109 sticker ed elementi animati, visualizzatori ed effetti a ritmo, curve di
  velocità, motion blur.
- **Creazione rapida**: template con segnaposto (scegli → metti i tuoi video → fatto), slideshow dalle foto sul ritmo
  della musica, registrazione di schermo/webcam/voce con gobbo.
- **Kit del marchio**: colori, font, loghi (anche come filigrana), intro/outro, musiche.
- **Colore e audio**: LUT, curve, ruote, HSL, scope; loudness LUFS, ducking, riduzione rumore, effetti voce.
- **Export**: MP4 H.264/H.265/AV1, encoding hardware con ripiego software, preset per le piattaforme, dimensione
  massima, copertina incorporata, fotogramma come immagine.
- **Sottotitoli**: da file SRT/WebVTT, scritti, da copione o automatici dal parlato; 40 stili social con la parola
  pronunciata evidenziata (colore, ingrandita, riquadro, karaoke) e animazioni; editor delle righe, cerca/sostituisci.
- **AI locali**: rimuovi pause, dividi le scene, stabilizza, rallentatore fluido, adatta a 9:16 seguendo il soggetto
  (senza modelli); con programmi facoltativi: sottotitoli automatici, editing dalla trascrizione, parole di
  riempimento, capitoli per YouTube (whisper.cpp), rimuovi sfondo (rembg), separa voce e musica (Demucs), leggi ad
  alta voce (Piper). Gestore modelli con download solo su richiesta.
- **Preferenze**: tema chiaro/scuro e colori dinamici, lingua, motore grafico e accelerazioni, pacchetti di asset,
  modelli AI.

### Cosa manca (vedi `docs/PROGRESS.md`)
Verifica delle funzioni AI con i programmi veri (non installati qui), RIFE per il rallentatore, funzioni della Fase 7
(montaggio automatico, da copione a video, tracciamento, ritocco viso…), coda di rendering ed export multi-formato,
cronologia delle versioni, tour iniziale.

## Requisiti

### Build
- CMake ≥ 3.21
- C++20 compiler (GCC ≥ 11, Clang ≥ 14)
- Qt 6.5+
- MLT Framework 7.x
- FFmpeg 6.x+

### Runtime
- Linux con Wayland o X11
- GPU: Intel/AMD/NVIDIA con driver Vulkan o OpenGL 3.3+
- Fallback software completo disponibile (`--safe-mode`)

## Installazione come Applicazione

Puoi installare Velacut come una vera applicazione desktop per Linux (con icona nel menu di sistema, associazione file `.vproj` e lanciatore desktop).

### Metodo 1: Script Diretto (Consigliato)

```bash
git clone https://github.com/owaismounir206-art/Velacut.git
cd Velacut

# Installazione per l'utente corrente (senza sudo, in ~/.local):
./install.sh

# Oppure installazione di sistema (in /usr/local):
sudo ./install.sh
```

### Metodo 2: Arch Linux / EndeavourOS (Pacman)

```bash
cd Velacut
makepkg -si
```

### Metodo 3: CMake Standard

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DVELACUT_DEV_SANDBOX=OFF -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build
sudo cmake --install build
```

---

## Tutorial e Guida all'Uso

Consulta la guida dettagliata passo-passo in [**docs/TUTORIAL.md**](docs/TUTORIAL.md).

### Guida Rapida:
1. **Avvio**: Apri "Velacut" dal menu delle applicazioni o digita `velacut` nel terminale.
2. **Nuovo progetto**: Clicca su "Nuovo Progetto" o trascina un video nella schermata iniziale.
3. **Importazione media**: Trascina video, musica o immagini sulla timeline.
4. **Montaggio**:
   - `S`: Taglia (split) la clip nel punto del cursore.
   - `Q` / `W`: Ripple-trim (cancella ed elimina lo spazio a sinistra o a destra).
   - `Canc`: Elimina la clip selezionata.
   - `Spazio`: Riproduci o metti in pausa.
5. **Transizioni ed Effetti**: Clicca sui tagli per scegliere tra oltre 100 transizioni, oppure seleziona una clip per applicare filtri ed effetti video.
6. **Sottotitoli**: Genera automaticamente i sottotitoli sincronizzati parola per parola con AI locale (`whisper.cpp`).
7. **Esportazione**: Premi `Ctrl+E` per aprire la finestra di export (MP4, HEVC, AV1, ProRes, GIF) con accelerazione hardware.

Vedi anche [SHORTCUTS.md](docs/SHORTCUTS.md) per l'elenco completo delle scorciatoie.

---

## Disinstallazione

Per rimuovere completamente Velacut dal sistema:

```bash
cd Velacut

# Disinstallazione automatica dei binari, icona e lanciatore:
./uninstall.sh

# Oppure per rimuovere anche tutte le impostazioni, bozze e cache utente:
./uninstall.sh --purge
```

Se hai installato tramite `makepkg` su Arch Linux:
```bash
sudo pacman -R velacut
```

Se hai installato tramite CMake:
```bash
cd Velacut/build && sudo ninja uninstall
```

---

## Compilazione per Sviluppatori

```bash
# Clone
git clone https://github.com/owaismounir206-art/Velacut.git
cd Velacut

# Build con sandbox di sviluppo
cmake --preset dev
cmake --build build

# Test
ctest --test-dir build --output-on-failure

# Esegui dalla cartella di build
./build/velacut
```

### Preset CMake Disponibili
- `dev`: RelWithDebInfo, ottimizzato per sviluppo (con sandbox dev-home)
- `debug`: Debug con ASan+UBSan per trovare bug
- `release`: Release ottimizzato per produzione

## Test

```bash
# Tutti i test (32 suite)
ctest --test-dir build --output-on-failure

# Test specifici
./build/tests/unit/tst_timelineeditor    # Editor logica
./build/tests/unit/tst_transitions       # Rendering transizioni
./build/tests/integration/tst_ui         # Interfaccia completa
```

**Stato test**: 32/32 passano (100%)

## Architettura

Il progetto è strutturato in moduli indipendenti:

- **`src/core/`**: modello dati, undo/redo, serializzazione (no Qt GUI)
- **`src/fx/`**: kernel effetti/transizioni, libreria asset (no MLT)
- **`src/engine/`**: integrazione MLT, player, rendering
- **`src/ui/`**: interfaccia QML, controller, modelli Qt

Vedi [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) per dettagli completi.

## Formato Progetto

I progetti sono salvati come `.vproj` (JSON UTF-8 leggibile):
- Salvataggio automatico ogni ~2s (no tasto "Salva")
- Compatibile con git (diff leggibili)
- Formato documentato in [docs/FILE_FORMAT.md](docs/FILE_FORMAT.md)

## Licenza

GPL-3.0-or-later

## Sviluppo

Progetto attivamente in sviluppo. Contributi benvenuti dopo la versione 1.0.

**Stato documentazione**:
- ✅ ARCHITECTURE.md: decisioni architetturali
- ✅ FILE_FORMAT.md: formato `.vproj` v1
- ✅ PROGRESS.md: storico sviluppo e decisioni
- ⏳ SHORTCUTS.md: scorciatoie tastiera (da completare)
- ⏳ USABILITY.md: test di semplicità (parziale)

Per domande o bug: vedi issue tracker (quando pubblico).
