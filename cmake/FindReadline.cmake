# Readline for CuteMacDebugSession.
#
# readline comes from the vcpkg manifest, whose `readline` metaport resolves to
# readline-unix on Unix hosts and readline-win32 on Windows. The Windows port
# exports a CMake config package under its own name rather than installing a
# module-mode-discoverable layout alone, so try that first and fall back to a
# plain search for hosts supplying readline some other way.

if(NOT TARGET Readline::Readline)
    find_package(unofficial-readline-win32 CONFIG QUIET)
endif()

if(TARGET unofficial::readline-win32::readline)
    set(Readline_LIBRARY "unofficial::readline-win32::readline")
    get_target_property(Readline_INCLUDE_DIR unofficial::readline-win32::readline
        INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT Readline_INCLUDE_DIR)
        # The config package already carries its own usage requirements; this
        # variable only has to be non-empty for the standard args check.
        set(Readline_INCLUDE_DIR "unofficial-readline-win32")
    endif()

    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(Readline
        REQUIRED_VARS Readline_LIBRARY Readline_INCLUDE_DIR
    )

    if(NOT TARGET Readline::Readline)
        add_library(Readline::Readline INTERFACE IMPORTED)
        set_target_properties(Readline::Readline PROPERTIES
            INTERFACE_LINK_LIBRARIES unofficial::readline-win32::readline
        )
    endif()
    return()
endif()

find_path(Readline_INCLUDE_DIR
    NAMES readline/readline.h
)

find_library(Readline_LIBRARY
    NAMES readline
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Readline
    REQUIRED_VARS Readline_INCLUDE_DIR Readline_LIBRARY
)

if(Readline_FOUND AND NOT TARGET Readline::Readline)
    add_library(Readline::Readline UNKNOWN IMPORTED)
    set_target_properties(Readline::Readline PROPERTIES
        IMPORTED_LOCATION "${Readline_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${Readline_INCLUDE_DIR}"
    )
endif()
