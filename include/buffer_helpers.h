// Add this to a new header file: buffer_helpers.h
// Safe buffer upload utilities for Intel integrated graphics

#ifndef BUFFER_HELPERS_H
#define BUFFER_HELPERS_H

#include <glad/glad.h>
#include <iostream>
#include <vector>

namespace BufferHelpers {

/**
 * Safely upload data to a buffer, handling zero-sized uploads
 * Intel iGPU requires non-zero buffer sizes
 */
template<typename T>
inline void SafeBufferData(GLenum target, const std::vector<T>& data, GLenum usage) {
    if (data.empty()) {
        // Upload minimal valid buffer (1 element) to satisfy Intel drivers
        T dummy = T();
        glBufferData(target, sizeof(T), &dummy, usage);
    } else {
        glBufferData(target, data.size() * sizeof(T), data.data(), usage);
    }
}

/**
 * Check for OpenGL errors with context information
 */
inline bool CheckGLError(const char* operation, bool printSuccess = false) {
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::cerr << "[GL ERROR] " << operation << ": ";
        switch(err) {
            case GL_INVALID_ENUM:
                std::cerr << "GL_INVALID_ENUM (1280)"; break;
            case GL_INVALID_VALUE:
                std::cerr << "GL_INVALID_VALUE (1281) - Usually negative/zero size or invalid offset"; break;
            case GL_INVALID_OPERATION:
                std::cerr << "GL_INVALID_OPERATION (1282)"; break;
            case GL_STACK_OVERFLOW:
                std::cerr << "GL_STACK_OVERFLOW (1283)"; break;
            case GL_STACK_UNDERFLOW:
                std::cerr << "GL_STACK_UNDERFLOW (1284)"; break;
            case GL_OUT_OF_MEMORY:
                std::cerr << "GL_OUT_OF_MEMORY (1285)"; break;
            case GL_INVALID_FRAMEBUFFER_OPERATION:
                std::cerr << "GL_INVALID_FRAMEBUFFER_OPERATION (1286)"; break;
            default:
                std::cerr << "Unknown error code: " << err;
        }
        std::cerr << std::endl;
        return false;
    }
    
    if (printSuccess) {
        std::cout << "[GL OK] " << operation << std::endl;
    }
    return true;
}

/**
 * Validate buffer before binding/drawing
 */
inline bool ValidateBuffer(GLuint buffer, const char* name) {
    if (buffer == 0) {
        std::cerr << "[Buffer Validation] " << name << " is 0 (not initialized)" << std::endl;
        return false;
    }
    
    if (!glIsBuffer(buffer)) {
        std::cerr << "[Buffer Validation] " << name << " (" << buffer 
                  << ") is not a valid buffer object" << std::endl;
        return false;
    }
    
    return true;
}

/**
 * Print OpenGL context info (useful for debugging driver issues)
 */
inline void PrintGLInfo() {
    std::cout << "========== OpenGL Context Info ==========" << std::endl;
    std::cout << "Vendor:   " << glGetString(GL_VENDOR) << std::endl;
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << std::endl;
    std::cout << "Version:  " << glGetString(GL_VERSION) << std::endl;
    std::cout << "GLSL:     " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;
    
    GLint major, minor;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    std::cout << "Context:  " << major << "." << minor << " Core" << std::endl;
    
    // Check for Intel-specific issues
    const char* vendor = (const char*)glGetString(GL_VENDOR);
    if (vendor && strstr(vendor, "Intel")) {
        std::cout << "\n⚠️  Intel GPU Detected - Using safe buffer practices" << std::endl;
        std::cout << "   - Zero-sized buffer uploads will be avoided" << std::endl;
        std::cout << "   - Extra validation enabled" << std::endl;
    }
    
    std::cout << "==========================================" << std::endl;
}

/**
 * Intel GPU-safe glDrawArrays wrapper
 */
inline void SafeDrawArrays(GLenum mode, GLsizei count, const char* debugName) {
    if (count <= 0) {
        static int warnCount = 0;
        if (warnCount++ < 5) {
            std::cerr << "[SafeDrawArrays] Skipping " << debugName 
                      << " - count is " << count << std::endl;
        }
        return;
    }
    
    glDrawArrays(mode, 0, count);
    
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        static int errCount = 0;
        if (errCount++ < 5) {
            std::cerr << "[SafeDrawArrays] Error drawing " << debugName 
                      << ": " << err << std::endl;
        }
    }
}

} // namespace BufferHelpers

#endif // BUFFER_HELPERS_H