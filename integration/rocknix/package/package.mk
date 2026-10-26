# SPDX-License-Identifier: GPL-2.0-or-later
# Esqueleto de paquete para packages/xx-cfw/xx-lidd/package.mk en un fork de ROCKNIX.
PKG_NAME="xx-lidd"
PKG_VERSION="0.1.0"
PKG_LICENSE="GPL-2.0-or-later"
PKG_SITE="https://github.com/afsenovilla/XX-CFW"
PKG_URL="${PKG_SITE}/archive/v${PKG_VERSION}.tar.gz"
PKG_DEPENDS_TARGET="toolchain"
PKG_LONGDESC="Lid policy daemon for Allwinner H700 clamshells (RG34XXSP, RG35XXSP, RG SP)"
PKG_TOOLCHAIN="manual"

make_target() {
  make -C ${PKG_BUILD}/src/lidd CC="${CC}" CFLAGS="${CFLAGS}" LDFLAGS="${LDFLAGS}"
}

makeinstall_target() {
  install -D -m 0755 ${PKG_BUILD}/src/lidd/xx-lidd ${INSTALL}/usr/bin/xx-lidd
  install -D -m 0644 ${PKG_BUILD}/src/lidd/lidd.conf.example ${INSTALL}/usr/config/xx-lidd.conf
  install -D -m 0644 ${PKG_BUILD}/integration/rocknix/logind-xx-lid.conf \
    ${INSTALL}/usr/lib/systemd/logind.conf.d/xx-lid.conf
  for d in pre-sleep post-wake deep-transition; do
    install -d ${INSTALL}/usr/config/xx-lidd/hooks/${d}.d
    for f in ${PKG_BUILD}/src/lidd/hooks/${d}.d/*; do
      [ -e "${f}" ] && install -m 0755 "${f}" ${INSTALL}/usr/config/xx-lidd/hooks/${d}.d/
    done
  done
  # La unidad del paquete apunta a /usr/bin y a la copia de /storage/.config que
  # ROCKNIX crea a partir de /usr/config en el primer arranque.
  sed -e 's#/storage/xx-cfw/xx-lidd#/usr/bin/xx-lidd#' \
    ${PKG_BUILD}/integration/rocknix/xx-lidd.service > ${PKG_BUILD}/xx-lidd.service
  install -D -m 0644 ${PKG_BUILD}/xx-lidd.service ${INSTALL}/usr/lib/systemd/system/xx-lidd.service
}

post_install() {
  enable_service xx-lidd.service
}
