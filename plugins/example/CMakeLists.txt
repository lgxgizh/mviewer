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

# M14.2: test-only plugin that declares an incompatible abiVersion (999) so the
# ABI freeze gate can be exercised by test_pluginabi. Never shipped.
add_library(example_analyzer_badabi SHARED
    BadAbiAnalyzerPlugin.cpp
)

target_link_libraries(example_analyzer_badabi
    PRIVATE
        mviewer_core
)

target_compile_features(example_analyzer_badabi PRIVATE cxx_std_20)
target_include_directories(example_analyzer_badabi PRIVATE ${CMAKE_SOURCE_DIR}/src)

set_target_properties(example_analyzer_badabi PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    PREFIX ""
)

# M14.3: example decoder plugin (PPM). Implements IDecoder and registers into
# DecoderRegistry via the unified PluginManager loader.
add_library(example_decoder SHARED
    ExampleDecoderPlugin.cpp
)

target_link_libraries(example_decoder
    PRIVATE
        mviewer_core
)

target_compile_features(example_decoder PRIVATE cxx_std_20)
target_include_directories(example_decoder PRIVATE ${CMAKE_SOURCE_DIR}/src)

set_target_properties(example_decoder PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    PREFIX ""
)

# M14.3: example exporter plugin (PNG/BMP). Implements IExporter and registers
# into ExporterRegistry via the unified PluginManager loader. Uses Qt (QImage)
# for the actual encode, so it links Qt6::Gui.
add_library(example_exporter SHARED
    ExampleExporterPlugin.cpp
)

target_link_libraries(example_exporter
    PRIVATE
        mviewer_core
        Qt6::Gui
)

target_compile_features(example_exporter PRIVATE cxx_std_20)
target_include_directories(example_exporter PRIVATE ${CMAKE_SOURCE_DIR}/src)

set_target_properties(example_exporter PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    PREFIX ""
)

