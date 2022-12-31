# Enable ccache compiler cache
function(useCompilerCache)
    if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
        return()
    endif()
    find_program(CCACHE_EXECUTABLE ccache)
    if(NOT CCACHE_EXECUTABLE)
        return()
    endif()
    # Use a cache variable so the user can override this
    set(CCACHE_ENV CCACHE_SLOPPINESS=pch_defines,time_macros
            CACHE STRING
            "List of environment variables for ccache, each in key=value form"
            )
    if(CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
        message("Using compiler cache")
        foreach(lang IN ITEMS C CXX)
            set(CMAKE_${lang}_COMPILER_LAUNCHER
                    ${CMAKE_COMMAND} -E env ${CCACHE_ENV} ${CCACHE_EXECUTABLE}
                    PARENT_SCOPE
                    )
        endforeach()
    endif()

    message(STATUS "Using ccache (${CCACHE_EXECUTABLE}).")

    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON CACHE BOOL "" FORCE)
        message(STATUS "Precompiled headers disabled because of non-Debug "
                "build with ccache."
                )
    endif()
endfunction()
