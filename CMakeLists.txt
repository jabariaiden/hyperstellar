cmake_minimum_required(VERSION 3.15)
project(StellarPhysics)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC flags
if(MSVC)
    add_compile_options(/W4 /EHsc)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/glm
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/glad/include
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/glfw/include  # GLFW headers
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/backends
)

# Source files
file(GLOB_RECURSE ALL_SRC_FILES 
    "src/*.cpp"
    "lib/imgui/*.cpp"
    "lib/imgui/backends/imgui_impl_glfw.cpp"
    "lib/imgui/backends/imgui_impl_opengl3.cpp"
    "lib/ImGuiFileDialog/ImGuiFileDialog.cpp"
)

list(FILTER ALL_SRC_FILES EXCLUDE REGEX ".*python_bindings\\.cpp$")

add_executable(stellar_main ${ALL_SRC_FILES} src/glad.c)

# Link libraries - FIXED: Use the correct path to glfw3.lib
target_link_libraries(stellar_main
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/glfw/glfw3.lib  # Correct path
    opengl32
    gdi32
)

# Copy shaders
add_custom_command(TARGET stellar_main PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
)

add_custom_command(TARGET stellar_main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders
        ${CMAKE_CURRENT_BINARY_DIR}/shaders
    COMMENT "Copying shaders"
)

# Check and copy GLFW DLL if it exists in the glfw folder
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/glfw/glfw3.dll")
    add_custom_command(TARGET stellar_main POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/lib/glfw/glfw3.dll
            ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Copying GLFW DLL"
    )
endif()

# Also check in bin folder (common location)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bin/glfw3.dll")
    add_custom_command(TARGET stellar_main POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/bin/glfw3.dll
            ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Copying GLFW DLL from bin folder"
    )
endif()

# For Visual Studio debugging
if(MSVC)
    set_target_properties(stellar_main PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
endif()