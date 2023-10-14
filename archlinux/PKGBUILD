# Contributor: Rumen Jekov <rvjekov@gmail.com>
# Maintainer: Rumen Jekov <rvjekov@gmail.com>
# Maintainer: Boian Bonev <bbonev@ipacct.com>

pkgname=iotop-c
pkgver=1.25
pkgrel=1
pkgdesc="simple top-like I/O monitor (implemented in C)"
arch=('i686' 'x86_64' 'aarch64' 'armv7h' 'ppc64le')
url="https://github.com/Tomas-M/iotop"
license=('GPL2')
depends=('ncurses')
makedepends=('pkgconf')
conflicts=('iotop' 'iotop-git')
provides=('iotop')
source=("${url}/releases/download/v${pkgver}/iotop-${pkgver}.tar.xz" "${url}/releases/download/v${pkgver}/iotop-${pkgver}.tar.xz.asc")
validpgpkeys=('BA60BC20F37E59444D6D25001365720913D2F22D')
md5sums=('SKIP' 'SKIP')

prepare() {
	cd iotop-${pkgver}
	sed -i 's/sbin/bin/g' Makefile
}

build() {
	cd iotop-${pkgver}
	make DESTDIR="${pkgdir}" V=1
}

package() {
	cd iotop-${pkgver}
	unset PREFIX
	make DESTDIR="${pkgdir}" install
}
