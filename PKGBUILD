# Maintainer: mole <mole@example.com>
pkgname=crow-git
pkgver=r1.deec4f3
pkgrel=1
pkgdesc="A minimalist, Wayland-native Guilty Gear -Strive- Mod Manager written in C and GTK4."
arch=('x86_64')
url="https://github.com/moemairu/crow"
license=('MIT')
depends=('gtk4')
makedepends=('git' 'cmake' 'pkgconf' 'gcc')
provides=("${pkgname%-git}")
conflicts=("${pkgname%-git}")
source=("git+https://github.com/moemairu/crow.git")
sha256sums=('SKIP')

pkgver() {
  cd "$srcdir/${pkgname%-git}"
  printf "r%s.%s" "$(git rev-list --count HEAD)" "$(git rev-parse --short HEAD)"
}

build() {
  cd "$srcdir/${pkgname%-git}"
  cmake -B build -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release
  cmake --build build
}

package() {
  cd "$srcdir/${pkgname%-git}"
  DESTDIR="$pkgdir" cmake --install build
}
