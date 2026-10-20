# Per-target compiler warning policy for MViewer's own targets (not LibRaw,
# not plugins/tests). Applied with target_compile_options(PRIVATE), so it never
# leaks into consumers and no global add_compile_options is needed.
#
#   GCC / Clang : -Wall -Wextra (diagnostics only; no -Werror). Local Ninja / IDE
#                 builds now surface the common warning set directly.
#   MSVC        : /W4 when MVIEWER_MSVC_W4=ON. It is OFF by default because the
#                 CI "Enforce zero warnings" step fails on ANY "warning Cxxxx" in
#                 build.log and is calibrated at the compiler default level; the
#                 /W4 backlog has to be cleared before the default can flip.
#                 Configure with -DMVIEWER_MSVC_W4=ON to see it locally.
option(MVIEWER_MSVC_W4 "Compile MViewer targets with MSVC /W4 (see cmake/MViewerWarnings.cmake)" OFF)

function(mviewer_target_warnings)
    foreach(_target IN LISTS ARGN)
        if(MSVC)
            if(MVIEWER_MSVC_W4)
                target_compile_options(${_target} PRIVATE /W4)
            endif()
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${_target} PRIVATE -Wall -Wextra)
        endif()
    endforeach()
endfunction()
