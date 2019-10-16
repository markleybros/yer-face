#!/bin/bash

BASEPATH="$( cd "$(dirname "${0}")/.." ; pwd -P )"

function _log() {
	echo upload.Windows.sh: "${@}" 1>&2
}

function _die() {
	_log FATAL: "${@}"
	exit 1
}


set -o pipefail

_log "Starting up..."
_log "Base path is: ${BASEPATH}"
cd "${BASEPATH}" || _die "Uh oh."

if [ -z "${ARTIFACT_BASES3}" -o -z "${ARTIFACT_BASEURL}" ]; then
	_die "Missing one or more buildspec environment variables."
fi

VERSION_STRING="$(cat build/VersionString)"
export OUTPUT_ZIP_FILE="${VERSION_STRING}-x86_64.zip"
export OUTPUT_HASH_FILE="${VERSION_STRING}.SHA256SUMS"

_log "Uploading build/${OUTPUT_ZIP_FILE} to ${ARTIFACT_BASES3}/Windows/"
aws s3 cp build/"${OUTPUT_ZIP_FILE}" "${ARTIFACT_BASES3}"/Windows/ --acl public-read --content-type application/octet-stream --content-disposition attachment || _die "Failed uploading binary."
_log "Uploading build/${OUTPUT_HASH_FILE} to ${ARTIFACT_BASES3}/Windows/"
aws s3 cp build/"${OUTPUT_HASH_FILE}" "${ARTIFACT_BASES3}"/Windows/ --acl public-read --content-type text/plain --content-disposition attachment || _die "Failed uploading hash file."

GIT_BRANCH=$("${BASEPATH}"/ci/branch.sh) || _die "Failed resolving branch."
echo "${GIT_BRANCH}" | egrep -i 'master|tag:' >/dev/null
GIT_BRANCH_MATCH=${?}
if [ "${GIT_BRANCH_MATCH}" -ne 0 ]; then
	_log "This does not appear to be either a master or tag build, so we won't update the latest file."
	exit 0
fi

export VERSION_STRING_HTML="$(echo "${VERSION_STRING}" | ci/util/htmlescape.py)"
export DATE_STRING_HTML="$(date '+%B %-d, %Y' | ci/util/htmlescape.py)"

export DOWNLOAD_URL="${ARTIFACT_BASEURL}/Windows/$(echo "${OUTPUT_ZIP_FILE}" | ci/util/urlencode.py)"
export DOWNLOAD_URL_HTML="$(echo "${DOWNLOAD_URL}" | ci/util/htmlescape.py)"

export HASH_URL="${ARTIFACT_BASEURL}/Windows/$(echo "${OUTPUT_HASH_FILE}" | ci/util/urlencode.py)"
export HASH_URL_HTML="$(echo "${HASH_URL}" | ci/util/htmlescape.py)"
export SIZE_STRING_HTML="$(du -h build/"${OUTPUT_ZIP_FILE}" | cut -f1 | ci/util/htmlescape.py)"
_log "Templating latest.html..."
envsubst < ci/data/latest.Windows.html > /tmp/latest.html || _die "Failed to envsubst the latest file."

_log "Uploading /tmp/latest.html ${ARTIFACT_BASES3}/Windows/latest.html"
aws s3 cp /tmp/latest.html "${ARTIFACT_BASES3}"/Windows/latest.html --acl public-read --content-type text/html --content-disposition inline || _die "Failed to upload the latest file."

exit 0
