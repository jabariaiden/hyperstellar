#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "objects.h"
#include "shader_utils.h"
#include "../include/globals.h"
#include "utils.h"
#include "debug_helpers.h"
#include "gpu_serializer.h"
#include "constraints.h"
#include "async_shader_loader.h"
#include "script_manager.h"
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <map>
#include <atomic>
#include <cstdint>   // for uint32_t
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <memory>

// ============================================================================
// SCRATCHPAD AND SIGNAL QUEUE – new architecture
// ============================================================================

// Scratchpad storage
struct Scratchpad {
    GLuint buffer = 0;
    size_t numElements = 0;
    size_t offsetInPool = 0; // offset within the big pool (in floats)
    bool valid = false;
};

static std::unordered_map<int, Scratchpad> g_scratchpads;
static int g_nextScratchpadId = 0;
static GLuint g_scratchpadPool = 0;      // single large SSBO
static size_t g_scratchpadPoolSize = 0;  // total allocated size (in bytes)
static bool g_scratchpadPoolDirty = false;
static std::queue<int> g_freeScratchpadIds;   // IDs available for reuse
static std::vector<std::pair<size_t, size_t>> g_freeBlocks; // offset (bytes), size (bytes)


// Maximum size of the scratchpad pool (64 MB)
static const size_t MAX_SCRATCHPAD_POOL_SIZE = 64 * 1024 * 1024;

// Signal queue
static GLuint g_signalQueueBuffer = 0;
static size_t g_signalQueueCapacity = 1024;
static int g_signalQueueOverflowPolicy = 0; // 0=drop, 1=block
static size_t g_signalQueueCount = 0; // read from GPU

// Scratchpad binding point (consistent with shader)
static const GLuint SCRATCHPAD_BINDING = 10;
static const GLuint SIGNAL_QUEUE_BINDING = 11;

// ============================================================================
// EXISTING STATIC VARIABLES
// ============================================================================

// Static buffer IDs for double-buffered object data
static GLuint g_objectSSBO[2] = {0, 0};
static GLuint g_renderVAO[2] = {0, 0};
static GLuint g_programCompute = 0;
static GLuint g_programQuad = 0;

// Paint double‑buffering (replaces single texture)
static GLuint g_paintTexture[2] = {0, 0}; // two textures for double‑buffering
static int g_paintWriteIdx = 0;           // currently writing to this texture index
static int g_paintReadIdx = 1;            // currently reading from this texture index
static int g_paintScriptID = -1;

// Uniform location for the previous frame sampler
static GLuint uPrevFrameLoc = 0;

// Paint shader resources
int g_paintWidth = 0;
int g_paintHeight = 0;

// Paint shader and related resources
static AsyncShaderLoader g_paintLoader;
static bool g_paintShaderReady = false;

// Equation and constraint storage buffers
static GLuint g_allTokensSSBO = 0;
static GLuint g_allConstantsSSBO = 0;
static GLuint g_mappingsSSBO = 0;
static GLuint g_initialPosSSBO = 0;
static GLuint g_constraintsSSBO = 0;
static GLuint g_objectConstraintsSSBO = 0;

// Collision system buffers
static GLuint g_collisionPropsSSBO = 0;
static GLuint g_contactBufferSSBO = 0; //  Contact persistence buffer
static std::vector<CollisionProperties> g_collisionProperties(Objects::MAX_OBJECTS);
static std::vector<std::vector<bool>> g_collisionMatrix(Objects::MAX_OBJECTS,
                                                        std::vector<bool>(Objects::MAX_OBJECTS, true));
                                                        
//  Collision system parameters
static bool g_enableWarmStart = false;
static int g_maxContactIterations = 3;
static bool g_useAnalyticalCollision = true; // Use analytical elastic collisions

// Object count and data storage
static int g_numObjects = 0;
float g_simulationTime = 0.0f;
static std::vector<int> g_allTokens;
static std::vector<float> g_allConstants;
static std::vector<EquationMapping> g_equationMappings(Objects::MAX_EQUATIONS);
static std::unordered_map<std::string, int> g_equationStringToID;

// Constraint management
static std::vector<Constraint> g_allConstraints;
static std::vector<ObjectConstraints> g_objectConstraintMappings(Objects::MAX_OBJECTS);

// Default system parameters
static int g_currentDefaultObjectType = SKIN_CIRCLE;
static float g_currentSystemGravity = 9.81f;
static float g_currentSystemDamping = 0.1f;
static float g_currentSystemStiffness = 1.0f;

// Memory mapping for direct CPU access
static Object *g_mappedSSBO[2] = {nullptr, nullptr};
static bool g_useMapBuffer = true;

// Async shader loading
static AsyncShaderLoader g_computeLoader;
static AsyncShaderLoader g_quadLoader;
static bool g_computeShaderReady = false;
static bool g_quadShaderReady = false;

static GLuint g_paintShaderProgram = 0;
static GLuint g_paintTokenSSBO = 0;
static GLuint g_paintConstSSBO = 0;
static GLuint g_paintQuadVAO = 0;
static GLuint g_paintQuadVBO = 0;
static GLuint g_paintQuadProgram = 0;
static bool g_hasPaintEquation = false;
static std::vector<int> g_paintTokens_r, g_paintTokens_g, g_paintTokens_b, g_paintTokens_a;
static std::vector<float> g_paintConsts_r, g_paintConsts_g, g_paintConsts_b, g_paintConsts_a;

static std::vector<int> g_scriptIDs(Objects::MAX_OBJECTS, -1);
static std::unordered_map<int, std::vector<int>> g_scriptGroups;
static bool g_groupsDirty = true;
static ScriptManager *g_scriptManager = nullptr;

// Static index SSBO for group dispatch (reused each frame)
static GLuint g_indexSSBO = 0;

// Paint camera tracking for pan/zoom compensation
static glm::vec2 g_prevCamPos(0.0f, 0.0f);
static float    g_prevZoom = 10.0f;   // default half‑height
static bool     g_firstPaintFrame = true;

// current read buffer index for agent dispatch
static int g_currentReadBufferIndex = 0;

// I am sorry for my sloppy code
GLuint Objects::GetContactBuffer() { return g_contactBufferSSBO; }
// ============================================================================
// Persistent JPEG worker (off‑thread compression)
// ============================================================================
struct FrameBuffer {
    int width;
    int height;
    int quality;
    std::vector<unsigned char> pixels;
};

class JpegWorker {
public:
    JpegWorker() : running_(true) {
        worker_ = std::thread(&JpegWorker::run, this);
    }
    ~JpegWorker() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = false;
        }
        cv_.notify_one();
        if (worker_.joinable()) worker_.join();
    }

    void pushFrame(int w, int h, int quality, std::vector<unsigned char>&& pixels) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.size() >= 1) {
            queue_.pop();   // drop old, keep only the newest
        }
        queue_.emplace(FrameBuffer{w, h, quality, std::move(pixels)});
        cv_.notify_one();
    }

    std::shared_ptr<const std::vector<unsigned char>> getLatestJpeg() const {
        std::lock_guard<std::mutex> lock(resultMutex_);
        return latestJpeg_;
    }

private:
    void run() {
        while (true) {
            FrameBuffer fb;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return !running_ || !queue_.empty(); });
                if (!running_ && queue_.empty()) break;
                if (!queue_.empty()) {
                    fb = std::move(queue_.front());
                    queue_.pop();
                } else {
                    continue;
                }
            }
            // Compress to JPEG
            std::vector<unsigned char> compressed;
            auto write_func = [](void* context, void* data, int size) {
                std::vector<unsigned char>* vec = static_cast<std::vector<unsigned char>*>(context);
                vec->insert(vec->end(), static_cast<unsigned char*>(data), static_cast<unsigned char*>(data) + size);
            };
            stbi_write_jpg_to_func(write_func, &compressed,
                                   fb.width, fb.height, 4,
                                   fb.pixels.data(), fb.quality);
            // Store result
            {
                std::lock_guard<std::mutex> lock(resultMutex_);
                latestJpeg_ = std::make_shared<const std::vector<unsigned char>>(std::move(compressed));
            }
        }
    }

    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<FrameBuffer> queue_;
    bool running_;

    mutable std::mutex resultMutex_;
    std::shared_ptr<const std::vector<unsigned char>> latestJpeg_;
};

static std::unique_ptr<JpegWorker> g_jpegWorker;
static std::once_flag g_workerInitFlag;

// Objects namespace – main physics object manager

// ============================================================================
// HELPER FUNCTIONS 
// ============================================================================
GLuint Objects::GetPaintTexture(int &width, int &height) {
    width = g_paintWidth ? g_paintWidth : g_width;
    height = g_paintHeight ? g_paintHeight : g_height;
    return g_paintTexture[g_paintReadIdx];
}

void Objects::GetPaintImage(std::vector<unsigned char> &jpeg_data, int quality) {
    int width, height;
    GLuint tex = GetPaintTexture(width, height);
    if (!tex) return;

    glBindTexture(GL_TEXTURE_2D, tex);
    std::vector<unsigned char> pixels(width * height * 4);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    auto write_func = [](void* context, void* data, int size) {
        std::vector<unsigned char>* vec = (std::vector<unsigned char>*)context;
        vec->insert(vec->end(), (unsigned char*)data, (unsigned char*)data + size);
    };
    stbi_write_jpg_to_func(write_func, &jpeg_data, width, height, 4, pixels.data(), quality);
}

// Main capture function
void Objects::GetFullFrameImage(std::vector<unsigned char> &jpeg_data,
                                 int quality,
                                 int objectBufferIndex,
                                 const glm::mat4 &projView) {
    int width = g_paintWidth > 0 ? g_paintWidth : g_width;
    int height = g_paintHeight > 0 ? g_paintHeight : g_height;

    static GLuint fbo = 0, tex = 0, rbo = 0;
    static GLuint pbo[3] = {0, 0, 0};
    static GLsync fences[3] = {0, 0, 0};
    static int currentPBO = 0;
    static int lastWidth = 0, lastHeight = 0;
    static bool havePreviousFrame = false;
    static GLenum fboStatus = GL_FRAMEBUFFER_COMPLETE;
    static bool firstFrame = true;

    static GLint paintQuad_uTex = -1;
    static GLint quadProg_uProjection = -1;
    static GLint quadProg_uView = -1;
    static bool uniformsCached = false;
    if (!uniformsCached) {
        paintQuad_uTex = glGetUniformLocation(g_paintQuadProgram, "uTex");
        quadProg_uProjection = glGetUniformLocation(g_programQuad, "uProjection");
        quadProg_uView = glGetUniformLocation(g_programQuad, "uView");
        uniformsCached = true;
    }

    if (fbo == 0) {
        glGenFramebuffers(1, &fbo);
        glGenTextures(1, &tex);
        glGenRenderbuffers(1, &rbo);
        glGenBuffers(3, pbo);
    }

    if (lastWidth != width || lastHeight != height) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);

        glBindRenderbuffer(GL_RENDERBUFFER, rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);

        size_t size = width * height * 4;
        for (int i = 0; i < 3; ++i) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[i]);
            glBufferData(GL_PIXEL_PACK_BUFFER, size, nullptr, GL_STREAM_READ);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);
        fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        lastWidth = width;
        lastHeight = height;
        firstFrame = true;
    }

    if (fboStatus != GL_FRAMEBUFFER_COMPLETE) {
        jpeg_data.clear();
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);

    GLint oldVp[4];
    glGetIntegerv(GL_VIEWPORT, oldVp);
    glViewport(0, 0, width, height);

    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (g_paintShaderReady || g_paintScriptID >= 0) {
        DispatchPaint(width, height, g_camera.position.x, g_camera.position.y, g_camera.zoom, objectBufferIndex);

        glUseProgram(g_paintQuadProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_paintTexture[g_paintReadIdx]);
        if (paintQuad_uTex != -1) glUniform1i(paintQuad_uTex, 0);
        glBindVertexArray(g_paintQuadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glUseProgram(0);
    }

    if (g_programQuad && g_quadShaderReady) {
        glUseProgram(g_programQuad);
        if (quadProg_uProjection != -1) glUniformMatrix4fv(quadProg_uProjection, 1, GL_FALSE, glm::value_ptr(projView));
        if (quadProg_uView != -1) glUniformMatrix4fv(quadProg_uView, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
        glBindVertexArray(g_renderVAO[objectBufferIndex]);
        glDrawArrays(GL_POINTS, 0, g_numObjects);
        glBindVertexArray(0);
        glUseProgram(0);
    }

    std::call_once(g_workerInitFlag, []() {
        g_jpegWorker = std::make_unique<JpegWorker>();
    });

    if (firstFrame) {
        std::vector<unsigned char> pixels(width * height * 4);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

        std::vector<unsigned char> flipped(width * height * 4);
        for (int y = 0; y < height; ++y) {
            memcpy(&flipped[y * width * 4],
                   &pixels[(height - 1 - y) * width * 4],
                   width * 4);
        }
        g_jpegWorker->pushFrame(width, height, quality, std::move(flipped));

        int nextPBO = (currentPBO + 1) % 3;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[nextPBO]);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        fences[nextPBO] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        currentPBO = nextPBO;
        havePreviousFrame = true;
        firstFrame = false;

        auto latest = g_jpegWorker->getLatestJpeg();
        if (latest) jpeg_data = *latest;
        else jpeg_data.clear();
        return;
    }

    int nextPBO = (currentPBO + 1) % 3;
    bool canRead = true;
    if (fences[nextPBO] != 0) {
        GLenum status = glClientWaitSync(fences[nextPBO], 0, 0);
        if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED)
            canRead = false;
    }

    if (canRead) {
        if (fences[nextPBO] != 0) {
            glDeleteSync(fences[nextPBO]);
            fences[nextPBO] = 0;
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[nextPBO]);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        fences[nextPBO] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    }

    if (havePreviousFrame && fences[currentPBO] != 0) {
        GLenum result = glClientWaitSync(fences[currentPBO], 0, 0);
        if (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[currentPBO]);
            void* data = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
            if (data) {
                std::vector<unsigned char> flipped(width * height * 4);
                for (int y = 0; y < height; ++y) {
                    memcpy(&flipped[y * width * 4],
                           static_cast<unsigned char*>(data) + (height - 1 - y) * width * 4,
                           width * 4);
                }
                glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
                g_jpegWorker->pushFrame(width, height, quality, std::move(flipped));
            }
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            glDeleteSync(fences[currentPBO]);
            fences[currentPBO] = 0;
        }
    }

    if (canRead) {
        currentPBO = nextPBO;
        havePreviousFrame = true;
    }

    glViewport(oldVp[0], oldVp[1], oldVp[2], oldVp[3]);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    auto latest = g_jpegWorker->getLatestJpeg();
    if (latest) jpeg_data = *latest;
    else jpeg_data.clear();
}

// Helper function to safely delete buffers
static void SafeDeleteBuffers(GLuint *buf, GLsizei n)
{
    if (n <= 0)
        return;
    std::vector<GLuint> toDelete;
    for (GLsizei i = 0; i < n; ++i)
        if (buf[i])
            toDelete.push_back(buf[i]);
    if (!toDelete.empty())
        glDeleteBuffers(static_cast<GLsizei>(toDelete.size()), toDelete.data());
    for (GLsizei i = 0; i < n; ++i)
        buf[i] = 0;
}

// Helper function to safely delete vertex arrays
static void SafeDeleteVertexArrays(GLuint *arr, GLsizei n)
{
    if (n <= 0)
        return;
    std::vector<GLuint> toDelete;
    for (GLsizei i = 0; i < n; ++i)
        if (arr[i])
            toDelete.push_back(arr[i]);
    if (!toDelete.empty())
        glDeleteVertexArrays(static_cast<GLsizei>(toDelete.size()), toDelete.data());
    for (GLsizei i = 0; i < n; ++i)
        arr[i] = 0;
}

// Clamp a value between min and max
static float clamp(float value, float min, float max)
{
    if (value < min)
        return min;
    if (value > max)
        return max;
    return value;
}

// Create a default object with specified skin type
static Object CreateDefaultObjectInternal(int skinType, int objectIndex, int equationID = 0)
{
    Object p;
    p.mass = 1.0f;
    p.charge = 0.0f;
    p.visualSkinType = skinType;
    p.collisionShapeType = COLLISION_NONE;
    p.equationID = equationID;

    // Initialize with zeros - wrapper will set actual values
    p.position = glm::vec2(0.0f, 0.0f);
    p.velocity = glm::vec2(0.0f, 0.0f);
    p.visualData = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    p.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

    // Set defaults based on skin type
    switch (skinType)
    {
    case SKIN_CIRCLE:
        p.visualData = glm::vec4(0.3f, 0.0f, 0.0f, 0.0f);
        p.color = glm::vec4(0.4f, 0.8f, 0.3f, 1.0f);
        break;
    case SKIN_RECTANGLE:
        p.visualData = glm::vec4(0.5f, 0.3f, 0.0f, 0.0f);
        p.color = glm::vec4(1.0f, 0.5f, 0.0f, 1.0f);
        break;
    case SKIN_POLYGON:
        p.visualData = glm::vec4(0.3f, 6.0f, 0.0f, 0.0f);
        p.color = glm::vec4(0.8f, 0.3f, 0.8f, 1.0f);
        break;
    }

    p.collisionData = glm::vec4(0.0f);
    p.scriptID = -1;
    return p;
}

// Set up VAO to read from SSBO for rendering
static void SetupRenderVAOFromSSBO(GLuint vaoID, GLuint ssboID)
{
    glBindVertexArray(vaoID);
    glBindBuffer(GL_ARRAY_BUFFER, ssboID);

    // Position attribute
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Object),
                          (void *)offsetof(Object, position));
    glEnableVertexAttribArray(0);

    // Velocity attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Object),
                          (void *)offsetof(Object, velocity));
    glEnableVertexAttribArray(1);

    // Mass attribute
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Object),
                          (void *)offsetof(Object, mass));
    glEnableVertexAttribArray(2);

    // Charge attribute
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Object),
                          (void *)offsetof(Object, charge));
    glEnableVertexAttribArray(3);

    // Skin type attribute
    glVertexAttribIPointer(4, 1, GL_INT, sizeof(Object),
                           (void *)offsetof(Object, visualSkinType));
    glEnableVertexAttribArray(4);

    // Visual data attribute
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(Object),
                          (void *)offsetof(Object, visualData));
    glEnableVertexAttribArray(5);

    // Color attribute
    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(Object),
                          (void *)offsetof(Object, color));
    glEnableVertexAttribArray(6);

    // Equation ID attribute
    glVertexAttribIPointer(7, 1, GL_INT, sizeof(Object),
                           (void *)offsetof(Object, equationID));
    glEnableVertexAttribArray(7);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// Upload packed equation data to GPU
static void UploadPackedEquationsToGPU()
{
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_allTokensSSBO);
    if (!g_allTokens.empty())
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     g_allTokens.size() * sizeof(int),
                     g_allTokens.data(),
                     GL_STATIC_DRAW);
    else
    {
        int dummy = 0;
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(int), &dummy, GL_STATIC_DRAW);
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_allConstantsSSBO);
    if (!g_allConstants.empty())
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     g_allConstants.size() * sizeof(float),
                     g_allConstants.data(),
                     GL_STATIC_DRAW);
    else
    {
        float dummy = 0.0f;
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(float), &dummy, GL_STATIC_DRAW);
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

// Upload constraint data to GPU
static void UploadConstraintsToGPU()
{
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_constraintsSSBO);
    if (!g_allConstraints.empty())
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     g_allConstraints.size() * sizeof(Constraint),
                     g_allConstraints.data(),
                     GL_DYNAMIC_DRAW);
    else
    {
        Constraint dummy = {};
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(Constraint), &dummy, GL_DYNAMIC_DRAW);
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_objectConstraintsSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 Objects::MAX_OBJECTS * sizeof(ObjectConstraints),
                 g_objectConstraintMappings.data(),
                 GL_DYNAMIC_DRAW);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

// Initialize contact buffer for warm starting
static void InitializeContactBuffer()
{
    if (g_contactBufferSSBO == 0)
    {
        glGenBuffers(1, &g_contactBufferSSBO);

        // Calculate exact size: MAX_OBJECTS * MAX_CONTACTS_PER_OBJECT * sizeof(ContactPoint)
        // ContactPoint in GLSL: objectA(4), objectB(4), normal(8), position(8), penetration(4),
        // accumulatedNormalImpulse(4), accumulatedTangentImpulse(4), frameCount(4) = 40 bytes
        // With std430 alignment, it pads to 48 bytes. Use 48 to be safe.
        const size_t contactPointSize = 48;
        size_t contactBufferSize = Objects::MAX_OBJECTS * 4 * contactPointSize;

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_contactBufferSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     contactBufferSize,
                     nullptr, // Initialize with zeros
                     GL_DYNAMIC_COPY);

        // Clear buffer to zeros
        void *data = glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_WRITE_ONLY);
        if (data)
        {
            memset(data, 0, contactBufferSize);
            glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
        }

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }
}

// Clear all contact data (useful for resetting simulation)
static void ClearContactBuffer()
{
    if (g_contactBufferSSBO == 0)
        return;

    const size_t contactPointSize = 48;
    size_t contactBufferSize = Objects::MAX_OBJECTS * 4 * contactPointSize;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_contactBufferSSBO);
    void *data = glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_WRITE_ONLY);
    if (data)
    {
        memset(data, 0, contactBufferSize);
        glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

static void rebuildScriptGroups()
{
    if (!g_groupsDirty)
        return;
    g_scriptGroups.clear();
    for (int i = 0; i < g_numObjects; ++i)
    {
        int sid = g_scriptIDs[i];
        g_scriptGroups[sid].push_back(i);
    }
    g_groupsDirty = false;
}

// ============================================================================
// EXISTING OBJECTS METHODS
// ============================================================================

void Objects::SetScriptID(int objectIndex, int scriptID)
{
    if (objectIndex < 0 || objectIndex >= g_numObjects)
        return;
    g_scriptIDs[objectIndex] = scriptID;
    g_groupsDirty = true;
}

int Objects::GetScriptID(int objectIndex)
{
    if (objectIndex < 0 || objectIndex >= g_numObjects)
        return -1;
    return g_scriptIDs[objectIndex];
}

void Objects::SetScriptManager(ScriptManager *mgr)
{
    g_scriptManager = mgr;
}

void Objects::SetPaintScript(int scriptID)
{
    g_paintScriptID = scriptID;
}
// Compact constraint array by removing invalid constraints
void Objects::CompactConstraintArray()
{
    std::vector<Constraint> compacted;
    std::unordered_map<int, int> oldToNewIndex;

    // Build new compacted array and mapping
    int newIndex = 0;
    for (int oldIndex = 0; oldIndex < static_cast<int>(g_allConstraints.size()); oldIndex++)
    {
        if (g_allConstraints[oldIndex].type != -1)
        {
            oldToNewIndex[oldIndex] = newIndex;
            compacted.push_back(g_allConstraints[oldIndex]);
            newIndex++;
        }
    }

    // Update constraint offsets in object mappings
    for (int i = 0; i < Objects::MAX_OBJECTS; i++)
    {
        ObjectConstraints &mapping = g_objectConstraintMappings[i];
        if (mapping.numConstraints > 0)
        {
            int oldOffset = mapping.constraintOffset;
            auto it = oldToNewIndex.find(oldOffset);
            if (it != oldToNewIndex.end())
                mapping.constraintOffset = it->second;
            else
            {
                // Invalidate mapping if offset not found
                mapping.objectID = -1;
                mapping.constraintOffset = 0;
                mapping.numConstraints = 0;
            }
        }
    }

    g_allConstraints = compacted;
}

// Add a constraint to an object
void Objects::AddConstraint(int objectIndex, const Constraint &constraint)
{
    if (objectIndex < 0 || objectIndex >= g_numObjects)
    {
        std::cerr << "[Objects] Invalid object index: " << objectIndex << std::endl;
        return;
    }

    // Validate constraint type-specific parameters
    if (constraint.type == CONSTRAINT_DISTANCE)
    {
        if (constraint.targetObjectID < 0 || constraint.targetObjectID >= g_numObjects)
        {
            std::cerr << "[Objects] Distance constraint has invalid target: "
                      << constraint.targetObjectID << std::endl;
            return;
        }
        if (constraint.targetObjectID == objectIndex)
        {
            std::cerr << "[Objects] Cannot create distance constraint to self!" << std::endl;
            return;
        }

        if (constraint.param1 <= 0.0f) // rest_length must be positive
        {
            std::cerr << "[Objects] Distance constraint has invalid rest length: "
                      << constraint.param1 << std::endl;
            return;
        }
    }
    else if (constraint.type == CONSTRAINT_BOUNDARY)
    {
        float minX = (constraint.param1 < constraint.param2) ? constraint.param1 : constraint.param2;
        float maxX = (constraint.param1 > constraint.param2) ? constraint.param1 : constraint.param2;
        float minY = (constraint.param3 < constraint.param4) ? constraint.param3 : constraint.param4;
        float maxY = (constraint.param3 > constraint.param4) ? constraint.param3 : constraint.param4;

        if (maxX - minX < 0.01f || maxY - minY < 0.01f)
        {
            std::cerr << "[Objects] Boundary constraint has invalid bounds!" << std::endl;
            return;
        }
    }

    ObjectConstraints &mapping = g_objectConstraintMappings[objectIndex];

    // Add constraint to object's constraint list
    if (mapping.numConstraints == 0)
    {
        mapping.objectID = objectIndex;
        mapping.constraintOffset = static_cast<int>(g_allConstraints.size());
        mapping.numConstraints = 1;
        g_allConstraints.push_back(constraint);
    }
    else
    {
        int nextSlot = mapping.constraintOffset + mapping.numConstraints;
        bool canAppend = (nextSlot == static_cast<int>(g_allConstraints.size()));

        if (canAppend)
        {
            g_allConstraints.push_back(constraint);
            mapping.numConstraints++;
        }
        else
        {
            // Need to relocate constraints to maintain contiguous storage
            std::vector<Constraint> existingConstraints;
            for (int i = 0; i < mapping.numConstraints; i++)
                existingConstraints.push_back(g_allConstraints[mapping.constraintOffset + i]);

            // Mark old constraints for removal
            for (int i = 0; i < mapping.numConstraints; i++)
                g_allConstraints[mapping.constraintOffset + i].type = -1;

            // Create new contiguous block
            int newOffset = static_cast<int>(g_allConstraints.size());
            for (const auto &c : existingConstraints)
                g_allConstraints.push_back(c);
            g_allConstraints.push_back(constraint);

            mapping.constraintOffset = newOffset;
            mapping.numConstraints = static_cast<int>(existingConstraints.size()) + 1;
            CompactConstraintArray();
        }
    }

    UploadConstraintsToGPU();
}

// Remove a constraint from an object
void Objects::RemoveConstraint(int objectIndex, int constraintLocalIndex)
{
    if (objectIndex < 0 || objectIndex >= g_numObjects)
        return;

    ObjectConstraints &mapping = g_objectConstraintMappings[objectIndex];
    if (constraintLocalIndex < 0 || constraintLocalIndex >= mapping.numConstraints)
        return;

    // Mark constraint as invalid and shift others
    int globalIndex = mapping.constraintOffset + constraintLocalIndex;
    g_allConstraints[globalIndex].type = -1;

    // Shift remaining constraints down
    for (int i = constraintLocalIndex; i < mapping.numConstraints - 1; i++)
    {
        int srcGlobal = mapping.constraintOffset + i + 1;
        int dstGlobal = mapping.constraintOffset + i;
        g_allConstraints[dstGlobal] = g_allConstraints[srcGlobal];
    }

    // Mark last slot as invalid
    g_allConstraints[mapping.constraintOffset + mapping.numConstraints - 1].type = -1;
    mapping.numConstraints--;

    // Clear mapping if no constraints remain
    if (mapping.numConstraints == 0)
    {
        mapping.objectID = -1;
        mapping.constraintOffset = 0;
    }

    CompactConstraintArray();
    UploadConstraintsToGPU();
}

// Clear all constraints from an object
void Objects::ClearConstraints(int objectIndex)
{
    if (objectIndex < 0 || objectIndex >= g_numObjects)
        return;

    ObjectConstraints &mapping = g_objectConstraintMappings[objectIndex];
    if (mapping.numConstraints == 0)
        return;

    // Mark all constraints as invalid
    for (int i = 0; i < mapping.numConstraints; i++)
    {
        int globalIndex = mapping.constraintOffset + i;
        if (globalIndex >= 0 && globalIndex < static_cast<int>(g_allConstraints.size()))
            g_allConstraints[globalIndex].type = -1;
    }

    // Reset object's constraint mapping
    mapping.objectID = -1;
    mapping.constraintOffset = 0;
    mapping.numConstraints = 0;

    CompactConstraintArray();
    UploadConstraintsToGPU();
}

// Clear all constraints from all objects
void Objects::ClearAllConstraints()
{
    for (int i = 0; i < Objects::MAX_OBJECTS; i++)
        g_objectConstraintMappings[i] = ObjectConstraints();
    g_allConstraints.clear();
    UploadConstraintsToGPU();
}

// Get all constraints for an object
std::vector<Constraint> Objects::GetConstraints(int objectIndex)
{
    std::vector<Constraint> result;
    if (objectIndex < 0 || objectIndex >= g_numObjects)
        return result;

    const ObjectConstraints &mapping = g_objectConstraintMappings[objectIndex];
    if (mapping.numConstraints == 0)
        return result;

    // Copy constraints to result vector
    for (int i = 0; i < mapping.numConstraints; i++)
        result.push_back(g_allConstraints[mapping.constraintOffset + i]);

    return result;
}

// Update an existing constraint
void Objects::UpdateConstraint(int objectIndex, int constraintLocalIndex, const Constraint &newConstraint)
{
    if (objectIndex < 0 || objectIndex >= g_numObjects)
        return;

    ObjectConstraints &mapping = g_objectConstraintMappings[objectIndex];
    if (constraintLocalIndex < 0 || constraintLocalIndex >= mapping.numConstraints)
        return;

    // Validate updated constraint
    if (newConstraint.type == CONSTRAINT_DISTANCE)
    {
        if (newConstraint.targetObjectID < 0 || newConstraint.targetObjectID >= g_numObjects)
        {
            std::cerr << "[Objects] Updated constraint has invalid target!" << std::endl;
            return;
        }
        if (newConstraint.targetObjectID == objectIndex)
        {
            std::cerr << "[Objects] Cannot target self!" << std::endl;
            return;
        }
    }

    // Update constraint data
    int globalIndex = mapping.constraintOffset + constraintLocalIndex;
    g_allConstraints[globalIndex] = newConstraint;
    UploadConstraintsToGPU();
}

// ============================================================================
//  COLLISION PARAMETER MANAGEMENT
// ============================================================================

void Objects::SetCollisionParameters(bool enableWarmStart, int maxContactIterations)
{
    g_enableWarmStart = enableWarmStart;
    g_maxContactIterations = clamp(maxContactIterations, 1, 20);

    // Initialize contact buffer if warm starting is enabled
    if (g_enableWarmStart && g_contactBufferSSBO == 0)
    {
        InitializeContactBuffer();
    }

    // Set uniform values when compute shader is ready
    if (g_programCompute && g_computeShaderReady)
    {
        glUseProgram(g_programCompute);

        GLint enableWarmStartLoc = glGetUniformLocation(g_programCompute, "uEnableWarmStart");
        if (enableWarmStartLoc != -1)
            glUniform1i(enableWarmStartLoc, g_enableWarmStart ? 1 : 0);

        GLint maxContactIterationsLoc = glGetUniformLocation(g_programCompute, "uMaxContactIterations");
        if (maxContactIterationsLoc != -1)
            glUniform1i(maxContactIterationsLoc, g_maxContactIterations);

        glUseProgram(0);
    }
}

void Objects::GetCollisionParameters(bool &enableWarmStart, int &maxContactIterations)
{
    enableWarmStart = g_enableWarmStart;
    maxContactIterations = g_maxContactIterations;
}
// Set common uniforms for JIT shaders (bindings 0,1,9; uDt, uTime, uGroupCount, etc.)
static void setCommonUniforms(GLuint program, float dt, int groupSize, int totalObjects)
{
    if (!program)
        return;
    glUniform1f(glGetUniformLocation(program, "uTime"), g_simulationTime);
    glUniform1f(glGetUniformLocation(program, "uDt"), dt);
    glUniform1i(glGetUniformLocation(program, "uNumObjects"), totalObjects);
    glUniform1i(glGetUniformLocation(program, "uGroupCount"), groupSize);
    glUniform1f(glGetUniformLocation(program, "k"), 1.0f);
    glUniform1f(glGetUniformLocation(program, "b"), 0.1f);
    glUniform1f(glGetUniformLocation(program, "g"), 9.81f);
    glUniform2f(glGetUniformLocation(program, "uGravityDir"), 0.0f, -1.0f);
    glUniform1f(glGetUniformLocation(program, "uRestitution"), 0.7f);
    glUniform1f(glGetUniformLocation(program, "uCoupling"), 1.0f);
    glUniform2f(glGetUniformLocation(program, "uExternalForce"), 0.0f, 0.0f);
    glUniform1f(glGetUniformLocation(program, "uDriveFreq"), 1.0f);
    glUniform1f(glGetUniformLocation(program, "uDriveAmp"), 0.0f);
    glUniform1f(glGetUniformLocation(program, "uDerivativeEpsilon"), 1e-4f);
}

// Dispatch a group of objects with a specific compute program (JIT)
static void dispatchGroupWithProgram(GLuint program,
                                     const std::vector<int> &indices,
                                     int inputIndex, int outputIndex,
                                     float dt)
{
    if (program == 0 || indices.empty())
        return;

    glUseProgram(program);

    // Bind object SSBOs (binding 0 and 1)
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, g_objectSSBO[inputIndex]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, g_objectSSBO[outputIndex]);

    // Index SSBO (binding 9)
    if (g_indexSSBO == 0)
        glGenBuffers(1, &g_indexSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_indexSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 indices.size() * sizeof(int),
                 indices.data(),
                 GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9, g_indexSSBO);

    // ---- Bind constraint and collision SSBOs ----
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, g_constraintsSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, g_objectConstraintsSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, g_collisionPropsSSBO);
    if (g_contactBufferSSBO)  // only if warm starting is enabled
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, g_contactBufferSSBO);
    // ----------------------------------------------------

    // ---- Bind scratchpad pool (binding 10) and signal queue (binding 11) ----
    if (g_scratchpadPool != 0)
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SCRATCHPAD_BINDING, g_scratchpadPool);
    if (g_signalQueueBuffer != 0)
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SIGNAL_QUEUE_BINDING, g_signalQueueBuffer);
    // ---------------------------------------------------------------------------

    // Set group count and common uniforms
    GLint countLoc = glGetUniformLocation(program, "uGroupCount");
    if (countLoc != -1)
        glUniform1i(countLoc, (int)indices.size());
    setCommonUniforms(program, dt, (int)indices.size(), g_numObjects);

    // ---- Set collision‑related uniforms ----
    GLint enableWarmStartLoc = glGetUniformLocation(program, "uEnableWarmStart");
    if (enableWarmStartLoc != -1)
        glUniform1i(enableWarmStartLoc, g_enableWarmStart ? 1 : 0);
    GLint maxContactIterationsLoc = glGetUniformLocation(program, "uMaxContactIterations");
    if (maxContactIterationsLoc != -1)
        glUniform1i(maxContactIterationsLoc, g_maxContactIterations);
    // -------------------------------------------------

    // ---- Set signal‑queue uniforms for JIT object scripts ----
    GLint capLoc = glGetUniformLocation(program, "uSignalQueueCapacity");
    if (capLoc != -1)
        glUniform1ui(capLoc, (GLuint)g_signalQueueCapacity);
    GLint policyLoc = glGetUniformLocation(program, "uSignalQueueOverflowPolicy");
    if (policyLoc != -1)
        glUniform1i(policyLoc, g_signalQueueOverflowPolicy);
    // -----------------------------------------------------------------

    // ---- Set scratchpad offsets for JIT object scripts ----
    GLint offsetLoc = glGetUniformLocation(program, "uScratchpadOffsets");
    if (offsetLoc != -1) {
        int offsets[16] = {0};
        for (auto& pair : g_scratchpads) {
            if (pair.first < 16)
                offsets[pair.first] = (int)(pair.second.offsetInPool); // offset is already in floats
        }
        glUniform1iv(offsetLoc, 16, offsets);
    }
    // -------------------------------------------------------------

    // Dispatch
    int workGroupSize = 64;
    int numWorkGroups = (indices.size() + workGroupSize - 1) / workGroupSize;
    glDispatchCompute(numWorkGroups, 1, 1);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
    glUseProgram(0);
}

// ============================================================================
// Initialize the objects system
// ============================================================================
bool Objects::Init(void *glfwWindow)
{
    // Check OpenGL state
    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
        return false;

    // Test OpenGL buffer creation
    GLuint testBuffer;
    glGenBuffers(1, &testBuffer);
    if (glGetError() != GL_NO_ERROR)
        return false;
    glDeleteBuffers(1, &testBuffer);

    // Initialize data structures
    g_equationMappings.resize(Objects::MAX_EQUATIONS);
    g_objectConstraintMappings.resize(Objects::MAX_OBJECTS);

    for (auto &mapping : g_equationMappings)
        mapping = EquationMapping{};

    for (auto &mapping : g_objectConstraintMappings)
        mapping = ObjectConstraints{};

    // Create default equation
    ParserContext context;
    ParsedEquation defaultEq = ParseEquation("vx, vy, -k*x/mass, -k*y/mass, 0, 1, 0, 0, 1", context);
    int defaultEqID = AddOrGetEquation("default_zero", defaultEq);
    g_numObjects = 0;

    // Create double-buffered SSBOs for objects
    if (g_objectSSBO[0] == 0)
    {
        while (glGetError() != GL_NO_ERROR)
            ;
        glGenBuffers(2, g_objectSSBO);
        err = glGetError();
        if (err != GL_NO_ERROR)
            return false;

        // Allocate buffer memory
        for (int i = 0; i < 2; i++)
        {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_objectSSBO[i]);
            glBufferData(GL_SHADER_STORAGE_BUFFER,
                         Objects::MAX_OBJECTS * sizeof(Object),
                         nullptr,
                         GL_DYNAMIC_COPY);
            err = glGetError();
            if (err != GL_NO_ERROR)
                return false;
        }
        g_useMapBuffer = false;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    // Create VAOs for rendering
    if (g_renderVAO[0] == 0)
    {
        glGenVertexArrays(2, g_renderVAO);
        GLenum err = glGetError();
        if (err != GL_NO_ERROR)
            return false;

        SetupRenderVAOFromSSBO(g_renderVAO[0], g_objectSSBO[0]);
        SetupRenderVAOFromSSBO(g_renderVAO[1], g_objectSSBO[1]);
    }

    // Create additional SSBOs
    if (g_allTokensSSBO == 0)
        glGenBuffers(1, &g_allTokensSSBO);
    if (g_allConstantsSSBO == 0)
        glGenBuffers(1, &g_allConstantsSSBO);
    if (g_mappingsSSBO == 0)
        glGenBuffers(1, &g_mappingsSSBO);
    if (g_initialPosSSBO == 0)
        glGenBuffers(1, &g_initialPosSSBO);
    if (g_constraintsSSBO == 0)
        glGenBuffers(1, &g_constraintsSSBO);
    if (g_objectConstraintsSSBO == 0)
        glGenBuffers(1, &g_objectConstraintsSSBO);

    // Initialize collision properties buffer
    if (g_collisionPropsSSBO == 0)
    {
        glGenBuffers(1, &g_collisionPropsSSBO);

        // Initialize collision properties with defaults
        for (auto &prop : g_collisionProperties)
        {
            prop.enabled = 1; // Collisions enabled by default
            prop.shapeType = COLLISION_NONE;
            prop.restitution = 0.7f; // Default bounciness
            prop.friction = 0.3f;    // Default friction
            prop.mass_factor = 1.0f;
            prop._pad1 = prop._pad2 = prop._pad3 = 0;
        }

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_collisionPropsSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     Objects::MAX_OBJECTS * sizeof(CollisionProperties),
                     g_collisionProperties.data(),
                     GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    // Initialize contact buffer (will be created when needed)
    // g_contactBufferSSBO is initialized lazily when warm starting is enabled

    err = glGetError();
    if (err != GL_NO_ERROR)
        return false;

    // Upload initial data to GPU
    UploadPackedEquationsToGPU();
    UploadConstraintsToGPU();

    // Upload equation mappings
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_mappingsSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 Objects::MAX_EQUATIONS * sizeof(EquationMapping),
                 g_equationMappings.data(),
                 GL_DYNAMIC_DRAW);
    err = glGetError();
    if (err != GL_NO_ERROR)
        return false;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // Load compute shader asynchronously
    if (g_programCompute == 0)
    {
        g_computeLoader.LoadComputeShaderAsync(
            "math.comp",
            [](GLuint program)
            {
                g_programCompute = program;
                g_computeShaderReady = true;

                // Set initial collision parameters when shader is loaded
                if (g_programCompute)
                {
                    glUseProgram(g_programCompute);

                    GLint enableWarmStartLoc = glGetUniformLocation(g_programCompute, "uEnableWarmStart");
                    if (enableWarmStartLoc != -1)
                        glUniform1i(enableWarmStartLoc, g_enableWarmStart ? 1 : 0);

                    GLint maxContactIterationsLoc = glGetUniformLocation(g_programCompute, "uMaxContactIterations");
                    if (maxContactIterationsLoc != -1)
                        glUniform1i(maxContactIterationsLoc, g_maxContactIterations);

                    glUseProgram(0);
                }
            },
            [](const std::string &error)
            {
                std::cerr << "\n[Objects] Compute shader FAILED: " << error << std::endl;
                g_computeShaderReady = false;
            });
    }

    // Load quad rendering shader asynchronously
    if (g_programQuad == 0)
    {
        g_quadLoader.LoadGraphicsShaderAsync(
            "quad.vert",
            "quad.frag",
            "quad.geom",
            [](GLuint program)
            {
                g_programQuad = program;
                g_quadShaderReady = true;
            },
            [](const std::string &error)
            {
                std::cerr << "\n[Objects] Quad shader FAILED: " << error << std::endl;
                g_quadShaderReady = false;
            });
    }
    InitPaintShader(g_width, g_height);

    // ---- initialise signal queue with default capacity ----
    SetSignalQueueCapacity(1024); // default

    // ---- allocate scratchpad pool with fixed maximum size ----
    if (g_scratchpadPool == 0) {
        glGenBuffers(1, &g_scratchpadPool);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_scratchpadPool);
        glBufferData(GL_SHADER_STORAGE_BUFFER, MAX_SCRATCHPAD_POOL_SIZE, nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        // Bind the pool to binding point once (or each frame)
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SCRATCHPAD_BINDING, g_scratchpadPool);
    }

    return true;
}

// ============================================================================
// Update object physics using compute shader
// ============================================================================
void Objects::Update(int inputIndex, int outputIndex, float dt)
{
    UpdateShaderLoadingStatus();
    rebuildScriptGroups();

    // -------------------------------------------------------------
    // 1 DSL dispatch
    // -------------------------------------------------------------
    if (g_programCompute && g_computeShaderReady)
    {
        glUseProgram(g_programCompute);

        // ---- Set all uniforms ----
        glUniform1f(glGetUniformLocation(g_programCompute, "uDt"), dt);
        glUniform1f(glGetUniformLocation(g_programCompute, "uTime"), g_simulationTime);
        glUniform1f(glGetUniformLocation(g_programCompute, "k"), 1.0f);
        glUniform1f(glGetUniformLocation(g_programCompute, "b"), 0.1f);
        glUniform1f(glGetUniformLocation(g_programCompute, "g"), 9.81f);
        glUniform2f(glGetUniformLocation(g_programCompute, "uGravityDir"), 0.0f, -1.0f);
        glUniform1f(glGetUniformLocation(g_programCompute, "uRestitution"), 0.7f);
        glUniform1f(glGetUniformLocation(g_programCompute, "uCoupling"), 1.0f);
        glUniform2f(glGetUniformLocation(g_programCompute, "uExternalForce"), 0.0f, 0.0f);
        glUniform1f(glGetUniformLocation(g_programCompute, "uDriveFreq"), 1.0f);
        glUniform1f(glGetUniformLocation(g_programCompute, "uDriveAmp"), 0.0f);
        glUniform1i(glGetUniformLocation(g_programCompute, "uEquationMode"), 0);
        glUniform1i(glGetUniformLocation(g_programCompute, "uNumObjects"), g_numObjects);
        glUniform1i(glGetUniformLocation(g_programCompute, "uEnableWarmStart"), g_enableWarmStart ? 1 : 0);
        glUniform1i(glGetUniformLocation(g_programCompute, "uMaxContactIterations"), g_maxContactIterations);

        // ---- signal queue uniforms for object shaders ----
        GLint capLoc = glGetUniformLocation(g_programCompute, "uSignalQueueCapacity");
        if (capLoc != -1) glUniform1ui(capLoc, (GLuint)g_signalQueueCapacity);
        GLint policyLoc = glGetUniformLocation(g_programCompute, "uSignalQueueOverflowPolicy");
        if (policyLoc != -1) glUniform1i(policyLoc, g_signalQueueOverflowPolicy);

        // ---- set scratchpad offsets ----
        GLint offsetLoc = glGetUniformLocation(g_programCompute, "uScratchpadOffsets");
        if (offsetLoc != -1) {
            int offsets[16] = {0};
            for (auto& pair : g_scratchpads) {
                if (pair.first < 16)
                    offsets[pair.first] = (int)(pair.second.offsetInPool); // offset already in floats
            }
            glUniform1iv(offsetLoc, 16, offsets);
        }

        // ---- Bind SSBOs (bindings 0–8) ----
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, g_objectSSBO[inputIndex]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, g_objectSSBO[outputIndex]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, g_allTokensSSBO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, g_allConstantsSSBO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, g_mappingsSSBO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, g_constraintsSSBO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, g_objectConstraintsSSBO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, g_collisionPropsSSBO);
        if (g_enableWarmStart && g_contactBufferSSBO)
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, g_contactBufferSSBO);

        // ---- bind scratchpad pool (binding 10) and signal queue (binding 11) ----
        if (g_scratchpadPool != 0)
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SCRATCHPAD_BINDING, g_scratchpadPool);
        if (g_signalQueueBuffer != 0)
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SIGNAL_QUEUE_BINDING, g_signalQueueBuffer);
        // -------------------------------------------------------------

        // ---- Dispatch over all objects ----
        int workGroupSize = 64;
        int numWorkGroups = (g_numObjects + workGroupSize - 1) / workGroupSize;
        glDispatchCompute(numWorkGroups, 1, 1);

        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
        glUseProgram(0);
    }

    // -------------------------------------------------------------
    // 2) JIT group overrides (overwrites results for JIT objects)
    // -------------------------------------------------------------
    if (g_scriptManager)
    {
        for (auto &pair : g_scriptGroups)
        {
            int sid = pair.first;
            if (sid == -1)
                continue; // skip DSL group
            GLuint prog = g_scriptManager->getProgram(sid);
            if (prog)
            {
                dispatchGroupWithProgram(prog, pair.second, inputIndex, outputIndex, dt);
            }
        }
    }

    // ---- store the current read buffer for agents ----
    g_currentReadBufferIndex = outputIndex;
}

// ============================================================================
// Add or retrieve equation ID for a given equation string
// ============================================================================
int Objects::AddOrGetEquation(const std::string &equationString, const ParsedEquation &eq)
{
    // Return existing ID if equation already registered
    auto it = g_equationStringToID.find(equationString);
    if (it != g_equationStringToID.end())
        return it->second;

    // Serialize equation for GPU
    ParserContext context;
    GPUSerializedEquation gpu_eq = serializeEquationForGPU(eq);

    // Find available equation slot
    int newID = -1;
    for (int i = 0; i < Objects::MAX_EQUATIONS; i++)
    {
        if (g_equationMappings[i].tokenCount_ax == 0 &&
            g_equationMappings[i].tokenCount_ay == 0 &&
            g_equationMappings[i].tokenCount_angular == 0)
        {
            newID = i;
            break;
        }
    }

    if (newID == -1)
    {
        std::cerr << "[Objects] ERROR: Max equations reached!" << std::endl;
        return 0;
    }

    // Calculate offsets for equation components
    int currentTokenOffset = static_cast<int>(g_allTokens.size());
    int currentConstantOffset = static_cast<int>(g_allConstants.size());

    EquationMapping mapping;
    mapping.tokenOffset_ax = currentTokenOffset;
    mapping.tokenCount_ax = static_cast<int>(gpu_eq.tokenBuffer_ax.size());
    mapping.constantOffset_ax = currentConstantOffset;
    mapping._pad1 = 0;

    mapping.tokenOffset_ay = currentTokenOffset + mapping.tokenCount_ax;
    mapping.tokenCount_ay = static_cast<int>(gpu_eq.tokenBuffer_ay.size());
    mapping.constantOffset_ay = currentConstantOffset + static_cast<int>(gpu_eq.constantBuffer_ax.size());
    mapping._pad2 = 0;

    mapping.tokenOffset_angular = mapping.tokenOffset_ay + mapping.tokenCount_ay;
    mapping.tokenCount_angular = static_cast<int>(gpu_eq.tokenBuffer_angular.size());
    mapping.constantOffset_angular = mapping.constantOffset_ay + static_cast<int>(gpu_eq.constantBuffer_ay.size());
    mapping._pad3 = 0;

    mapping.tokenOffset_r = mapping.tokenOffset_angular + mapping.tokenCount_angular;
    mapping.tokenCount_r = static_cast<int>(gpu_eq.tokenBuffer_r.size());
    mapping.constantOffset_r = mapping.constantOffset_angular + static_cast<int>(gpu_eq.constantBuffer_angular.size());
    mapping._pad4 = 0;

    mapping.tokenOffset_g = mapping.tokenOffset_r + mapping.tokenCount_r;
    mapping.tokenCount_g = static_cast<int>(gpu_eq.tokenBuffer_g.size());
    mapping.constantOffset_g = mapping.constantOffset_r + static_cast<int>(gpu_eq.constantBuffer_r.size());
    mapping._pad5 = 0;

    mapping.tokenOffset_b = mapping.tokenOffset_g + mapping.tokenCount_g;
    mapping.tokenCount_b = static_cast<int>(gpu_eq.tokenBuffer_b.size());
    mapping.constantOffset_b = mapping.constantOffset_g + static_cast<int>(gpu_eq.constantBuffer_g.size());
    mapping._pad6 = 0;

    mapping.tokenOffset_a = mapping.tokenOffset_b + mapping.tokenCount_b;
    mapping.tokenCount_a = static_cast<int>(gpu_eq.tokenBuffer_a.size());
    mapping.constantOffset_a = mapping.constantOffset_b + static_cast<int>(gpu_eq.constantBuffer_b.size());
    mapping._pad7 = 0;

    // Store mapping
    g_equationMappings[newID] = mapping;
    g_equationStringToID[equationString] = newID;

    // Append tokens and constants
    g_allTokens.insert(g_allTokens.end(), gpu_eq.tokenBuffer_ax.begin(), gpu_eq.tokenBuffer_ax.end());
    g_allTokens.insert(g_allTokens.end(), gpu_eq.tokenBuffer_ay.begin(), gpu_eq.tokenBuffer_ay.end());
    g_allTokens.insert(g_allTokens.end(), gpu_eq.tokenBuffer_angular.begin(), gpu_eq.tokenBuffer_angular.end());
    g_allTokens.insert(g_allTokens.end(), gpu_eq.tokenBuffer_r.begin(), gpu_eq.tokenBuffer_r.end());
    g_allTokens.insert(g_allTokens.end(), gpu_eq.tokenBuffer_g.begin(), gpu_eq.tokenBuffer_g.end());
    g_allTokens.insert(g_allTokens.end(), gpu_eq.tokenBuffer_b.begin(), gpu_eq.tokenBuffer_b.end());
    g_allTokens.insert(g_allTokens.end(), gpu_eq.tokenBuffer_a.begin(), gpu_eq.tokenBuffer_a.end());

    g_allConstants.insert(g_allConstants.end(), gpu_eq.constantBuffer_ax.begin(), gpu_eq.constantBuffer_ax.end());
    g_allConstants.insert(g_allConstants.end(), gpu_eq.constantBuffer_ay.begin(), gpu_eq.constantBuffer_ay.end());
    g_allConstants.insert(g_allConstants.end(), gpu_eq.constantBuffer_angular.begin(), gpu_eq.constantBuffer_angular.end());
    g_allConstants.insert(g_allConstants.end(), gpu_eq.constantBuffer_r.begin(), gpu_eq.constantBuffer_r.end());
    g_allConstants.insert(g_allConstants.end(), gpu_eq.constantBuffer_g.begin(), gpu_eq.constantBuffer_g.end());
    g_allConstants.insert(g_allConstants.end(), gpu_eq.constantBuffer_b.begin(), gpu_eq.constantBuffer_b.end());
    g_allConstants.insert(g_allConstants.end(), gpu_eq.constantBuffer_a.begin(), gpu_eq.constantBuffer_a.end());

    // Update GPU data
    UploadPackedEquationsToGPU();

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_mappingsSSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    Objects::MAX_EQUATIONS * sizeof(EquationMapping),
                    g_equationMappings.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    return newID;
}

// ============================================================================
// Set equation for an object
// ============================================================================
void Objects::SetEquation(const std::string &equationString, const ParsedEquation &eq, int objectIndex)
{
    int eqID = AddOrGetEquation(equationString, eq);

    if (objectIndex >= 0 && objectIndex < g_numObjects)
    {
        if (g_useMapBuffer && g_mappedSSBO[0])
        {
            // Update via mapped memory
            g_mappedSSBO[0][objectIndex].equationID = eqID;
            g_mappedSSBO[1][objectIndex].equationID = eqID;
        }
        else
        {
            // Update via buffer subdata
            for (int i = 0; i < 2; i++)
            {
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_objectSSBO[i]);
                glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                                objectIndex * sizeof(Object) + offsetof(Object, equationID),
                                sizeof(int), &eqID);
            }
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }
    }
}

// ============================================================================
// Upload CPU data to GPU (compatibility function)
// ============================================================================
void Objects::UploadCpuDataToGpu()
{
    // NO-OP for compatibility
}

// ============================================================================
// Fetch object data from GPU to CPU
// ============================================================================
void Objects::FetchToCPU(int sourceIndex, std::vector<Object> &out)
{
    out.resize(g_numObjects);
    if (g_useMapBuffer && g_mappedSSBO[sourceIndex])
        std::memcpy(out.data(), g_mappedSSBO[sourceIndex], g_numObjects * sizeof(Object));
    else
    {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_objectSSBO[sourceIndex]);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                           g_numObjects * sizeof(Object),
                           out.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }
}

// ============================================================================
// Draw all objects
// ============================================================================
void Objects::Draw(int sourceIndex)
{
    if (!g_programQuad)
        return;

    glUseProgram(g_programQuad);
    glBindVertexArray(g_renderVAO[sourceIndex]);
    glDrawArrays(GL_POINTS, 0, g_numObjects);
    glBindVertexArray(0);
    glUseProgram(0);
}

// ============================================================================
// Add a new object with default properties
// ============================================================================
void Objects::AddObject()
{
    if (g_numObjects >= Objects::MAX_OBJECTS)
    {
        std::cerr << "[Objects::AddObject] Max objects reached!" << std::endl;
        return;
    }

    // Create default object
    Object newObject = CreateDefaultObjectInternal(
        g_currentDefaultObjectType,
        g_numObjects,
        0);

    newObject.scriptID = -1;

    // Store object data
    if (g_useMapBuffer && g_mappedSSBO[0])
    {
        g_mappedSSBO[0][g_numObjects] = newObject;
        g_mappedSSBO[1][g_numObjects] = newObject;
    }
    else
    {
        for (int i = 0; i < 2; i++)
        {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_objectSSBO[i]);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                            g_numObjects * sizeof(Object),
                            sizeof(Object),
                            &newObject);
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    // Initialize empty constraint mapping
    g_objectConstraintMappings[g_numObjects] = ObjectConstraints();
    g_numObjects++;
}

// ============================================================================
// Upload multiple objects at once
// ============================================================================
void Objects::UploadBulkObjects(const std::vector<Object> &objects, int startIndex)
{
    if (startIndex < 0 || startIndex + static_cast<int>(objects.size()) > Objects::MAX_OBJECTS)
    {
        std::cerr << "[Objects::UploadBulkObjects] Invalid range!" << std::endl;
        return;
    }

    // Copy objects to GPU buffers
    if (g_useMapBuffer && g_mappedSSBO[0])
    {
        std::memcpy(&g_mappedSSBO[0][startIndex], objects.data(),
                    objects.size() * sizeof(Object));
        std::memcpy(&g_mappedSSBO[1][startIndex], objects.data(),
                    objects.size() * sizeof(Object));
    }
    else
    {
        for (int i = 0; i < 2; i++)
        {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_objectSSBO[i]);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                            startIndex * sizeof(Object),
                            objects.size() * sizeof(Object),
                            objects.data());
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    // Update object count
    if (startIndex + static_cast<int>(objects.size()) > g_numObjects)
        g_numObjects = startIndex + static_cast<int>(objects.size());
}

// ============================================================================
// Get direct pointer to object data (read-only)
// ============================================================================
const Object *Objects::GetObjectDataDirect(int sourceIndex)
{
    if (sourceIndex < 0 || sourceIndex > 1)
        return nullptr;
    if (g_useMapBuffer && g_mappedSSBO[sourceIndex])
        return g_mappedSSBO[sourceIndex];
    return nullptr;
}

// ============================================================================
// Get direct pointer to object data (mutable)
// ============================================================================
Object *Objects::GetObjectDataDirectMutable(int sourceIndex)
{
    if (sourceIndex < 0 || sourceIndex > 1)
        return nullptr;
    if (g_useMapBuffer && g_mappedSSBO[sourceIndex])
        return g_mappedSSBO[sourceIndex];
    return nullptr;
}

// ============================================================================
// Remove an object from the system
// ============================================================================
void Objects::RemoveObject(int index)
{
    if (g_numObjects == 0)
    {
        std::cerr << "[Objects::RemoveObject] No objects to remove!" << std::endl;
        return;
    }

    // Determine which index to remove
    int removeIdx = (index >= 0 && index < g_numObjects) ? index : (g_numObjects - 1);
    ClearConstraints(removeIdx);

    // Remove constraints that reference the removed object
    for (int i = 0; i < g_numObjects; i++)
    {
        if (i == removeIdx)
            continue;

        ObjectConstraints &pc = g_objectConstraintMappings[i];
        for (int j = pc.numConstraints - 1; j >= 0; j--)
        {
            int globalIdx = pc.constraintOffset + j;
            Constraint &c = g_allConstraints[globalIdx];
            if (c.type == CONSTRAINT_DISTANCE && c.targetObjectID == removeIdx)
                RemoveConstraint(i, j);
        }
    }

    // If removing the last object, just decrement count
    if (removeIdx == g_numObjects - 1)
    {
        g_objectConstraintMappings[removeIdx] = ObjectConstraints();
        g_numObjects--;
        UploadConstraintsToGPU();
        return;
    }

    // Swap with last object and decrement count
    int lastObjectIdx = g_numObjects - 1;

    if (g_useMapBuffer && g_mappedSSBO[0])
    {
        g_mappedSSBO[0][removeIdx] = g_mappedSSBO[0][lastObjectIdx];
        g_mappedSSBO[1][removeIdx] = g_mappedSSBO[1][lastObjectIdx];
    }
    else
    {
        Object lastObject;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_objectSSBO[0]);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,
                           lastObjectIdx * sizeof(Object),
                           sizeof(Object),
                           &lastObject);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        for (int i = 0; i < 2; i++)
        {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_objectSSBO[i]);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                            removeIdx * sizeof(Object),
                            sizeof(Object),
                            &lastObject);
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    // Update constraint mappings
    g_objectConstraintMappings[removeIdx] = g_objectConstraintMappings[lastObjectIdx];
    if (g_objectConstraintMappings[removeIdx].objectID == lastObjectIdx)
        g_objectConstraintMappings[removeIdx].objectID = removeIdx;

    // Update constraint references to the moved object
    for (int i = 0; i < g_numObjects - 1; i++)
    {
        ObjectConstraints &pc = g_objectConstraintMappings[i];
        for (int j = 0; j < pc.numConstraints; j++)
        {
            int globalIdx = pc.constraintOffset + j;
            Constraint &c = g_allConstraints[globalIdx];
            if (c.type == CONSTRAINT_DISTANCE && c.targetObjectID == lastObjectIdx)
                c.targetObjectID = removeIdx;
        }
    }

    // Clear last slot and decrement count
    g_objectConstraintMappings[lastObjectIdx] = ObjectConstraints();
    g_numObjects--;

    g_scriptIDs[removeIdx] = -1; // the removed slot
    if (removeIdx != lastObjectIdx)
    {
        g_scriptIDs[lastObjectIdx] = -1; // the slot that was moved
    }
    g_groupsDirty = true;
    UploadConstraintsToGPU();
}

// ============================================================================
// Reset all objects to initial conditions
// ============================================================================
void Objects::ResetToInitialConditions()
{
    for (int i = 0; i < g_numObjects; i++)
    {
        // Preserve equation ID
        int preservedEqID = 0;
        if (g_useMapBuffer && g_mappedSSBO[0])
            preservedEqID = g_mappedSSBO[0][i].equationID;
        else
        {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_objectSSBO[0]);
            glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,
                               i * sizeof(Object) + offsetof(Object, equationID),
                               sizeof(int),
                               &preservedEqID);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }

        // Create reset object with preserved equation ID
        Object resetObject = CreateDefaultObjectInternal(
            g_currentDefaultObjectType,
            i,
            preservedEqID);

        // Update object data
        if (g_useMapBuffer && g_mappedSSBO[0])
        {
            g_mappedSSBO[0][i] = resetObject;
            g_mappedSSBO[1][i] = resetObject;
        }
        else
        {
            for (int j = 0; j < 2; j++)
            {
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_objectSSBO[j]);
                glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                                i * sizeof(Object),
                                sizeof(Object),
                                &resetObject);
            }
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }
    }

    //  Clear contact buffer to remove stale warm start data
    ClearContactBuffer();
    g_groupsDirty = true;
}

// ============================================================================
// Update a single object's data from CPU
// ============================================================================
void Objects::UpdateObjectCPU(int index, const Object &newData)
{
    if (index >= 0 && index < g_numObjects)
    {
        if (g_useMapBuffer && g_mappedSSBO[0])
        {
            g_mappedSSBO[0][index] = newData;
            g_mappedSSBO[1][index] = newData;
        }
        else
        {
            for (int i = 0; i < 2; i++)
            {
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_objectSSBO[i]);
                glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                                index * sizeof(Object),
                                sizeof(Object),
                                &newData);
            }
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }
    }
}

// ============================================================================
// Set default object type for new objects
// ============================================================================
void Objects::SetDefaultObjectType(int type)
{
    g_currentDefaultObjectType = type;
}

// ============================================================================
// Set global system parameters
// ============================================================================
void Objects::SetSystemParameters(float gravity, float damping, float stiffness)
{
    g_currentSystemGravity = gravity;
    g_currentSystemDamping = damping;
    g_currentSystemStiffness = stiffness;
}

// ============================================================================
// Get quad rendering shader program ID
// ============================================================================
GLuint Objects::GetQuadProgram()
{
    return g_programQuad;
}

// ============================================================================
// Get compute shader program ID
// ============================================================================
GLuint Objects::GetComputeProgram()
{
    return g_programCompute;
}

// ============================================================================
// Get current number of objects
// ============================================================================
int Objects::GetNumObjects()
{
    return g_numObjects;
}

// ============================================================================
// Clean up all resources
// ============================================================================
void Objects::Cleanup()
{
    // Unmap buffers if mapped
    if (g_useMapBuffer)
    {
        for (int i = 0; i < 2; i++)
        {
            if (g_mappedSSBO[i])
            {
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_objectSSBO[i]);
                glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
                g_mappedSSBO[i] = nullptr;
            }
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    if (g_paintTexture[0] || g_paintTexture[1])
    {
        glDeleteTextures(2, g_paintTexture);
        g_paintTexture[0] = 0;
        g_paintTexture[1] = 0;
    }

    // Delete shader programs
    glDeleteProgram(g_programCompute);
    glDeleteProgram(g_programQuad);
    g_programCompute = 0;
    g_programQuad = 0;

    g_computeShaderReady = false;
    g_quadShaderReady = false;

    // Delete all buffers and VAOs
    SafeDeleteBuffers(g_objectSSBO, 2);
    SafeDeleteVertexArrays(g_renderVAO, 2);

    SafeDeleteBuffers(&g_allTokensSSBO, 1);
    SafeDeleteBuffers(&g_allConstantsSSBO, 1);
    SafeDeleteBuffers(&g_mappingsSSBO, 1);
    SafeDeleteBuffers(&g_initialPosSSBO, 1);
    SafeDeleteBuffers(&g_constraintsSSBO, 1);
    SafeDeleteBuffers(&g_objectConstraintsSSBO, 1);
    SafeDeleteBuffers(&g_collisionPropsSSBO, 1);
    SafeDeleteBuffers(&g_contactBufferSSBO, 1); //  Delete contact buffer

    // ---- clean up scratchpad and signal queue ----
    if (g_scratchpadPool)
        glDeleteBuffers(1, &g_scratchpadPool);
    if (g_signalQueueBuffer)
        glDeleteBuffers(1, &g_signalQueueBuffer);
    g_scratchpads.clear();
    // ----------------------------------------------------

    // Clear all data structures
    g_numObjects = 0;
    g_allTokens.clear();
    g_allConstants.clear();
    g_equationMappings.clear();
    g_equationStringToID.clear();
    g_allConstraints.clear();
    g_objectConstraintMappings.clear();
    g_collisionProperties.clear();
    g_collisionMatrix.clear();

    // Reset collision parameters
    g_enableWarmStart = false;
    g_maxContactIterations = 3;
    g_useAnalyticalCollision = true;
}

// ============================================================================
// Initialize paint shader and related resources
// ============================================================================
void Objects::InitPaintShader(int screenWidth, int screenHeight)
{
    // Determine texture size (user‑set resolution or fallback to window size)
    int texW = (g_paintWidth > 0) ? g_paintWidth : screenWidth;
    int texH = (g_paintHeight > 0) ? g_paintHeight : screenHeight;

    // Load paint.comp asynchronously (same as before)
    g_paintLoader.LoadComputeShaderAsync(
        "paint.comp",
        [](GLuint program)
        {
            g_paintShaderProgram = program;
            g_paintShaderReady = true;
            // Get the uniform location for uPrevFrame after linking
            uPrevFrameLoc = glGetUniformLocation(program, "uPrevFrame");
            // Also store other uniform locations if needed (but we'll use glGetUniformLocation each time)
        },
        [](const std::string &error)
        {
            std::cerr << "[AsyncLoader] Paint shader FAILED: " << error << std::endl;
            g_paintShaderReady = false;
        });

    // Create two textures for double‑buffering
    glGenTextures(2, g_paintTexture);
    for (int i = 0; i < 2; ++i)
    {
        glBindTexture(GL_TEXTURE_2D, g_paintTexture[i]);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, texW, texH);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    // Initialize read/write indices
    g_paintWriteIdx = 0;
    g_paintReadIdx = 1;

    // Create SSBOs for paint tokens and constants
    glGenBuffers(1, &g_paintTokenSSBO);
    glGenBuffers(1, &g_paintConstSSBO);

    // Create fullscreen quad VAO and VBO 
    float verts[] = {
        -1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1,
        -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
    glGenVertexArrays(1, &g_paintQuadVAO);
    glGenBuffers(1, &g_paintQuadVBO);
    glBindVertexArray(g_paintQuadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_paintQuadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glBindVertexArray(0);

    // Simple quad shader (draws the paint texture)
    const char *quadVert = R"(
        #version 430 core
        layout(location=0) in vec2 pos;
        layout(location=1) in vec2 uv;
        out vec2 vUV;
        void main() { vUV = uv; gl_Position = vec4(pos, 0, 1); }
    )";
    const char *quadFrag = R"(
        #version 430 core
        in vec2 vUV;
        out vec4 FragColor;
        uniform sampler2D uTex;
        void main() { FragColor = texture(uTex, vUV); }
    )";
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &quadVert, nullptr);
    glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &quadFrag, nullptr);
    glCompileShader(fs);
    g_paintQuadProgram = glCreateProgram();
    glAttachShader(g_paintQuadProgram, vs);
    glAttachShader(g_paintQuadProgram, fs);
    glLinkProgram(g_paintQuadProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);
}

void Objects::ResizePaintTexture(int width, int height)
{
    for (int i = 0; i < 2; ++i)
    {
        if (g_paintTexture[i])
        {
            glDeleteTextures(1, &g_paintTexture[i]);
            g_paintTexture[i] = 0;
        }
        glGenTextures(1, &g_paintTexture[i]);
        glBindTexture(GL_TEXTURE_2D, g_paintTexture[i]);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Objects::SetPaintResolution(int width, int height)
{
    g_paintWidth = width;
    g_paintHeight = height;
    if (g_paintTexture[0])
    {
        ResizePaintTexture(width, height);
    }
}

void Objects::SetPaintEquation(const std::vector<int> &tokens_r, const std::vector<float> &consts_r,
                               const std::vector<int> &tokens_g, const std::vector<float> &consts_g,
                               const std::vector<int> &tokens_b, const std::vector<float> &consts_b,
                               const std::vector<int> &tokens_a, const std::vector<float> &consts_a)
{
    g_paintTokens_r = tokens_r;
    g_paintConsts_r = consts_r;
    g_paintTokens_g = tokens_g;
    g_paintConsts_g = consts_g;
    g_paintTokens_b = tokens_b;
    g_paintConsts_b = consts_b;
    g_paintTokens_a = tokens_a;
    g_paintConsts_a = consts_a;
    g_hasPaintEquation = true;

    std::vector<int> allTokens;
    std::vector<float> allConsts;
    allTokens.insert(allTokens.end(), tokens_r.begin(), tokens_r.end());
    allTokens.insert(allTokens.end(), tokens_g.begin(), tokens_g.end());
    allTokens.insert(allTokens.end(), tokens_b.begin(), tokens_b.end());
    allTokens.insert(allTokens.end(), tokens_a.begin(), tokens_a.end());
    allConsts.insert(allConsts.end(), consts_r.begin(), consts_r.end());
    allConsts.insert(allConsts.end(), consts_g.begin(), consts_g.end());
    allConsts.insert(allConsts.end(), consts_b.begin(), consts_b.end());
    allConsts.insert(allConsts.end(), consts_a.begin(), consts_a.end());

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_paintTokenSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, allTokens.size() * sizeof(int), allTokens.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_paintConstSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, allConsts.size() * sizeof(float), allConsts.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void Objects::DispatchPaint(int screenWidth, int screenHeight, float camX, float camY, float zoom, int objectBufferIndex)
{
    if (!g_paintShaderReady && g_paintScriptID < 0)
        return; // nothing to render

    int texW = (g_paintWidth > 0) ? g_paintWidth : screenWidth;
    int texH = (g_paintHeight > 0) ? g_paintHeight : screenHeight;

    float aspect = (float)screenWidth / (float)screenHeight;
    float halfHeight = zoom;
    float halfWidth = halfHeight * aspect;

    // ---- Track previous camera state for pan/zoom compensation ----
    if (g_firstPaintFrame) {
        g_prevCamPos = glm::vec2(camX, camY);
        g_prevZoom = zoom;
        g_firstPaintFrame = false;
    }

    // Compute pan delta in UV space (fraction of texture dimensions)
    glm::vec2 panWorld = glm::vec2(camX, camY) - g_prevCamPos;
    glm::vec2 panUV = panWorld / glm::vec2(2.0f * halfWidth, 2.0f * halfHeight);

    // Zoom ratio: previous zoom / current zoom ( >1 when zooming in)
    float zoomRatio = g_prevZoom / zoom;

    // World‑space size of one pixel (for scaling sampling radii)
    float scale = 2.0f * zoom / (float)screenHeight;

    // Update previous values for next frame
    g_prevCamPos = glm::vec2(camX, camY);
    g_prevZoom = zoom;

    // -------------------------------------------------------------
    // 1. Check if we have a paint script (JIT) to run
    // -------------------------------------------------------------
    if (g_paintScriptID >= 0 && g_scriptManager)
    {
        GLuint program = g_scriptManager->getProgram(g_paintScriptID);
        if (program)
        {
            glUseProgram(program);

            // ---- Set all uniforms for the paint script ----
            glUniform1f(glGetUniformLocation(program, "uCameraX"), camX);
            glUniform1f(glGetUniformLocation(program, "uCameraY"), camY);
            glUniform1f(glGetUniformLocation(program, "uHalfWidth"), halfWidth);
            glUniform1f(glGetUniformLocation(program, "uHalfHeight"), halfHeight);
            glUniform1i(glGetUniformLocation(program, "uScreenWidth"), screenWidth);
            glUniform1i(glGetUniformLocation(program, "uScreenHeight"), screenHeight);
            glUniform1i(glGetUniformLocation(program, "uTexWidth"), texW);
            glUniform1i(glGetUniformLocation(program, "uTexHeight"), texH);
            glUniform1f(glGetUniformLocation(program, "uTime"), g_simulationTime);
            glUniform1f(glGetUniformLocation(program, "uDt"), 0.001f);
            glUniform1i(glGetUniformLocation(program, "uNumObjects"), g_numObjects);

            // ---- New uniforms for pan/zoom compensation ----
            glUniform2f(glGetUniformLocation(program, "uTexSize"), (float)texW, (float)texH);
            glUniform2f(glGetUniformLocation(program, "uPanDelta"), panUV.x, panUV.y);
            glUniform1f(glGetUniformLocation(program, "uZoomRatio"), zoomRatio);
            glUniform1f(glGetUniformLocation(program, "uScale"), scale);

            // ---- set scratchpad offsets ----
            GLint offsetLoc = glGetUniformLocation(program, "uScratchpadOffsets");
            if (offsetLoc != -1) {
                int offsets[16] = {0};
                for (auto& pair : g_scratchpads) {
                    if (pair.first < 16)
                        offsets[pair.first] = (int)(pair.second.offsetInPool);
                }
                glUniform1iv(offsetLoc, 16, offsets);
            }
            GLint capLoc = glGetUniformLocation(program, "uSignalQueueCapacity");
            if (capLoc != -1) glUniform1ui(capLoc, (GLuint)g_signalQueueCapacity);

            // Bind textures
            glBindImageTexture(0, g_paintTexture[g_paintWriteIdx], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, g_paintTexture[g_paintReadIdx]);
            glUniform1i(glGetUniformLocation(program, "uPrevFrame"), 1);

            // Bind object SSBO (binding 0) for p[index] access
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, g_objectSSBO[objectBufferIndex]);

            // ---- bind scratchpad pool (binding 10) for paint shader ----
            if (g_scratchpadPool != 0)
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SCRATCHPAD_BINDING, g_scratchpadPool);

            // Dispatch compute shader
            glDispatchCompute((texW + 15) / 16, (texH + 15) / 16, 1);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

            // ---- Draw the quad using the texture we just wrote ----
            glUseProgram(g_paintQuadProgram);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, g_paintTexture[g_paintWriteIdx]);
            glUniform1i(glGetUniformLocation(g_paintQuadProgram, "uTex"), 0);
            glBindVertexArray(g_paintQuadVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);
            glUseProgram(0);

            // ---- Swap indices for next frame ----
            int oldWrite = g_paintWriteIdx;
            g_paintWriteIdx = g_paintReadIdx;
            g_paintReadIdx = oldWrite;

            return; // done with script path
        }
    }

    // -------------------------------------------------------------
    // 2. Fallback to token‑based paint equation (original implementation)
    // -------------------------------------------------------------
    if (!g_hasPaintEquation || !g_paintShaderReady)
        return;

    glUseProgram(g_paintShaderProgram);

    // Bind SSBOs (object data, token buffer, constant buffer)
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, g_objectSSBO[objectBufferIndex]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, g_paintTokenSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, g_paintConstSSBO);

    // ---- bind scratchpad pool (binding 10) for paint shader ----
    if (g_scratchpadPool != 0)
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SCRATCHPAD_BINDING, g_scratchpadPool);

    // ---- set scratchpad offsets ----
    GLint offsetLoc = glGetUniformLocation(g_paintShaderProgram, "uScratchpadOffsets");
    if (offsetLoc != -1) {
        int offsets[16] = {0};
        for (auto& pair : g_scratchpads) {
            if (pair.first < 16)
                offsets[pair.first] = (int)(pair.second.offsetInPool);
        }
        glUniform1iv(offsetLoc, 16, offsets);
    }

    // Double‑buffering: bind write texture as image, read texture as sampler
    glBindImageTexture(0, g_paintTexture[g_paintWriteIdx], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_paintTexture[g_paintReadIdx]);
    GLint uPrevFrameLoc = glGetUniformLocation(g_paintShaderProgram, "uPrevFrame");
    if (uPrevFrameLoc != -1)
        glUniform1i(uPrevFrameLoc, 1);

    // Camera and projection uniforms
    glUniform1f(glGetUniformLocation(g_paintShaderProgram, "uCameraX"), camX);
    glUniform1f(glGetUniformLocation(g_paintShaderProgram, "uCameraY"), camY);
    glUniform1f(glGetUniformLocation(g_paintShaderProgram, "uHalfWidth"), halfWidth);
    glUniform1f(glGetUniformLocation(g_paintShaderProgram, "uHalfHeight"), halfHeight);
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uScreenWidth"), screenWidth);
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uScreenHeight"), screenHeight);
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uTexWidth"), texW);
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uTexHeight"), texH);
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uNumObjects"), g_numObjects);
    glUniform1f(glGetUniformLocation(g_paintShaderProgram, "uTime"), g_simulationTime);

    // ---- New uniforms for pan/zoom compensation (token shader may ignore them, but we set them anyway) ----
    glUniform2f(glGetUniformLocation(g_paintShaderProgram, "uTexSize"), (float)texW, (float)texH);
    glUniform2f(glGetUniformLocation(g_paintShaderProgram, "uPanDelta"), panUV.x, panUV.y);
    glUniform1f(glGetUniformLocation(g_paintShaderProgram, "uZoomRatio"), zoomRatio);
    glUniform1f(glGetUniformLocation(g_paintShaderProgram, "uScale"), scale);

    // Paint equation parameters (R, G, B, A)
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uTokenOffset_r"), 0);
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uTokenCount_r"), (int)g_paintTokens_r.size());
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uConstOffset_r"), 0);

    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uTokenOffset_g"), (int)g_paintTokens_r.size());
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uTokenCount_g"), (int)g_paintTokens_g.size());
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uConstOffset_g"), (int)g_paintConsts_r.size());

    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uTokenOffset_b"),
                (int)(g_paintTokens_r.size() + g_paintTokens_g.size()));
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uTokenCount_b"), (int)g_paintTokens_b.size());
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uConstOffset_b"),
                (int)(g_paintConsts_r.size() + g_paintConsts_g.size()));

    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uTokenOffset_a"),
                (int)(g_paintTokens_r.size() + g_paintTokens_g.size() + g_paintTokens_b.size()));
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uTokenCount_a"), (int)g_paintTokens_a.size());
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uConstOffset_a"),
                (int)(g_paintConsts_r.size() + g_paintConsts_g.size() + g_paintConsts_b.size()));

    // Dispatch compute shader
    glDispatchCompute((texW + 15) / 16, (texH + 15) / 16, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    // Draw the paint texture as a fullscreen quad (using the texture we just wrote)
    glUseProgram(g_paintQuadProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_paintTexture[g_paintWriteIdx]);
    glUniform1i(glGetUniformLocation(g_paintQuadProgram, "uTex"), 0);
    glBindVertexArray(g_paintQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    // Swap indices for next frame
    int oldWrite = g_paintWriteIdx;
    g_paintWriteIdx = g_paintReadIdx;
    g_paintReadIdx = oldWrite;

    glUseProgram(0);
}

// ============================================================================
// Update shader loading status
// ============================================================================
void Objects::UpdateShaderLoadingStatus()
{
    g_computeLoader.Update();
    g_quadLoader.Update();
    g_paintLoader.Update();
}

// ============================================================================
// Check if compute shader is ready
// ============================================================================
bool Objects::IsComputeShaderReady()
{
    return g_computeShaderReady;
}

// ============================================================================
// Check if quad shader is ready
// ============================================================================
bool Objects::IsQuadShaderReady()
{
    return g_quadShaderReady;
}

// ============================================================================
// Check if all shaders are ready
// ============================================================================
bool Objects::AreAllShadersReady()
{
    return g_computeShaderReady && g_quadShaderReady && g_paintShaderReady;
}

// ============================================================================
// Get overall shader loading progress
// ============================================================================
float Objects::GetShaderLoadProgress()
{
    float computeProgress = g_computeLoader.GetProgress();
    float quadProgress = g_quadLoader.GetProgress();
    float paintProgress = g_paintLoader.GetProgress();
    return (computeProgress + quadProgress + paintProgress) / 3.0f;
}

// ============================================================================
// Get shader loading status message
// ============================================================================
std::string Objects::GetShaderLoadStatusMessage()
{
    if (!g_computeShaderReady)
        return "[1/3] " + g_computeLoader.GetStatusMessage();
    else if (!g_quadShaderReady)
        return "[2/3] " + g_quadLoader.GetStatusMessage();
    else if (!g_paintShaderReady)
        return "[3/3] " + g_paintLoader.GetStatusMessage();
    else
        return "All shaders ready!";
}

// ============================================================================
// COLLISION MANAGEMENT FUNCTIONS
// ============================================================================

// Enable or disable collisions for an object
void Objects::SetCollisionEnabled(int objectIndex, bool enabled)
{
    if (objectIndex < 0 || objectIndex >= g_numObjects)
        return;

    g_collisionProperties[objectIndex].enabled = enabled ? 1 : 0;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_collisionPropsSSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                    objectIndex * sizeof(CollisionProperties),
                    sizeof(CollisionProperties),
                    &g_collisionProperties[objectIndex]);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

// Set collision shape for an object
void Objects::SetCollisionShape(int objectIndex, CollisionShape shape)
{
    if (objectIndex < 0 || objectIndex >= g_numObjects)
        return;

    g_collisionProperties[objectIndex].shapeType = static_cast<int>(shape);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_collisionPropsSSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                    objectIndex * sizeof(CollisionProperties),
                    sizeof(CollisionProperties),
                    &g_collisionProperties[objectIndex]);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

// Set collision material properties for an object
void Objects::SetCollisionProperties(int objectIndex, float restitution, float friction)
{
    if (objectIndex < 0 || objectIndex >= g_numObjects)
        return;

    g_collisionProperties[objectIndex].restitution = clamp(restitution, 0.0f, 1.0f);
    g_collisionProperties[objectIndex].friction = clamp(friction, 0.0f, 1.0f);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_collisionPropsSSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                    objectIndex * sizeof(CollisionProperties),
                    sizeof(CollisionProperties),
                    &g_collisionProperties[objectIndex]);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

// Get collision properties for an object
CollisionProperties Objects::GetCollisionProperties(int objectIndex)
{
    if (objectIndex < 0 || objectIndex >= g_numObjects)
        return CollisionProperties{};

    return g_collisionProperties[objectIndex];
}

// Enable or disable collisions between two specific objects
void Objects::EnableCollisionBetween(int obj1, int obj2, bool enable)
{
    if (obj1 < 0 || obj1 >= Objects::MAX_OBJECTS || obj2 < 0 || obj2 >= Objects::MAX_OBJECTS)
        return;

    g_collisionMatrix[obj1][obj2] = enable;
    g_collisionMatrix[obj2][obj1] = enable;
}

// Check if collisions are enabled for an object
bool Objects::IsCollisionEnabled(int objectIndex)
{
    if (objectIndex < 0 || objectIndex >= g_numObjects)
        return false;
    return g_collisionProperties[objectIndex].enabled == 1;
}

// ============================================================================
// SCRATCHPAD AND SIGNAL QUEUE METHODS
// ============================================================================

// ----------------------------------------------------------------------------
// Scratchpad implementation
// ----------------------------------------------------------------------------

int Objects::CreateScratchpad(size_t numElements) {
    if (numElements == 0) return -1;

    // Ensure pool is allocated
    if (g_scratchpadPool == 0) {
        glGenBuffers(1, &g_scratchpadPool);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_scratchpadPool);
        glBufferData(GL_SHADER_STORAGE_BUFFER, MAX_SCRATCHPAD_POOL_SIZE, nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SCRATCHPAD_BINDING, g_scratchpadPool);
    }

    // Calculate aligned size in bytes
    size_t sizeNeeded = numElements * sizeof(float);
    size_t alignment = 16;
    sizeNeeded = (sizeNeeded + alignment - 1) & ~(alignment - 1);

    // Allocate ID
    int id = -1;
    if (!g_freeScratchpadIds.empty()) {
        id = g_freeScratchpadIds.front();
        g_freeScratchpadIds.pop();
    } else if (g_nextScratchpadId < 16) {
        id = g_nextScratchpadId++;
    } else {
        std::cerr << "[Objects] Scratchpad ID limit (16) reached. Cannot create new scratchpad." << std::endl;
        return -1;
    }

    // Allocate pool space (first-fit)
    size_t offset = 0;
    bool found = false;
    for (auto it = g_freeBlocks.begin(); it != g_freeBlocks.end(); ++it) {
        if (it->second >= sizeNeeded) {
            offset = it->first;
            if (it->second > sizeNeeded) {
                // Split the block
                g_freeBlocks.emplace_back(it->first + sizeNeeded, it->second - sizeNeeded);
            }
            g_freeBlocks.erase(it);
            found = true;
            break;
        }
    }
    if (!found) {
        // Allocate at the end
        offset = g_scratchpadPoolSize;
        if (offset + sizeNeeded > MAX_SCRATCHPAD_POOL_SIZE) {
            std::cerr << "[Objects] Scratchpad pool exhausted! Requested " << sizeNeeded
                      << " bytes, available " << (MAX_SCRATCHPAD_POOL_SIZE - g_scratchpadPoolSize)
                      << " bytes." << std::endl;
            // Return the ID to the free list
            g_freeScratchpadIds.push(id);
            return -1;
        }
        g_scratchpadPoolSize += sizeNeeded;
    }

    Scratchpad sp;
    sp.buffer = g_scratchpadPool;
    sp.numElements = numElements;
    sp.offsetInPool = offset / sizeof(float); // offset in floats
    sp.valid = true;
    g_scratchpads[id] = sp;

    return id;
}

std::vector<int> Objects::GetScratchpadIDs()
{
    std::vector<int> ids;
    ids.reserve(g_scratchpads.size());
    for (const auto& pair : g_scratchpads) {
        ids.push_back(pair.first);
    }
    return ids;
}

void Objects::DestroyScratchpad(int id) {
    auto it = g_scratchpads.find(id);
    if (it == g_scratchpads.end()) return;

    // Free the ID
    g_freeScratchpadIds.push(id);

    // Free the pool block
    size_t offset = it->second.offsetInPool * sizeof(float);
    size_t size = it->second.numElements * sizeof(float);
    // Ensure size is aligned (it should be)
    size_t alignment = 16;
    size = (size + alignment - 1) & ~(alignment - 1);

    // Insert into free blocks and merge with adjacent blocks
    auto insert_pos = g_freeBlocks.begin();
    while (insert_pos != g_freeBlocks.end() && insert_pos->first < offset) ++insert_pos;

    // Merge with previous if adjacent
    if (insert_pos != g_freeBlocks.begin()) {
        auto prev = std::prev(insert_pos);
        if (prev->first + prev->second == offset) {
            prev->second += size;
            // Now check merge with next
            if (insert_pos != g_freeBlocks.end() && prev->first + prev->second == insert_pos->first) {
                prev->second += insert_pos->second;
                g_freeBlocks.erase(insert_pos);
            }
            g_scratchpads.erase(it);
            return;
        }
    }

    // Merge with next if adjacent
    if (insert_pos != g_freeBlocks.end() && offset + size == insert_pos->first) {
        insert_pos->first = offset;
        insert_pos->second += size;
    } else {
        g_freeBlocks.insert(insert_pos, {offset, size});
    }

    g_scratchpads.erase(it);
}

void Objects::UploadScratchpadData(int id, const void* data, size_t count) {
    auto it = g_scratchpads.find(id);
    if (it == g_scratchpads.end() || count > it->second.numElements) return;
    Scratchpad& sp = it->second;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, sp.buffer);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                    sp.offsetInPool * sizeof(float),
                    count * sizeof(float),
                    data);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void* Objects::MapScratchpad(int id, GLenum access) {
    auto it = g_scratchpads.find(id);
    if (it == g_scratchpads.end()) return nullptr;
    Scratchpad& sp = it->second;
    // Ensure GPU writes are visible before CPU read
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, sp.buffer);
    return glMapBufferRange(GL_SHADER_STORAGE_BUFFER,
                            sp.offsetInPool * sizeof(float),
                            sp.numElements * sizeof(float),
                            access);
}

void Objects::UnmapScratchpad(int id) {
    auto it = g_scratchpads.find(id);
    if (it == g_scratchpads.end()) return;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, it->second.buffer);
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

size_t Objects::GetScratchpadSize(int id) {
    auto it = g_scratchpads.find(id);
    if (it == g_scratchpads.end()) return 0;
    return it->second.numElements;
}

bool Objects::IsValidScratchpad(int id) {
    return g_scratchpads.find(id) != g_scratchpads.end();
}

// ----------------------------------------------------------------------------
// Signal Queue
// ----------------------------------------------------------------------------

void Objects::SetSignalQueueCapacity(size_t capacity) {
    if (capacity == g_signalQueueCapacity) return;
    g_signalQueueCapacity = capacity;
    // Recreate buffer
    if (g_signalQueueBuffer != 0) {
        glDeleteBuffers(1, &g_signalQueueBuffer);
        g_signalQueueBuffer = 0;
    }
    glGenBuffers(1, &g_signalQueueBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_signalQueueBuffer);
    // Structure: uint count; then array of signals: struct { uint agentID; uint objectIdx; float payload; }
    size_t bufferSize = sizeof(uint32_t) + capacity * (sizeof(uint32_t) + sizeof(uint32_t) + sizeof(float));
    glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, nullptr, GL_DYNAMIC_DRAW);
    // Set count to 0
    uint32_t zero = 0;
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(uint32_t), &zero);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    // Bind to binding point
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SIGNAL_QUEUE_BINDING, g_signalQueueBuffer);
}

void Objects::SetSignalQueueOverflowPolicy(int policy) {
    g_signalQueueOverflowPolicy = policy;
}

void Objects::ClearSignalQueue() {
    if (g_signalQueueBuffer == 0) return;
    uint32_t zero = 0;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_signalQueueBuffer);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(uint32_t), &zero);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

size_t Objects::GetSignalQueueCount() {
    if (g_signalQueueBuffer == 0) return 0;
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);  // ensure GPU writes are visible
    uint32_t count;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_signalQueueBuffer);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(uint32_t), &count);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    return count;
}

// ----------------------------------------------------------------------------
// Agent Dispatch
// ----------------------------------------------------------------------------

void Objects::DispatchAgent(int agentID, bool clearAfter) {
    if (g_signalQueueBuffer == 0) return;
    GLuint prog = g_scriptManager ? g_scriptManager->getProgram(agentID) : 0;
    if (prog == 0) return;

    // Ensure GPU writes are visible to CPU before reading count
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);

    uint32_t count;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_signalQueueBuffer);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(uint32_t), &count);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // Clamp to capacity to avoid reading past allocated buffer
    uint32_t effectiveCount = (count > g_signalQueueCapacity) ? (uint32_t)g_signalQueueCapacity : count;
    if (effectiveCount == 0) return;

    glUseProgram(prog);
    // Bind object SSBO (binding 0) as readonly – use the most recently updated buffer
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, g_objectSSBO[g_currentReadBufferIndex]);
    // Bind scratchpad pool (binding 10)
    if (g_scratchpadPool != 0) {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SCRATCHPAD_BINDING, g_scratchpadPool);
    }
    // Signal queue already bound at binding 11

    // ---- set scratchpad offsets ----
    GLint offsetLoc = glGetUniformLocation(prog, "uScratchpadOffsets");
    if (offsetLoc != -1) {
        int offsets[16] = {0};
        for (auto& pair : g_scratchpads) {
            if (pair.first < 16)
                offsets[pair.first] = (int)(pair.second.offsetInPool);
        }
        glUniform1iv(offsetLoc, 16, offsets);
    }

    // Set uniforms
    glUniform1i(glGetUniformLocation(prog, "uNumObjects"), g_numObjects);
    glUniform1f(glGetUniformLocation(prog, "uTime"), g_simulationTime);
    glUniform1f(glGetUniformLocation(prog, "uDt"), 0.0f); // not used in agent
    glUniform1i(glGetUniformLocation(prog, "uAgentID"), agentID);
    glUniform1i(glGetUniformLocation(prog, "uSignalCount"), (int)effectiveCount);
    GLint capLoc = glGetUniformLocation(prog, "uSignalQueueCapacity");
    if (capLoc != -1) glUniform1ui(capLoc, (GLuint)g_signalQueueCapacity);

    // Dispatch using effectiveCount
    int workGroupSize = 64;
    int numWorkGroups = (effectiveCount + workGroupSize - 1) / workGroupSize;
    glDispatchCompute(numWorkGroups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    glUseProgram(0);

    if (clearAfter) {
        ClearSignalQueue();
    }
}

void Objects::DispatchAllAgents(bool clearAfter) {
    if (!g_scriptManager) return;
    // Get all agent IDs from the ScriptManager
    auto agent_ids = g_scriptManager->getAgentIDs();
    // Dispatch each agent without clearing the queue between them
    for (int id : agent_ids) {
        DispatchAgent(id, false);
    }
    // Clear the queue once at the end if requested
    if (clearAfter) {
        ClearSignalQueue();
    }
}