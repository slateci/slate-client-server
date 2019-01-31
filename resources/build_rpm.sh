#!/bin/sh

set -e

PKG_VERSION=$("${CMAKE_SOURCE_DIR}/cmake/extract_version.sh" "${CMAKE_SOURCE_DIR}")
if [ "$PKG_VERSION" = "unknown version" ]; then
	echo "Unable to determine valid package version" 1>&2
	exit 1
fi
if echo "$PKG_VERSION" | grep -q M; then
	echo "Modified sources detected; only clean versions should be packaged" 1>&2
	exit 1
fi
PKG_NAME="slate-client-server-$PKG_VERSION"
PKG_SHORT_NAME="slate-client-server"
mkdir -p "${CMAKE_BINARY_DIR}/SOURCES"

rm -rf "${CMAKE_BINARY_DIR}/SOURCES/${PKG_SHORT_NAME}" "${CMAKE_BINARY_DIR}/SOURCES/${PKG_NAME}.tar.gz"

# Get a clean copy of the source tree into a tarball
if which svn > /dev/null 2>&1 && svn info "${CMAKE_SOURCE_DIR}" > /dev/null 2>&1 ; then
	svn export -q "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/SOURCES/${PKG_SHORT_NAME}"
	tar czf "${CMAKE_BINARY_DIR}/SOURCES/${PKG_NAME}.tar.gz" --exclude "${PKG_SHORT_NAME}/\.*" \
	    -C "${CMAKE_BINARY_DIR}/SOURCES" "${PKG_SHORT_NAME}"
elif which git > /dev/null 2>&1 && [ -d "${CMAKE_SOURCE_DIR}/.git" ] ; then
	SRC_DIR_NAME=$(basename "${CMAKE_SOURCE_DIR}")
	git clone -q "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/SOURCES/${PKG_SHORT_NAME}"
	tar czf "${CMAKE_BINARY_DIR}/SOURCES/${PKG_NAME}.tar.gz" -C "${CMAKE_BINARY_DIR}/SOURCES" \
	    --exclude "${PKG_SHORT_NAME}/\.*" "${PKG_SHORT_NAME}"
else
	SRC_DIR_NAME=$(basename "${CMAKE_SOURCE_DIR}")
	# $CMAKE_BINARY_DIR might be inside $CMAKE_SOURCE_DIR, so we need to exclude it from the tarball
	# To do that, we need to figure out its name realtive to $CMAKE_SOURCE_DIR. We can do that by
	# checking whether $CMAKE_SOURCE_DIR is a prefix of $CMAKE_BINARY_DIR, but we must take care to
	# ensure that the prefix we use for checking ends with a slash to avoid being fooled by
	# stuff/package being a prefix of stuff/package_build or similar. 
	DIR_PREFIX="${CMAKE_SOURCE_DIR}"
	if echo "${DIR_PREFIX}" | grep -q "[^/]$"; then
		# append a slash if one was not already there
		DIR_PREFIX="${DIR_PREFIX}/"
	fi
	# Now compute the part of $CMAKE_BINARY_DIR which remains after eliminating any $DIR_PREFIX prefix
	# TODO: this will fail in exciting ways if DIR_PREFIX contains '|' or other characters which are 
	#       special to sed
	RELATIVE_BUILD_DIR=$(echo "${CMAKE_BINARY_DIR}" | sed 's|^'"${DIR_PREFIX}"'||')
	# We are now ready to build a temporary file of exclusion patterns for tar
	# TODO: this will be too broad if $RELATIVE_BUILD_DIR contains '.' or other characters which are 
	#       special to tar's pattern interpretation
	echo "${SRC_DIR_NAME}/${RELATIVE_BUILD_DIR}" > export_exclusions
	echo "${SRC_DIR_NAME}/\.*" >> export_exclusions
	echo "\.DS_Store" >> export_exclusions
	# Note: running this on darwin leaves lots of ._ clutter, but this shouldn't matter as rpmbuild 
	#       should only be relevant on linux. Emacs/vim clutter might be a problem, however. 
	tar czf "${CMAKE_BINARY_DIR}/SOURCES/${PKG_NAME}.tar.gz" -C "${CMAKE_SOURCE_DIR}/.." \
	    -X export_exclusions "${SRC_DIR_NAME}"
	rm export_exclusions
fi
