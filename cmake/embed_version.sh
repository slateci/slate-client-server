#!/bin/sh

SRC_DIR="$1"
ORIG_DIR=$(pwd)

VERSION="unknown version"

if which svnversion 2> /dev/null; then
	VERSION=$(svnversion "${SRC_DIR}")
	if [ "$VERSION" = "Unversioned directory" ]; then
		VERSION="unknown version"
	else
		DONE=1
	fi
fi
if [ -z ${DONE} ]; then
	cd "${SRC_DIR}" # git is retarded and cannot be pointed to other directories
	if which git 2> /dev/null; then
		VERSION=$(git rev-parse HEAD)
		if [ -z "$VERSION" ]; then
			VERSION="unknown version"
		else
			DONE=1
		fi
	fi
fi
echo '#define clientVersionString "'${VERSION}'"' > client_version.h
