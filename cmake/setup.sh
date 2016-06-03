#!/bin/bash

orig=$1
dest=$2
progname="proxenet"

KEYS_DIR="${dest}/keys"
AUTOLOAD_DIR="${dest}/proxenet-plugins/autoload"

echo "* Setup ${progname} environment"

echo "** Building proxenet-plugins tree in '${AUTOLOAD_DIR}'"
mkdir -p ${AUTOLOAD_DIR}

echo "** Building SSL CA in '${KEYS_DIR}'"
cp -r ${orig}/keys ${dest}
chmod a+rx ${KEYS_DIR}
make -C ${KEYS_DIR} clean keys

echo "* Success"
echo "! Run ${dest}/${progname} to start"
echo "! The plugin directory is at '${dest}/proxenet-plugins'"

exit 0
