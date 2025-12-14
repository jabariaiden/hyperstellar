#pragma once
#include <cstdint>

namespace Framebuffer
{
    class Framebuffer
    {
    public:
        Framebuffer(uint32_t width, uint32_t height);
        ~Framebuffer();

        inline uint32_t GetFBO() const { return mFbo; }
        inline uint32_t GetTextureId() const { return mTextureId; }
        inline uint32_t GetRenderBufferId() const { return mRenderbufferId; }
        inline void GetSize(uint32_t &w, uint32_t &h)
        {
            w = mWidth;
            h = mHeight;
        }
        inline void SetClearColor(float r, float g, float b, float a)
        {
            mCCR = r;
            mCCG = g;
            mCCB = b;
            mCCA = a;
        }
        inline void GetClearColor(float &r, float &g, float &b, float &a)
        {
            r = mCCR;
            g = mCCG;
            b = mCCB;
            a = mCCA;
        }
        void Bind();
        void Unbind();

    private:
        uint32_t mFbo = 0;
        uint32_t mTextureId = 0;
        uint32_t mRenderbufferId = 0;

        uint32_t mWidth = 0, mHeight = 0;
        float mCCR, mCCG, mCCB, mCCA;
    };
}