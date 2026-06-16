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

// Helper to get shader path relative to module location (unchanged)
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
    if (content.size() >= 3 && (unsigned char)content[0] == 0xEF && (unsigned char)content[1] == 0xBB && (unsigned char)content[2] == 0xBF)
        return content.substr(3);
    return content;
}

class AsyncShaderLoader
{
public:
    AsyncShaderLoader()
        : m_state(ShaderLoadState::IDLE), m_progress(0.0f), m_program(0), m_shouldStop(false)
    {
    }

    ~AsyncShaderLoader() {
        if (m_program != 0) glDeleteProgram(m_program);
    }

    void LoadComputeShaderAsync(const std::string& computePath,
        std::function<void(GLuint)> onComplete,
        std::function<void(const std::string&)> onError) {
        ShaderPaths paths;
        paths.compute = computePath;
        LoadShaderAsync(paths, onComplete, onError);
    }

    void LoadGraphicsShaderAsync(const std::string& vertPath, const std::string& fragPath,
        const std::string& geomPath, std::function<void(GLuint)> onComplete,
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
            m_state = ShaderLoadState::IDLE;
            callback(m_program);
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
        if (m_state != ShaderLoadState::IDLE && m_state != ShaderLoadState::COMPLETE) return;

        m_state = ShaderLoadState::READING_FILES;
        m_progress = 0.0f;
        m_onComplete = onComplete;
        m_onError = onError;
        m_program = 0;

        // Read files on main thread
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
            fseek(f, 0, SEEK_SET);
            std::string content;
            content.resize(size);
            fread(&content[0], 1, size, f);
            fclose(f);
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

    // ------------------------------------------------------------------
    // CACHE METHODS
    // ------------------------------------------------------------------
    std::string GetCacheKey() const {
        // Combine all shader sources + driver info
        std::string all = m_sources.vertex + m_sources.geometry + m_sources.fragment + m_sources.compute;
        uint32_t srcHash = simpleHash(all);
        const char* vendor = (const char*)glGetString(GL_VENDOR);
        const char* version = (const char*)glGetString(GL_VERSION);
        uint32_t vendorHash = simpleHash(vendor ? vendor : "");
        uint32_t versionHash = simpleHash(version ? version : "");
        uint32_t key = srcHash ^ (vendorHash << 16) ^ versionHash;
        return std::to_string(key);
    }

    std::string GetCachePath() const {
        return GetCacheDirectory() + "/compute_" + GetCacheKey() + ".bin";
    }

    bool TryLoadCachedBinary() {
        std::string cachePath = GetCachePath();
        std::ifstream in(cachePath, std::ios::binary);
        if (!in) return false;

        GLsizei binaryLength;
        GLenum binaryFormat;
        in.read(reinterpret_cast<char*>(&binaryLength), sizeof(binaryLength));
        in.read(reinterpret_cast<char*>(&binaryFormat), sizeof(binaryFormat));
        std::vector<unsigned char> binaryData(binaryLength);
        in.read(reinterpret_cast<char*>(binaryData.data()), binaryLength);
        in.close();

        GLuint prog = glCreateProgram();
        glProgramBinary(prog, binaryFormat, binaryData.data(), binaryLength);
        GLint success;
        glGetProgramiv(prog, GL_LINK_STATUS, &success);
        if (success == GL_TRUE) {
            m_program = prog;
            m_progress = 1.0f;
            m_state = ShaderLoadState::COMPLETE;
            std::cout << "[AsyncLoader] Loaded cached binary from " << cachePath << std::endl;
            return true;
        }
        glDeleteProgram(prog);
        return false;
    }

    void SaveBinaryToCache(GLuint program) {
        GLint binaryLength = 0;
        glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &binaryLength);
        if (binaryLength <= 0) return;

        std::vector<unsigned char> binaryData(binaryLength);
        GLenum binaryFormat = 0;
        glGetProgramBinary(program, binaryLength, &binaryLength, &binaryFormat, binaryData.data());

        std::string cachePath = GetCachePath();
        std::ofstream out(cachePath, std::ios::binary);
        if (out) {
            out.write(reinterpret_cast<char*>(&binaryLength), sizeof(binaryLength));
            out.write(reinterpret_cast<char*>(&binaryFormat), sizeof(binaryFormat));
            out.write(reinterpret_cast<char*>(binaryData.data()), binaryLength);
            std::cout << "[AsyncLoader] Saved binary to " << cachePath << std::endl;
        }
    }

    // ------------------------------------------------------------------
    // COMPILATION (with cache attempt)
    // ------------------------------------------------------------------
    void CompileShadersOnMainThread() {
        // Try to load from cache first (only for compute shader)
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
            // Graphics pipeline (vertex, geometry, fragment)
            float step = m_sources.hasGeometry ? 0.2f : 0.3f;
            float prog = 0.2f;
            m_state = ShaderLoadState::COMPILING_VERTEX;
            vertShader = CompileShader(GL_VERTEX_SHADER, m_sources.vertex, "vertex");
            if (vertShader == 0) return;
            prog += step;
            m_progress = prog;

            if (m_sources.hasGeometry && !m_sources.geometry.empty()) {
                m_state = ShaderLoadState::COMPILING_GEOMETRY;
                geomShader = CompileShader(GL_GEOMETRY_SHADER, m_sources.geometry, "geometry");
                if (geomShader == 0) { glDeleteShader(vertShader); return; }
                prog += step;
                m_progress = prog;
            }

            m_state = ShaderLoadState::COMPILING_FRAGMENT;
            fragShader = CompileShader(GL_FRAGMENT_SHADER, m_sources.fragment, "fragment");
            if (fragShader == 0) { glDeleteShader(vertShader); if (geomShader) glDeleteShader(geomShader); return; }
            m_progress = 0.8f;
        }

        m_state = ShaderLoadState::LINKING;
        m_progress = 0.85f;
        std::cout << "[AsyncLoader] Linking shader program..." << std::endl;

        GLuint program = glCreateProgram();
        if (compShader) glAttachShader(program, compShader);
        else {
            glAttachShader(program, vertShader);
            if (geomShader) glAttachShader(program, geomShader);
            glAttachShader(program, fragShader);
        }
        glLinkProgram(program);

        GLint linkStatus;
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

        // Cleanup shader objects
        if (compShader) glDeleteShader(compShader);
        else {
            glDeleteShader(vertShader);
            if (geomShader) glDeleteShader(geomShader);
            glDeleteShader(fragShader);
        }

        m_program = program;
        m_progress = 1.0f;
        m_state = ShaderLoadState::COMPLETE;

        // Save binary to cache (only for compute shader)
        if (m_sources.isCompute) {
            SaveBinaryToCache(program);
        }
        std::cout << "[AsyncLoader] ✓ Shader program ready (ID: " << program << ")" << std::endl;
    }

    GLuint CompileShader(GLenum type, const std::string& source, const char* typeName) {
        GLuint shader = glCreateShader(type);
        const char* src = source.c_str();
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        GLint success;
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
        std::cerr << "[AsyncLoader] ERROR: " << error << std::endl;
    }

    std::atomic<ShaderLoadState> m_state;
    std::atomic<float> m_progress;
    std::atomic<GLuint> m_program;
    std::atomic<bool> m_shouldStop;
    std::string m_errorMessage;
    std::function<void(GLuint)> m_onComplete;
    std::function<void(const std::string&)> m_onError;
    ShaderSources m_sources;
};

#endif // ASYNC_SHADER_LOADER_H