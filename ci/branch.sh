#!/bin/bash

# Emit to STDOUT the closest branch name or nothing.

BASEPATH="$( cd "$(dirname "${0}")/.." ; pwd -P )"

function _log() {
	echo branch.sh: "${@}" 1>&2
}

function _die() {
	_log FATAL: "${@}"
	exit 1
}

cd "${BASEPATH}" || _die "Uh oh."

### Figure out what branch we're associated with.
# GIT_BRANCH=$(git show -s --pretty=%D HEAD | rev | cut -d, -f 1 | cut -d" " -f 1 | rev | sed -E 's/^[ ]*(.*\/)?//')
GIT_BRANCH=$(git show -s --pretty=%D HEAD | cut -d, -f 1 | rev | cut -d" " -f 1 | rev | sed -E 's/^[ ]*(.*\/)?//')
_log "Resolved branch: ${GIT_BRANCH}"

echo "${GIT_BRANCH}"
exit 0
