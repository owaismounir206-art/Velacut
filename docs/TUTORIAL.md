# Tutorial Ufficiale: Guida all'Uso e Disinstallazione di Velacut

Benvenuto nella guida ufficiale di **Velacut**, l'editor video desktop nativo per Linux, moderno, rapido e completamente offline.

---

## Indice
1. [Requisiti di Sistema](#1-requisiti-di-sistema)
2. [Installazione dell'Applicazione](#2-installazione-dellapplicazione)
   - [Metodo 1: Script Rapido (Consigliato)](#metodo-1-script-rapido-consigliato)
   - [Metodo 2: Pacchetto Arch Linux / EndeavourOS](#metodo-2-pacchetto-arch-linux--endeavouros)
   - [Metodo 3: CMake Diretto](#metodo-3-cmake-diretto)
3. [Guida all'Uso: Il Tuo Primo Video](#3-guida-alluso-il-tuo-primo-video)
   - [Avvio e Schermata Iniziale](#avvio-e-schermata-iniziale)
   - [Importazione dei File Multimediali](#importazione-dei-file-multimediali)
   - [Montaggio sulla Timeline](#montaggio-sulla-timeline)
   - [Transizioni ed Effetti](#transizioni-ed-effetti)
   - [Testo, Titoli e Sottotitoli Automatici](#testo-titoli-e-sottotitoli-automatici)
   - [Regolazione Colore e Audio](#regolazione-colore-e-audio)
   - [Esportazione del Progetto](#esportazione-del-progetto)
4. [Scorciatoie da Tastiera Principali](#4-scorciatoie-da-tastiera-principali)
5. [Disinstallazione Completa](#5-disinstallazione-completa)
   - [Disinstallazione con lo Script](#disinstallazione-con-lo-script)
   - [Disinstallazione con Pacman (Arch Linux)](#disinstallazione-con-pacman-arch-linux)
   - [Disinstallazione con CMake](#disinstallazione-con-cmake)
   - [Pulizia Completa di Dati e Cache (--purge)](#pulizia-completa-di-dati-e-cache---purge)

---

## 1. Requisiti di Sistema

Velacut è ottimizzato per le distribuzioni Linux moderne (Ubuntu, Fedora, Arch Linux, EndeavourOS, Debian, Manjaro, ecc.):
- **Sistema Operativo**: Linux con server grafico X11 o Wayland.
- **Librerie di sistema**: Qt 6 (≥ 6.7), MLT 7 (≥ 7.20), FFmpeg (≥ 6.x), SDL2.
- **Scheda Grafica**: Supporto hardware Vulkan o OpenGL 3.3+. È presente anche la modalità di emergenza software (`--safe-mode`).

---

## 2. Installazione dell'Applicazione

Puoi installare Velacut con diversi metodi a seconda delle tue preferenze.

### Metodo 1: Script Rapido (Consigliato)

Il repository include uno script di installazione automatizzato che compila in modalità Release, installa i binari, l'icona di sistema, il file `.desktop` per il menu applicazioni e le associazioni file per i progetti `.vproj`.

```bash
# Clona il repository se non lo hai già fatto
git clone https://github.com/owaismounir206-art/Velacut.git
cd Velacut

# Opzione A: Installazione per il solo utente corrente (Nessun sudo necessario!)
./install.sh
# I file verranno installati in ~/.local/bin e ~/.local/share

# Opzione B: Installazione di sistema (Disponibile per tutti gli utenti)
sudo ./install.sh
# I file verranno installati in /usr/local
```

Al termine dell'installazione, troverai **Velacut** direttamente nel menu delle applicazioni del tuo desktop (GNOME, KDE Plasma, XFCE, Cinnamon, ecc.) con la sua icona ufficiale.

---

### Metodo 2: Pacchetto Arch Linux / EndeavourOS

Se sei su Arch Linux o derivate (EndeavourOS, Manjaro):

```bash
cd Velacut
# Compila e installa il pacchetto gestito da pacman
makepkg -si
```

---

### Metodo 3: CMake Diretto

```bash
cd Velacut
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DVELACUT_DEV_SANDBOX=OFF

cmake --build build --parallel
sudo cmake --install build
```

---

## 3. Guida all'Uso: Il Tuo Primo Video

### Avvio e Schermata Iniziale
- Clicca sull'icona di **Velacut** nel menu delle applicazioni oppure digita nel terminale:
  ```bash
  velacut
  ```
- All'avvio ti verrà mostrata la schermata principale (in stile CapCut):
  - **Nuovo Progetto**: Crea una nuova timeline vuota.
  - **Bozze Recenti**: Accedi rapidamente ai progetti su cui stavi lavorando (Velacut salva continuamente ogni modifica in automatico, senza bisogno di premere Ctrl+S!).
  - **Template Rapidi**: Modelli predefiniti per formati verticali (TikTok/Reels/Shorts 9:16) o orizzontali (YouTube 16:9).

### Importazione dei File Multimediali
1. Trascina direttamente i file video, audio o immagini dal tuo gestore file (Nautilus, Dolphin, Thunar) dentro l'area **Media** o direttamente sulla **Timeline**.
2. In alternativa, premi il pulsante **"+" (Importa)** nel pannello multimediale in alto a sinistra.

### Montaggio sulla Timeline
- **Muovere il Playhead**: Clicca o trascina sulla riga del tempo in cima alla timeline.
- **Tagliare / Dividere una Clip**:
  - Posiziona il playhead dove desideri tagliare e premi **`S`** (Split istantaneo).
  - Premi **`Q`** per eseguire un *ripple-trim a sinistra* (cancella la parte precedente al cursore e ricompatta la timeline).
  - Premi **`W`** per eseguire un *ripple-trim a destra* (cancella la parte successiva al cursore e ricompatta la timeline).
- **Trimming manuale**: Trascina i bordi sinistro o destro di qualsiasi clip.
- **Spostare le clip**: Trascina le clip lungo la traccia principale magnetica o spostale sulle tracce sovrapposte per creare picture-in-picture o overlay.

### Transizioni ed Effetti
1. **Transizioni**:
   - Clicca sulla barra o sull'icona di taglio tra due clip adiacenti.
   - Sfoglia la libreria con oltre 100 transizioni (dissolvenze, zoom, whip pan, glitch, split).
   - Fai doppio clic per applicarla e regola la durata trascinando la maniglia della transizione.
2. **Effetti e Filtri**:
   - Seleziona la clip desiderata.
   - Apri la scheda **Effetti** o **Filtri** nella barra laterale.
   - Clicca sull'effetto per visualizzare l'anteprima in tempo reale; regolane i parametri dal pannello proprietà a destra.

### Testo, Titoli e Sottotitoli Automatici
- **Aggiungere Testo**:
  - Seleziona la scheda **Testo** nel pannello degli strumenti.
  - Scegli tra stili statici o animati e trascinalo sulla timeline.
  - Modifica il font, colore, bordo, ombra e animazione nel pannello proprietà.
- **Sottotitoli Automatici**:
  - Nel menu Sottotitoli, clicca su **Genera Sottotitoli Automatici**.
  - Velacut utilizzerà `whisper.cpp` locale per trascrivere la traccia audio parola per parola, applicando stili animati evidenziati sincronizzati con il parlato.

### Regolazione Colore e Audio
- **Colore**: Seleziona una clip e vai alla sezione **Regolazione**:
  - Applica Look-Up Tables (LUT `.cube`).
  - Modifica luminosità, contrasto, saturazione, temperatura e ruote colore (ombre, mezzitoni, alte luci).
- **Audio**:
  - Regola il volume e il bilanciamento.
  - Applica la normalizzazione loudness LUFS o il filtro di riduzione del rumore.
  - Utilizza il **Ducking automatico** per abbassare il volume della musica di sottofondo quando è presente la voce narrante.

### Esportazione del Progetto
1. Quando il tuo montaggio è pronto, premi **`Ctrl+E`** oppure clicca sul pulsante **Esporta** in alto a destra.
2. Scegli le impostazioni desiderate:
   - **Risoluzione**: 1080p, 4K, 720p, verticale (1080x1920) o personalizzata.
   - **Codec**: H.264 (MP4), H.265 (HEVC), AV1, Apple ProRes (MOV), WebM o GIF animata.
   - **Accelerazione Hardware**: Intel QuickSync (VAAPI), AMD (VAAPI/Mesa), NVIDIA (NVENC) o codifica CPU ad alta fedeltà.
3. Clicca su **Esporta Video**. L'esportazione avviene in background con un processo dedicato e sicuro (`velacut-render`).

---

## 4. Scorciatoie da Tastiera Principali

| Tasto | Azione |
| :--- | :--- |
| **Spazio** | Riproduci / Metti in pausa |
| **S** | Taglia (Split) la clip al punto del cursore |
| **Q** | Taglia ed elimina tutto a sinistra del cursore (Ripple left) |
| **W** | Taglia ed elimina tutto a destra del cursore (Ripple right) |
| **Canc / Backspace** | Elimina la clip selezionata |
| **Ctrl + Z** | Annulla (Undo) |
| **Ctrl + Shift + Z** / **Ctrl + Y** | Ripristina (Redo) |
| **J / K / L** | Riavvolgi / Pausa / Avanza veloce |
| **F11** | Anteprima video a schermo intero |
| **Ctrl + E** | Apri la finestra di Esportazione |

Tutte le scorciatoie dettagliate sono consultabili nel file [docs/SHORTCUTS.md](SHORTCUTS.md).

---

## 5. Disinstallazione Completa

Rimuovere Velacut dal sistema è altrettanto semplice e pulito.

### Disinstallazione con lo Script

Esegui lo script `uninstall.sh` fornito:

```bash
cd Velacut

# Se installato localmente per l'utente:
./uninstall.sh

# Se installato a livello di sistema (/usr/local o /usr):
sudo ./uninstall.sh
```

Lo script provvederà a:
- Rimuovere i binari eseguibili (`velacut`, `velacut-render`, `velacut-gpuprobe`).
- Rimuovere il lanciatore desktop `velacut.desktop` dal menu di sistema.
- Rimuovere l'icona ufficiale da `icons/hicolor`.
- Rimuovere le associazioni MIME dei file `.vproj`.
- Aggiornare i database di sistema delle icone e delle applicazioni.

---

### Disinstallazione con Pacman (Arch Linux)

Se avevi installato il pacchetto tramite `makepkg`:

```bash
sudo pacman -R velacut
```

---

### Disinstallazione con CMake

Se hai compilato e installato tramite la cartella `build`:

```bash
cd Velacut/build
sudo ninja uninstall
# oppure: sudo make uninstall
```

---

### Pulizia Completa di Dati e Cache (--purge)

Velacut salva le impostazioni, le bozze e i file di cache temporanei nelle directory standard XDG dell'utente:
- Bozze e progetti: `~/.local/share/velacut/`
- Impostazioni e preferenze: `~/.config/velacut/`
- Cache temporanee (anteprime proxy, forme d'onda): `~/.cache/velacut/`
- Log applicativi: `~/.local/state/velacut/`

Per rimuovere anche tutti questi file e lasciare il sistema totalmente pulito, esegui:

```bash
./uninstall.sh --purge
```
