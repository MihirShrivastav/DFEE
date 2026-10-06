cmake_minimum_required(VERSION 3.26)
project(DFEEDesktop VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

# Build the engine core, but not its Python module or its tests.
set(DFEE_BUILD_TESTS OFF)
set(DFEE_BUILD_PYTHON OFF)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../cpp_engine ${CMAKE_BINARY_DIR}/cpp_engine)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick Gui Concurrent QuickControls2 Sql Test)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(DFEE
    src/main.cpp
    src/DesktopDiagnostics.cpp
    src/DesktopDiagnostics.h
    src/EngineController.cpp
    src/EngineController.h
    src/RenderWorker.cpp
    src/RenderWorker.h
    src/PreviewImageProvider.h
    src/ThumbnailImageProvider.h
    src/LibraryController.cpp
    src/LibraryController.h
    src/UiScript.cpp
    src/UiScript.h
    src/EditStore.cpp
    src/EditStore.h
    src/ImageInfo.cpp
    src/ImageInfo.h
    src/StockCatalog.cpp
    src/StockCatalog.h
    src/WindowChrome.cpp
    src/WindowChrome.h
)

set_source_files_properties(qml/v2/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(DFEE
    URI DFEE
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/v2/MainV2.qml
        qml/v2/Theme.qml
        qml/v2/controls/FlIcon.qml
        qml/v2/ControlsGallery.qml
        qml/v2/shell/FlToolbar.qml
        qml/v2/shell/FlSidebar.qml
        qml/v2/canvas/FlCanvas.qml
        qml/v2/controls/FlButton.qml
        qml/v2/controls/FlIconButton.qml
        qml/v2/controls/FlSegmented.qml
        qml/v2/controls/FlSlider.qml
        qml/v2/controls/FlSliderRow.qml
        qml/v2/controls/FlSectionHeader.qml
        qml/v2/controls/FlHairline.qml
        qml/v2/controls/FlTip.qml
        qml/v2/controls/FlMenu.qml
        qml/v2/controls/FlSwitch.qml
        qml/v2/controls/FlFilmSlider.qml
        qml/v2/controls/FlGroupLabel.qml
        qml/v2/controls/FlInspectorSection.qml
        qml/v2/controls/FlListPopup.qml
        qml/v2/inspector/FlInspector.qml
        qml/v2/inspector/FlScope.qml
        qml/v2/inspector/FlFilmSection.qml
        qml/v2/inspector/FlExposureSection.qml
        qml/v2/inspector/FlToneSection.qml
        qml/v2/controls/FlColorWheel.qml
        qml/v2/controls/FlToneSwatch.qml
        qml/v2/inspector/FlColorSection.qml
        qml/v2/inspector/FlGrainLightSection.qml
        qml/v2/inspector/FlPrintSection.qml
        qml/v2/inspector/FlFineTuneSection.qml
        qml/v2/controls/FlTextField.qml
        qml/v2/tray/FlTray.qml
        qml/v2/tray/FlRollStrip.qml
        qml/v2/tray/FlFilmsStrip.qml
)

# Bundled assets: Geist (Swiss grotesque) + film-stock boxart tiles. BASE "resources"
# maps e.g. resources/fonts/Geist-Regular.ttf -> :/fonts/Geist-Regular.ttf and
# resources/boxart/portra_400.svg -> :/boxart/portra_400.svg.
file(GLOB DFEE_BOXART CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/boxart/*.svg")
file(GLOB DFEE_ICONS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/*.svg")
qt_add_resources(DFEE "app_assets"
    PREFIX "/"
    BASE "resources"
    FILES
        resources/fonts/Geist-Regular.ttf
        resources/fonts/Geist-Medium.ttf
        resources/fonts/Geist-SemiBold.ttf
        ${DFEE_BOXART}
        ${DFEE_ICONS}
        resources/stocks/catalog.json
)

target_compile_definitions(DFEE PRIVATE DFEE_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(DFEE PRIVATE Qt6::Quick Qt6::Gui Qt6::Concurrent Qt6::QuickControls2 Qt6::Sql dfee_core $<$<PLATFORM_ID:Windows>:dwmapi>)
set_target_properties(DFEE PROPERTIES WIN32_EXECUTABLE ON)

# The build tree carries a partial Qt deployment (Qt6Core.dll + platforms/ beside the
# exe), so Qt resolves plugins next to the executable. Give the build-tree app and the
# unit tests the SQL module and the SQLite driver the edit catalog needs.
add_custom_command(TARGET DFEE POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:DFEE>/sqldrivers
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt6::Sql> $<TARGET_FILE_DIR:DFEE>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt6::QSQLiteDriverPlugin>
            $<TARGET_FILE_DIR:DFEE>/sqldrivers)
# The v2 inspector persists its section state with QtCore's Settings type; the
# build tree's partial deployment lacks that QML module (deploy.ps1 ships it).
add_custom_command(TARGET DFEE POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}/Qt6QmlCore.dll" $<TARGET_FILE_DIR:DFEE>
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_QML}/QtCore" $<TARGET_FILE_DIR:DFEE>/qml/QtCore)

# Desktop unit tests (Qt Test). Pure desktop logic only -- no engine session.
enable_testing()
qt_add_executable(desktop_tests
    tests/edit_store_test.cpp
    src/EditStore.cpp
    src/EditStore.h
)
target_include_directories(desktop_tests PRIVATE src)
target_link_libraries(desktop_tests PRIVATE Qt6::Core Qt6::Sql Qt6::Test)
target_compile_definitions(desktop_tests PRIVATE DFEE_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
add_test(NAME desktop_tests COMMAND desktop_tests)
add_custom_command(TARGET desktop_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:desktop_tests>/sqldrivers
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt6::QSQLiteDriverPlugin>
            $<TARGET_FILE_DIR:desktop_tests>/sqldrivers)

qt_add_executable(image_info_tests
    tests/image_info_test.cpp
    src/ImageInfo.cpp
    src/ImageInfo.h
)
target_include_directories(image_info_tests PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../cpp_engine/include)
target_link_libraries(image_info_tests PRIVATE Qt6::Core Qt6::Test)
add_test(NAME image_info_tests COMMAND image_info_tests)

qt_add_executable(stock_catalog_tests
    tests/stock_catalog_test.cpp
    src/StockCatalog.cpp
    src/StockCatalog.h
)
target_include_directories(stock_catalog_tests PRIVATE src)
target_link_libraries(stock_catalog_tests PRIVATE Qt6::Core Qt6::Test)
target_compile_definitions(stock_catalog_tests PRIVATE DFEE_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
add_test(NAME stock_catalog_tests COMMAND stock_catalog_tests)
