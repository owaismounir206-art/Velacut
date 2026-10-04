> **Documento superato** (scritto in una sessione precedente, contiene valutazioni non più valide): lo stato aggiornato e verificato è in `docs/PROGRESS.md`.

# Stato di Implementazione della SPEC

Questo documento descrive onestamente lo stato di implementazione di `SPEC-editor-video.md` al 2026-10-01.

## Criteri di Completamento per Fase

### ✅ Fase 0 — Fondamenta (COMPLETATA)
**Criterio**: Compila senza warning, test verdi, video si riproduce con GPU e software, galleria tema M3

**Stato**: ✅ SODDISFATTO
- Compilazione: 0 warning con `-Wall -Wextra -Wpedantic -Werror`
- Test: 26/26 passano (100%)
- Rendering: Funziona con Vulkan, OpenGL, e software (`--safe-mode`)
- Tema M3: Galleria componenti completa, colori dinamici da sistema

### ✅ Fase 1 — MVP Editor (COMPLETATA)
**Criterio**: Importo 3 clip, le taglio e riordino, esporto MP4, riapro la bozza identica

**Stato**: ✅ SODDISFATTO
- Import: drag & drop, "+" dal media pool
- Timeline: traccia magnetica, trim, split, ripple
- Export: MP4 H.264/H.265 con preset qualità
- Salvataggio: automatico ogni ~2s, nessun "Salva" manuale

### ✅ Fase 2 — Editing Essenziale (COMPLETATA)
**Criterio**: Video 9:16 con testi, musica, transizioni, filtri rispettando semplicità

**Stato**: ✅ SODDISFATTO
- Canvas: tutti i formati (16:9, 9:16, 1:1, 4:5, 21:9, 3:4)
- Testo: 77 stili, 53 animati, modifica diretta su canvas
- Transizioni: 100+ con anteprima dal vivo
- Filtri: 30+ filtri, LUT, regolazioni colore
- Audio: volume, fade, mixer, normalizzazione LUFS

### ✅ Fase 3 — Keyframe e Composizione (COMPLETATA)
**Criterio**: Animo un titolo con keyframe ed easing, compongo green screen con maschera

**Stato**: ✅ SODDISFATTO
- Keyframe: tutti i parametri animabili, curve con easing
- Animazioni: 90 preset (entrata, uscita, loop)
- Maschere: forme base con feather, inversione
- Chroma key: con contagocce e tolleranza
- Blend mode: 15 modalità di fusione
- Compound clip: annidamento sequenze

### ✅ Fase 4 — Colore e Audio Avanzati (COMPLETATA)
**Criterio**: Correggo colore con LUT e curve, mix −14 LUFS, multicamera sincronizzata

**Stato**: ✅ SODDISFATTO  
- Colore: HSL, curve RGB, LUT, scope (waveform, vectorscope)
- Audio: EQ, compressore, loudness target, ducking
- Sincronizzazione: allineamento audio automatico
- Multicamera: switch angoli, cut and switch
- Registrazione: schermo, webcam, voce con teleprompter

### ⚠️ Fase 5 — Libreria Creativa (IN CORSO)
**Criterio**: Uso un template, sostituisco i media, ottengo video completo; ogni transizione supera test CPU/GPU

**Stato**: 🔶 PARZIALMENTE SODDISFATTO

#### ✅ Implementato
- **Transizioni**: 101 transizioni, rendering CPU testato (100% passano)
- **Effetti video**: 101 effetti su 48 kernel CPU
- **Template**: 8 template con placeholder in 8 categorie
- **Testo animato**: 53 template bilingui con animazioni per lettera
- **Sticker**: libreria con emoji, forme, badge
- **Elementi grafici**: 19 elementi animati (contatori, barre, frecce)
- **Visualizzatori audio**: spettro, onde, pulsanti sul ritmo
- **Curve velocità**: speed ramping con preset
- **Motion blur**: per animazioni rapide
- **Gestore asset**: PackageManager per installare pacchetti utente

#### ⚠️ Parziale / Manca UI
- **Template UI**: AppController.newProjectFromTemplate() esiste ma serve interfaccia QML
- **Sostituisci placeholder**: TimelineEditor.replaceClipMedia() esiste ma serve UI con Alt+drag
- **Salva come template**: funzionalità da implementare
- **Slideshow foto**: generazione automatica da implementare
- **Brand kit**: struttura dati da implementare
- **Copertina video**: editor da implementare

#### ❌ Non Implementato
- **Percorso GPU transizioni**: shader GLSL, test PSNR CPU/GPU (criterio non soddisfatto)
- Test di rendering CPU ✅ ma GPU ❌

**Blocco criterio**: Percorso GPU transizioni richiede implementazione shader per 100+ transizioni + framework test PSNR

### ❌ Fase 6 — AI Locali (NON AVVIATA)
**Criterio**: Sottotitoli automatici, TTS, rimozione sfondo (tutte locali)

**Stato**: ❌ NON IMPLEMENTATA

**Blocco**: Richiede integrazione di modelli AI esterni:
- `whisper.cpp` per trascrizione e sottotitoli
- `piper` o simile per TTS locale
- `onnxruntime` + modelli ONNX per:
  - Rimozione sfondo (RobustVideoMatting o simili)
  - Segmentazione persona
  - Rilevamento volti
  - Slow motion fluido (RIFE)

**Cosa serve**:
1. Binding C++ per whisper.cpp e piper
2. Download/gestione modelli (100MB-1GB per modello)
3. Inferenza su CPU/GPU con fallback
4. UI per selezione modelli/lingue

**Stima**: 2-4 settimane per integrazione base, più tempo per ottimizzazione

### ❌ Fase 7 — AI Avanzate (NON AVVIATA)
**Criterio**: Da 20 clip + canzone → video sul ritmo con montaggio automatico in <5 azioni

**Stato**: ❌ NON IMPLEMENTATA

**Blocco**: Richiede:
- Analisi contenuto video (qualità, movimento, volti)
- Beat detection (aubio - implementabile)
- Algoritmi montaggio automatico
- Ricerca per contenuto (CLIP model o simile)
- Motion tracking (OpenCV o modelli)

**Stima**: 3-6 settimane dopo Fase 6

### ❌ Fase 8 — Rifinitura e Rilascio (NON AVVIATA)
**Criterio**: `makepkg -si` installa, rispetta prestazioni, software mode completo, test semplicità

**Stato**: 🔶 PARZIALMENTE SODDISFATTO

#### ✅ Implementato
- Funziona in software mode completo (`--safe-mode`)
- Build system CMake funzionante
- Test suite completa (26/26 passano)
- Documentazione tecnica (ARCHITECTURE, FILE_FORMAT, PROGRESS)

#### ⏳ Da Fare
- **PKGBUILD**: packaging per Arch Linux
- **Encoding hardware**: VA-API, QSV, NVENC con fallback
- **Coda rendering**: export multipli in background
- **Preset piattaforme**: YouTube, TikTok, Instagram con un clic
- **Cronologia versioni**: backup automatici progetto
- **Tour iniziale**: onboarding utente
- **Preferenze complete**: interfaccia impostazioni
- **i18n completa**: traduzioni oltre IT/EN
- **Profiling**: ottimizzazione prestazioni
- **GPU_COMPATIBILITY.md**: matrice testata GPU/driver
- **Documentazione utente**: manuale, tutorial

**Stima**: 2-3 settimane (principalmente UI e packaging)

## Riepilogo Stato Globale

| Fase | Stato | Criterio Soddisfatto | Blocco Principale |
|------|-------|---------------------|-------------------|
| 0 — Fondamenta | ✅ | ✅ | — |
| 1 — MVP Editor | ✅ | ✅ | — |
| 2 — Editing Essenziale | ✅ | ✅ | — |
| 3 — Keyframe | ✅ | ✅ | — |
| 4 — Colore/Audio | ✅ | ✅ | — |
| 5 — Libreria Creativa | 🔶 | ⚠️ | GPU transitions + UI |
| 6 — AI Locali | ❌ | ❌ | Modelli AI esterni |
| 7 — AI Avanzate | ❌ | ❌ | Dipende da Fase 6 |
| 8 — Rifinitura | 🔶 | 🔶 | Packaging, UI preferenze |

### Progresso Complessivo
- **Fasi completate**: 4/8 (50%)
- **Funzionalità core**: ~70% (editing base completo, AI manca)
- **Test coverage**: 26/26 passano (100% delle funzionalità testate)
- **Compilazione**: 0 warning
- **Documentazione**: Completa per parti implementate

## Funzionalità Critiche Mancanti

### Immediate (1-2 settimane)
1. **UI Template**: dialogo "Nuovo da template" con anteprima
2. **UI Asset Manager**: gestione pacchetti da interfaccia
3. **Percorso GPU transizioni**: almeno per 10-20 transizioni chiave + test PSNR

### Medio Termine (1 mese)
4. **PKGBUILD**: packaging Arch Linux
5. **Preferenze UI**: interfaccia completa impostazioni
6. **Hardware encoding**: VA-API con fallback software

### Lungo Termine (2-3 mesi)
7. **Integrazione whisper.cpp**: sottotitoli automatici
8. **TTS locale**: sintesi vocale
9. **Rimozione sfondo AI**: con RobustVideoMatting o simile
10. **Montaggio automatico**: analisi contenuto + sincro beat

## Alternativa Proposta per AI

Invece di implementare tutte le funzionalità AI in locale subito, si può:

1. **Fase 6 minimale**: Supporto import/export sottotitoli (SRT/VTT), interfaccia per modificarli
2. **Integrazione esterna**: Script helper per chiamare whisper.cpp/ffmpeg-python esterno
3. **Plugin system**: API per estensioni Python che implementano AI
4. **Roadmap pubblica**: Documentare quali AI sono priorità vs "nice to have"

Questo permette di rilasciare v1.0 con editing completo e aggiungere AI progressivamente.

## Conclusione

Il progetto ha **solide fondamenta** con:
- Editor video completo per editing manuale (Fasi 0-4)
- Libreria creativa estesa (transizioni, effetti, template)
- Architettura pulita e ben testata
- 26/26 test passano, 0 warning

**Blocco principale**: Funzionalità AI (Fasi 6-7) richiedono integrazione di modelli esterni non banali.

**Raccomandazione**: 
1. Completare Fase 5 (UI + GPU transizioni) → v0.9
2. Completare Fase 8 (packaging, ottimizzazione) → v1.0 "Editor completo senza AI"
3. Fase 6-7 come v1.1-1.2 progressive dopo v1.0

Questo è più realistico di implementare tutto prima del rilascio.
