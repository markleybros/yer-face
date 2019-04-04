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

VERSION_STRING="$("${BASEPATH}/ci/version.sh")"
_log "Resolved version string: ${VERSION_STRING}"

_log "Setting up build directory..."
rm -rf build
mkdir -p build
cd build

_log "Configuring..."
cmake ..
_log "Compiling..."
cmake --build . --config Release
_log "Staging installation..."
make install DESTDIR=AppDir

_log "Running packaging script..."
rm AppDir/usr/local/bin/yer-face
export PATH=".:${PATH}"
export OUTPUT="${VERSION_STRING}-x86_64.AppImage"
linuxdeploy --appdir AppDir --plugin checkrt --create-desktop-file --executable yer-face --icon-file AppDir/usr/local/share/yer-face/doc/images/yer-face.png --output appimage

_log "All done."
exit 0
