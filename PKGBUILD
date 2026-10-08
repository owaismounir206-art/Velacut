# Maintainer: velacut team
pkgname=velacut
pkgver=0.9.0
pkgrel=1
pkgdesc="Video editor desktop per Linux, completamente offline"
arch=('x86_64')
url="https://github.com/yourusername/velacut"
license=('GPL3')
depends=(
    'qt6-base'
    'qt6-declarative'
    'qt6-multimedia'
    'qt6-shadertools'
    'qt6-svg'
    'qt6-5compat'
    'mlt'
    'ffmpeg'
    'sdl2'
    'frei0r-plugins'
    'rubberband'
)
makedepends=(
    'cmake'
    'ninja'
    'git'
    'qt6-tools'
)
optdepends=(
    'intel-media-driver: hardware encoding on Intel GPUs'
    'libva-mesa-driver: hardware encoding on AMD GPUs'
    'nvidia-utils: hardware encoding on NVIDIA GPUs'
)
source=("git+https://github.com/yourusername/velacut.git#tag=v${pkgver}")
sha256sums=('SKIP')

build() {
    cd "$pkgname"
    cmake -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DVELACUT_DEV_SANDBOX=OFF
    cmake --build build
}

check() {
    cd "$pkgname"
    ctest --test-dir build --output-on-failure
}

package() {
    cd "$pkgname"
    DESTDIR="$pkgdir" cmake --install build

    # Desktop file
    install -Dm644 packaging/velacut.desktop "$pkgdir/usr/share/applications/velacut.desktop"

    # Icon
    install -Dm644 src/assets/icons/app-icon.svg "$pkgdir/usr/share/icons/hicolor/scalable/apps/velacut.svg"

    # Man page
    install -Dm644 docs/velacut.1 "$pkgdir/usr/share/man/man1/velacut.1"

    # License
    install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
