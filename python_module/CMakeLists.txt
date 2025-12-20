# python_module/CMakeLists.txt
# For building stellar.pyd Python module

cmake_minimum_required(VERSION 3.15)
project(stellar_python)

# ============================================================================
# FIND PYTHON
# ============================================================================
find_package(Python3 3.13 REQUIRED COMPONENTS Interpreter Development.Module)
message(STATUS "Python include dirs: ${Python3_INCLUDE_DIRS}")
message(STATUS "Python library: ${Python3_LIBRARIES}")

# ============================================================================
# DOWNLOAD PYBIND11
# ============================================================================
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/pybind11")
    message(STATUS "Downloading pybind11...")
    file(DOWNLOAD 
        https://github.com/pybind/pybind11/archive/refs/tags/v2.11.1.zip
        ${CMAKE_CURRENT_BINARY_DIR}/pybind11.zip
    )
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf pybind11.zip
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    file(RENAME 
        ${CMAKE_CURRENT_BINARY_DIR}/pybind11-2.11.1
        ${CMAKE_CURRENT_SOURCE_DIR}/pybind11
    )
endif()
add_subdirectory(pybind11)

# ============================================================================
# GLFW CONFIGURATION
# ============================================================================
# Define paths to GLFW
set(GLFW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(GLFW_LIBRARY "${GLFW_ROOT}/lib/glfw/glfw3.lib")
set(GLFW_INCLUDE_DIR "${GLFW_ROOT}/lib/glfw/include")

# Check if GLFW files exist
if(EXISTS "${GLFW_LIBRARY}")
    message(STATUS "Found GLFW static library: ${GLFW_LIBRARY}")
else()
    message(FATAL_ERROR "GLFW library not found at: ${GLFW_LIBRARY}")
endif()

# ============================================================================
# COMPILER SETTINGS
# ============================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(
        /EHsc
        /std:c++17
        /W3
        /wd4464 /wd4365 /wd4820 /wd4530 /wd4577
        /D_CRT_SECURE_NO_WARNINGS
        /DNOMINMAX
        /DWIN32_LEAN_AND_MEAN
        /DNO_TEXT_RENDERING=1
    )
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# ============================================================================
# SOURCE FILES
# ============================================================================
set(CORE_SOURCES
    ../src/axis.cpp
    ../src/camera.cpp
    ../src/globals.cpp
    ../src/objects.cpp
    ../src/parser.cpp
    ../src/physics_system.cpp
    ../src/utils.cpp
    ../src/vectorfield.cpp
)

set(PYTHON_SOURCES
    bindings.cpp
    simulation_wrapper.cpp
)

set(GLAD_SOURCE ../src/glad.c)

file(GLOB IMGUI_SOURCES
    ../lib/imgui/*.cpp
    ../lib/imgui/backends/imgui_impl_glfw.cpp
    ../lib/imgui/backends/imgui_impl_opengl3.cpp
)

set(ALL_SOURCES
    ${CORE_SOURCES}
    ${PYTHON_SOURCES}
    ${GLAD_SOURCE}
    ${IMGUI_SOURCES}
)

# ============================================================================
# CREATE PYTHON MODULE
# ============================================================================
pybind11_add_module(stellar ${ALL_SOURCES})

target_compile_definitions(stellar PRIVATE
    NO_TEXT_RENDERING
    _WINDLL
    PYTHON_MODULE=1
)

# ============================================================================
# INCLUDE DIRECTORIES
# ============================================================================
if(EXISTS "${GLFW_ROOT}/lib/glfw/include/GLFW")
    target_include_directories(stellar PRIVATE
        "${GLFW_ROOT}/lib/glfw/include"
    )
    message(STATUS "Using GLFW includes from: ${GLFW_ROOT}/lib/glfw/include")
else()
    target_include_directories(stellar PRIVATE
        "C:/Users/user/jabariaiden-ProjStellar-main/glfw-3.4.bin.WIN64/include"
    )
    message(STATUS "Using GLFW includes from downloaded package")
endif()

target_include_directories(stellar PRIVATE
    ../include
    ../lib/glm
    ../lib/glad/include
    ../lib/imgui
    ../lib/imgui/backends
    ${Python3_INCLUDE_DIRS}
)

# ============================================================================
# LINK LIBRARIES
# ============================================================================
target_link_libraries(stellar PRIVATE
    ${Python3_LIBRARIES}
    opengl32.lib
    gdi32.lib
    "${GLFW_LIBRARY}"
    user32.lib
    shell32.lib
    advapi32.lib
    kernel32.lib
    comdlg32.lib
)

# ============================================================================
# COPY .pyd TO PACKAGE DIRECTORY
# ============================================================================
add_custom_command(TARGET stellar POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/../hyperstellar/src/hyperstellar/_native/windows-x64"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:stellar>"
        "${CMAKE_CURRENT_SOURCE_DIR}/../hyperstellar/src/hyperstellar/_native/windows-x64/stellar.pyd"
    COMMENT "Copying stellar.pyd to Python package"
)

# ============================================================================
# COPY ALL SHADERS TO PACKAGE DIRECTORY - FIXED VERSION
# ============================================================================
add_custom_command(TARGET stellar POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/../hyperstellar/src/hyperstellar/_native/windows-x64/shaders"
    # Copy ALL shader files individually
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/math.comp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../hyperstellar/src/hyperstellar/_native/windows-x64/shaders/math.comp"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/quad.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/../hyperstellar/src/hyperstellar/_native/windows-x64/shaders/quad.vert"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/quad.geom"
        "${CMAKE_CURRENT_SOURCE_DIR}/../hyperstellar/src/hyperstellar/_native/windows-x64/shaders/quad.geom"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/quad.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/../hyperstellar/src/hyperstellar/_native/windows-x64/shaders/quad.frag"
    # Add more shaders if you have them
    COMMENT "Copying shaders to Python package"
)

# ============================================================================
# SHOW BUILD INFO
# ============================================================================
message(STATUS "Building Python module: stellar")
message(STATUS "Output: $<TARGET_FILE:stellar>")
message(STATUS "Will copy to: ${CMAKE_CURRENT_SOURCE_DIR}/../hyperstellar/src/hyperstellar/_native/windows-x64/stellar.pyd")













