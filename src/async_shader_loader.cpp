#include "async_shader_loader.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#ifndef GL_PROGRAM_BINARY_RETRIEVABLE_HINT
#define GL_PROGRAM_BINARY_RETRIEVABLE_HINT 0x8257
#endif

namespace
{
namespace fs = std::filesystem;
constexpr std::streamoff MAX_SHADER_SIZE = 16LL * 1024LL * 1024LL;
constexpr GLsizei MAX_BINARY_SIZE = 64 * 1024 * 1024;

uint32_t hashText(const std::string &value)
{
    uint32_t hash = 0;
    for (char c : value)
        hash = (hash << 5) + hash + static_cast<uint32_t>(c);
    return hash;
}

std::string shaderDirectory()
{
#ifdef _WIN32
    char modulePath[2048] = {};
    HMODULE module = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&shaderDirectory), &module) &&
        GetModuleFileNameA(module, modulePath, sizeof(modulePath)) > 0)
    {
        fs::path moduleFile(modulePath);
        return (moduleFile.parent_path() / "shaders").string();
    }
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<void *>(&shaderDirectory), &info) && info.dli_fname)
        return (fs::path(info.dli_fname).parent_path() / "shaders").string();
#endif
    return (fs::current_path() / "shaders").string();
}

std::string cacheDirectory()
{
#ifdef _WIN32
    const char *root = std::getenv("LOCALAPPDATA");
    fs::path path = root ? fs::path(root) / "hyperstellar" / "cache"
                         : fs::current_path() / "shader_cache";
#else
    const char *root = std::getenv("HOME");
    fs::path path = root ? fs::path(root) / ".cache" / "hyperstellar"
                         : fs::current_path() / "shader_cache";
#endif
    std::error_code ec;
    fs::create_directories(path, ec);
    return path.string();
}

bool readShader(const std::string &name, std::string &source, std::string &error)
{
    if (name.empty())
        return true;

    const fs::path path = fs::path(shaderDirectory()) / name;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        error = "Cannot open shader file: " + path.string();
        return false;
    }

    const std::streamoff size = file.tellg();
    if (size < 0 || size > MAX_SHADER_SIZE)
    {
        error = "Invalid shader file size: " + path.string();
        return false;
    }

    source.resize(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if (size > 0)
        file.read(source.data(), size);
    if (!file && size > 0)
    {
        error = "Could not read shader file: " + path.string();
        return false;
    }

    if (source.size() >= 3 &&
        static_cast<unsigned char>(source[0]) == 0xEF &&
        static_cast<unsigned char>(source[1]) == 0xBB &&
        static_cast<unsigned char>(source[2]) == 0xBF)
        source.erase(0, 3);
    return true;
}

std::string makeCachePath(const ShaderSources &sources)
{
    const auto *vendorBytes = glGetString(GL_VENDOR);
    const auto *versionBytes = glGetString(GL_VERSION);
    const std::string vendor = vendorBytes ? reinterpret_cast<const char *>(vendorBytes) : "";
    const std::string version = versionBytes ? reinterpret_cast<const char *>(versionBytes) : "";
    const std::string allSources = sources.vertex + sources.geometry +
                                   sources.fragment + sources.compute;
    const uint32_t key = hashText(allSources) ^ (hashText(vendor) << 16) ^ hashText(version);
    const std::string prefix = sources.isCompute ? "compute_" : "graphics_";
    return (fs::path(cacheDirectory()) / (prefix + std::to_string(key) + ".bin")).string();
}

ShaderLoader::CachedBinary readCachedBinary(const std::string &path)
{
    ShaderLoader::CachedBinary result;
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return result;

    GLsizei length = 0;
    GLenum format = 0;
    in.read(reinterpret_cast<char *>(&length), sizeof(length));
    in.read(reinterpret_cast<char *>(&format), sizeof(format));
    if (!in || length <= 0 || length > MAX_BINARY_SIZE)
        return result;

    result.bytes.resize(static_cast<size_t>(length));
    in.read(reinterpret_cast<char *>(result.bytes.data()), length);
    result.valid = static_cast<bool>(in);
    result.format = format;
    if (!result.valid)
        result.bytes.clear();
    return result;
}
} // namespace

ShaderLoader::~ShaderLoader()
{
    Cancel();
}

void ShaderLoader::LoadComputeShader(
    const std::string &computePath,
    std::function<void(GLuint)> onComplete,
    std::function<void(const std::string &)> onError)
{
    ShaderPaths paths;
    paths.compute = computePath;
    LoadShader(paths, std::move(onComplete), std::move(onError));
}

void ShaderLoader::LoadGraphicsShader(
    const std::string &vertPath,
    const std::string &fragPath,
    const std::string &geomPath,
    std::function<void(GLuint)> onComplete,
    std::function<void(const std::string &)> onError)
{
    ShaderPaths paths;
    paths.vertex = vertPath;
    paths.fragment = fragPath;
    paths.geometry = geomPath;
    LoadShader(paths, std::move(onComplete), std::move(onError));
}

void ShaderLoader::LoadGraphicsShaderFromSource(
    const std::string &vertPath,
    const std::string &fragmentSource,
    const std::string &geomPath,
    std::function<void(GLuint)> onComplete,
    std::function<void(const std::string &)> onError)
{
    ShaderPaths paths;
    paths.vertex = vertPath;
    paths.geometry = geomPath;
    paths.fragment = "<generated-material-fragment>";
    LoadShader(paths, std::move(onComplete), std::move(onError), &fragmentSource);
}

std::string ShaderLoader::BuildShaderName(const ShaderPaths &paths) const
{
    if (!paths.compute.empty())
        return paths.compute;
    std::string name = paths.vertex + " + " + paths.fragment;
    if (!paths.geometry.empty())
        name += " + " + paths.geometry;
    return name;
}

bool ShaderLoader::CompileStage(GLenum type, const std::string &source,
                                     const std::string &stageName, GLuint &shader,
                                     std::string &error)
{
    if (source.empty())
        return true;

    m_state = type == GL_COMPUTE_SHADER ? ShaderLoadState::COMPILING_COMPUTE :
              type == GL_VERTEX_SHADER ? ShaderLoadState::COMPILING_VERTEX :
              type == GL_GEOMETRY_SHADER ? ShaderLoadState::COMPILING_GEOMETRY :
                                           ShaderLoadState::COMPILING_FRAGMENT;

    shader = glCreateShader(type);
    if (!shader)
    {
        error = "glCreateShader failed for " + stageName;
        return false;
    }

    const char *text = source.c_str();
    const GLint length = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &text, &length);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE)
    {
        GLint logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(static_cast<size_t>(std::max(1, logLength)), '\0');
        glGetShaderInfoLog(shader, logLength, nullptr, log.data());
        error = stageName + " shader compile error:\n" + log;
        glDeleteShader(shader);
        shader = 0;
        return false;
    }

    m_shaderObjects.push_back(shader);
    return true;
}

bool ShaderLoader::TryLoadCachedBinary(GLuint &program, const CachedBinary &cached)
{
    if (!cached.valid || cached.bytes.empty())
        return false;

    GLint binaryFormats = 0;
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &binaryFormats);
    if (binaryFormats <= 0)
        return false;

    program = glCreateProgram();
    if (!program)
        return false;

    glProgramBinary(program, cached.format, cached.bytes.data(),
                    static_cast<GLsizei>(cached.bytes.size()));
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_TRUE)
        return true;

    glDeleteProgram(program);
    program = 0;
    return false;
}

void ShaderLoader::SaveBinaryToCache(GLuint program, const std::string &path)
{
    GLint binaryFormats = 0;
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &binaryFormats);
    if (binaryFormats <= 0)
        return;

    GLint length = 0;
    glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &length);
    if (length <= 0 || length > MAX_BINARY_SIZE)
        return;

    std::vector<unsigned char> bytes(static_cast<size_t>(length));
    GLenum format = 0;
    GLsizei written = 0;
    glGetProgramBinary(program, length, &written, &format, bytes.data());
    if (written <= 0)
        return;
    bytes.resize(static_cast<size_t>(written));

    const fs::path destination(path);
    const fs::path temporary = destination.string() + ".tmp";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out)
        return;
    const GLsizei binaryLength = static_cast<GLsizei>(bytes.size());
    out.write(reinterpret_cast<const char *>(&binaryLength), sizeof(binaryLength));
    out.write(reinterpret_cast<const char *>(&format), sizeof(format));
    out.write(reinterpret_cast<const char *>(bytes.data()), binaryLength);
    out.close();

    std::error_code ec;
    fs::rename(temporary, destination, ec);
    if (ec)
    {
        fs::remove(destination, ec);
        ec.clear();
        fs::rename(temporary, destination, ec);
        if (ec)
            fs::remove(temporary, ec);
    }
}

void ShaderLoader::LoadShader(
    const ShaderPaths &paths,
    std::function<void(GLuint)> onComplete,
    std::function<void(const std::string &)> onError,
    const std::string *fragmentSourceOverride)
{
    if (m_state != ShaderLoadState::IDLE)
    {
        if (onError)
            onError("Shader loader is busy: " + GetStatusMessage());
        return;
    }

    m_shaderName = BuildShaderName(paths);
    m_errorMessage.clear();
    m_progress = 0.0f;
    m_state = ShaderLoadState::READING_FILES;

    if (!glGetString(GL_VERSION))
    {
        m_state = ShaderLoadState::FAILED;
        m_errorMessage = "No current OpenGL context";
        if (onError)
            onError(m_errorMessage);
        m_state = ShaderLoadState::IDLE;
        return;
    }

    ShaderSources sources;
    sources.isCompute = paths.IsComputeShader();
    sources.hasGeometry = paths.HasGeometry();

    std::string error;
    bool sourcesReady = readShader(paths.vertex, sources.vertex, error) &&
                        readShader(paths.geometry, sources.geometry, error);
    if (sourcesReady)
    {
        if (fragmentSourceOverride)
            sources.fragment = *fragmentSourceOverride;
        else
            sourcesReady = readShader(paths.fragment, sources.fragment, error);
    }
    if (sourcesReady)
        sourcesReady = readShader(paths.compute, sources.compute, error);

    if (!sourcesReady)
    {
        m_state = ShaderLoadState::FAILED;
        m_errorMessage = error;
        std::cerr << "[ShaderLoader] " << error << std::endl;
        if (onError)
            onError(error);
        m_state = ShaderLoadState::IDLE;
        return;
    }
    m_progress = 0.15f;

    const std::string cachePath = makeCachePath(sources);
    const CachedBinary cached = readCachedBinary(cachePath);
    GLuint program = 0;
    if (TryLoadCachedBinary(program, cached))
    {
        m_progress = 1.0f;
        m_state = ShaderLoadState::COMPLETE;
        std::cout << "[ShaderLoader] Loaded cached program: " << m_shaderName << std::endl;
        if (onComplete)
            onComplete(program);
        m_state = ShaderLoadState::IDLE;
        m_progress = 1.0f;
        return;
    }

    m_shaderObjects.clear();
    bool success = true;
    if (sources.isCompute)
    {
        GLuint shader = 0;
        success = CompileStage(GL_COMPUTE_SHADER, sources.compute, "compute", shader, error);
    }
    else
    {
        GLuint shader = 0;
        success = CompileStage(GL_VERTEX_SHADER, sources.vertex, "vertex", shader, error);
        if (success && sources.hasGeometry)
            success = CompileStage(GL_GEOMETRY_SHADER, sources.geometry, "geometry", shader, error);
        if (success)
            success = CompileStage(GL_FRAGMENT_SHADER, sources.fragment, "fragment", shader, error);
    }

    if (!success)
    {
        for (GLuint shader : m_shaderObjects)
            glDeleteShader(shader);
        m_shaderObjects.clear();
        m_state = ShaderLoadState::FAILED;
        m_errorMessage = error;
        std::cerr << "[ShaderLoader] " << error << std::endl;
        if (onError)
            onError(error);
        m_state = ShaderLoadState::IDLE;
        return;
    }

    m_progress = 0.75f;
    m_state = ShaderLoadState::LINKING;
    program = glCreateProgram();
    if (!program)
        error = "glCreateProgram failed";

    if (program)
    {
        glProgramParameteri(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
        for (GLuint shader : m_shaderObjects)
            glAttachShader(program, shader);
        glLinkProgram(program);

        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE)
        {
            GLint logLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
            std::string log(static_cast<size_t>(std::max(1, logLength)), '\0');
            glGetProgramInfoLog(program, logLength, nullptr, log.data());
            error = "Program link error:\n" + log;
        }
    }

    for (GLuint shader : m_shaderObjects)
    {
        if (program)
            glDetachShader(program, shader);
        glDeleteShader(shader);
    }
    m_shaderObjects.clear();

    if (!error.empty())
    {
        if (program)
            glDeleteProgram(program);
        m_state = ShaderLoadState::FAILED;
        m_errorMessage = error;
        std::cerr << "[ShaderLoader] " << error << std::endl;
        if (onError)
            onError(error);
        m_state = ShaderLoadState::IDLE;
        return;
    }

    SaveBinaryToCache(program, cachePath);
    m_progress = 1.0f;
    m_state = ShaderLoadState::COMPLETE;
    std::cout << "[ShaderLoader] Compiled and linked: " << m_shaderName << std::endl;
    if (onComplete)
        onComplete(program);
    else
        glDeleteProgram(program);
    m_state = ShaderLoadState::IDLE;
}

void ShaderLoader::Cancel()
{
    for (GLuint shader : m_shaderObjects)
        if (shader)
            glDeleteShader(shader);
    m_shaderObjects.clear();
    m_state = ShaderLoadState::IDLE;
    m_progress = 0.0f;
}

std::string ShaderLoader::GetStatusMessage() const
{
    switch (m_state)
    {
    case ShaderLoadState::IDLE: return "Idle";
    case ShaderLoadState::READING_FILES: return "Reading shader files...";
    case ShaderLoadState::FILES_READY: return "Shader files ready";
    case ShaderLoadState::COMPILING_VERTEX: return "Compiling vertex shader...";
    case ShaderLoadState::COMPILING_GEOMETRY: return "Compiling geometry shader...";
    case ShaderLoadState::COMPILING_FRAGMENT: return "Compiling fragment shader...";
    case ShaderLoadState::COMPILING_COMPUTE: return "Compiling compute shader...";
    case ShaderLoadState::LINKING: return "Linking program...";
    case ShaderLoadState::COMPLETE: return "Complete";
    case ShaderLoadState::FAILED: return "Failed: " + m_errorMessage;
    default: return "Unknown";
    }
}
