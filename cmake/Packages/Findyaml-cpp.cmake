IF(NOT YAMLCPP_FOUND)
	# try to use pkg-config
	include(FindPkgConfig)
	IF(PKG_CONFIG_FOUND)
		pkg_check_modules(YAMLCPP QUIET yaml-cpp)
	ENDIF(PKG_CONFIG_FOUND)
ENDIF(NOT YAMLCPP_FOUND)

# TODO: handle case that pkg-config wasn't found or couldn't find the library

IF(YAMLCPP_FOUND)
	MESSAGE("-- Found yaml-cpp: ${YAMLCPP_PREFIX} (found version \"${YAMLCPP_VERSION}\")")
ENDIF(YAMLCPP_FOUND)