cmake_minimum_required(VERSION 3.21)
project(omasketch VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)

find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Qml Quick Test)
qt_standard_project_setup()
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

qt_add_executable(omasketch
    src/main.cpp
    src/page.cpp
    src/page.h
    src/palette.h
    src/pathcompleter.cpp
    src/pathcompleter.h
    src/stroke.cpp
    src/stroke.h
    src/tools.cpp
    src/tools.h
)

qt_add_qml_module(omasketch
    URI Omasketch
    VERSION 1.0
    QML_FILES
        src/Main.qml
        src/PathBar.qml
        src/Toast.qml
)

target_link_libraries(omasketch PRIVATE Qt6::Quick Qt6::Gui)
target_include_directories(omasketch PRIVATE src)

# The tests need a page too; the QML module itself only matters for the app.
set(OMASKETCH_PAGE_SOURCES
    src/page.cpp
    src/page.h
    src/palette.h
    src/stroke.cpp
    src/stroke.h
    src/tools.cpp
    src/tools.h
)

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
)
target_include_directories(tst_app PRIVATE src)
target_link_libraries(tst_app PRIVATE Qt6::Quick Qt6::Gui Qt6::Test)
qt_add_qml_module(tst_app
    URI Omasketch
    VERSION 1.0
    OUTPUT_DIRECTORY tst_app_qml
    QML_FILES
        src/Main.qml
)

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
add_test(NAME skeleton COMMAND tst_skeleton)
set_tests_properties(skeleton PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=gl"
)
add_test(NAME page COMMAND tst_page)
set_tests_properties(page PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=gl"
)
add_test(NAME app COMMAND tst_app)
set_tests_properties(app PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=gl"
)
add_test(NAME pathbar COMMAND tst_pathbar)
set_tests_properties(pathbar PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
)

include(GNUInstallDirs)
install(TARGETS omasketch RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES omasketch.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
