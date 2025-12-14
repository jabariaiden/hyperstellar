// async_shader_loader.h - FIXED FOR MINGW THREADING AND RELATIVE PATHS
#ifndef ASYNC_SHADER_LOADER_H
#define ASYNC_SHADER_LOADER_H

#include <thread>
#include <atomic>
#include <string>
#include <functional>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <glad/glad.h>

// Platform-specific includes for module path detection
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

enum class ShaderLoadState
{
    IDLE,
    READING_FILES,
    FILES_READY,
    COMPILING_VERTEX,
    COMPILING_GEOMETRY,
    COMPILING_FRAGMENT,
    COMPILING_COMPUTE,
    LINKING,
    COMPLETE,
    FAILED
};

struct ShaderPaths
{
    std::string vertex;
    std::string geometry;
    std::string fragment;
    std::string compute;

    bool IsComputeShader() const { return !compute.empty(); }
    bool HasGeometry() const { return !geometry.empty(); }
};

struct ShaderSources
{
    std::string vertex;
    std::string geometry;
    std::string fragment;
    std::string compute;
    bool isCompute = false;
    bool hasGeometry = false;
};

// Helper function to get shader path relative to module location
static std::string GetShaderPath(const std::string& shaderName) {
    static std::string shaderBasePath;
    
    if (shaderBasePath.empty()) {
        #ifdef _WIN32
        // Get path to this DLL/.pyd
        char modulePath[1024];
        HMODULE hm = NULL;
        
        // Get the address of this function to locate our module
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | 
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              (LPCSTR)&GetShaderPath, &hm)) {
            GetModuleFileNameA(hm, modulePath, sizeof(modulePath));
            
            // Extract directory and add "shaders/"
            std::string fullPath(modulePath);
            size_t lastSlash = fullPath.find_last_of("\\/");
            if (lastSlash != std::string::npos) {
                shaderBasePath = fullPath.substr(0, lastSlash + 1) + "shaders\\";
                std::cout << "[ShaderLoader] Module located at: " << fullPath << std::endl;
                std::cout << "[ShaderLoader] Shader directory: " << shaderBasePath << std::endl;
                
                // Debug: Check if directory exists
                #ifdef _WIN32
                DWORD attrib = GetFileAttributesA(shaderBasePath.c_str());
                if (attrib == INVALID_FILE_ATTRIBUTES || !(attrib & FILE_ATTRIBUTE_DIRECTORY)) {
                    std::cerr << "[ShaderLoader] WARNING: Shader directory not found: " << shaderBasePath << std::endl;
                } else {
                    std::cout << "[ShaderLoader] ✓ Shader directory exists" << std::endl;
                }
                #endif
            } else {
                std::cerr << "[ShaderLoader] WARNING: Could not extract directory from: " << fullPath << std::endl;
                shaderBasePath = "./shaders/";
            }
        } else {
            std::cerr << "[ShaderLoader] WARNING: Could not get module handle, using default path" << std::endl;
            shaderBasePath = "./shaders/";
        }
        #else
        // Linux/macOS: use dladdr to get module path
        Dl_info info;
        if (dladdr((void*)&GetShaderPath, &info)) {
            std::string fullPath(info.dli_fname);
            size_t lastSlash = fullPath.find_last_of("/");
            if (lastSlash != std::string::npos) {
                shaderBasePath = fullPath.substr(0, lastSlash + 1) + "shaders/";
            } else {
                shaderBasePath = "./shaders/";
            }
        } else {
            shaderBasePath = "./shaders/";
        }
        #endif
        
        std::cout << "[ShaderLoader] Using shader base path: " << shaderBasePath << std::endl;
    }
    
    return shaderBasePath + shaderName;
}

class AsyncShaderLoader
{
public:
    AsyncShaderLoader()
        : m_state(ShaderLoadState::IDLE), m_progress(0.0f), m_program(0), m_shouldStop(false)
    {
    }

    ~AsyncShaderLoader()
    {
        // Clean up any OpenGL resources if shader was created
        if (m_program != 0)
        {
            glDeleteProgram(m_program);
            m_program = 0;
        }
    }

    // Load compute shader
    void LoadComputeShaderAsync(const std::string &computePath,
                                std::function<void(GLuint)> onComplete,
                                std::function<void(const std::string &)> onError)
    {
        ShaderPaths paths;
        paths.compute = computePath;  // Just filename, GetShaderPath will add directory
        LoadShaderAsync(paths, onComplete, onError);
    }

    // Load graphics pipeline (vert + frag + optional geom)
    void LoadGraphicsShaderAsync(const std::string &vertPath,
                                 const std::string &fragPath,
                                 const std::string &geomPath,
                                 std::function<void(GLuint)> onComplete,
                                 std::function<void(const std::string &)> onError)
    {
        ShaderPaths paths;
        paths.vertex = vertPath;      // Just filename
        paths.fragment = fragPath;    // Just filename
        paths.geometry = geomPath;    // Just filename
        LoadShaderAsync(paths, onComplete, onError);
    }

    // MUST be called from main thread (OpenGL context thread)
    void Update()
    {
        static int debugCounter = 0;
        if (debugCounter++ % 100 == 0)
        {
            std::cout << "[AsyncLoader::Update] State=" << (int)m_state.load()
                      << ", Progress=" << m_progress.load() << std::endl;
            std::flush(std::cout);
        }

        // Check if files are ready for compilation
        if (m_state == ShaderLoadState::FILES_READY)
        {
            std::cout << "[AsyncLoader::Update] FILES_READY detected, starting compilation..." << std::endl;
            std::flush(std::cout);
            
            CompileShadersOnMainThread();
        }
        
        // Check if we just completed AND haven't called callback yet
        if (m_state == ShaderLoadState::COMPLETE && m_onComplete)
        {
            GLuint completedProgram = m_program;

            // Mark callback as processed by clearing it
            auto callback = std::move(m_onComplete);
            m_onComplete = nullptr;

            // Reset state
            m_state = ShaderLoadState::IDLE;
            //remove program from loader since ownership is passed to caller
            //m_program = 0;

            // Call the callback
            std::cout << "[AsyncLoader::Update] Calling completion callback with program " << completedProgram << std::endl;
            std::flush(std::cout);
            callback(completedProgram);
        }
        else if (m_state == ShaderLoadState::FAILED && m_onError)
        {
            std::string error = m_errorMessage;
            auto callback = std::move(m_onError);
            m_onError = nullptr;

            m_state = ShaderLoadState::IDLE;

            std::cout << "[AsyncLoader::Update] Calling error callback" << std::endl;
            std::flush(std::cout);
            callback(error);
        }
    }

    ShaderLoadState GetState() const { return m_state; }
    float GetProgress() const { return m_progress; }

    std::string GetStatusMessage() const
    {
        switch (m_state)
        {
        case ShaderLoadState::IDLE:
            return "Idle";
        case ShaderLoadState::READING_FILES:
            return "Reading shader files...";
        case ShaderLoadState::FILES_READY:
            return "Files loaded, ready to compile...";
        case ShaderLoadState::COMPILING_VERTEX:
            return "Compiling vertex shader...";
        case ShaderLoadState::COMPILING_GEOMETRY:
            return "Compiling geometry shader...";
        case ShaderLoadState::COMPILING_FRAGMENT:
            return "Compiling fragment shader...";
        case ShaderLoadState::COMPILING_COMPUTE:
            return "Compiling compute shader (may take 5-10s on Intel HD)...";
        case ShaderLoadState::LINKING:
            return "Linking program...";
        case ShaderLoadState::COMPLETE:
            return "Complete!";
        case ShaderLoadState::FAILED:
            return "Failed: " + m_errorMessage;
        default:
            return "Unknown";
        }
    }

    bool IsLoading() const
    {
        return m_state != ShaderLoadState::IDLE &&
               m_state != ShaderLoadState::COMPLETE &&
               m_state != ShaderLoadState::FAILED;
    }

private:
    void LoadShaderAsync(const ShaderPaths &paths,
                         std::function<void(GLuint)> onComplete,
                         std::function<void(const std::string &)> onError)
    {
        if (m_state != ShaderLoadState::IDLE && m_state != ShaderLoadState::COMPLETE)
        {
            std::cout << "[AsyncLoader] Already loading, ignoring request" << std::endl;
            return;
        }

        std::cout << "[AsyncLoader] Starting shader load..." << std::endl;
        std::flush(std::cout);

        m_state = ShaderLoadState::READING_FILES;
        m_progress = 0.0f;
        m_shouldStop = false;
        m_onComplete = onComplete;
        m_onError = onError;
        m_program = 0;

        // CRITICAL FIX: Read files on MAIN THREAD to avoid MinGW file I/O issues
        std::cout << "[AsyncLoader] Reading shader files on main thread..." << std::endl;
        std::flush(std::cout);

        ShaderSources sources;
        sources.isCompute = paths.IsComputeShader();
        sources.hasGeometry = paths.HasGeometry();

        std::cout << "[AsyncLoader] IsCompute=" << sources.isCompute 
                  << ", HasGeometry=" << sources.hasGeometry << std::endl;
        std::flush(std::cout);

        try {
            // Read vertex shader
            if (!paths.vertex.empty())
            {
                std::string fullPath = GetShaderPath(paths.vertex);
                std::cout << "[AsyncLoader] Reading vertex: " << fullPath << std::endl;
                std::flush(std::cout);
                sources.vertex = ReadTextFile(fullPath);
                
                if (sources.vertex.empty())
                {
                    SetError("Failed to read vertex shader: " + fullPath);
                    return;
                }
                std::cout << "[AsyncLoader] ✓ Vertex shader loaded (" << sources.vertex.length() << " bytes)" << std::endl;
                std::flush(std::cout);
            }

            // Read geometry shader (optional)
            if (!paths.geometry.empty())
            {
                std::string fullPath = GetShaderPath(paths.geometry);
                std::cout << "[AsyncLoader] Reading geometry: " << fullPath << std::endl;
                std::flush(std::cout);
                sources.geometry = ReadTextFile(fullPath);
                
                if (!sources.geometry.empty())
                {
                    std::cout << "[AsyncLoader] ✓ Geometry shader loaded (" << sources.geometry.length() << " bytes)" << std::endl;
                }
                else
                {
                    std::cout << "[AsyncLoader] ⚠ Geometry shader empty (optional)" << std::endl;
                }
                std::flush(std::cout);
            }

            // Read fragment shader
            if (!paths.fragment.empty())
            {
                std::string fullPath = GetShaderPath(paths.fragment);
                std::cout << "[AsyncLoader] Reading fragment: " << fullPath << std::endl;
                std::flush(std::cout);
                sources.fragment = ReadTextFile(fullPath);
                
                if (sources.fragment.empty())
                {
                    SetError("Failed to read fragment shader: " + fullPath);
                    return;
                }
                std::cout << "[AsyncLoader] ✓ Fragment shader loaded (" << sources.fragment.length() << " bytes)" << std::endl;
                std::flush(std::cout);
            }

            // Read compute shader
            if (!paths.compute.empty())
            {
                std::string fullPath = GetShaderPath(paths.compute);
                std::cout << "[AsyncLoader] Reading compute: " << fullPath << std::endl;
                std::flush(std::cout);
                sources.compute = ReadTextFile(fullPath);
                
                if (sources.compute.empty())
                {
                    SetError("Failed to read compute shader: " + fullPath);
                    return;
                }
                std::cout << "[AsyncLoader] ✓ Compute shader loaded (" << sources.compute.length() << " bytes)" << std::endl;
                std::flush(std::cout);
            }

            // Store sources
            m_sources = sources;
            m_progress = 0.1f;
            
            std::cout << "[AsyncLoader] All files loaded, setting FILES_READY" << std::endl;
            std::flush(std::cout);

            m_state = ShaderLoadState::FILES_READY;
        }
        catch (const std::exception& e)
        {
            std::cerr << "[AsyncLoader] EXCEPTION: " << e.what() << std::endl;
            std::flush(std::cerr);
            SetError(std::string("Exception during file loading: ") + e.what());
        }
        catch (...)
        {
            std::cerr << "[AsyncLoader] UNKNOWN EXCEPTION!" << std::endl;
            std::flush(std::cerr);
            SetError("Unknown exception during file loading");
        }
    }

    // Main thread - OpenGL compilation
    void CompileShadersOnMainThread()
    {
        ShaderSources sources = m_sources;

        GLuint vertShader = 0, geomShader = 0, fragShader = 0, compShader = 0;

        if (sources.isCompute)
        {
            // Compute shader pipeline
            m_state = ShaderLoadState::COMPILING_COMPUTE;
            m_progress = 0.2f;

            std::cout << "[AsyncLoader] Compiling compute shader..." << std::endl;
            std::flush(std::cout);

            compShader = CompileShader(GL_COMPUTE_SHADER, sources.compute, "compute");
            if (compShader == 0)
                return;

            m_progress = 0.8f;
        }
        else
        {
            // Graphics pipeline
            float progressStep = sources.hasGeometry ? 0.2f : 0.3f;
            float currentProgress = 0.1f;

            // Vertex shader
            m_state = ShaderLoadState::COMPILING_VERTEX;
            currentProgress += 0.05f;
            m_progress = currentProgress;

            std::cout << "[AsyncLoader] Compiling vertex shader..." << std::endl;
            std::flush(std::cout);

            vertShader = CompileShader(GL_VERTEX_SHADER, sources.vertex, "vertex");
            if (vertShader == 0)
                return;

            currentProgress += progressStep;
            m_progress = currentProgress;

            // Geometry shader (optional)
            if (sources.hasGeometry && !sources.geometry.empty())
            {
                m_state = ShaderLoadState::COMPILING_GEOMETRY;
                currentProgress += 0.05f;
                m_progress = currentProgress;

                std::cout << "[AsyncLoader] Compiling geometry shader..." << std::endl;
                std::flush(std::cout);

                geomShader = CompileShader(GL_GEOMETRY_SHADER, sources.geometry, "geometry");
                if (geomShader == 0)
                {
                    glDeleteShader(vertShader);
                    return;
                }

                currentProgress += progressStep;
                m_progress = currentProgress;
            }

            // Fragment shader
            m_state = ShaderLoadState::COMPILING_FRAGMENT;
            currentProgress += 0.05f;
            m_progress = currentProgress;

            std::cout << "[AsyncLoader] Compiling fragment shader..." << std::endl;
            std::flush(std::cout);

            fragShader = CompileShader(GL_FRAGMENT_SHADER, sources.fragment, "fragment");
            if (fragShader == 0)
            {
                glDeleteShader(vertShader);
                if (geomShader)
                    glDeleteShader(geomShader);
                return;
            }

            m_progress = 0.8f;
        }

        // Link program
        m_state = ShaderLoadState::LINKING;
        m_progress = 0.85f;

        std::cout << "[AsyncLoader] Linking shader program..." << std::endl;
        std::flush(std::cout);

        GLuint program = glCreateProgram();

        if (compShader)
        {
            glAttachShader(program, compShader);
        }
        else
        {
            glAttachShader(program, vertShader);
            if (geomShader)
                glAttachShader(program, geomShader);
            glAttachShader(program, fragShader);
        }

        glLinkProgram(program);

        GLint success;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success)
        {
            char infoLog[1024];
            glGetProgramInfoLog(program, 1024, nullptr, infoLog);
            SetError(std::string("Program linking failed:\n") + infoLog);

            if (compShader)
                glDeleteShader(compShader);
            if (vertShader)
                glDeleteShader(vertShader);
            if (geomShader)
                glDeleteShader(geomShader);
            if (fragShader)
                glDeleteShader(fragShader);
            glDeleteProgram(program);
            return;
        }

        // Cleanup shaders
        if (compShader)
            glDeleteShader(compShader);
        if (vertShader)
            glDeleteShader(vertShader);
        if (geomShader)
            glDeleteShader(geomShader);
        if (fragShader)
            glDeleteShader(fragShader);

        m_program = program;
        m_progress = 1.0f;
        m_state = ShaderLoadState::COMPLETE;

        std::cout << "[AsyncLoader] ✓ Shader compilation complete! Program ID: " << program << std::endl;
        std::flush(std::cout);
    }

    GLuint CompileShader(GLenum type, const std::string &source, const char *typeName)
    {
        GLuint shader = glCreateShader(type);
        const char *src = source.c_str();
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);

        GLint success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            char infoLog[1024];
            glGetShaderInfoLog(shader, 1024, nullptr, infoLog);
            SetError(std::string(typeName) + " shader compilation failed:\n" + infoLog);
            glDeleteShader(shader);
            return 0;
        }

        std::cout << "[AsyncLoader] ✓ " << typeName << " shader compiled" << std::endl;
        std::flush(std::cout);
        return shader;
    }

    void SetError(const std::string &error)
    {
        m_errorMessage = error;
        m_state = ShaderLoadState::FAILED;
        m_progress = 0.0f;
        std::cerr << "[AsyncLoader] ERROR: " << error << std::endl;
        std::flush(std::cerr);
    }

    std::string ReadTextFile(const std::string &fullPath)
    {
        std::cout << "[AsyncLoader] Opening file with C API: " << fullPath << std::endl;
        std::flush(std::cout);
        
        // Use C file I/O to avoid MinGW std::ifstream issues
        FILE* file = fopen(fullPath.c_str(), "rb");
        
        if (!file)
        {
            std::cerr << "[AsyncLoader] fopen failed for: " << fullPath << std::endl;
            #ifdef _WIN32
            DWORD error = GetLastError();
            std::cerr << "[AsyncLoader] Windows error code: " << error << std::endl;
            #endif
            std::flush(std::cerr);
            return "";
        }
        
        // Get file size
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, 0, SEEK_SET);
        
        if (size <= 0)
        {
            std::cerr << "[AsyncLoader] Invalid file size: " << fullPath << std::endl;
            std::flush(std::cerr);
            fclose(file);
            return "";
        }
        
        // Read content
        std::string content;
        content.resize(size);
        
        size_t bytesRead = fread(&content[0], 1, size, file);
        fclose(file);
        
        if (bytesRead != (size_t)size)
        {
            std::cerr << "[AsyncLoader] Read size mismatch for: " << fullPath 
                      << " (expected " << size << ", got " << bytesRead << ")" << std::endl;
            std::flush(std::cerr);
            return "";
        }
        
        std::cout << "[AsyncLoader] ✓ File read successfully: " << bytesRead << " bytes" << std::endl;
        std::flush(std::cout);
        
        return content;
    }

    std::atomic<ShaderLoadState> m_state;
    std::atomic<float> m_progress;
    std::atomic<GLuint> m_program;
    std::atomic<bool> m_shouldStop;
    std::string m_errorMessage;
    std::function<void(GLuint)> m_onComplete;
    std::function<void(const std::string &)> m_onError;

    // Shader sources (no longer needs mutex since no threading)
    ShaderSources m_sources;
};

#endif // ASYNC_SHADER_LOADER_H