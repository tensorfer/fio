#!/bin/bash

set -e

if [[ -z "${XFER_FIO:-}" ]]; then
	echo "Missing XFER_FIO"
	exit -1
fi

XFER_FIO_PACKAGING_DIR=${XFER_FIO}/packaging
cd ${XFER_FIO_PACKAGING_DIR}

rm -f $XFER_FIO_PACKAGING_DIR/redhat/*.rpm

for dir in fio-gd2fs
do
	echo "Build " $dir
	rpmbuild -bb redhat/$dir/$dir.spec
done
