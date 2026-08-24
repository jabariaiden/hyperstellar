cmake_minimum_required(VERSION 3.15)
project(stellar_python)

find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter Development.Module)
message(STATUS "Python include dirs: ${Python3_INCLUDE_DIRS}")

# ============================================================================
# PYBIND11 (v2.13.6 for Python 3.13/3.14 compatibility)
# ============================================================================
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/pybind11")
    message(STATUS "Downloading pybind11...")
    file(DOWNLOAD
        https://github.com/pybind/pybind11/archive/refs/tags/v2.13.6.zip
        ${CMAKE_CURRENT_BINARY_DIR}/pybind11.zip
    )
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf pybind11.zip
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    file(RENAME
        ${CMAKE_CURRENT_BINARY_DIR}/pybind11-2.13.6
        ${CMAKE_CURRENT_SOURCE_DIR}/pybind11
    )
endif()
add_subdirectory(pybind11)

if(WIN32)
    set(PLATFORM_NAME "windows-x64")
elseif(UNIX AND NOT APPLE)
    set(PLATFORM_NAME "linux-x64")
elseif(APPLE)
    set(PLATFORM_NAME "macos-x64")
endif()

set(NATIVE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../hyperstellar/src/hyperstellar/_native/${PLATFORM_NAME}")
message(STATUS "Platform: ${PLATFORM_NAME}")
message(STATUS "Native output dir: ${NATIVE_DIR}")

set(GLFW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

if(WIN32)
    set(GLFW_LIBRARY "${GLFW_ROOT}/lib/glfw/glfw3.lib")
    set(GLFW_INCLUDE_DIR "${GLFW_ROOT}/lib/glfw/include")
    if(NOT EXISTS "${GLFW_LIBRARY}")
        message(FATAL_ERROR "GLFW not found at: ${GLFW_LIBRARY}")
    endif()
    message(STATUS "Using GLFW: ${GLFW_LIBRARY}")
else()
    if(DEFINED GLFW_LIBRARY AND DEFINED GLFW_INCLUDE_DIR)
        message(STATUS "Using provided GLFW: ${GLFW_LIBRARY}")
    else()
        find_package(PkgConfig REQUIRED)
        pkg_search_module(GLFW REQUIRED glfw3)
        set(GLFW_INCLUDE_DIR ${GLFW_INCLUDE_DIRS})
        set(GLFW_LIBRARY ${GLFW_LIBRARIES})
        message(STATUS "Using system GLFW: ${GLFW_LIBRARY}")
    endif()
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(
        /EHsc /std:c++17 /W3
        /wd4464 /wd4365 /wd4820 /wd4530 /wd4577
        /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /DWIN32_LEAN_AND_MEAN
        /DNO_TEXT_RENDERING=1
    )
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter -O2)
endif()

set(CORE_SOURCES
    ../src/axis.cpp
    ../src/camera.cpp
    ../src/globals.cpp
    ../src/objects.cpp
    ../src/parser.cpp
    ../src/physics_system.cpp
    ../src/utils.cpp
    ../src/vectorfield.cpp
    ../src/script_manager.cpp
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

pybind11_add_module(stellar
    ${CORE_SOURCES}
    ${PYTHON_SOURCES}
    ${GLAD_SOURCE}
    ${IMGUI_SOURCES}
)

target_compile_definitions(stellar PRIVATE
    NO_TEXT_RENDERING
    PYTHON_MODULE=1
    $<$<PLATFORM_ID:Windows>:_WINDLL>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
)

if(UNIX)
    set_target_properties(stellar PROPERTIES SUFFIX ".so")
endif()

target_include_directories(stellar PRIVATE
    ${GLFW_INCLUDE_DIR}
    ../include
    ../lib/glm
    ../lib/glad/include
    ../lib/imgui
    ../lib/imgui/backends
    ${Python3_INCLUDE_DIRS}
)

# Windows linking
if(WIN32)
    target_link_libraries(stellar PRIVATE
        ${Python3_LIBRARIES}
        opengl32.lib
        gdi32.lib
        "${GLFW_LIBRARY}"
        user32.lib shell32.lib advapi32.lib kernel32.lib comdlg32.lib
    )
else()
    # Linux: Do NOT link Python3_LIBRARIES dynamically.
    # GLFW is linked statically, while system X11/GL libs are linked dynamically.
    # auditwheel permits standard system dependencies (libX11.so, libGL.so, etc.).
    target_link_libraries(stellar PRIVATE
        ${GLFW_LIBRARY}
        X11
        Xrandr
        Xi
        Xxf86vm
        Xinerama
        Xcursor
        Xrender
        Xext
        Xdamage
        xcb
        X11-xcb
        GL
        dl
        pthread
    )
endif()

# Post-build copy
if(WIN32)
    set(NATIVE_FILENAME "stellar.pyd")
else()
    set(NATIVE_FILENAME "stellar.so")
endif()

add_custom_command(TARGET stellar POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${NATIVE_DIR}/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:stellar>"
        "${NATIVE_DIR}/${NATIVE_FILENAME}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/math.comp"   "${NATIVE_DIR}/shaders/math.comp"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/paint.comp"  "${NATIVE_DIR}/shaders/paint.comp"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/quad.vert"   "${NATIVE_DIR}/shaders/quad.vert"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/quad.geom"   "${NATIVE_DIR}/shaders/quad.geom"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/quad.frag"   "${NATIVE_DIR}/shaders/quad.frag"
    COMMENT "Copying ${NATIVE_FILENAME} and shaders to ${NATIVE_DIR}"
)