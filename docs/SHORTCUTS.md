# vedit — Scorciatoie da Tastiera

## Globali

| Scorciatoia | Azione |
|-------------|--------|
| `Ctrl+N` | Nuovo progetto |
| `Ctrl+O` | Apri progetto |
| `Ctrl+S` | Salva (nota: il salvataggio è automatico ogni ~2s) |
| `Ctrl+Z` | Annulla |
| `Ctrl+Shift+Z` o `Ctrl+Y` | Ripeti |
| `Ctrl+K` | Ricerca universale (comandi, effetti, transizioni, musica) |
| `Ctrl+Q` | Esci |

## Player e Timeline

| Scorciatoia | Azione |
|-------------|--------|
| `Space` | Play/Pausa |
| `K` | Play/Pausa (stile JKL) |
| `J` | Riproduci indietro |
| `L` | Riproduci avanti |
| `Ctrl+Space` | Play/Pausa con audio nella timeline (skimming) |
| `←` | Frame precedente |
| `→` | Frame successivo |
| `Shift+←` | 10 frame indietro |
| `Shift+→` | 10 frame avanti |
| `↑` | Taglio precedente |
| `↓` | Taglio successivo |
| `Home` | Vai all'inizio |
| `End` | Vai alla fine |
| `I` | Imposta punto di entrata (In) |
| `O` | Imposta punto di uscita (Out) |
| `Alt+X` | Cancella punti In/Out |
| `M` | Aggiungi marker |

## Editing

| Scorciatoia | Azione |
|-------------|--------|
| `S` | Dividi clip al playhead |
| `Del` o `Backspace` | Elimina selezione |
| `Shift+Del` o `Shift+Backspace` | Ripple delete (elimina e chiude lo spazio vuoto) |
| `Ctrl+D` | Duplica selezione |
| `Ctrl+C` | Copia attributi clip |
| `Ctrl+V` | Incolla attributi |
| `Ctrl+A` | Seleziona tutto |
| `Ctrl+Shift+A` | Deseleziona tutto |
| `[` | Trim inizio clip al playhead |
| `]` | Trim fine clip al playhead |
| `Q` | Ripple trim dall'inizio clip alla testina (taglia a sinistra del playhead) |
| `W` | Ripple trim dalla testina alla fine clip (taglia a destra del playhead) |
| `N` | Attiva/disattiva traccia principale magnetica |
| `\` | Attiva/disattiva snapping |

## Timeline

| Scorciatoia | Azione |
|-------------|--------|
| `+` o `=` | Zoom avanti timeline |
| `-` | Zoom indietro timeline |
| `Ctrl+0` | Zoom per vedere tutto |
| `Ctrl+1` | Zoom 1:1 (frame per pixel) |
| `Ctrl+↑` | Traccia precedente |
| `Ctrl+↓` | Traccia successiva |

## Canvas e Trasformazioni

| Scorciatoia | Azione |
|-------------|--------|
| `V` | Strumento selezione |
| `H` | Strumento mano (pan canvas) |
| `C` | Strumento zoom canvas |
| `T` | Strumento testo |
| `Ctrl+T` | Aggiungi nuovo testo |
| `R` | Ruota selezione |
| `Shift` (trascina) | Mantieni proporzioni/angoli |
| `Alt` (trascina maniglia) | Scala dal centro |

## Effetti e Pannelli

| Scorciatoia | Azione |
|-------------|--------|
| `Ctrl+1` | Pannello Media Pool |
| `Ctrl+2` | Pannello Librerie (Transizioni, Effetti, Testo) |
| `Ctrl+3` | Pannello Proprietà |
| `Ctrl+4` | Pannello Timeline |
| `F` | Pannello Effetti per clip selezionata |
| `G` | Pannello Colore |
| `A` | Pannello Audio |

## Export

| Scorciatoia | Azione |
|-------------|--------|
| `Ctrl+E` | Esporta video |
| `Ctrl+Shift+E` | Esporta frame corrente come immagine |

## Debug e Sviluppo

| Scorciatoia | Azione |
|-------------|--------|
| `F12` | Ispettore QML (solo build debug) |
| `Ctrl+Shift+D` | Attiva/disattiva overlay debug |

---

## Note

- **Salvataggio automatico**: vedit salva automaticamente ogni modifica entro ~2 secondi. Non serve premere Ctrl+S.
- **Snapping**: attivo di default sulla traccia principale. Disattiva con l'icona calamita o tieni `Shift` mentre trascini.
- **Skimming**: passa il mouse sulla timeline con Play in pausa per vedere i fotogrammi.
- **Tracce magnetiche**: le clip sulla traccia principale non lasciano spazi vuoti quando ne elimini una.

## Conflitti Risolti

Alcune scorciatoie comuni di video editor non sono disponibili perché:
- `Ctrl+I`: riservato a "Importa" in molti sistemi
- `Ctrl+M`: in conflitto con minimize su alcuni WM
- `Shift+Delete`: in conflitto con "taglia" del sistema

Se una scorciatoia non funziona, usa il menu contestuale (tasto destro) o la ricerca universale (`Ctrl+K`).
