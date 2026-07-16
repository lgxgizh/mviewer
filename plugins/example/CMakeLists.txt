# Example analyzer plugin (RFC-013). Built as a shared library and loaded at
# runtime by test_pluginregistry to prove the Plugin Registry round-trip
# (build -> load -> register -> create -> analyze).
add_library(example_analyzer SHARED
    ExampleAnalyzerPlugin.cpp
)

target_link_libraries(example_analyzer
    PRIVATE
        mviewer_core
)

target_compile_features(example_analyzer PRIVATE cxx_std_20)
target_include_directories(example_analyzer PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Co-locate the plugin .dll next to the test executables (root sets
# CMAKE_RUNTIME_OUTPUT_DIRECTORY to ${CMAKE_BINARY_DIR}/bin) so
# test_pluginregistry can load it by file name.
set_target_properties(example_analyzer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    PREFIX ""
)
