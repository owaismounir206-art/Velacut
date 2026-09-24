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
