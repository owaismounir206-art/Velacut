# vedit — Modelli e componenti AI locali (SPEC §1, §5.12)

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
| Funzione | Componente | Licenza codice | Licenza pesi | Pacchetto Arch | Stato in vedit |
|---|---|---|---|---|---|
| Sottotitoli automatici, testi delle canzoni, editing dal testo, parole di riempimento | whisper.cpp + modelli ggml di Whisper | MIT | MIT (OpenAI Whisper) | AUR `whisper.cpp` | da integrare (processo esterno `whisper-cli`); senza: funzioni disattivate con messaggio |
| Text-to-speech | Piper + voci | MIT | variabile per voce (molte CC BY 4.0 / MIT; da verificare voce per voce prima di offrirla) | AUR `piper-tts-bin` | da integrare; senza: disattivato |
| Rimozione sfondo, segmentazione persona | ONNX Runtime + modello di segmentazione | MIT | dipende dal modello (es. MODNet Apache 2.0) | `onnxruntime` | da integrare; senza: disattivato |
| Riduzione rumore / isolamento voce | RNNoise (già usato da MLT/FFmpeg) | BSD-3 | BSD-3 (modello incluso) | `rnnoise` (presente) | **già attivo** (Fase 4, pannello Audio) |
| Rilevamento beat | aubio / flusso spettrale proprio | GPL-3 / GPL-3 (vedit) | — | `aubio` (presente) | **già attivo** (rilevatore proprio, D-51) |
| Stabilizzazione | vid.stab via FFmpeg | GPL-2 | — | in `ffmpeg` (presente) | da integrare (senza modelli) |
| Rilevamento scene | differenza tra fotogrammi (propria, libavcodec) | GPL-3 (vedit) | — | — | **attivo**: "Dividi le scene" (barra della clip video) |
| Rimozione silenzi | livelli RMS dell'audio (propria, libavcodec) | GPL-3 (vedit) | — | — | **attivo**: "Rimuovi pause" (barra della clip video/audio) |
| Slow motion fluido | RIFE ncnn-vulkan → `minterpolate` (CPU) → frame blending | MIT / LGPL | MIT | AUR `rife-ncnn-vulkan` | da integrare; senza RIFE: ripiego CPU |
| Separazione voce/musica | Demucs (ONNX) | MIT | MIT | — | da integrare; senza: disattivato |

## 3. Comandi per l'utente (non eseguiti da vedit né dall'assistente)
```bash
# sottotitoli automatici
yay -S whisper.cpp
# rimozione dello sfondo e funzioni di segmentazione
sudo pacman -S onnxruntime
# voce sintetica
yay -S piper-tts-bin
# slow motion con interpolazione su GPU Vulkan
yay -S rife-ncnn-vulkan
```
I pesi dei modelli non si installano con questi comandi: si scaricano dal gestore modelli dell'app, quando c'è, su
richiesta, con la dimensione mostrata prima del download.
