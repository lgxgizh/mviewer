add_library(mviewer_core STATIC
    core/image/Decoder.cpp
    core/image/ImageObject.cpp
    core/image/ImageCache.cpp
    core/filesystem/FileSystem.cpp
    core/scheduler/TaskScheduler.cpp
    core/compare/CompareEngine.cpp
    core/analysis/AnalysisEngine.cpp
    core/render/RenderEngine.cpp
)

target_link_libraries(mviewer_core PUBLIC Qt6::Widgets)

target_compile_features(mviewer_core PUBLIC cxx_std_20)

target_include_directories(mviewer_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(mviewer_ui STATIC
    mainwindow.cpp
    imageviewer.cpp
    directorytree.cpp
    thumbnailpanel.cpp
    previewpanel.cpp
    thumbnailcache.cpp
    compareworkspace.cpp
    analysispanel.cpp
)

target_link_libraries(mviewer_ui PUBLIC Qt6::Widgets mviewer_core)

target_compile_features(mviewer_ui PUBLIC cxx_std_20)

target_include_directories(mviewer_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(MViewer
    main.cpp
)

target_link_libraries(MViewer
    PRIVATE
        Qt6::Widgets
        mviewer_core
        mviewer_ui
)

target_compile_features(MViewer PRIVATE cxx_std_20)

target_include_directories(MViewer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(core_tests
    core/test_compare.cpp
)

target_link_libraries(core_tests
    PRIVATE
        Qt6::Widgets
        mviewer_core
)

target_compile_features(core_tests PRIVATE cxx_std_20)

target_include_directories(core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(visual_test
    visual_test.cpp
)

target_link_libraries(visual_test
    PRIVATE
        Qt6::Widgets
        mviewer_core
        mviewer_ui
)

target_compile_features(visual_test PRIVATE cxx_std_20)

target_include_directories(visual_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(analyze_main
    analyze_main.cpp
)

target_link_libraries(analyze_main
    PRIVATE
        Qt6::Gui
        Qt6::Core
)

target_compile_features(analyze_main PRIVATE cxx_std_20)
