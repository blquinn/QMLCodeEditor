# Strict warning set, applied to our own targets by linking qce_warnings.
# -Werror is opt-in: cmake -DQCE_WERROR=ON.
option(QCE_WERROR "Treat compiler warnings as errors" OFF)

add_library(qce_warnings INTERFACE)
if(MSVC)
    target_compile_options(qce_warnings INTERFACE /W4)
    if(QCE_WERROR)
        target_compile_options(qce_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(qce_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast
        -Woverloaded-virtual -Wimplicit-fallthrough -Wnull-dereference)
    if(QCE_WERROR)
        target_compile_options(qce_warnings INTERFACE -Werror)
    endif()
endif()
