# vedit — istruzioni per Claude

## All'inizio di OGNI sessione
1. Rileggi per intero `SPEC-editor-video.md`: è la specifica vincolante del progetto.
2. Rileggi `docs/PROGRESS.md` (stato, prossimi passi, bug noti, decisioni) e, se tocchi l'architettura,
   `docs/ARCHITECTURE.md` e `docs/FILE_FORMAT.md`.
3. Riparti dai "Prossimi passi" di `docs/PROGRESS.md`; non ricominciare lavoro già fatto.

## Regole di sicurezza (valide per tutto il progetto)
- Lavora **solo dentro questa cartella**. Non modificare file fuori da qui (niente `~/.config`, `/etc`, `/usr`, ecc.).
  Appunti e stato di lavoro vanno nei file del progetto (`docs/PROGRESS.md`), non altrove.
- **Mai `sudo`** e mai installare pacchetti di sistema (né con pacman né con yay/paru/makepkg -i):
  se serve qualcosa, scrivi all'utente il comando esatto e lo esegue lui.
- **Niente comandi distruttivi** (`rm -rf`, `git reset --hard`, `git push --force`, `git clean`, ecc.)
  senza chiederlo esplicitamente all'utente.
- Niente `git push` in assoluto, salvo richiesta esplicita.
- Niente download di script da eseguire (`curl … | sh` e simili).

## Modo di lavorare (sintesi della sezione 9 della specifica)
1. **Prima del codice**: `docs/ARCHITECTURE.md` e `docs/FILE_FORMAT.md` devono essere approvati dall'utente.
2. **Piccoli incrementi**: dopo ognuno compila, esegui i test, avvia l'app se la modifica tocca la UI,
   correggi finché tutto funziona, poi fai commit (Conventional Commits, messaggi piccoli e descrittivi).
3. **Il progetto compila sempre** e i test passano sempre alla fine di ogni incremento.
4. **Niente finzioni**: niente stub vuoti, niente `TODO` spacciati per funzioni, niente pulsanti che non fanno nulla.
   Ciò che non è finito va scritto in `docs/PROGRESS.md`.
5. **Verifica invece di indovinare**: se un'API di MLT, Qt o FFmpeg non si comporta come previsto,
   scrivi un piccolo programma di prova, osserva il comportamento reale e poi procedi.
6. **Aggiorna `docs/PROGRESS.md` alla fine di ogni sessione**: fatto, in corso, prossimi passi, bug noti,
   decisioni e motivazioni. Deve bastare per ripartire senza altra memoria.
7. **Fine fase**: aggiorna README e SHORTCUTS, riepilogo onesto (cosa funziona, cosa no, limiti),
   verifica il criterio di completamento della fase (sezione 8) e **aspetta il via dell'utente**.
   Dalla fase 1 in poi ripeti i test di semplicità e riporta il conteggio delle azioni in `docs/USABILITY.md`.
8. **Se qualcosa è irrealizzabile o troppo costoso** in locale, dillo con una proposta alternativa: mai omettere in silenzio.
9. **Domande solo quando cambiano davvero il risultato**; per il resto scegli l'opzione più ragionevole
   e annotala in `docs/ARCHITECTURE.md`.
10. **Pensa come CapCut**: meno passaggi possibile, niente dialoghi se basta un controllo, default giusti per l'80%
    delle persone. Se l'interfaccia richiede istruzioni per essere usata, semplificala.

## Promemoria tecnici vincolanti
- C++20, Qt 6 (Qt Quick/QML), MLT 7, FFmpeg, CMake + Ninja, QtTest + CTest.
- Il `core` è la fonte di verità e non dipende da MLT né da QML; ogni modifica passa da un `QUndoCommand`.
- Tempi con `RationalTime`, mai `double` di secondi.
- Ogni effetto/transizione ha un percorso CPU di riferimento; la GPU è solo un acceleratore opzionale (sezione 1bis).
- Nessun colore, dimensione, raggio o durata scritti a mano nei QML: tutto dal singleton `Theme`.
- Nessuna stringa visibile hardcoded: sempre `qsTr()`/`tr()` (italiano e inglese).
- Warning = errori (`-Wall -Wextra -Wpedantic -Werror`), ASan/UBSan in Debug.

## Comandi
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
./build/vedit
# modalità software (verifica sezione 1bis)
QT_QUICK_BACKEND=software LIBGL_ALWAYS_SOFTWARE=1 ./build/vedit
```
