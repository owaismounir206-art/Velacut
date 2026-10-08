# velacut — Design system (Material 3 / Material You)

Riferimento per chi scrive interfaccia. Verifica visiva: `./build/velacut --component-gallery`
(opzioni per screenshot automatici: `--theme light|dark|auto`, `--contrast standard|medium|high`,
`--window-size 1280x3900`, `--screenshot file.png`).

## 1. Regole
- **Nessun colore, dimensione, raggio o durata scritti a mano nei QML**: tutto viene da `Theme` (`import Velacut.Theme`).
- I controlli con nome Qt Quick Controls (`Button`, `Slider`, `Switch`, `CheckBox`, `RadioButton`, `TextField`,
  `ProgressBar`, `BusyIndicator`, `ToolTip`, `Menu`, `MenuItem`, `MenuSeparator`, `Dialog`, `Label`) si usano con
  `import QtQuick.Controls`: lo stile attivo è `Velacut.Style`. **Non importare `Velacut.Style` direttamente**: il suo
  `qmldir` importa lo stile Material di Qt (fallback) e ne riesporterebbe i tipi, rendendo ambiguo `Button`.
- I componenti M3 senza equivalente Qt si importano da `Velacut.Components`.
- Per i colori dei controlli Qt non ridefiniti (ScrollBar, ComboBox, ScrollView…) la finestra imposta gli attached
  `Material.*` dal `Theme` (vedi `Main.qml`), importando Material con un alias (`import QtQuick.Controls.Material as M`).
- Testi visibili sempre con `qsTr()` (sorgenti in inglese, traduzione italiana in `i18n/velacut_it.ts`).
- L'anteprima video e le miniature non vengono mai tinte dal tema.

## 2. Colori
Generati da **material-color-utilities** (spazio HCT) a partire da un **colore seme**:
- varianti: TonalSpot (predefinita), Vibrant, Expressive, Neutral, Fidelity, Content, Monochrome;
- modalità: chiaro, scuro (predefinita), automatico (segue `color-scheme` del portale);
- contrasto: sistema (predefinito: alto se il portale lo chiede), standard, medio, alto.

**Sorgenti del seme** (scelta dell'utente; se non disponibile si passa alla successiva):
1. accento di sistema: portale `org.freedesktop.appearance accent-color` (aggiornato dal vivo), poi GNOME
   `gsettings … accent-color`, poi KDE `kdeglobals` `AccentColor`;
2. sfondo del desktop (GNOME, Plasma, hyprpaper, swww) → quantizzatore Celebi + score di MCU;
3. copertina del progetto; 4. colore manuale; 5. predefinito `#00b4c4` (il ciano degli editor video; era `#4f5bd5`).

**Superfici neutre** (`Theme.neutralSurfaces`, attive di default, interruttore in Preferenze → Aspetto): le palette
neutral e neutral variant dello schema prendono la tinta del seme con croma 2 e 3 invece di quello della variante, così
pannelli, contorni e testi secondari sono grigi come negli editor video (CapCut, DaVinci) e l'occhio resta neutro nel
giudicare i colori del video; primary, secondary, tertiary ed error restano dinamici. Stessi toni, quindi stesso
contrasto (verificato da `tst_theme` per ogni seme, variante, modalità e livello di contrasto, con e senza).

**49 ruoli**, accessibili come `Theme.color.<ruolo>`:
primary, onPrimary, primaryContainer, onPrimaryContainer, inversePrimary, secondary, onSecondary, secondaryContainer,
onSecondaryContainer, tertiary, onTertiary, tertiaryContainer, onTertiaryContainer, error, onError, errorContainer,
onErrorContainer, background, onBackground, surface, onSurface, surfaceVariant, onSurfaceVariant, surfaceDim,
surfaceBright, surfaceContainerLowest, surfaceContainerLow, surfaceContainer, surfaceContainerHigh,
surfaceContainerHighest, inverseSurface, inverseOnSurface, outline, outlineVariant, shadow, scrim, surfaceTint,
primaryFixed, primaryFixedDim, onPrimaryFixed, onPrimaryFixedVariant, secondaryFixed, secondaryFixedDim,
onSecondaryFixed, onSecondaryFixedVariant, tertiaryFixed, tertiaryFixedDim, onTertiaryFixed, onTertiaryFixedVariant.

Il cambio di schema è animato (un'unica animazione C++, `medium4` 400 ms, easing *emphasized*); immediato con
"riduci animazioni". **Verificato dai test** (`tst_theme`): per 5 semi × 7 varianti × chiaro/scuro, ogni coppia
testo/contenitore ha contrasto ≥ 4.5:1 (standard) e ≥ 7:1 (alto).

Funzioni di supporto: `Theme.surfaceAt(livello)` (superficie tonale per l'elevazione 0–5),
`Theme.alpha(colore, opacità)`, `Theme.readableOn(colore)`, `Theme.colorRoleNames()`.

## 3. Tipografia
Font incluso: **Inter Variable** (OFL). Dimensioni in pixel logici indipendenti dal DPI.
Uso: `Label { role: "titleMedium" }` (controlli) o `TypeText { role: … }` (dentro i componenti);
oppure `Theme.type.<stile>` (QFont) e `Theme.type.<stile>LineHeight`.

| Stile | Dimensione / interlinea | Spaziatura | Peso |
|---|---|---|---|
| displayLarge / Medium / Small | 57/64 · 45/52 · 36/44 | −0.5 · −0.25 · −0.2 | 400 |
| headlineLarge / Medium / Small | 32/40 · 28/36 · 24/32 | −0.5 · −0.25 · −0.2 | 400 |
| titleLarge / Medium / Small | 22/28 · 16/24 · 14/20 | −0.15 · 0.15 · 0.1 | 400 · 500 · 500 |
| bodyLarge / Medium / Small | 16/24 · 14/20 · 12/16 | 0.5 · 0.25 · 0.4 | 400 |
| labelLarge / Medium / Small | 14/20 · 12/16 · 11/16 | 0.1 · 0.5 · 0.5 | 500 |

Tracking ottico "SF" (§4bis): display e headline si compattano leggermente (più grande il testo,
più stretto), i label restano aperti. Dimensioni, interlinee e pesi restano la baseline M3.

## 4. Altri token
| Gruppo | Token |
|---|---|
| `Theme.shape` | none 0, extraSmall 4, small 8, medium 12, large 16, extraLarge 28, full (pillola) |
| `Theme.state` | hover 0.08, focus 0.10, pressed 0.10, dragged 0.16, disabledContent 0.38, disabledContainer 0.12 |
| `Theme.space` | xxs 2, xs 4, sm 8, md 12, lg 16, xl 24, xxl 32, xxxl 48; `density` (0 / −1 compatta), `control(base)`, `minimumTarget` (48, 40 compatto) |
| `Theme.elevation` | level0–5: 0, 1, 3, 6, 8, 12 dp (resa soprattutto con i colori tonali; ombra leggera `Shadow` solo dove serve, disattivata in rendering software) |
| `Theme.motion` | short1–4 50–200, medium1–4 250–400, long1–4 450–600 ms (0 con "riduci animazioni"); `essentialMedium` 300 e `essentialLong` 500 restano (indicatori di progresso); curve `emphasized`, `emphasizedDecelerate`, `emphasizedAccelerate`, `standard`, `standardDecelerate`, `standardAccelerate` per `easing.type: Easing.BezierSpline`; micro-interazioni §4bis: `pressScaleSmall` 0.92, `pressScale` 0.97, `hoverLift` −1.5 dp, molle `springFast` 4/0.4, `springSoft` 3/0.3, `springMass` 0.9 |

### 4bis. Apple slickness (polish di stato e moto sopra M3)
I colori restano i 49 ruoli M3; sono cambiate solo le transizioni di stato, le ombre e la
tipografia. Tutto senza shader (il backend software disegna tutto, le ombre come prima non ci
vanno) e tutto spento con "riduci animazioni".
- **Molle (feel Apple)**: IconButton si comprime a `pressScaleSmall` e torna con molla
  `SpringAnimation` (`springFast`); Button e Fab a `pressScale`; Fab, Card `elevated`/`interactive`
  e MediaTile si sollevano di `hoverLift` in hover (solo transform: mai width/height, niente scale
  su card e tile — la griglia non si muove sotto il puntatore). Button filled si scurisce del 4%
  alla pressione (`Qt.darker` del colore del tema, non un colore scritto a mano).
- **Ombre morbide**: `Shadow` è uno stack di 8 rettangoli che si allarga con alpha calante
  (bordo sfumato) e "pende" verso il basso, più in alto il livello; niente shader, nascosta in
  rendering software come prima.
- **Vibrancy "lite"** (frosted senza blur, dichiarato: il backdrop-blur vero è impossibile senza
  shader): `Dialog`, `Menu` e `TopAppBar` (solo quando il contenuto scorre sotto) al 92% di
  superficie su scrim con hairline `outlineVariant` come bordo del vetro. Sempre >90% opachi:
  il contrasto testo/superficie resta quello della superficie piena. I pannelli dell'editor
  restano opachi (l'occhio resta neutro sul video).
- **Scrollbar macOS**: `style/ScrollBar` — pillola 6 dp (8 in hover/drag), traccia invisibile,
  fade-out dopo `Theme.editor.autoHideDelay` di inattività; resta fuori mentre premuta, in hover
  o in uso (`active`). Si applica a ogni `ScrollBar {}` via stile.
- **Focus ring da tastiera**: `components/FocusFrame` — bordo 2 dp `primary` con gap 2 dp,
  solo su `visualFocus` (o `focusReason` Tab per i tipi non-`Control`, come TextField); il
  mouse-focus resta solo il layer M3, come su macOS. In Button, IconButton, TextField, Chip.

### 4.1 Token dell'editor (`Theme.editor`)
Dimensioni delle superfici proprie dell'editor, così che nessun QML scriva numeri a mano:
pannelli (`libraryWidth`, `mediaTileWidth`/`Height`, `previewMinimumHeight`, `timelineMinimumHeight`,
`timelineDefaultHeight`, `splitterSize`, `toolbarHeight`, `dialogWidth`), schermata iniziale (`draftCardWidth`,
`draftThumbnailHeight`, `heroHeight`, `contentMaxWidth`), timeline (`rulerHeight`, `trackHeaderWidth`,
`mainTrackHeight`, `overlayTrackHeight`, `audioTrackHeight`, `trackGap`, `newTrackZone`, `trimHandleWidth`,
`playheadWidth`, `playheadKnob`, `selectionBorder`, `hairline`, `snapThreshold`, `rulerLabelSpacing`,
`dragStartDistance`), zoom in pixel per fotogramma (`zoomDefault`, `zoomMinimum`, `zoomMaximum`, `zoomStep`) e
`tailRatio` (spazio dopo l'ultima clip, in frazione della vista).

## 5. Icone
**Material Symbols Rounded** (Apache-2.0), font variabile WOFF2. `Icon { name: "play_arrow"; filled: true }`.
Il nome viene risolto tramite il file `.codepoints` (`Theme.icon(nome)`): un nome inesistente mostra l'icona `help`
e scrive un avviso nel log, invece di disegnare il nome come testo. Asse FILL per lo stato attivo, `weight` per il peso.

## 6. Componenti
| Modulo | Componente | Note |
|---|---|---|
| Style | `Button` | `variant`: filled, tonal, outlined, text, elevated; `iconName` |
| Style | `Slider` | indicatore del valore durante il trascinamento (`valueText`) |
| UI | `SliderRow` | proprietà su una riga come negli editor video: nome · cursore · riquadro del valore scrivibile (Invio applica, Esc annulla, doppio clic sul nome = valore neutro); sotto `inlinePropertyWidth` il cursore va sotto il nome. Base di `PropertySlider` ed `EffectSlider` |
| Style | `Switch`, `CheckBox` (anche tristate), `RadioButton` | |
| Style | `TextField` | `variant` filled/outlined, `label` flottante, `supportingText`, `error`, `leadingIconName`, placeholder |
| Style | `ProgressBar` (lineare), `BusyIndicator` (circolare, `progress` per il determinato) | movimento essenziale |
| Style | `ToolTip`, `Menu`, `MenuItem` (`iconName`, `shortcutText`), `MenuSeparator`, `Dialog` (`iconName`) | |
| Style | `Label` | `role` tipografico |
| Components | `IconButton` | `variant` standard/filled/tonal/outlined, toggle, `label` = nome accessibile + tooltip, `shortcutText` |
| Components | `Fab` | con `text` diventa extended FAB; `color` primary/secondary/tertiary/surface |
| Components | `SegmentedButton` | scelte esclusive (formato 16:9 / 9:16 / 1:1…) |
| Components | `Chip` | assist, filter, input |
| Components | `Card` | filled, elevated, outlined; `interactive` |
| Components | `NavigationRail`, `NavigationRailItem` | indicatore a pillola, badge |
| Components | `TopAppBar` | leading/trailing, tonale quando il contenuto scorre sotto |
| Components | `Snackbar` | `show(testo, azione, callback)`: "Clip eliminata — Annulla" |
| Components | `SearchBar`, `Badge`, `Divider`, `Icon`, `TypeText` | |
| Components | `StateLayer`, `Shadow` | mattoni interni: livelli di stato con ripple senza shader, ombra senza shader |

**Ripple**: un rettangolo con lo stesso raggio del controllo cresce dal punto di pressione fino a coincidere con la
forma, quindi niente shader (funziona anche col backend software) e niente angoli fuori forma.

Elementi C++ dell'editor (dipinti su CPU, quindi identici su ogni backend): `MediaThumbnail` (fotogrammi di un
media: una tessera con skimming nel media pool, oppure tasselli lungo una clip) e `WaveformView` (picchi min/max
dell'audio di una clip). Schermate e pannelli dell'editor: `HomeScreen`, `DraftCard`, `EditorScreen`, `EditorTopBar`,
`MediaPanel`, `MediaTile`, `AudioPanel`, `PreviewPanel`, `ContextToolbar`, `TimelineView`, `TimelineClip`,
`ExportDialog` (modulo `Velacut.UI`).

Da aggiungere nelle fasi successive: side sheet / bottom sheet, rich tooltip, time/date picker se servono, liste con
elementi M3. Nota: i componenti della Fase 0 contengono ancora alcune misure della specifica M3 scritte nel file
(es. altezza 48 della snackbar, padding 8 dei pulsanti icona); vanno portate nei token nella rifinitura (Fase 8).

## 7. Accessibilità e desktop
- `Accessible.role`/`name` su ogni componente; focus visibile (state layer di focus) e navigazione da tastiera
  (frecce su navigation rail e segmented button).
- Target cliccabili: `Theme.space.minimumTarget`.
- Densità compatta disponibile (`Theme.density`); layout responsive con le classi di finestra M3 dalla Fase 1.
- Rendering software: niente ombre, stesse forme e colori.
