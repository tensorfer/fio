#!/bin/bash

set -e

if [[ -z "${XFER_FIO:-}" ]]; then
	echo "Missing XFER_FIO"
	exit -1
fi

XFER_FIO_PACKAGING_DIR=${XFER_FIO}/packaging

rm -rf $XFER_FIO_PACKAGING_DIR/debian/fio-gd2fs/usr
rm -rf $XFER_FIO_PACKAGING_DIR/debian/*.deb

mkdir -p ${XFER_FIO}/packaging/debian/fio-gd2fs/usr/bin
cp -rf ${XFER_FIO}/fio ${XFER_FIO_PACKAGING_DIR}/debian/fio-gd2fs/usr/bin/fio-gd2fs
dpkg-deb -b ${XFER_FIO_PACKAGING_DIR}/debian/fio-gd2fs ${XFER_FIO_PACKAGING_DIR}/debian/fio-gd2fs.deb
