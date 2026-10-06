cmake_minimum_required(VERSION 3.21)
project(omasketch VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)

# The exporter deliberately uses Qt's private RHI headers (see files.cpp);
# this is the documented switch for "yes, on purpose", not a hidden problem.
set(QT_NO_PRIVATE_MODULE_WARNING ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Qml Quick Test)
find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
qt_standard_project_setup()
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

# Everything the page is drawn with: shared by the app and every test that
# builds a Page without the QML module.
set(OMASKETCH_PAGE_SOURCES
    src/arrow.cpp
    src/arrow.h
    src/geometry.h
    src/imageitem.cpp
    src/imageitem.h
    src/page.cpp
    src/page.h
    src/pageitem.h
    src/palette.h
    src/selection.cpp
    src/selection.h
    src/stroke.cpp
    src/stroke.h
    src/textbox.cpp
    src/textbox.h
    src/tools.cpp
    src/tools.h
)

qt_add_executable(omasketch
    ${OMASKETCH_PAGE_SOURCES}
    src/main.cpp
    src/files.cpp
    src/files.h
    src/pathcompleter.cpp
    src/pathcompleter.h
)

qt_add_qml_module(omasketch
    URI Omasketch
    VERSION 1.0
    QML_FILES
        src/Main.qml
        src/PathBar.qml
        src/Toast.qml
        src/KeysCard.qml
)

target_link_libraries(omasketch PRIVATE Qt6::Quick Qt6::Gui Qt6::GuiPrivate)
target_include_directories(omasketch PRIVATE src)

qt_add_executable(tst_skeleton
    tests/tst_skeleton.cpp
    ${OMASKETCH_PAGE_SOURCES}
)
target_include_directories(tst_skeleton PRIVATE src)
target_link_libraries(tst_skeleton PRIVATE Qt6::Quick Qt6::Test)

qt_add_executable(tst_page
    tests/tst_page.cpp
    ${OMASKETCH_PAGE_SOURCES}
)
target_include_directories(tst_page PRIVATE src)
target_link_libraries(tst_page PRIVATE Qt6::Quick Qt6::Gui Qt6::Test)

# Loads the real Main.qml module, so it needs the same QML module as the app.
qt_add_executable(tst_app
    tests/tst_app.cpp
    ${OMASKETCH_PAGE_SOURCES}
    src/files.cpp
    src/files.h
    src/pathcompleter.cpp
    src/pathcompleter.h
)
target_include_directories(tst_app PRIVATE src)
target_link_libraries(tst_app PRIVATE Qt6::Quick Qt6::Gui Qt6::GuiPrivate Qt6::Test)
qt_add_qml_module(tst_app
    URI Omasketch
    VERSION 1.0
    OUTPUT_DIRECTORY tst_app_qml
    QML_FILES
        src/Main.qml
        src/PathBar.qml
        src/Toast.qml
        src/KeysCard.qml
)

# Saving, opening, the clipboard and image items.
qt_add_executable(tst_files
    tests/tst_files.cpp
    ${OMASKETCH_PAGE_SOURCES}
    src/files.cpp
    src/files.h
)
target_include_directories(tst_files PRIVATE src)
target_link_libraries(tst_files PRIVATE Qt6::Quick Qt6::Gui Qt6::GuiPrivate Qt6::Test)

# Frame-time harness, run by hand on the real display; not a ctest.
qt_add_executable(bench_frames
    tests/bench_frames.cpp
    ${OMASKETCH_PAGE_SOURCES}
)
target_include_directories(bench_frames PRIVATE src)
target_link_libraries(bench_frames PRIVATE Qt6::Quick Qt6::Gui Qt6::Test)

qt_add_executable(tst_pathbar
    tests/tst_pathbar.cpp
    ${OMASKETCH_PAGE_SOURCES}
    src/pathcompleter.cpp
    src/pathcompleter.h
)
target_include_directories(tst_pathbar PRIVATE src)
target_compile_definitions(tst_pathbar PRIVATE
    SRC_DIR="${CMAKE_CURRENT_LIST_DIR}/src")
target_link_libraries(tst_pathbar PRIVATE Qt6::Quick Qt6::Test)

enable_testing()
foreach(test skeleton page app files pathbar)
    add_test(NAME ${test} COMMAND tst_${test})
endforeach()
set_tests_properties(skeleton page app files PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=gl"
)
set_tests_properties(pathbar PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
)

# End-to-end: a real session in a hidden sway (tests/e2e/run.sh). Skips
# itself with exit 77 when the tools it drives are missing.
add_test(NAME e2e COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/run.sh $<TARGET_FILE:omasketch>)
set_tests_properties(e2e PROPERTIES
    LABELS e2e
    SKIP_RETURN_CODE 77
    TIMEOUT 300
)

include(GNUInstallDirs)
install(TARGETS omasketch RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES omasketch.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES omasketch.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
