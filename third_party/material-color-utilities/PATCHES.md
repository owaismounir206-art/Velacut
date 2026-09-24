# material-color-utilities — copia vendored per vedit

- Origine: https://github.com/material-foundation/material-color-utilities (cartella `cpp/`)
- Commit: 5b3618b16fdc3825e21d5679bafd144662088ea1 (2026-08-21)
- Licenza: Apache-2.0 (`LICENSE`)
- File esclusi: i test upstream (`*_test.cc`, dipendono da GoogleTest).

## Patch applicate (solo per eliminare la dipendenza da Abseil)
1. `cpp/utils/utils.cc`: `HexFromArgb` usa `std::snprintf("%x")` invece di `absl::StrCat(absl::Hex())`
   (stesso risultato: esadecimale minuscolo senza zeri iniziali).
2. `cpp/quantize/wsmeans.cc`: `absl::flat_hash_map<Argb, int>` sostituita da `std::unordered_map<Argb, int>`
   (solo prestazioni, stesso risultato).
3. `cpp/temperature/temperature_cache.h`: aggiunto `#include <optional>` mancante (necessario con GCC 16,
   dove l'header non è più incluso indirettamente).
