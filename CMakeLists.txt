cmake_minimum_required(VERSION 3.21)
project(omasketch VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)

find_package(Qt6 6.8 REQUIRED COMPONENTS Qml Quick Test)
qt_standard_project_setup()
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

qt_add_executable(omasketch
    src/main.cpp
    src/palette.h
    src/tools.cpp
    src/tools.h
)

qt_add_qml_module(omasketch
    URI Omasketch
    VERSION 1.0
    QML_FILES
        src/Main.qml
)

target_link_libraries(omasketch PRIVATE Qt6::Quick)

qt_add_executable(tst_skeleton
    tests/tst_skeleton.cpp
    src/palette.h
    src/tools.cpp
    src/tools.h
)
target_include_directories(tst_skeleton PRIVATE src)
target_link_libraries(tst_skeleton PRIVATE Qt6::Quick Qt6::Test)

enable_testing()
add_test(NAME skeleton COMMAND tst_skeleton)
set_tests_properties(skeleton PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
)

include(GNUInstallDirs)
install(TARGETS omasketch RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES omasketch.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
