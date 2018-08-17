IF(NOT SSL_FOUND)
	# try to use pkg-config
	include(FindPkgConfig)
	IF(PKG_CONFIG_FOUND)
		pkg_check_modules(SSL QUIET openssl)
	ENDIF(PKG_CONFIG_FOUND)
ENDIF(NOT SSL_FOUND)

# TODO: handle case that pkg-config wasn't found or couldn't find the library

IF(SSL_FOUND)
	MESSAGE("-- Found ssl: ${SSL_LIBRARY_DIRS} (found version \"${SSL_VERSION}\")")
ENDIF(SSL_FOUND)