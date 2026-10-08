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

echo -e "${BOLD}"
echo "Disinstallazione di Velacut Video Editor"
echo "=================================================="
echo -e "${RESET}"

PURGE=false
for arg in "$@"; do
    if [ "$arg" = "--purge" ] || [ "$arg" = "-p" ]; then
        PURGE=true
    fi
done

# Possibili prefissi in cui Velacut potrebbe essere installato
PREFIXES=()
if [ "$(id -u)" -eq 0 ]; then
    PREFIXES=("/usr/local" "/usr")
else
    PREFIXES=("${HOME}/.local" "/usr/local" "/usr")
fi

REMOVED_ANY=false

for PREFIX in "${PREFIXES[@]}"; do
    BIN="${PREFIX}/bin/velacut"
    if [ -f "$BIN" ] || [ -f "${PREFIX}/share/applications/velacut.desktop" ]; then
        # Se si tenta di rimuovere da directory di sistema senza permessi di root, chiedi sudo
        if [ "$PREFIX" != "${HOME}/.local" ] && [ "$(id -u)" -ne 0 ]; then
            log_warn "Trovata installazione di sistema in ${PREFIX}. Richiesta elevazione sudo..."
            exec sudo "$0" "$@"
        fi

        log_info "Rimozione file da ${PREFIX}..."

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
        log_success "Rimossa installazione da ${PREFIX}."

        # Aggiorna database
        if [ -d "${PREFIX}/share/applications" ] && command -v update-desktop-database &>/dev/null; then
            update-desktop-database "${PREFIX}/share/applications" 2>/dev/null || true
        fi
        if [ -d "${PREFIX}/share/icons/hicolor" ] && command -v gtk-update-icon-cache &>/dev/null; then
            gtk-update-icon-cache -f -t "${PREFIX}/share/icons/hicolor" 2>/dev/null || true
        fi
        if [ -d "${PREFIX}/share/mime" ] && command -v update-mime-database &>/dev/null; then
            update-mime-database "${PREFIX}/share/mime" 2>/dev/null || true
        fi
    fi
done

if [ "$REMOVED_ANY" = false ]; then
    log_warn "Nessuna installazione attiva di Velacut trovata nei percorsi standard."
fi

# Gestione pulizia dati utente / impostazioni / bozze
DATA_DIR="${HOME}/.local/share/velacut"
CONFIG_DIR="${HOME}/.config/velacut"
CACHE_DIR="${HOME}/.cache/velacut"
STATE_DIR="${HOME}/.local/state/velacut"

if [ "$PURGE" = true ]; then
    log_info "Opzione --purge attivata: eliminazione di cache, preferenze e bozze..."
    rm -rf "$DATA_DIR" "$CONFIG_DIR" "$CACHE_DIR" "$STATE_DIR"
    log_success "Dati e configurazioni utente rimossi."
else
    if [ -d "$DATA_DIR" ] || [ -d "$CONFIG_DIR" ] || [ -d "$CACHE_DIR" ]; then
        echo ""
        echo "Nota: I dati utente, le bozze e le configurazioni sono conservati in:"
        echo " - Configurazioni: ${CONFIG_DIR}"
        echo " - Bozze e progetti: ${DATA_DIR}"
        echo " - Cache:          ${CACHE_DIR}"
        echo "Per rimuovere anche tutti i dati e le impostazioni, esegui:"
        echo "  ./tools/uninstall.sh --purge"
    fi
fi

echo ""
log_success "Disinstallazione completata con successo!"
echo "=================================================="
