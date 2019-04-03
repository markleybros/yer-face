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


PACKAGE_VERSION=$(cat "${BASEPATH}/VERSION")
VERSION_EXACT=false
GIT_TAG=$(git describe --tags --exact-match 2> /dev/null)
if [ -n "${GIT_TAG}" ]; then
	if [ "${PACKAGE_VERSION}" != "${GIT_TAG}" ]; then
		_die "VERSION file (${PACKAGE_VERSION}) and Git tag (${GIT_TAG}) do not match."
	fi
	PACKAGE_VERSION="${GIT_TAG}"
	VERSION_EXACT=true
fi
VERSION_STRING="${PACKAGE_VERSION}"

if [ "${VERSION_EXACT}" != "true" ]; then
	GIT_BRANCH=$(git symbolic-ref -q --short HEAD)
	if [ -n "${GIT_BRANCH}" ]; then
		VERSION_STRING="${VERSION_STRING}-${GIT_BRANCH}"
	fi

	GIT_COMMIT=$(git rev-parse --short HEAD)
	VERSION_STRING="${VERSION_STRING}-${GIT_COMMIT}"
fi

echo "${VERSION_STRING}"
exit 0
