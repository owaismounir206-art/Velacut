#!/usr/bin/env bash
# ==============================================================================
# Velacut - Script di disinstallazione per Linux
# https://github.com/owaismounir206-art/Velacut
# ==============================================================================
set -euo pipefail

BOLD="\033[1m"
GREEN="\033[32m"
BLUE="\033[34m"
YELLOW="\033[33m"
RED="\033[31m"
RESET="\033[0m"

log_info() { echo -e "${BLUE}${BOLD}[INFO]${RESET} $1"; }
log_success() { echo -e "${GREEN}${BOLD}[SUCCESSO]${RESET} $1"; }
log_warn() { echo -e "${YELLOW}${BOLD}[AVVISO]${RESET} $1"; }
log_error() { echo -e "${RED}${BOLD}[ERRORE]${RESET} $1" >&2; }

# Risoluzione robusta della cartella radice anche tramite symlink
SOURCE="${BASH_SOURCE[0]}"
while [ -h "$SOURCE" ]; do
    DIR="$(cd -P "$(dirname "$SOURCE")" && pwd)"
    SOURCE="$(readlink "$SOURCE")"
    [[ $SOURCE != /* ]] && SOURCE="$DIR/$SOURCE"
done
SCRIPT_DIR="$(cd -P "$(dirname "$SOURCE")" && pwd)"
if [ "$(basename "$SCRIPT_DIR")" = "tools" ]; then
    REPO_DIR="$(cd -P "$SCRIPT_DIR/.." && pwd)"
else
    REPO_DIR="$SCRIPT_DIR"
fi
cd "$REPO_DIR"

PURGE=false
LEGACY_ONLY=false
for arg in "$@"; do
    if [ "$arg" = "--purge" ] || [ "$arg" = "-p" ]; then
        PURGE=true
    elif [ "$arg" = "--legacy" ] || [ "$arg" = "--old-version" ]; then
        LEGACY_ONLY=true
    fi
done

echo -e "${BOLD}"
if [ "$LEGACY_ONLY" = true ]; then
    echo "Pulizia e rimozione della VECCHIA versione (vedit)"
else
    echo "Disinstallazione di Velacut Video Editor"
fi
echo "=================================================="
echo -e "${RESET}"

# Possibili prefissi in cui l'applicazione potrebbe essere installata
PREFIXES=()
if [ "$(id -u)" -eq 0 ]; then
    PREFIXES=("/usr/local" "/usr")
else
    PREFIXES=("${HOME}/.local" "/usr/local" "/usr")
fi

REMOVED_ANY=false

for PREFIX in "${PREFIXES[@]}"; do
    # 1. Rimozione della vecchia versione 'vedit'
    OLD_DESKTOP="${PREFIX}/share/applications/vedit.desktop"
    if [ -f "$OLD_DESKTOP" ] || [ -f "${PREFIX}/share/icons/hicolor/scalable/apps/vedit.svg" ]; then
        if [ "$PREFIX" != "${HOME}/.local" ] && [ "$(id -u)" -ne 0 ]; then
            log_warn "Trovata vecchia versione di sistema in ${PREFIX}. Richiesta elevazione sudo..."
            exec sudo "$0" "$@"
        fi
        log_info "Rimozione file della vecchia versione (vedit) da ${PREFIX}..."
        rm -f "$OLD_DESKTOP"
        rm -f "${PREFIX}/share/icons/hicolor/scalable/apps/vedit.svg"
        rm -f "${PREFIX}/share/mime/packages/vedit.xml"
        rm -f "${PREFIX}/bin/vedit-render" "${PREFIX}/bin/vedit-gpuprobe"
        if [ "$PREFIX" = "${HOME}/.local" ] && [ -f "${PREFIX}/bin/vedit" ]; then
            rm -f "${PREFIX}/bin/vedit"
        fi
        REMOVED_ANY=true
        log_success "Vecchia versione rimossa da ${PREFIX}."
    fi

    # 2. Rimozione di Velacut (se non in modalità solo-legacy)
    if [ "$LEGACY_ONLY" = false ]; then
        BIN="${PREFIX}/bin/velacut"
        if [ -f "$BIN" ] || [ -f "${PREFIX}/share/applications/velacut.desktop" ]; then
            if [ "$PREFIX" != "${HOME}/.local" ] && [ "$(id -u)" -ne 0 ]; then
                log_warn "Trovata installazione di sistema in ${PREFIX}. Richiesta elevazione sudo..."
                exec sudo "$0" "$@"
            fi

            log_info "Rimozione file di Velacut da ${PREFIX}..."

            rm -f "${PREFIX}/bin/velacut" \
                  "${PREFIX}/bin/velacut-render" \
                  "${PREFIX}/bin/velacut-gpuprobe" \
                  "${PREFIX}/libexec/velacut-gpuprobe" 2>/dev/null || true

            rm -f "${PREFIX}/share/applications/velacut.desktop"
            rm -f "${PREFIX}/share/icons/hicolor/scalable/apps/velacut.svg"
            rm -f "${PREFIX}/share/mime/packages/velacut.xml"
            rm -f "${PREFIX}/share/metainfo/velacut.metainfo.xml"
            rm -f "${PREFIX}/share/man/man1/velacut.1"
            rm -rf "${PREFIX}/share/licenses/velacut"

            REMOVED_ANY=true
            log_success "Rimossa installazione di Velacut da ${PREFIX}."
        fi
    fi

    # Aggiorna database di sistema
    if [ -d "${PREFIX}/share/applications" ] && command -v update-desktop-database &>/dev/null; then
        update-desktop-database "${PREFIX}/share/applications" 2>/dev/null || true
    fi
    if [ -d "${PREFIX}/share/icons/hicolor" ] && command -v gtk-update-icon-cache &>/dev/null; then
        gtk-update-icon-cache -f -t "${PREFIX}/share/icons/hicolor" 2>/dev/null || true
    fi
    if [ -d "${PREFIX}/share/mime" ] && command -v update-mime-database &>/dev/null; then
        update-mime-database "${PREFIX}/share/mime" 2>/dev/null || true
    fi
done

if [ "$REMOVED_ANY" = false ]; then
    log_warn "Nessun file o lanciatore trovato da rimuovere nei percorsi standard."
fi

# Gestione pulizia dati utente / impostazioni / bozze
DATA_DIR="${HOME}/.local/share/velacut"
CONFIG_DIR="${HOME}/.config/velacut"
CACHE_DIR="${HOME}/.cache/velacut"
STATE_DIR="${HOME}/.local/state/velacut"

OLD_DATA_DIR="${HOME}/.local/share/vedit"
OLD_CONFIG_DIR="${HOME}/.config/vedit"
OLD_CACHE_DIR="${HOME}/.cache/vedit"
OLD_STATE_DIR="${HOME}/.local/state/vedit"

if [ "$PURGE" = true ]; then
    log_info "Opzione --purge attivata: eliminazione di cache, preferenze e bozze (nuove e vecchie)..."
    rm -rf "$DATA_DIR" "$CONFIG_DIR" "$CACHE_DIR" "$STATE_DIR"
    rm -rf "$OLD_DATA_DIR" "$OLD_CONFIG_DIR" "$OLD_CACHE_DIR" "$OLD_STATE_DIR"
    log_success "Tutti i dati, le cache e le impostazioni utente sono stati rimossi."
else
    if [ -d "$DATA_DIR" ] || [ -d "$OLD_DATA_DIR" ]; then
        echo ""
        echo "Nota: I dati utente, le bozze e le configurazioni sono conservati."
        echo "Per rimuovere anche tutti i dati, bozze e cache, esegui:"
        echo "  ./uninstall.sh --purge"
    fi
fi

echo ""
log_success "Operazione completata con successo!"
echo "=================================================="
