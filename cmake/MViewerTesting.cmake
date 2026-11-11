# Test registration helpers (review P2-7).
#
#   mviewer_add_test(<name> SRCS <files...> [LIBS <libs...>] [LABELS <labels...>]
#                    [TIMEOUT <seconds>] [SERIAL])
#
# Creates the executable, links Qt Core/Gui/Widgets + mviewer_core + LIBS,
# adds src/ and tests/support/ (shared check.h) to the include path,
# registers the CTest and applies TIMEOUT / RUN_SERIAL / LABELS. The per-test
# runtime isolation (APPDATA, HOME, TEMP ...) is applied afterwards for every
# registered test at the end of src/CMakeLists.txt.
#
#   mviewer_test_support(<target>)
#
# Only adds tests/support/ to an existing hand-written test target.

set(MVIEWER_TEST_SUPPORT_DIR "${CMAKE_SOURCE_DIR}/tests/support")

function(mviewer_test_support target)
    target_include_directories(${target} PRIVATE "${MVIEWER_TEST_SUPPORT_DIR}")
endfunction()

function(mviewer_add_test name)
    cmake_parse_arguments(T "SERIAL" "TIMEOUT" "SRCS;LIBS;LABELS" ${ARGN})
    if(NOT T_SRCS)
        message(FATAL_ERROR "mviewer_add_test(${name}): SRCS is required")
    endif()
    add_executable(${name} ${T_SRCS})
    target_link_libraries(${name} PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core mviewer_core ${T_LIBS})
    target_compile_features(${name} PRIVATE cxx_std_20)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    mviewer_test_support(${name})
    add_test(NAME ${name} COMMAND ${name})
    if(T_SERIAL)
        set_tests_properties(${name} PROPERTIES RUN_SERIAL TRUE)
    endif()
    if(T_TIMEOUT)
        set_tests_properties(${name} PROPERTIES TIMEOUT ${T_TIMEOUT})
    endif()
    if(T_LABELS)
        set_tests_properties(${name} PROPERTIES LABELS "${T_LABELS}")
    endif()
endfunction()
