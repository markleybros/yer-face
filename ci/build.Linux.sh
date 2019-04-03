#!/bin/bash

set -e
set -o pipefail

BASEPATH="$( cd "$(dirname "${0}")/.." ; pwd -P )"

function _log() {
	echo build.Linux.sh: "${@}" 1>&2
}


_log "Starting up..."
_log "Base path is: ${BASEPATH}"
cd "${BASEPATH}"
rm -rf build
mkdir -p build
cd build

_log "Configuring..."
cmake ..
_log "Compiling..."
cmake --build . --config Release -- -j 8
_log "Staging installation..."
make install DESTDIR=AppDir

_log "Fetching linuxdeploy for packaging..."
wget 'https://github.com/TheAssassin/linuxdeploy-plugin-checkrt/releases/download/continuous/linuxdeploy-plugin-checkrt-x86_64.sh'
wget 'https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage'
chmod +x linuxdeploy-x86_64.AppImage linuxdeploy-plugin-checkrt-x86_64.sh
./linuxdeploy-x86_64.AppImage --appimage-extract

_log "Running packaging script..."
rm AppDir/usr/local/bin/yer-face
export PATH=".:${PATH}"
squashfs-root/AppRun --appdir AppDir --plugin checkrt --create-desktop-file --executable yer-face --icon-file AppDir/usr/local/share/yer-face/doc/images/yer-face.png --output appimage

_log "All done."
exit 0
