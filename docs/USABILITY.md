# vedit — Test di semplicità (SPEC §0bis)

Alla fine di ogni fase si ripetono gli scenari applicabili, si **contano le azioni** (clic, tasti, trascinamenti;
la digitazione di un testo non conta) e si riporta il risultato. Se uno scenario supera il limite, si semplifica
l'interfaccia prima di proseguire.

| # | Scenario | Limite | Dalla fase |
|---|---|---|---|
| 1 | Dall'avvio al primo taglio di una clip (nuovo progetto, trascina clip, dividi) | 4 azioni | 1 |
| 2 | Aggiungere musica dalla libreria locale sotto il video | 2 azioni | 1 |
| 3 | Applicare un filtro a tutte le clip | 3 azioni | 2 |
| 4 | Aggiungere un testo, scriverlo e dargli un'animazione di ingresso | 5 azioni + digitazione | 2–3 |
| 5 | Generare i sottotitoli automatici con uno stile animato | 3 azioni | 6 |
| 6 | Mettere una transizione tra tutte le clip | 3 azioni | 2 |
| 7 | Trasformare un video 16:9 in 9:16 con il soggetto inquadrato | 2 azioni | 6 |
| 8 | Esportare con le impostazioni consigliate | 2 azioni | 1 |
| 9 | Video TikTok completo: 5 clip, musica sul ritmo, testo, sottotitoli, transizioni, filtro, export | 25 azioni | 7 |
| 10 | Nessuna funzione principale più in profondità di | 2 livelli di menu/pannello | 1 |

## Risultati

### Fase 0 — Fondamenta
Nessuno scenario è applicabile: la Fase 0 non ha ancora l'editor (solo riproduzione di un file e galleria dei
componenti). Osservazioni utili per la Fase 1: aprire e riprodurre un video richiede 2 azioni (Apri video → scelta del
file, oppure trascinamento del file sulla finestra = 1 azione) e la riproduzione parte con 1 azione (Spazio o Play).

### Fase 1 — MVP editor
Conteggi misurati da `tests/integration/tst_ui.cpp`, che guida l'interfaccia reale con mouse e tastiera (ogni clic,
tasto o trascinamento conta 1; il test fallisce se si supera il limite). Ripetibili con
`tools/run-test.sh build tst_ui` (con `VEDIT_UI_SHOTS=<cartella>` salva uno screenshot per passo).

| # | Scenario | Limite | Risultato | Percorso |
|---|---|---|---|---|
| 1 | Dall'avvio al primo taglio | 4 | ✅ **4** | "Nuovo progetto" → trascina il video dal file manager sulla timeline → clic nella timeline dove tagliare → "Dividi" (oppure `S`) |
| 2 | Musica dalla libreria locale sotto il video | 2 | ✅ **2** | rail "Audio" → "+" sul brano (va sotto il video, al playhead) |
| 8 | Esportare con le impostazioni consigliate | 2 | ✅ **2** | "Esporta" in alto → "Esporta" nella finestra (risoluzione, fps e qualità già uguali al progetto) |
| 10 | Nessuna funzione principale oltre 2 livelli | 2 | ✅ | vedi sotto |

Note:
- Scenario 1: il trascinamento dal file manager non si può simulare in un test headless (serve il drag and drop della
  piattaforma): il test chiama il gestore del rilascio e lo conta come l'unico trascinamento che è. Alternativa senza
  file manager: "Importa" + scelta del file + "+" = 3 azioni al posto di 1 (totale 6: oltre il limite, per questo il
  percorso consigliato e l'area vuota della timeline invitano a trascinare i file).
- Il trascinamento **dal media pool alla timeline** è invece verificato col mouse (`dragMediaAndContextMenu`).
- Scenario 10, profondità delle funzioni della Fase 1:
  livello 1 (sempre visibili): Importa, "+" dei media, Dividi/Elimina/Duplica nella barra contestuale, riproduzione,
  zoom, annulla/ripeti, nome del progetto, Esporta, trascinamenti e trim nella timeline;
  livello 2: formato del canvas (menu del pulsante formato), musica (rail "Audio" → "+"), opzioni di export (finestra),
  rinomina/duplica/elimina di una bozza (menu della card).

### Fase 2 — Editing essenziale
Misurati da `tests/integration/tst_ui.cpp` con l'interfaccia guidata reale.

| # | Scenario | Limite | Risultato | Percorso |
|---|---|---|---|---|
| 3 | Applicare un filtro a tutte le clip | 3 | ✅ **3** | rail "Filtri" → clic sul filtro per la clip corrente → "Applica a tutte" |
| 6 | Transizione tra tutte le clip | 3 | ✅ **3** | rail "Transizioni" → clic sulla transizione per il taglio più vicino → "Applica a tutte" |
| 1 | Dall'avvio al primo taglio | 4 | ✅ **4** | invariato (confermato in `tst_ui`) |
| 2 | Musica dalla libreria locale | 2 | ✅ **2** | invariato (confermato in `tst_ui`) |
| 8 | Esportare con le impostazioni consigliate | 2 | ✅ **2** | invariato (confermato in `tst_ui`) |
| 10 | Nessuna funzione principale oltre 2 livelli | 2 | ✅ | livello 1: formato sotto il player (1 clic), maniglie canvas, toolbar timeline; livello 2: schede proprietà, librerie (rail) |

### Fase 3 — Keyframe e composizione
Misurati da `tests/integration/tst_ui.cpp` con l'interfaccia guidata reale.

| # | Scenario | Limite | Risultato | Percorso |
|---|---|---|---|---|
| 4 | Un testo, scritto, con un'animazione di ingresso | 5 + digitazione | ✅ **4** | "Aggiungi testo" nella barra → clic nel campo "Testo" a destra e digitazione → rail "Animazioni" → clic sull'animazione di entrata |
| 3 | Filtro su tutte le clip | 3 | ✅ **3** | invariato |
| 6 | Transizione tra tutte le clip | 3 | ✅ **3** | invariato |
| 1 | Dall'avvio al primo taglio | 4 | ✅ **4** | invariato |
| 2 | Musica dalla libreria locale | 2 | ✅ **2** | invariato |
| 8 | Esportare con le impostazioni consigliate | 2 | ✅ **2** | invariato |
| 10 | Nessuna funzione principale oltre 2 livelli | 2 | ✅ | livello 1: diamante dei keyframe accanto al parametro, marker (M), azioni della barra, Ctrl+K; livello 2: schede Animazione e Scontorno del pannello proprietà, libreria Animazioni, menu della clip (clip composta) |

Criterio della Fase 3 verificato dall'interfaccia: `phaseThreeCriterionTitle` (titolo con due keyframe di opacità e
andamento "Morbido") e `phaseThreeCriterionGreenScreen` (colore scelto col contagocce sul player, maschera a cerchio
ridimensionata dal suo angolo, fotogramma renderizzato controllato).

