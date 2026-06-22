# Maintainer: Your Name <your@email.com>
# Contributor: Your Name <your@email.com>

pkgname=voxflow
pkgver=1.0.0
pkgrel=1
pkgdesc="Push-to-talk speech-to-text bar widget for Noctalia"
arch=('x86_64')
url="https://github.com/yourusername/voxflow"
license=('MIT')
depends=(
  'quickshell'
  'libcurl'
  'wtype'
  'wl-clipboard'
)
makedepends=(
  'gcc'
  'make'
)
source=("$pkgname-$pkgver.tar.gz::https://github.com/yourusername/voxflow/archive/v$pkgver.tar.gz")
sha256sums=('SKIP')

prepare() {
  cd "$srcdir/$pkgname-$pkgver"
  mkdir -p vendor
}

build() {
  cd "$srcdir/$pkgname-$pkgver/backend"
  make
}

package() {
  cd "$srcdir/$pkgname-$pkgver"

  # Plugin files go to noctalia plugins dir
  install -dm755 "$pkgdir/usr/share/noctalia/plugins/voxflow/"{bin,i18n}

  install -Dm755 backend/voxflow-backend "$pkgdir/usr/share/noctalia/plugins/voxflow/bin/voxflow-backend"
  install -Dm644 Main.qml "$pkgdir/usr/share/noctalia/plugins/voxflow/Main.qml"
  install -Dm644 BarWidget.qml "$pkgdir/usr/share/noctalia/plugins/voxflow/BarWidget.qml"
  install -Dm644 Settings.qml "$pkgdir/usr/share/noctalia/plugins/voxflow/Settings.qml"
  install -Dm644 manifest.json "$pkgdir/usr/share/noctalia/plugins/voxflow/manifest.json"
  install -Dm644 i18n/en.json "$pkgdir/usr/share/noctalia/plugins/voxflow/i18n/en.json"

  # Symlink for user-facing plugin dir
  install -dm755 "$pkgdir/etc/skel/.config/noctalia/plugins"
}
