#!/usr/bin/env bash
# ==============================================================================
# Velacut - Script di installazione per Linux
# https://github.com/owaismounir206-art/Velacut
# ==============================================================================
set -euo pipefail

# Colori per il terminale
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
echo "  __     __   _                  _   "
echo "  \ \   / /__| | __ _  ___ _   _| |_ "
echo "   \ \ / / _ \ |/ _\` |/ __| | | | __|"
echo "    \ V /  __/ | (_| | (__| |_| | |_ "
echo "     \_/ \___|_|\__,_|\___|\__,_|\__|"
echo -e "${RESET}"
echo "Installazione di Velacut Video Editor"
echo "=================================================="

# Determinazione del prefisso di installazione
PREFIX=""
if [ "$(id -u)" -eq 0 ]; then
    PREFIX="/usr/local"
    log_info "Esecuzione come root/sudo: installazione di sistema in ${PREFIX}"
else
    # Se passato un argomento --system, chiedi sudo
    if [ "${1:-}" = "--system" ]; then
        log_info "Richiesta installazione di sistema (--system). Elevazione permessi..."
        exec sudo "$0" "$@"
    elif [ -n "${1:-}" ] && [ "$1" != "--user" ]; then
        PREFIX="$1"
        log_info "Prefisso personalizzato: ${PREFIX}"
    else
        PREFIX="${HOME}/.local"
        log_info "Installazione per l'utente corrente in ${PREFIX} (nessun sudo richiesto)"
    fi
fi

# Verifica dipendenze essenziali
log_info "Controllo degli strumenti di compilazione..."
for cmd in cmake ninja; do
    if ! command -v "$cmd" &>/dev/null; then
        if [ "$cmd" = "ninja" ] && command -v make &>/dev/null; then
            continue
        fi
        log_error "Strumento richiesto non trovato: $cmd. Installa cmake e ninja."
        exit 1
    fi
done

GENERATOR="Ninja"
if ! command -v ninja &>/dev/null; then
    GENERATOR="Unix Makefiles"
fi

BUILD_DIR="${REPO_DIR}/build-install"

log_info "Configurazione CMake (Release, DEV_SANDBOX=OFF, Prefix=${PREFIX})..."
cmake -B "${BUILD_DIR}" -G "${GENERATOR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
    -DVELACUT_DEV_SANDBOX=OFF

log_info "Compilazione di Velacut..."
cmake --build "${BUILD_DIR}" --parallel "$(nproc)"

log_info "Installazione dei file in ${PREFIX}..."
cmake --install "${BUILD_DIR}"

# Aggiornamento database desktop, icone e MIME
log_info "Aggiornamento delle cache di sistema..."

if [ "$PREFIX" = "/usr/local" ] || [ "$PREFIX" = "/usr" ]; then
    APP_DIR="${PREFIX}/share/applications"
    ICON_DIR="${PREFIX}/share/icons/hicolor"
    MIME_DIR="${PREFIX}/share/mime"
else
    APP_DIR="${HOME}/.local/share/applications"
    ICON_DIR="${HOME}/.local/share/icons/hicolor"
    MIME_DIR="${HOME}/.local/share/mime"
fi

if command -v update-desktop-database &>/dev/null && [ -d "$APP_DIR" ]; then
    update-desktop-database "$APP_DIR" 2>/dev/null || true
fi

if command -v gtk-update-icon-cache &>/dev/null && [ -d "$ICON_DIR" ]; then
    gtk-update-icon-cache -f -t "$ICON_DIR" 2>/dev/null || true
fi

if command -v update-mime-database &>/dev/null && [ -d "$MIME_DIR" ]; then
    update-mime-database "$MIME_DIR" 2>/dev/null || true
fi

# Controllo della variabile PATH se installato per utente locale
if [ "$PREFIX" = "${HOME}/.local" ]; then
    if [[ ":$PATH:" != *":${HOME}/.local/bin:"* ]]; then
        log_warn "${HOME}/.local/bin non è attualmente nel tuo PATH."
        log_warn "Aggiungi al tuo ~/.bashrc o ~/.zshrc: export PATH=\"\$HOME/.local/bin:\$PATH\""
    fi
fi

echo ""
log_success "Velacut è stato installato con successo!"
echo "--------------------------------------------------"
echo "Eseguibile principale: ${PREFIX}/bin/velacut"
echo "Lanciatore desktop:    ${APP_DIR}/velacut.desktop"
echo "Icona:                 ${ICON_DIR}/scalable/apps/velacut.svg"
echo ""
echo "Puoi avviare Velacut:"
echo " 1) Dal menu applicazioni del tuo ambiente desktop (cerca 'Velacut')"
echo " 2) Da terminale digitando: velacut"
echo " 3) Per disinstallare in qualsiasi momento: ./tools/uninstall.sh"
echo "=================================================="
