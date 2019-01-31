#!/bin/sh

SRC_DIR="$1"
BIN_DIR="$2"
COMPONENT="$3"

VERSION=$("${SRC_DIR}/cmake/extract_version.sh" "$SRC_DIR")

echo '#define '${COMPONENT}'VersionString "'${VERSION}'"' > "${BIN_DIR}/${COMPONENT}"_version.h
