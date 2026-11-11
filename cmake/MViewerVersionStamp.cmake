# Review P2-14: refresh the git-describe part of the generated version header
# on every build, not only at configure time. Run in script mode (-P) by the
# mviewer_version_stamp target. Files are rewritten only when their content
# changes, so an unchanged describe does not trigger any recompilation.
#
# Inputs: SOURCE_DIR, BINARY_DIR, PROJECT_VERSION, PROJECT_VERSION_MAJOR,
#         PROJECT_VERSION_MINOR, PROJECT_VERSION_PATCH.
set(MVIEWER_GIT_DESCRIBE "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
        WORKING_DIRECTORY ${SOURCE_DIR}
        OUTPUT_VARIABLE _describe
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _describe_rc
        ERROR_QUIET)
    if(_describe_rc EQUAL 0 AND NOT _describe STREQUAL "")
        set(MVIEWER_GIT_DESCRIBE "${_describe}")
    endif()
endif()

# configure_file only touches the output when the generated text differs.
configure_file(${SOURCE_DIR}/cmake/MViewerVersion.h.in
               ${BINARY_DIR}/generated/MViewerVersion.h @ONLY)
file(WRITE ${BINARY_DIR}/version_info.txt.tmp
     "MVIEWER_VERSION=${PROJECT_VERSION}\nMVIEWER_GIT_DESCRIBE=${MVIEWER_GIT_DESCRIBE}\n")
file(COPY_FILE ${BINARY_DIR}/version_info.txt.tmp ${BINARY_DIR}/version_info.txt
     ONLY_IF_DIFFERENT)
file(REMOVE ${BINARY_DIR}/version_info.txt.tmp)
