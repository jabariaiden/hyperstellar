#include "framebuffer.h"
#include <glad/glad.h>
#include <iostream>

namespace Framebuffer
{
    Framebuffer::Framebuffer(uint32_t width, uint32_t height)
    : mFbo(0),
    mTextureId(0),
    mRenderbufferId(0),
    mWidth(width),
    mHeight(height),
    mCCR(0.0f), mCCG(0.0f), mCCB(0.0f), mCCA(1.0f)
    {
        glGenFramebuffers(1, &mFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, mFbo);

        // Create the texture to render to
        glGenTextures(1, &mTextureId);
        glBindTexture(GL_TEXTURE_2D, mTextureId);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, mWidth, mHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        // Attach the texture to the framebuffer
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTextureId, 0);

        // Create and attach a renderbuffer for depth and stencil (not used here but good practice)
        glGenRenderbuffers(1, &mRenderbufferId);
        glBindRenderbuffer(GL_RENDERBUFFER, mRenderbufferId);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, mWidth, mHeight);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, mRenderbufferId);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::cerr << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << std::endl;

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    Framebuffer::~Framebuffer()
    {
        if (mRenderbufferId)
            glDeleteRenderbuffers(1, &mRenderbufferId);
        if (mTextureId)
            glDeleteTextures(1, &mTextureId);
        if (mFbo)
            glDeleteFramebuffers(1, &mFbo);
    }
    void Framebuffer::Bind()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
        glViewport(0, 0, mWidth, mHeight);
        glClearColor(mCCR, mCCG, mCCB, mCCA);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }
    void Framebuffer::Unbind()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}