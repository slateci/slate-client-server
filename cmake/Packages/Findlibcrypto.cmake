IF(NOT LIBCRYPTO_FOUND)
	# try to use pkg-config
	include(FindPkgConfig)
	IF(PKG_CONFIG_FOUND)
		pkg_check_modules(LIBCRYPTO QUIET libcrypto)
	ENDIF(PKG_CONFIG_FOUND)
ENDIF(NOT LIBCRYPTO_FOUND)

# TODO: handle case that pkg-config wasn't found or couldn't find the library

IF(LIBCRYPTO_FOUND)
	MESSAGE("-- Found libcrypto: ${LIBCRYPTO_LIBRARY_DIRS} (found version \"${LIBCRYPTO_VERSION}\")")
ENDIF(LIBCRYPTO_FOUND)