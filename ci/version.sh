#!/bin/bash

# Emit to STDOUT a usable version string for this build or die with an error status.

BASEPATH="$( cd "$(dirname "${0}")/.." ; pwd -P )"

function _log() {
	echo version.sh: "${@}" 1>&2
}

function _die() {
	_log FATAL: "${@}"
	exit 1
}


VERSION_STRING="yer-face"
cd "${BASEPATH}" || _die "Uh oh."

### Calculate the version, validate via git tag.
PACKAGE_VERSION=$(cat "${BASEPATH}/VERSION")
VERSION_EXACT=false
GIT_TAG=$(git describe --tags --exact-match 2> /dev/null)
if [ -n "${GIT_TAG}" ]; then
	if [ "v${PACKAGE_VERSION}" != "${GIT_TAG}" ]; then
		_die "VERSION file (${PACKAGE_VERSION}) and Git tag (${GIT_TAG}) do not match."
	fi
	VERSION_EXACT=true
fi
VERSION_STRING="${VERSION_STRING}-${PACKAGE_VERSION}"

### If we don't exactly match a tag, append more information to the version string.
if [ "${VERSION_EXACT}" != "true" ]; then
	### Figure out what branch we're associated with.
	GIT_BRANCH=$("${BASEPATH}"/ci/branch.sh)
	if [ -n "${GIT_BRANCH}" ]; then
		VERSION_STRING="${VERSION_STRING}-${GIT_BRANCH}"
	fi

	### Append the current date.
	BUILD_DATE=$(date '+%Y%m%d')
	VERSION_STRING="${VERSION_STRING}-${BUILD_DATE}"

	### Append the short commit hash.
	GIT_COMMIT=$(git rev-parse --short HEAD)
	VERSION_STRING="${VERSION_STRING}-${GIT_COMMIT}"
fi

echo "${VERSION_STRING}"
exit 0
