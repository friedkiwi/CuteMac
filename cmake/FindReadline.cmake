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
