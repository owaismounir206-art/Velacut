# vedit — Design system (Material 3 / Material You)

Riferimento per chi scrive interfaccia. Verifica visiva: `./build/vedit --component-gallery`
(opzioni per screenshot automatici: `--theme light|dark|auto`, `--contrast standard|medium|high`,
`--window-size 1280x3900`, `--screenshot file.png`).

## 1. Regole
- **Nessun colore, dimensione, raggio o durata scritti a mano nei QML**: tutto viene da `Theme` (`import Vedit.Theme`).
- I controlli con nome Qt Quick Controls (`Button`, `Slider`, `Switch`, `CheckBox`, `RadioButton`, `TextField`,
  `ProgressBar`, `BusyIndicator`, `ToolTip`, `Menu`, `MenuItem`, `MenuSeparator`, `Dialog`, `Label`) si usano con
  `import QtQuick.Controls`: lo stile attivo è `Vedit.Style`. **Non importare `Vedit.Style` direttamente**: il suo
  `qmldir` importa lo stile Material di Qt (fallback) e ne riesporterebbe i tipi, rendendo ambiguo `Button`.
- I componenti M3 senza equivalente Qt si importano da `Vedit.Components`.
- Per i colori dei controlli Qt non ridefiniti (ScrollBar, ComboBox, ScrollView…) la finestra imposta gli attached
  `Material.*` dal `Theme` (vedi `Main.qml`), importando Material con un alias (`import QtQuick.Controls.Material as M`).
- Testi visibili sempre con `qsTr()` (sorgenti in inglese, traduzione italiana in `i18n/vedit_it.ts`).
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
3. copertina del progetto; 4. colore manuale; 5. predefinito `#4f5bd5`.

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
| displayLarge / Medium / Small | 57/64 · 45/52 · 36/44 | −0.25 · 0 · 0 | 400 |
| headlineLarge / Medium / Small | 32/40 · 28/36 · 24/32 | 0 | 400 |
| titleLarge / Medium / Small | 22/28 · 16/24 · 14/20 | 0 · 0.15 · 0.1 | 400 · 500 · 500 |
| bodyLarge / Medium / Small | 16/24 · 14/20 · 12/16 | 0.5 · 0.25 · 0.4 | 400 |
| labelLarge / Medium / Small | 14/20 · 12/16 · 11/16 | 0.1 · 0.5 · 0.5 | 500 |

## 4. Altri token
| Gruppo | Token |
|---|---|
| `Theme.shape` | none 0, extraSmall 4, small 8, medium 12, large 16, extraLarge 28, full (pillola) |
| `Theme.state` | hover 0.08, focus 0.10, pressed 0.10, dragged 0.16, disabledContent 0.38, disabledContainer 0.12 |
| `Theme.space` | xxs 2, xs 4, sm 8, md 12, lg 16, xl 24, xxl 32, xxxl 48; `density` (0 / −1 compatta), `control(base)`, `minimumTarget` (48, 40 compatto) |
| `Theme.elevation` | level0–5: 0, 1, 3, 6, 8, 12 dp (resa soprattutto con i colori tonali; ombra leggera `Shadow` solo dove serve, disattivata in rendering software) |
| `Theme.motion` | short1–4 50–200, medium1–4 250–400, long1–4 450–600 ms (0 con "riduci animazioni"); `essentialMedium` 300 e `essentialLong` 500 restano (indicatori di progresso); curve `emphasized`, `emphasizedDecelerate`, `emphasizedAccelerate`, `standard`, `standardDecelerate`, `standardAccelerate` per `easing.type: Easing.BezierSpline` |

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
`ExportDialog` (modulo `Vedit.UI`).

Da aggiungere nelle fasi successive: side sheet / bottom sheet, rich tooltip, time/date picker se servono, liste con
elementi M3. Nota: i componenti della Fase 0 contengono ancora alcune misure della specifica M3 scritte nel file
(es. altezza 48 della snackbar, padding 8 dei pulsanti icona); vanno portate nei token nella rifinitura (Fase 8).

## 7. Accessibilità e desktop
- `Accessible.role`/`name` su ogni componente; focus visibile (state layer di focus) e navigazione da tastiera
  (frecce su navigation rail e segmented button).
- Target cliccabili: `Theme.space.minimumTarget`.
- Densità compatta disponibile (`Theme.density`); layout responsive con le classi di finestra M3 dalla Fase 1.
- Rendering software: niente ombre, stesse forme e colori.
