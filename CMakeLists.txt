cmake_minimum_required(VERSION 3.15)
project(StellarPhysics)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler flags
set(CMAKE_C_FLAGS "-Wall -MMD -MP")
set(CMAKE_CXX_FLAGS "-Wall -std=c++17 -MMD -MP")

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/glm
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/glad/include
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/glfw/include
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/backends
    C:/msys64/mingw64/include/freetype2
)

# Source files
file(GLOB_RECURSE ALL_SRC_FILES 
    "src/*.cpp"
    "lib/imgui/*.cpp"
    "lib/imgui/backends/imgui_impl_glfw.cpp"
    "lib/imgui/backends/imgui_impl_opengl3.cpp"
    "lib/ImGuiFileDialog/ImGuiFileDialog.cpp"
)

# Remove python bindings from main project
list(FILTER ALL_SRC_FILES EXCLUDE REGEX ".*python_bindings\\.cpp$")

# Add executable
add_executable(stellar_main ${ALL_SRC_FILES} src/glad.c)

# Link libraries
target_link_libraries(stellar_main
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/libglfw3dll.a
    C:/msys64/mingw64/lib/libfreetype.dll.a
    opengl32
    gdi32
)

# ============================================================================
# COPY ALL SHADERS FROM ROOT/SHADERS TO BUILD/SHADERS
# ============================================================================
# Create shaders directory first
add_custom_command(TARGET stellar_main PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
)

# Copy ALL shader files from root/shaders to build/shaders
add_custom_command(TARGET stellar_main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders
        ${CMAKE_CURRENT_BINARY_DIR}/shaders
    COMMENT "Copying all shaders from root/shaders to build/shaders"
)

# ============================================================================
# Copy required DLLs
# ============================================================================
add_custom_command(TARGET stellar_main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/bin/glfw3.dll
        ${CMAKE_CURRENT_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        C:/msys64/mingw64/bin/libfreetype-6.dll
        ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Copying required DLLs"
)

# ============================================================================
# Copy assets if they exist
# ============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bin/assets")
    add_custom_command(TARGET stellar_main POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/bin/assets
            ${CMAKE_CURRENT_BINARY_DIR}/assets
        COMMENT "Copying assets"
    )
endif()

# ============================================================================
# Copy fonts if they exist
# ============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bin/fonts")
    add_custom_command(TARGET stellar_main POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/bin/fonts
            ${CMAKE_CURRENT_BINARY_DIR}/fonts
        COMMENT "Copying fonts"
    )
endif()