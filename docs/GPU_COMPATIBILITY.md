# vedit — Compatibilità GPU (SPEC §1bis)

## 1. Come funziona
- **Rilevamento isolato**: `vedit-gpuprobe --vulkan | --opengl | --video` gira in processi figli separati (Vulkan e
  OpenGL in parallelo, timeout 4 s; video in background, timeout 20 s). Un crash o un blocco del driver segna solo
  quell'API come non utilizzabile (`crashed` / `timedOut`).
- **Cache**: `~/.cache/vedit/gpu-caps.json`, valida finché non cambia l'impronta dei driver (kernel, dispositivi DRM,
  file dei driver Mesa/NVIDIA/Vulkan/VA-API, versione di Qt, variabili d'ambiente rilevanti). `vedit --reprobe` la ignora.
- **Catene di fallback** (`decideGraphics`, testata in `tst_gpu`):
  UI Vulkan (solo dispositivi hardware) → OpenGL (≥ 2.1 / ES 2.0) → software; effetti GPU solo con OpenGL hardware;
  decodifica/codifica hardware solo se il probe ha davvero decodificato/codificato dei fotogrammi.
- **Variabili che l'utente può imporre**: `QT_QUICK_BACKEND=software`, `QSG_RHI_BACKEND=vulkan|opengl` (sempre rispettate).
- **Safe mode**: `vedit --safe-mode`, o automatico dopo **2 avvii consecutivi** che non hanno raggiunto uno stato stabile
  (primo fotogramma + 3 s). Stato in `~/.local/state/vedit/startup.json`.
- **Fallback a runtime**: su `sceneGraphError` (errore del driver, perdita del dispositivo) vedit registra il backend come
  fallito *per quel driver*, si riavvia sul successivo della catena (il salvataggio continuo evita perdite).
- **Anteprima video**: superficie scelta in base al backend realmente attivo (`GraphicsInfo.api`): shader RHI con
  texture persistente su Vulkan/OpenGL, nodi immagine sul backend software.
- **Informazioni di sistema**: pulsante "Informazioni di sistema" → "Copia informazioni di sistema".

## 2. Matrice delle configurazioni

Legenda: ✅ provata (smoke test: file decodificato e **mostrato** ≥ 44 fotogrammi) · 🧩 progettata ma non provata ·
— non applicabile.

| Configurazione | Stato | Note |
|---|---|---|
| Intel Arc 140V (Lunar Lake, `xe`), Vulkan ANV, Wayland | ✅ | backend scelto in automatico |
| Stessa GPU, OpenGL (iris), Wayland | ✅ | `QSG_RHI_BACKEND=opengl` |
| Stessa GPU, X11 (XWayland, `QT_QPA_PLATFORM=xcb`), Vulkan e OpenGL | ✅ | |
| OpenGL software llvmpipe (`LIBGL_ALWAYS_SOFTWARE=1`) | ✅ | effetti GPU disattivati automaticamente |
| Qt Quick software (`QT_QUICK_BACKEND=software`) | ✅ | anche headless (`QT_QPA_PLATFORM=offscreen`), in CTest |
| Safe mode (`--safe-mode`) | ✅ | in CTest |
| Probe che va in crash / si blocca / fallisce | ✅ | simulato con un finto probe in `tst_gpu` |
| Video hardware: VA-API e QSV (intel-media-driver) | ✅ rilevati e usati | encoder H.264/HEVC/AV1, decoder H.264/HEVC/VP9/AV1; l'export li usa con fallback software automatico |
| NVIDIA proprietario (`nvidia`, `nvidia-open`), rami legacy 470/390, nouveau | 🧩 | nessuna GPU NVIDIA disponibile qui |
| AMD `amdgpu` (RADV/RadeonSI), `radeon` senza Vulkan | 🧩 | |
| Intel vecchie (`crocus`, `i915`, OpenGL 2.1–3.x) | 🧩 | |
| Grafica ibrida PRIME | 🧩 | scelta della GPU nelle Preferenze: Fase 8 |
| Macchine virtuali (virgl, VMware SVGA), sessioni remote | 🧩 | rilevate (`/sys/class/dmi`, variabili SSH/XRDP/VNC) |

## 3. Variabili utili per il debug
| Variabile / opzione | Effetto |
|---|---|
| `vedit --safe-mode` | nessuna accelerazione GPU |
| `vedit --reprobe` | ignora la cache delle capacità |
| `QT_QUICK_BACKEND=software` | scena Qt Quick in software |
| `QSG_RHI_BACKEND=vulkan\|opengl` | forza l'API della UI |
| `QSG_INFO=1` | Qt stampa backend e dispositivo usati |
| `LIBGL_ALWAYS_SOFTWARE=1` | OpenGL via llvmpipe |
| `QT_QPA_PLATFORM=xcb\|wayland\|offscreen` | piattaforma Qt |
| `DRI_PRIME=1`, `__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia` | GPU dedicata su sistemi ibridi |
| `LIBVA_DRIVER_NAME=iHD\|radeonsi` | driver VA-API |
| `VK_LOADER_DEBUG=all` | diagnostica del loader Vulkan |
| `SDL_AUDIODRIVER=dummy` | nessuna uscita audio (test) |
| `QT_LOGGING_RULES="vedit.*.debug=true"` | log dettagliati di vedit (anche in `~/.local/state/vedit/logs/vedit.log`) |
| `./build/vedit-gpuprobe --vulkan\|--opengl\|--video` | esegue a mano un singolo probe (stampa JSON) |

## 4. Verifiche della Fase 1 (editor completo)
Su questa macchina (Intel Arc 140V, Mesa 26.2, Hyprland Wayland) lo smoke test dell'editor (`vedit --smoke-test`:
nuovo progetto, import, timeline, proiezione MLT, anteprima; almeno 45 fotogrammi decodificati e 20 mostrati) passa con
UI **Vulkan**, **OpenGL** (`QSG_RHI_BACKEND=opengl`), **software** headless (`QT_QUICK_BACKEND=software`
`LIBGL_ALWAYS_SOFTWARE=1`, in CTest) e **safe mode**. Tutta la pipeline video dell'editor gira su CPU (composizione
`vedit.composite`, miniature e forme d'onda FFmpeg, export libx264): la GPU serve solo alla UI.

Variabili aggiunte per i test: `VEDIT_MUSIC_DIR` (cartella della libreria musicale), `VEDIT_UI_SHOTS` (screenshot di
`tst_ui`), `VEDIT_NO_SANDBOX=1` (disattiva la sandbox XDG delle build di sviluppo: **attenzione**, così l'app scrive
nella home vera).

## 5. Le due GPU obiettivo: AMD Radeon 740M e Intel Arc 130V

Entrambe sono **iGPU con memoria condivisa** (UMA): ogni pixel dell'anteprima passa per la RAM di sistema, e ogni byte
scritto dall'encoder VA-API arriva da lì. Le ottimizzazioni mirano a ridurre i pixel, non ad aggiungere percorsi GPU
del vendor (nessuna estensione proprietaria, SPEC 1bis regola 7).

### AMD Radeon 740M (Phoenix2, RDNA 3, VCN 4) — provata direttamente
- **Probe** (`vedit-gpuprobe --video`, Mesa 26.2 `radeonsi`/RADV): encoder **H.264, HEVC e AV1 VA-API funzionanti**
  (verificati codificando davvero 5 fotogrammi), decoder H.264/HEVC/VP9/AV1 VA-API e Vulkan Video. Il probe trova
  anche gli encoder `_vulkan`; VA-API resta il primo della catena (più maturo, identico su tutti i vendor).
- **Export hardware** (misurato): `h264_vaapi`, `hevc_vaapi`, `av1_vaapi` via consumer `avformat` di MLT 7.40
  funzionano (test `tst_export::exportsWithTheGpuEncoderWhenAvailable`). L'opzione `quality` del driver Mesa ha
  **range 0–32** (verificato: «Invalid quality level: valid range is 0-32»): vedit usa 8/18/28 per Alta/
  Consigliata/Bassa, valori validi anche sugli altri driver (Intel: 0–52).
- **Dimensione massima del file**: gli encoder VA-API non hanno il two-pass; vedit usa VBR con tetto a 1,5× il
  bitrate medio (`vb` + `maxrate`). Il file risultante resta sotto il limite scelto; in software (`libx264`)
  si usa il two-pass vero e proprio.
- **Decodifica hardware**: verificato con `melt avformat:… hwaccel=vaapi` su un H.264 1080p60: **nessun beneficio
  misurabile** (1,26 s vs 1,26 s su Ryzen 7840HS; il download GPU→RAM annulla il risparmio). La preferenza
  «decodifica hardware» resta rispettata dal probe ma il producer MLT non la usa: il beneficio sarebbe nullo e il
  rischio di fotogrammi neri con driver difettosi reale (SPEC 1bis regola 4: mai un fotogramma nero). Da rivalutare
  quando la pipeline preview lavorerà con frame sul device.
- **Anteprima ridotta**: la 740M riporta ~8,6 GB di «device local» (carveout GTT): `lowVideoMemory` resta spento, ma
  `decideGraphics` la riconosce come **iGPU** (`integratedGpu`) e l'anteprima viene renderizzata al massimo a 1080p
  di lato corto anche su canvas 4K (`TimelinePlayer::setPreviewLimit`). L'export resta sempre a piena risoluzione.

### Intel Arc 130V (Lunar Lake, Xe2, driver `xe` + intel-media-driver)
- **Pipeline identica**: la 130V espone VA-API (H.264/HEVC/AV1 encode, H.264/HEVC/VP9/AV1 decode) **e** QSV; la
  catena di vedit preferisce VA-API (stesso codice della 740M) e usa QSV solo se VA-API manca. L'encoder AV1
  hardware della Xe2 è il più veloce della categoria: nel pannello «Avanzate» dell'export «AV1 — il file più
  piccolo» diventa pratico.
- L'`intel-media-driver` deve essere installato (pacchetto Arch omonimo) perché il probe trovi VA-API: senza di esso
  l'export torna in software senza errori (catena di fallback, SPEC 1bis regola 3).
- Stessa riduzione dell'anteprima: iGPU riconosciuta → anteprima ≤ 1080p di lato corto.
- Se l'utente forza `QSG_RHI_BACKEND=opengl` tutto continua a funzionare (driver ANV/iris provati su Lunar Lake
  nella matrice della sezione 2).

### Cosa NON è ottimizzato per queste GPU (e perché)
- Nessuno shader/deps del vendor: gli effetti restano sui kernel CPU multithread (verificati identici su tutti i
  percorsi); il percorso GPU degli effetti (GLSL 2.1) è la Fase 5 residua.
- Nessuna decodifica VA-API nel producer MLT: beneficio nullo misurato, vedi sopra.

