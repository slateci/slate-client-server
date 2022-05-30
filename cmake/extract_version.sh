#!/bin/sh

SRC_DIR="$1"
ORIG_DIR=$(pwd)

if [ -z ${VERSION_OVERRIDE} ]; then
  if which svnversion > /dev/null 2>&1; then
  	VERSION=$(svnversion "${SRC_DIR}")
  	if [ "$VERSION" = "Unversioned directory" ]; then
  		VERSION="unknown version"
  	else
  		DONE=1
  	fi
  fi
  if [ -z ${DONE} ]; then
  	cd "${SRC_DIR}" # git is annoying and cannot be pointed to other directories
  	if which git > /dev/null 2>&1; then
  		# attempt to simulate sane version numbering
  		VERSION=$(git log | grep -c '^commit')
  		if [ -z "$VERSION" ]; then
  			VERSION="unknown version"
  		else
  			if git status -s -uno | grep -q '^.M'; then
  				VERSION=${VERSION}M
  			fi
  			DONE=1
  		fi
  	fi
  fi

  echo "$VERSION"

else
  # Versioning may be tied to other products such as docker image tags.
  echo "${VERSION_OVERRIDE}"
fi