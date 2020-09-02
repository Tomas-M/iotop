# Contributor: Rumen Jekov <rvjekov@gmail.com>
# Maintainer: Rumen Jekov <rvjekov@gmail.com>
# Maintainer: Boian Bonev <bbonev@ipacct.com>

pkgname=iotop-c
pkgver=1.10
pkgrel=1
pkgdesc="simple top-like I/O monitor (implemented in C)"
arch=('any')
url="https://github.com/Tomas-M/iotop"
license=('GPL2')
depends=('ncurses')
makedepends=('git' 'pkgconf')
conflicts=('iotop')
source=("git+${url}.git#tag=v${pkgver}")
sha256sums=('SKIP')

package() {
	cd "${srcdir}/iotop"
	make DESTDIR="${pkgdir}" V=1 install
}
