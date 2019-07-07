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

if [ -z "${CMAKE_BUILD_TYPE}" ]; then
	CMAKE_BUILD_TYPE=Release
fi

_log "Setting up build directory..."
rm -rf build
mkdir -p build
cd build

_log "Configuring..."
cmake -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" ..
VERSION_STRING="$(cat "${BASEPATH}/build/VersionString")"
_log "Resolved version string: ${VERSION_STRING}"
_log "Compiling..."
cmake --build . -- -j 8
_log "Staging installation..."
make install DESTDIR=AppDir

_log "Running packaging script..."
rm AppDir/usr/local/bin/yer-face
export PATH=".:${PATH}"
export OUTPUT="${VERSION_STRING}-x86_64.AppImage"
export VERSION="${VERSION_STRING}"
linuxdeploy --appdir AppDir --plugin checkrt --create-desktop-file --executable yer-face --icon-file AppDir/usr/local/share/yer-face/doc/images/yer-face.png --output appimage

_log "Creating SHA256SUM file..."
sha256sum "${OUTPUT}" > "${VERSION_STRING}".SHA256SUMS

_log "All done."
exit 0
