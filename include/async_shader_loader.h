// async_shader_loader.h
#ifndef ASYNC_SHADER_LOADER_H
#define ASYNC_SHADER_LOADER_H

#include <thread>
#include <atomic>
#include <string>
#include <functional>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <cstring>
#include <glad/glad.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;

// Bounds used to reject corrupt / hostile cache and shader files.
static constexpr long MAX_SHADER_SIZE  = 16 * 1024 * 1024;   // 16 MiB
static constexpr long MAX_BINARY_SIZE  = 64 * 1024 * 1024;   // 64 MiB

// Simple hash for shader sources + driver info
static uint32_t simpleHash(const std::string& s) {
    uint32_t h = 0;
    for (char c : s) h = (h << 5) + h + static_cast<uint32_t>(c);
    return h;
}

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

// Helper to get shader path relative to module location
static std::string GetShaderPath(const std::string& shaderName) {
    static std::string shaderBasePath;
    if (shaderBasePath.empty()) {
#ifdef _WIN32
        char modulePath[1024];
        HMODULE hm = NULL;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)&GetShaderPath, &hm)) {
            GetModuleFileNameA(hm, modulePath, sizeof(modulePath));
            std::string fullPath(modulePath);
            size_t lastSlash = fullPath.find_last_of("\\/");
            if (lastSlash != std::string::npos) {
                shaderBasePath = fullPath.substr(0, lastSlash + 1) + "shaders\\";
            } else shaderBasePath = ".\\shaders\\";
        } else shaderBasePath = ".\\shaders\\";
#else
        Dl_info info;
        if (dladdr((void*)&GetShaderPath, &info)) {
            std::string fullPath(info.dli_fname);
            size_t lastSlash = fullPath.find_last_of("/");
            if (lastSlash != std::string::npos)
                shaderBasePath = fullPath.substr(0, lastSlash + 1) + "shaders/";
            else shaderBasePath = "./shaders/";
        } else shaderBasePath = "./shaders/";
#endif
    }
    return shaderBasePath + shaderName;
}

static std::string GetCacheDirectory() {
    fs::path cachePath;
#ifdef _WIN32
    const char* localAppData = getenv("LOCALAPPDATA");
    if (localAppData) cachePath = fs::path(localAppData) / "hyperstellar" / "cache";
    else cachePath = fs::current_path() / "shader_cache";
#else
    const char* home = getenv("HOME");
    if (home) cachePath = fs::path(home) / ".cache" / "hyperstellar";
    else cachePath = fs::current_path() / "shader_cache";
#endif
    fs::create_directories(cachePath);
    return cachePath.string();
}

static std::string StripUTF8BOM(const std::string& content) {
    if (content.size() >= 3 && (unsigned char)content[0] == 0xEF
        && (unsigned char)content[1] == 0xBB && (unsigned char)content[2] == 0xBF)
        return content.substr(3);
    return content;
}

class AsyncShaderLoader
{
public:
    AsyncShaderLoader()
        : m_state(ShaderLoadState::IDLE), m_progress(0.0f),
          m_program(0), m_shaderName("(none)")
    {
    }

    ~AsyncShaderLoader() {
        // Only delete if we still own the program (i.e. it was never handed
        // off to a completion callback). The handoff path exchanges
        // m_program to 0, so this is a no-op after a successful delivery.
        GLuint p = m_program.load();
        if (p != 0) glDeleteProgram(p);
    }

    void LoadComputeShaderAsync(const std::string& computePath,
        std::function<void(GLuint)> onComplete,
        std::function<void(const std::string&)> onError) {
        ShaderPaths paths;
        paths.compute = computePath;
        LoadShaderAsync(paths, onComplete, onError);
    }

    void LoadGraphicsShaderAsync(const std::string& vertPath,
        const std::string& fragPath, const std::string& geomPath,
        std::function<void(GLuint)> onComplete,
        std::function<void(const std::string&)> onError) {
        ShaderPaths paths;
        paths.vertex = vertPath;
        paths.fragment = fragPath;
        paths.geometry = geomPath;
        LoadShaderAsync(paths, onComplete, onError);
    }

    void Update() {
        if (m_state == ShaderLoadState::FILES_READY) {
            CompileShadersOnMainThread();
        }
        if (m_state == ShaderLoadState::COMPLETE && m_onComplete) {
            auto callback = std::move(m_onComplete);
            m_onComplete = nullptr;
            // Hand off ownership of the program to the caller.
            GLuint prog = m_program.exchange(0);
            m_state = ShaderLoadState::IDLE;
            callback(prog);
        } else if (m_state == ShaderLoadState::FAILED && m_onError) {
            auto callback = std::move(m_onError);
            m_onError = nullptr;
            m_state = ShaderLoadState::IDLE;
            callback(m_errorMessage);
        }
    }

    ShaderLoadState GetState() const { return m_state; }
    float GetProgress() const { return m_progress; }
    std::string GetStatusMessage() const {
        switch (m_state) {
            case ShaderLoadState::IDLE: return "Idle";
            case ShaderLoadState::READING_FILES: return "Reading shader files...";
            case ShaderLoadState::FILES_READY: return "Files loaded, ready to compile...";
            case ShaderLoadState::COMPILING_VERTEX: return "Compiling vertex shader...";
            case ShaderLoadState::COMPILING_GEOMETRY: return "Compiling geometry shader...";
            case ShaderLoadState::COMPILING_FRAGMENT: return "Compiling fragment shader...";
            case ShaderLoadState::COMPILING_COMPUTE: return "Compiling compute shader...";
            case ShaderLoadState::LINKING: return "Linking program...";
            case ShaderLoadState::COMPLETE: return "Complete!";
            case ShaderLoadState::FAILED: return "Failed: " + m_errorMessage;
            default: return "Unknown";
        }
    }

private:
    void LoadShaderAsync(const ShaderPaths& paths,
        std::function<void(GLuint)> onComplete,
        std::function<void(const std::string&)> onError) {
        // Reject double-loads. Caller should call Update() until the loader
        // returns to IDLE before submitting a new shader.
        if (m_state != ShaderLoadState::IDLE) {
            std::cerr << "[AsyncLoader] Ignoring load request — loader busy ("
                      << GetStatusMessage() << ")\n";
            return;
        }

        m_state = ShaderLoadState::READING_FILES;
        m_progress = 0.0f;
        m_onComplete = onComplete;
        m_onError = onError;
        m_program = 0;
        m_shaderName = BuildShaderName(paths);

        ShaderSources sources;
        sources.isCompute = paths.IsComputeShader();
        sources.hasGeometry = paths.HasGeometry();

        auto read = [&](const std::string& path, std::string& dest) -> bool {
            if (path.empty()) return true;
            std::string full = GetShaderPath(path);
            FILE* f = fopen(full.c_str(), "rb");
            if (!f) { SetError("Cannot open: " + full); return false; }
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            if (size < 0 || size > MAX_SHADER_SIZE) {
                fclose(f);
                SetError("Invalid shader file size for: " + full);
                return false;
            }
            fseek(f, 0, SEEK_SET);
            std::string content;
            content.resize(size);
            size_t got = fread(&content[0], 1, size, f);
            fclose(f);
            if (static_cast<long>(got) != size) {
                SetError("Short read on shader file: " + full);
                return false;
            }
            dest = StripUTF8BOM(content);
            return true;
        };

        if (!read(paths.vertex, sources.vertex)) return;
        if (!read(paths.geometry, sources.geometry)) return;
        if (!read(paths.fragment, sources.fragment)) return;
        if (!read(paths.compute, sources.compute)) return;

        m_sources = sources;
        m_progress = 0.1f;
        m_state = ShaderLoadState::FILES_READY;
    }

    std::string BuildShaderName(const ShaderPaths& paths) const {
        if (!paths.compute.empty())
            return paths.compute;
        std::string name = paths.vertex + " + " + paths.fragment;
        if (!paths.geometry.empty())
            name += " + " + paths.geometry;
        return name;
    }

    // ------------------------------------------------------------------
    // CACHE
    // ------------------------------------------------------------------
    std::string GetCacheKey() const {
        std::string all = m_sources.vertex + m_sources.geometry
                        + m_sources.fragment + m_sources.compute;
        uint32_t srcHash = simpleHash(all);
        const char* vendor = (const char*)glGetString(GL_VENDOR);
        const char* version = (const char*)glGetString(GL_VERSION);
        uint32_t vendorHash  = simpleHash(vendor  ? vendor  : "");
        uint32_t versionHash = simpleHash(version ? version : "");
        uint32_t key = srcHash ^ (vendorHash << 16) ^ versionHash;
        return std::to_string(key);
    }

    std::string GetCachePath(bool isCompute) const {
        return GetCacheDirectory() + "/"
             + (isCompute ? "compute_" : "graphics_")
             + GetCacheKey() + ".bin";
    }

    bool TryLoadCachedBinary() {
        std::string cachePath = GetCachePath(m_sources.isCompute);
        std::ifstream in(cachePath, std::ios::binary);
        if (!in) return false;

        GLsizei binaryLength = 0;
        GLenum  binaryFormat = 0;
        in.read(reinterpret_cast<char*>(&binaryLength), sizeof(binaryLength));
        in.read(reinterpret_cast<char*>(&binaryFormat), sizeof(binaryFormat));
        if (!in || binaryLength <= 0 || binaryLength > MAX_BINARY_SIZE) {
            std::cerr << "[AsyncLoader] Cache file invalid (" << cachePath
                      << "), ignoring\n";
            return false;
        }

        std::vector<unsigned char> binaryData(binaryLength);
        in.read(reinterpret_cast<char*>(binaryData.data()), binaryLength);
        if (!in) {
            std::cerr << "[AsyncLoader] Cache file truncated (" << cachePath
                      << "), ignoring\n";
            return false;
        }
        in.close();

        GLuint prog = glCreateProgram();
        if (prog == 0) {
            std::cerr << "[AsyncLoader] glCreateProgram failed while loading "
                         "cached binary\n";
            return false;
        }
        glProgramBinary(prog, binaryFormat, binaryData.data(), binaryLength);
        GLint success = GL_FALSE;
        glGetProgramiv(prog, GL_LINK_STATUS, &success);
        if (success == GL_TRUE) {
            m_program = prog;
            m_progress = 1.0f;
            m_state = ShaderLoadState::COMPLETE;
            std::cout << "[AsyncLoader] Loaded cached binary (" << m_shaderName
                      << ") from " << cachePath << std::endl;
            return true;
        }

        GLenum err = glGetError();
        std::cerr << "[AsyncLoader] glProgramBinary failed (" << m_shaderName
                  << ", GL error " << err << "), falling back to source compile\n";
        glDeleteProgram(prog);
        return false;
    }

    void SaveBinaryToCache(GLuint program) {
        GLint binaryLength = 0;
        glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &binaryLength);
        if (binaryLength <= 0 || binaryLength > MAX_BINARY_SIZE) return;

        std::vector<unsigned char> binaryData(binaryLength);
        GLenum binaryFormat = 0;
        glGetProgramBinary(program, binaryLength, &binaryLength,
                           &binaryFormat, binaryData.data());

        std::string cachePath = GetCachePath(m_sources.isCompute);
        std::ofstream out(cachePath, std::ios::binary);
        if (out) {
            out.write(reinterpret_cast<char*>(&binaryLength), sizeof(binaryLength));
            out.write(reinterpret_cast<char*>(&binaryFormat), sizeof(binaryFormat));
            out.write(reinterpret_cast<char*>(binaryData.data()), binaryLength);
            std::cout << "[AsyncLoader] Saved binary (" << m_shaderName
                      << ") to " << cachePath << std::endl;
        }
    }

    // ------------------------------------------------------------------
    // COMPILATION
    // ------------------------------------------------------------------
    void CompileShadersOnMainThread() {
        if (m_sources.isCompute && TryLoadCachedBinary()) {
            return;
        }

        GLuint vertShader = 0, geomShader = 0, fragShader = 0, compShader = 0;

        if (m_sources.isCompute) {
            m_state = ShaderLoadState::COMPILING_COMPUTE;
            m_progress = 0.2f;
            compShader = CompileShader(GL_COMPUTE_SHADER, m_sources.compute, "compute");
            if (compShader == 0) return;
            m_progress = 0.8f;
        } else {
            float step = m_sources.hasGeometry ? 0.2f : 0.3f;
            float prog = 0.2f;

            m_state = ShaderLoadState::COMPILING_VERTEX;
            vertShader = CompileShader(GL_VERTEX_SHADER, m_sources.vertex, "vertex");
            if (vertShader == 0) return;
            prog += step; m_progress = prog;

            if (m_sources.hasGeometry && !m_sources.geometry.empty()) {
                m_state = ShaderLoadState::COMPILING_GEOMETRY;
                geomShader = CompileShader(GL_GEOMETRY_SHADER, m_sources.geometry, "geometry");
                if (geomShader == 0) { glDeleteShader(vertShader); return; }
                prog += step; m_progress = prog;
            }

            m_state = ShaderLoadState::COMPILING_FRAGMENT;
            fragShader = CompileShader(GL_FRAGMENT_SHADER, m_sources.fragment, "fragment");
            if (fragShader == 0) {
                glDeleteShader(vertShader);
                if (geomShader) glDeleteShader(geomShader);
                return;
            }
            m_progress = 0.8f;
        }

        m_state = ShaderLoadState::LINKING;
        m_progress = 0.85f;
        std::cout << "[AsyncLoader] Linking shader program (" << m_shaderName
                  << ")..." << std::endl;

        GLuint program = glCreateProgram();
        if (program == 0) {
            SetError("glCreateProgram failed");
            if (compShader) glDeleteShader(compShader);
            if (vertShader) glDeleteShader(vertShader);
            if (geomShader) glDeleteShader(geomShader);
            if (fragShader) glDeleteShader(fragShader);
            return;
        }

        if (compShader) glAttachShader(program, compShader);
        else {
            glAttachShader(program, vertShader);
            if (geomShader) glAttachShader(program, geomShader);
            glAttachShader(program, fragShader);
        }
        glLinkProgram(program);
        std::cout << "[AsyncLoader] glLinkProgram returned (" << m_shaderName
                  << ")" << std::endl;

        GLint linkStatus = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
        if (!linkStatus) {
            char log[1024];
            glGetProgramInfoLog(program, 1024, nullptr, log);
            SetError(std::string("Linking failed:\n") + log);
            glDeleteProgram(program);
            if (compShader) glDeleteShader(compShader);
            if (vertShader) glDeleteShader(vertShader);
            if (geomShader) glDeleteShader(geomShader);
            if (fragShader) glDeleteShader(fragShader);
            return;
        }

        if (compShader) glDeleteShader(compShader);
        else {
            glDeleteShader(vertShader);
            if (geomShader) glDeleteShader(geomShader);
            glDeleteShader(fragShader);
        }

        m_program = program;
        m_progress = 1.0f;
        m_state = ShaderLoadState::COMPLETE;

        if (m_sources.isCompute) {
            SaveBinaryToCache(program);
        }
        std::cout << "[AsyncLoader] Shader program ready (" << m_shaderName
                  << ", ID: " << program << ")" << std::endl;
    }

    GLuint CompileShader(GLenum type, const std::string& source, const char* typeName) {
        GLuint shader = glCreateShader(type);
        if (shader == 0) {
            SetError(std::string("glCreateShader failed for ") + typeName);
            return 0;
        }
        const char* src = source.c_str();
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        GLint success = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char log[1024];
            glGetShaderInfoLog(shader, 1024, nullptr, log);
            SetError(std::string(typeName) + " shader compile error:\n" + log);
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    void SetError(const std::string& error) {
        m_errorMessage = error;
        m_state = ShaderLoadState::FAILED;
        m_progress = 0.0f;
        std::cerr << "[AsyncLoader] ERROR (" << m_shaderName << "): "
                  << error << std::endl;
    }

    std::atomic<ShaderLoadState> m_state;
    std::atomic<float> m_progress;
    std::atomic<GLuint> m_program;
    std::string m_errorMessage;
    std::string m_shaderName;
    std::function<void(GLuint)> m_onComplete;
    std::function<void(const std::string&)> m_onError;
    ShaderSources m_sources;
};

#endif // ASYNC_SHADER_LOADER_H