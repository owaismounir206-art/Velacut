# velacut — Modelli e componenti AI locali (SPEC §1, §5.12)

Regole della specifica: tutto gira in locale; nessuna funzione AI è necessaria per compilare o avviare l'app; se un
componente manca, la funzione appare **disattivata con un messaggio** che dice cosa installare; i pesi dei modelli si
scaricano solo su richiesta esplicita, da un gestore nelle preferenze, con la dimensione mostrata prima. Solo licenze
che permettono la redistribuzione e l'uso nell'app (Apache 2.0, MIT, BSD o simili), verificate per codice **e** pesi.

## 1. Stato su questa macchina (2026-10-05)
Verificato con `command -v` e `pacman -Qi`: **nessun componente AI è installato** (né `whisper-cli`/whisper.cpp, né
onnxruntime, né piper, né espeak-ng, né rife-ncnn-vulkan, né demucs). Sono presenti FFmpeg (con `vidstabdetect`,
`vidstabtransform`, `scdet`, `silencedetect`, `minterpolate`, `afftdn`, `arnndn`, `loudnorm`), la libreria aubio e
la libreria rnnoise. Non installo pacchetti di sistema (regola del progetto): i comandi sono qui sotto.

## 2. Componenti previsti
| Funzione | Componente | Licenza codice | Licenza pesi | Pacchetto Arch | Stato in velacut |
|---|---|---|---|---|---|
| Sottotitoli automatici, testi delle canzoni, editing dal testo, parole di riempimento | whisper.cpp + modelli ggml di Whisper | MIT | MIT (OpenAI Whisper) | AUR `whisper.cpp` | **integrato** (processo esterno `whisper-cli … -ojf`, trascrizioni in cache per file); senza programma o modello: funzioni disattivate con il comando o il pulsante per il modello. Verificato con un programma sostitutivo nei test, **non ancora con il whisper-cli reale** (non installato qui) |
| Text-to-speech | Piper + voci | MIT | variabile per voce (da verificare voce per voce) | AUR `piper-tts-bin` | **integrato** ("Leggi ad alta voce" sui testi; `piper-tts --model … --output_file …`). Nessuna voce offerta da velacut: l'utente aggiunge le sue (Preferenze → Modelli AI) e ne controlla la licenza. Verificato con un sostituto |
| Rimozione sfondo, segmentazione persona | rembg (programma, usa ONNX Runtime; modello u2net) | MIT | Apache 2.0 (u2net) | `pipx install "rembg[cli]"` | **integrato** ("Rimuovi sfondo": fotogrammi → `rembg p` → copia QuickTime RLE con alfa in cache, audio copiato; trasparenza verificata nella proiezione). rembg scarica il modello (~170 MB) alla prima esecuzione: velacut lo dice e chiede un secondo clic. Verificato con un sostituto |
| Riduzione rumore / isolamento voce | RNNoise (già usato da MLT/FFmpeg) | BSD-3 | BSD-3 (modello incluso) | `rnnoise` (presente) | **già attivo** (Fase 4, pannello Audio) |
| Rilevamento beat | aubio / flusso spettrale proprio | GPL-3 / GPL-3 (velacut) | — | `aubio` (presente) | **già attivo** (rilevatore proprio, D-51) |
| Stabilizzazione | stima del movimento propria (4 regioni, corrispondenza a blocchi, mediana) + levigatura gaussiana | GPL-3 (velacut) | — | — | **attivo**: "Stabilizza" (traslazione; la rotazione è misurata ma non corretta) |
| Rilevamento scene | differenza tra fotogrammi (propria, libavcodec) | GPL-3 (velacut) | — | — | **attivo**: "Dividi le scene" (barra della clip video) |
| Rimozione silenzi | livelli RMS dell'audio (propria, libavcodec) | GPL-3 (velacut) | — | — | **attivo**: "Rimuovi pause" (barra della clip video/audio) |
| Slow motion fluido | RIFE ncnn-vulkan → `minterpolate` (CPU) → frame blending | MIT / LGPL | MIT | AUR `rife-ncnn-vulkan` | **attivo il percorso CPU**: `minterpolate` di FFmpeg (copia in cache della parte usata); RIFE da integrare quando installato |
| Separazione voce/musica | Demucs (programma `demucs`, Python) | MIT | MIT | `pipx install demucs` | **integrato** ("Separa voce e musica" sulle clip audio: `demucs --two-stems=vocals -n htdemucs`, risultati in cache); senza: messaggio con il comando. Verificato con un sostituto |

## 3. Comandi per l'utente (non eseguiti da velacut né dall'assistente)
```bash
# sottotitoli automatici
yay -S whisper.cpp
# rimozione dello sfondo e funzioni di segmentazione
sudo pacman -S onnxruntime
# voce sintetica (poi aggiungere una voce .onnx in Preferenze → Modelli AI)
yay -S piper-tts-bin
# separazione voce/musica
pipx install demucs
# rimozione dello sfondo (scarica il suo modello alla prima esecuzione)
pipx install "rembg[cli]"
# slow motion con interpolazione su GPU Vulkan
yay -S rife-ncnn-vulkan
```
I pesi dei modelli non si installano con questi comandi: si scaricano dal gestore modelli dell'app (Preferenze →
Modelli AI), su richiesta, con la dimensione mostrata prima del download.

## 4. Modelli del parlato (whisper.cpp)
Scaricati da `https://huggingface.co/ggerganov/whisper.cpp/resolve/main/<file>` in `<XDG data>/velacut/models/whisper/`
(file `.part` rinominato a download completo). Licenza dei pesi: MIT (OpenAI Whisper), del formato ggml: MIT.

| Modello | File | Dimensione | Uso |
|---|---|---|---|
| tiny | `ggml-tiny.bin` | ~78 MB | bozza rapida |
| base | `ggml-base.bin` | ~148 MB | consigliato, parlato chiaro |
| small | `ggml-small.bin` | ~488 MB | accenti, rumore |
| medium | `ggml-medium.bin` | ~1,5 GB | il più preciso, lento |

velacut usa il migliore installato. La trascrizione si fa una volta per file, modello e lingua ed è in cache
(`<cache>/media/<fingerprint>/transcript-<modello>-<lingua>.json`).
