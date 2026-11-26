# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.24)
project(SwiftifyAlignmentLab LANGUAGES CXX)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.8)
include(GNUInstallDirs)

qt_add_executable(alignment-lab gui/main.cpp gui/Lab.cpp gui/Lab.h)
target_compile_features(alignment-lab PRIVATE cxx_std_20)
target_link_libraries(alignment-lab PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
qt_add_qml_module(alignment-lab
    URI Swiftify.AlignmentLab
    VERSION 1.0
    QML_FILES gui/Main.qml
)

add_custom_target(alignment-lab-backend ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:alignment-lab>/alignment_lab"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/alignment_lab/__init__.py
        ${CMAKE_CURRENT_SOURCE_DIR}/alignment_lab/__main__.py
        ${CMAKE_CURRENT_SOURCE_DIR}/alignment_lab/audio.py
        ${CMAKE_CURRENT_SOURCE_DIR}/alignment_lab/benchmark.py
        ${CMAKE_CURRENT_SOURCE_DIR}/alignment_lab/cache.py
        ${CMAKE_CURRENT_SOURCE_DIR}/alignment_lab/catalog.py
        ${CMAKE_CURRENT_SOURCE_DIR}/alignment_lab/ctc.py
        ${CMAKE_CURRENT_SOURCE_DIR}/alignment_lab/domain.py
        ${CMAKE_CURRENT_SOURCE_DIR}/alignment_lab/export.py
        ${CMAKE_CURRENT_SOURCE_DIR}/alignment_lab/heuristic.py
        ${CMAKE_CURRENT_SOURCE_DIR}/alignment_lab/onnx_ctc.py
        ${CMAKE_CURRENT_SOURCE_DIR}/alignment_lab/storage.py
        ${CMAKE_CURRENT_SOURCE_DIR}/alignment_lab/text.py
        "$<TARGET_FILE_DIR:alignment-lab>/alignment_lab"
    VERBATIM
)
add_dependencies(alignment-lab alignment-lab-backend)

install(TARGETS alignment-lab RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY alignment_lab DESTINATION ${CMAKE_INSTALL_DATADIR}/swiftify-alignment-lab
        FILES_MATCHING PATTERN "*.py" PATTERN "__pycache__" EXCLUDE)
file(RELATIVE_PATH alignment_lab_data_dir "${CMAKE_INSTALL_FULL_BINDIR}"
     "${CMAKE_INSTALL_FULL_DATADIR}/swiftify-alignment-lab")
target_compile_definitions(alignment-lab PRIVATE ALIGNMENT_LAB_DATA_DIR="${alignment_lab_data_dir}")

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../../LICENSE"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/swiftify-alignment-lab)

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME alignment_cli
            COMMAND Python3::Interpreter -B -m unittest discover -s tests
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
    find_package(Qt6 6.8 QUIET COMPONENTS Test)
    if(TARGET Qt6::Test)
        qt_add_executable(tst_alignment_worker tests/tst_worker.cpp gui/Lab.cpp gui/Lab.h)
        target_compile_features(tst_alignment_worker PRIVATE cxx_std_20)
        target_include_directories(tst_alignment_worker PRIVATE gui)
        target_compile_definitions(tst_alignment_worker PRIVATE ALIGNMENT_LAB_DATA_DIR="${alignment_lab_data_dir}")
        target_link_libraries(tst_alignment_worker PRIVATE Qt6::Core Qt6::Test)
        add_dependencies(tst_alignment_worker alignment-lab-backend)
        add_test(NAME alignment_worker COMMAND tst_alignment_worker)
    endif()
endif()
