#!/bin/bash

BASEPATH="$( cd "$(dirname "${0}")/.." ; pwd -P )"

function _log() {
	echo branch.sh: "${@}" 1>&2
}

function _die() {
	_log FATAL: "${@}"
	exit 1
}


set -o pipefail

_log "Starting up..."
_log "Base path is: ${BASEPATH}"
cd "${BASEPATH}" || _die "Uh oh."

VERSION_STRING="$(cat build/VersionString)"
export OUTPUT_APPIMAGE_FILE="${VERSION_STRING}-x86_64.AppImage"
export OUTPUT_HASH_FILE="${VERSION_STRING}.SHA256SUMS"

aws s3 cp build/"${OUTPUT_APPIMAGE_FILE}" "${ARTIFACT_BASES3}"/Linux/ --acl public-read --content-type application/octet-stream --content-disposition attachment --no-progress || _die "Failed uploading binary."
aws s3 cp build/"${OUTPUT_HASH_FILE}" "${ARTIFACT_BASES3}"/Linux/ --acl public-read --content-type text/plain --content-disposition inline --no-progress || _die "Failed uploading hash file."

GIT_BRANCH=$("${BASEPATH}"/ci/branch.sh) || _die "Failed resolving branch."
if [ "${GIT_BRANCH}" != "master" ]; then
	_log "This is not a master build, so we won't update the latest file."
	exit 0
fi

export VERSION_STRING_HTML="$(echo "${VERSION_STRING}" | ci/util/htmlescape.py)"
export DATE_STRING_HTML="$(date '+%B %-d, %Y' | ci/util/htmlescape.py)"

export DOWNLOAD_URL="${ARTIFACT_BASEURL}/Linux/$(echo "${OUTPUT_APPIMAGE_FILE}" | ci/util/urlencode.py)"
export DOWNLOAD_URL_HTML="$(echo "${DOWNLOAD_URL}" | ci/util/htmlescape.py)"

export HASH_URL="${ARTIFACT_BASEURL}/Linux/$(echo "${OUTPUT_HASH_FILE}" | ci/util/urlencode.py)"
export HASH_URL_HTML="$(echo "${HASH_URL}" | ci/util/htmlescape.py)"
export SIZE_STRING_HTML="$(du -h build/"${OUTPUT_APPIMAGE_FILE}" | cut -f1 | ci/util/htmlescape.py)"
envsubst < ci/data/latest.Linux.html > /tmp/latest.html || _die "Failed to envsubst the latest file."

aws s3 cp /tmp/latest.html "${ARTIFACT_BASES3}"/Linux/latest.html --acl public-read --content-type text/html --content-disposition inline --no-progress || _die "Failed to upload the latest file."

exit 0
