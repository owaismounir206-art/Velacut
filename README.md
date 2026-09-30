# vedit — Editor Video Locale

Video editor desktop nativo per Linux, progettato per essere veloce, semplice e completamente locale (nessun cloud, nessun account richiesto).

## Stato del Progetto

**Versione**: Pre-alpha (in sviluppo attivo)  
**Fasi completate**: 0–4 (Fondamenta, MVP, Editing essenziale, Keyframe, Colore e audio)  
**Fase corrente**: 5 (Libreria creativa — in corso)

### Funzionalità Implementate

#### ✅ Core (Fasi 0-4)
- **Editing base**: trim, split, ripple, duplica, riordina clip
- **Timeline**: traccia magnetica, snapping, tracce automatiche
- **Trasformazioni**: posizione, scala, rotazione con maniglie su canvas
- **Testo**: stili pronti, animazioni, modifica diretta sul canvas
- **Keyframe**: animazione di tutti i parametri con curve di easing
- **Audio**: volume, fade, mixer, normalizzazione loudness LUFS
- **Transizioni**: libreria di 100+ transizioni (rendering CPU)
- **Effetti**: 101 effetti video su 48 kernel CPU
- **Filtri e colore**: HSL, curve, LUT, scope, correzione colore
- **Maschere**: forme base con feather, tracciamento manuale
- **Chroma key**: rimozione sfondo verde/blu con tolleranza
- **Velocità**: costante, reverse, freeze frame, curve di velocità
- **Export**: MP4 H.264/H.265, preset qualità, stima dimensione

#### 🔨 In Corso (Fase 5)
- **Template**: 8 template pronti con placeholder sostituibili
- **Sticker**: libreria di emoji, forme, badge
- **Elementi grafici animati**: contatori, barre progresso, frecce
- **Visualizzatori audio**: spettro, onde, pulsanti sul ritmo
- **Effetti sul beat**: flash, zoom, glitch sincronizzati
- **Gestore asset**: installazione pacchetti utente da cartelle

#### ⏳ Pianificate (Fasi 6-8)
- AI locali (sottotitoli auto, TTS, rimozione sfondo) - **richiede modelli esterni**
- Montaggio automatico sul ritmo
- Encoding hardware (VA-API, QSV, NVENC)
- Packaging (PKGBUILD per Arch Linux)

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

## Compilazione

```bash
# Clone
git clone https://github.com/yourusername/vedit.git
cd vedit

# Build
cmake --preset dev
cmake --build build

# Test
ctest --test-dir build --output-on-failure

# Run
./build/vedit
```

### Preset CMake Disponibili
- `dev`: RelWithDebInfo, ottimizzato per sviluppo
- `debug`: Debug con ASan+UBSan per trovare bug
- `release`: Release ottimizzato per produzione

## Uso Base

1. **Nuovo progetto**: avvia vedit o trascina un video
2. **Import media**: trascina file sulla timeline o usa "+"
3. **Editing**: trim con maniglie, split con `S`, cancella con `Del`
4. **Testo**: pannello Testo → scegli stile → scrivi sul canvas
5. **Transizioni**: clicca sul taglio tra due clip → scegli dalla libreria
6. **Effetti**: seleziona clip → Effetti → scegli dalla libreria
7. **Export**: `Ctrl+E` → scegli risoluzione e qualità → Esporta

Vedi [SHORTCUTS.md](docs/SHORTCUTS.md) per tutte le scorciatoie.

## Test

```bash
# Tutti i test (26 suite)
ctest --test-dir build --output-on-failure

# Test specifici
./build/tests/unit/tst_timelineeditor    # Editor logica
./build/tests/unit/tst_transitions       # Rendering transizioni
./build/tests/integration/tst_ui         # Interfaccia completa
```

**Stato test**: 26/26 passano (100%)

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
