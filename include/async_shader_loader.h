#ifndef ASYNC_SHADER_LOADER_H
#define ASYNC_SHADER_LOADER_H

#include <glad/glad.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

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

// The legacy class and method names are retained for source compatibility.
// Loading, cache lookup, compilation, and linking are synchronous and must be
// called while the simulation OpenGL context is current.
class ShaderLoader
{
public:
    ShaderLoader() = default;
    ~ShaderLoader();

    ShaderLoader(const ShaderLoader &) = delete;
    ShaderLoader &operator=(const ShaderLoader &) = delete;
    ShaderLoader(ShaderLoader &&) = delete;
    ShaderLoader &operator=(ShaderLoader &&) = delete;

    void LoadComputeShader(
        const std::string &computePath,
        std::function<void(GLuint)> onComplete,
        std::function<void(const std::string &)> onError);

    void LoadGraphicsShader(
        const std::string &vertPath,
        const std::string &fragPath,
        const std::string &geomPath,
        std::function<void(GLuint)> onComplete,
        std::function<void(const std::string &)> onError);

    void LoadGraphicsShaderFromSource(
        const std::string &vertPath,
        const std::string &fragmentSource,
        const std::string &geomPath,
        std::function<void(GLuint)> onComplete,
        std::function<void(const std::string &)> onError);

    void Cancel();

    ShaderLoadState GetState() const { return m_state; }
    float GetProgress() const { return m_progress; }
    std::string GetStatusMessage() const;

    struct CachedBinary
    {
        GLenum format = 0;
        std::vector<unsigned char> bytes;
        bool valid = false;
    };

private:
    void LoadShader(const ShaderPaths &paths,
                    std::function<void(GLuint)> onComplete,
                    std::function<void(const std::string &)> onError,
                    const std::string *fragmentSourceOverride = nullptr);
    bool TryLoadCachedBinary(GLuint &program, const CachedBinary &cached);
    void SaveBinaryToCache(GLuint program, const std::string &cachePath);
    bool CompileStage(GLenum type, const std::string &source,
                      const std::string &stageName, GLuint &shader,
                      std::string &error);
    std::string BuildShaderName(const ShaderPaths &paths) const;

    ShaderLoadState m_state = ShaderLoadState::IDLE;
    float m_progress = 0.0f;
    std::string m_errorMessage;
    std::string m_shaderName;
    std::vector<GLuint> m_shaderObjects;
};

#endif // ASYNC_SHADER_LOADER_H
