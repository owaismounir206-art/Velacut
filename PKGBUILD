# Maintainer: velacut team <https://github.com/owaismounir206-art/Velacut>
pkgname=velacut
pkgver=0.1.0
pkgrel=1
pkgdesc="Video editor desktop per Linux, completamente offline"
arch=('x86_64')
url="https://github.com/owaismounir206-art/Velacut"
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
    'hicolor-icon-theme'
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
    'whisper.cpp: automatic speech-to-text subtitling'
    'piper-tts: local text-to-speech audio generation'
    'rembg: local background removal'
)
source=("git+https://github.com/owaismounir206-art/Velacut.git")
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
}
