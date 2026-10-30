#include "mesh.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <string>
#include <cmath>
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace
{
    std::vector<Mesh> g_meshes;
    std::vector<int> g_freeSlots;
    std::unordered_map<size_t, int> g_cache;  // identical primitives share one VBO/IBO
    std::unordered_map<int, int> g_refCount;  // mesh id -> number of live users

    // FNV-1a 64-bit; deterministic across runs so cache hits work reliably.
    // The correct 64-bit offset basis is 14695981039346656037ULL (20 digits);
    // the previous value was missing the trailing 7.
    size_t HashString(const char *s)
    {
        size_t h = 14695981039346656037ULL;
        while (*s)
        {
            h ^= (size_t)(unsigned char)*s++;
            h *= 1099511628211ULL;
        }
        return h;
    }

    // Boost-style hash mixing. Used to combine independent fields into a
    // single cache key without the 16-bit overflow bug of the old
    // ((size_t)(dim*10000) << N) approach.
    size_t HashCombine(size_t seed, size_t v)
    {
        return seed ^ (v + 0x9E3779B97F4A7C15ULL + (seed << 6) + (seed >> 2));
    }

    // Quantize a float to a cache-key component. Returns 0 for NaN/inf, and
    // clamps huge magnitudes to keep the llround cast inside int64 range.
    size_t QuantizeKey(float v)
    {
        double d = (double)v * 10000.0;
        if (!std::isfinite(d) || std::fabs(d) > 9.0e15)
            return 0;
        return (size_t)(int64_t)std::llround(d);
    }

    // Builds the VAO/VBO/IBO for a single mesh from raw vertex/index data
    void UploadRaw(Mesh &m, const std::vector<float> &v, const std::vector<uint32_t> &i)
    {
        glGenVertexArrays(1, &m.vao);
        glGenBuffers(1, &m.vbo);
        glGenBuffers(1, &m.ibo);

        glBindVertexArray(m.vao);
        glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);

        // Attribute layout: 0=position, 1=normal, 2=uv
        const GLsizei stride = 8 * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void *)(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void *)(6 * sizeof(float)));

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, i.size() * sizeof(uint32_t), i.data(), GL_STATIC_DRAW);
        glBindVertexArray(0);

        m.vertexCount = (unsigned)(v.size() / 8);
        m.indexCount = (unsigned)i.size();

        // Compute AABB for future culling and collision use
        m.aabbMin = {1e30f, 1e30f, 1e30f};
        m.aabbMax = {-1e30f, -1e30f, -1e30f};
        for (size_t k = 0; k + 7 < v.size(); k += 8)
        {
            glm::vec3 p(v[k], v[k + 1], v[k + 2]);
            m.aabbMin = glm::min(m.aabbMin, p);
            m.aabbMax = glm::max(m.aabbMax, p);
        }
        m.valid = true;
    }
}

namespace Meshes
{
    // Single entry point for all mesh creation; every generator funnels through
    // here. The mesh starts with a refcount of 1 for the caller that just
    // created it — higher-level Register* functions that hit the cache instead
    // increment that count.
    //
    // Returns -1 on invalid input (null pointers, empty data, non-triangle
    // index counts, indices out of range for the vertex array).
    int RegisterRaw(const float *verts, size_t vertexCount,
                    const uint32_t *idx, size_t indexCount)
    {
        if (!verts || vertexCount == 0)
            return -1;
        if (!idx || indexCount == 0 || indexCount % 3 != 0)
            return -1;
        for (size_t k = 0; k < indexCount; ++k)
            if (idx[k] >= vertexCount)
                return -1;

        std::vector<float> v(verts, verts + vertexCount * 8);
        std::vector<uint32_t> i(idx, idx + indexCount);
        Mesh m;
        UploadRaw(m, v, i);

        int id;
        if (!g_freeSlots.empty())
        {
            id = g_freeSlots.back();
            g_freeSlots.pop_back();
            g_meshes[id] = m;
        }
        else
        {
            id = (int)g_meshes.size();
            g_meshes.push_back(m);
        }
        g_refCount[id] = 1;
        return id;
    }

    const Mesh *Get(int id)
    {
        if (id < 0 || id >= (int)g_meshes.size() || !g_meshes[id].valid)
            return nullptr;
        return &g_meshes[id];
    }

    int Count()
    {
        return (int)g_meshes.size();
    }

    // Returns the IDs of meshes that are currently alive. Use this to iterate
    // over meshes — Count() returns the slot-table size, which includes slots
    // freed by Destroy() and not yet reused.
    std::vector<int> GetLiveIds()
    {
        std::vector<int> out;
        out.reserve(g_meshes.size());
        for (int i = 0; i < (int)g_meshes.size(); ++i)
            if (g_meshes[i].valid)
                out.push_back(i);
        return out;
    }

    // Refcounted. If the mesh was obtained from a cached Register* call and
    // other users still hold a reference, only the count is decremented — the
    // GL resources stay alive. When the last reference is dropped, the mesh
    // is actually freed.
    void Destroy(int id)
    {
        if (id < 0 || id >= (int)g_meshes.size() || !g_meshes[id].valid)
            return;

        auto rc = g_refCount.find(id);
        if (rc != g_refCount.end())
        {
            if (--rc->second > 0)
                return;
            g_refCount.erase(rc);
        }

        Mesh &m = g_meshes[id];
        glDeleteBuffers(1, &m.vbo);
        glDeleteBuffers(1, &m.ibo);
        glDeleteVertexArrays(1, &m.vao);
        m = Mesh{};
        g_freeSlots.push_back(id);

        for (auto it = g_cache.begin(); it != g_cache.end();)
        {
            if (it->second == id)
                it = g_cache.erase(it);
            else
                ++it;
        }
    }

    void Cleanup()
    {
        for (auto &m : g_meshes)
        {
            if (!m.valid)
                continue;
            glDeleteBuffers(1, &m.vbo);
            glDeleteBuffers(1, &m.ibo);
            glDeleteVertexArrays(1, &m.vao);
        }
        g_meshes.clear();
        g_freeSlots.clear();
        g_cache.clear();
        g_refCount.clear();
    }

    static const float PI = 3.14159265359f;

    // ------------------------------------------------------------------------
    // Cached primitives
    //
    // Each Register* function:
    //   1. Computes a collision-safe cache key via HashCombine + QuantizeKey.
    //   2. On cache hit, bumps the refcount and returns the cached ID.
    //   3. On cache miss, calls RegisterRaw (which sets refcount = 1) and
    //      stores the new ID in the cache.
    // ------------------------------------------------------------------------

    int RegisterCircle(float radius, int segs)
    {
        if (segs < 3)
            segs = 3;

        size_t key = HashCombine(HashCombine(HashString("circle"), QuantizeKey(radius)),
                                 (size_t)segs);
        auto it = g_cache.find(key);
        if (it != g_cache.end())
        {
            g_refCount[it->second]++;
            return it->second;
        }

        std::vector<float> v;
        std::vector<uint32_t> i;
        v.insert(v.end(), {0, 0, 0, 0, 0, 1, 0.5f, 0.5f});
        for (int s = 0; s <= segs; ++s)
        {
            float a = 2.0f * PI * s / segs;
            v.insert(v.end(), {cosf(a) * radius, sinf(a) * radius, 0,
                               0, 0, 1,
                               cosf(a) * 0.5f + 0.5f, sinf(a) * 0.5f + 0.5f});
        }
        for (int s = 1; s <= segs; ++s)
            i.insert(i.end(), {0u, (uint32_t)s, (uint32_t)(s + 1)});

        int id = RegisterRaw(v.data(), v.size() / 8, i.data(), i.size());
        if (id < 0)
            return -1;
        g_cache[key] = id;
        return id;
    }

    int RegisterSquare(float w, float h)
    {
        size_t key = HashCombine(HashCombine(HashString("square"), QuantizeKey(w)),
                                 QuantizeKey(h));
        auto it = g_cache.find(key);
        if (it != g_cache.end())
        {
            g_refCount[it->second]++;
            return it->second;
        }

        float hw = w * 0.5f, hh = h * 0.5f;
        std::vector<float> v = {
            -hw, -hh, 0, 0, 0, 1, 0, 0,
            -hw,  hh, 0, 0, 0, 1, 0, 1,
             hw,  hh, 0, 0, 0, 1, 1, 1,
             hw, -hh, 0, 0, 0, 1, 1, 0,
        };
        std::vector<uint32_t> i = {0, 2, 1, 0, 3, 2};

        int id = RegisterRaw(v.data(), v.size() / 8, i.data(), i.size());
        if (id < 0)
            return -1;
        g_cache[key] = id;
        return id;
    }

    int RegisterSphere(float r, int segs, int rings)
    {
        if (segs < 3)
            segs = 3;
        if (rings < 2)
            rings = 2;

        size_t key = HashCombine(HashCombine(HashCombine(HashString("sphere"),
                                                         QuantizeKey(r)),
                                             (size_t)segs),
                                 (size_t)rings);
        auto it = g_cache.find(key);
        if (it != g_cache.end())
        {
            g_refCount[it->second]++;
            return it->second;
        }

        std::vector<float> v;
        std::vector<uint32_t> i;
        for (int y = 0; y <= rings; ++y)
        {
            float phi = PI * y / rings;
            float sp = sinf(phi), cp = cosf(phi);
            for (int x = 0; x <= segs; ++x)
            {
                float th = 2.0f * PI * x / segs;
                float nx = sp * cosf(th), ny = cp, nz = sp * sinf(th);
                v.insert(v.end(), {nx * r, ny * r, nz * r, nx, ny, nz,
                                   (float)x / segs, (float)y / rings});
            }
        }
        for (int y = 0; y < rings; ++y)
        {
            for (int x = 0; x < segs; ++x)
            {
                uint32_t a = y * (segs + 1) + x;
                uint32_t b = a + (segs + 1);
                // CCW viewed from outside (+r direction).
                i.insert(i.end(), {a, a + 1, b, a + 1, b + 1, b});
            }
        }

        int id = RegisterRaw(v.data(), v.size() / 8, i.data(), i.size());
        if (id < 0)
            return -1;
        g_cache[key] = id;
        return id;
    }

    int RegisterPolygon(float radius, int sides)
    {
        if (sides < 3)
            sides = 3;

        size_t key = HashCombine(HashCombine(HashString("poly"), QuantizeKey(radius)),
                                 (size_t)sides);
        auto it = g_cache.find(key);
        if (it != g_cache.end())
        {
            g_refCount[it->second]++;
            return it->second;
        }

        std::vector<float> v;
        std::vector<uint32_t> i;
        v.insert(v.end(), {0, 0, 0, 0, 0, 1, 0.5f, 0.5f});
        for (int s = 0; s <= sides; ++s)
        {
            float a = 2.0f * PI * s / sides;
            v.insert(v.end(), {cosf(a) * radius, sinf(a) * radius, 0,
                               0, 0, 1,
                               cosf(a) * 0.5f + 0.5f, sinf(a) * 0.5f + 0.5f});
        }
        for (int s = 1; s <= sides; ++s)
            i.insert(i.end(), {0u, (uint32_t)s, (uint32_t)(s + 1)});

        int id = RegisterRaw(v.data(), v.size() / 8, i.data(), i.size());
        if (id < 0)
            return -1;
        g_cache[key] = id;
        return id;
    }

    // Box centred at origin, 12 triangles.
    //
    // Winding note: the ±X and ±Z faces use {base, base+2, base+1, ...} which
    // produces an outward normal for those vertex orders, but the ±Y faces
    // with the same vertex order come out inward-facing. We therefore use the
    // opposite winding for faces whose normal has a non-zero Y component.
    int RegisterCube(float sx, float sy, float sz)
    {
        size_t key = HashCombine(HashCombine(HashCombine(HashString("cube"),
                                                         QuantizeKey(sx)),
                                             QuantizeKey(sy)),
                                 QuantizeKey(sz));
        auto it = g_cache.find(key);
        if (it != g_cache.end())
        {
            g_refCount[it->second]++;
            return it->second;
        }

        glm::vec3 h(sx * 0.5f, sy * 0.5f, sz * 0.5f);
        struct Face
        {
            glm::vec3 n;
            glm::vec3 p[4];
        };
        Face f[6] = {
            {{ 1, 0, 0}, {{ h.x, -h.y, -h.z}, { h.x, -h.y,  h.z}, { h.x,  h.y,  h.z}, { h.x,  h.y, -h.z}}},
            {{-1, 0, 0}, {{-h.x, -h.y,  h.z}, {-h.x, -h.y, -h.z}, {-h.x,  h.y, -h.z}, {-h.x,  h.y,  h.z}}},
            {{ 0, 1, 0}, {{-h.x,  h.y, -h.z}, {-h.x,  h.y,  h.z}, { h.x,  h.y,  h.z}, { h.x,  h.y, -h.z}}},
            {{ 0,-1, 0}, {{-h.x, -h.y,  h.z}, {-h.x, -h.y, -h.z}, { h.x, -h.y, -h.z}, { h.x, -h.y,  h.z}}},
            {{ 0, 0, 1}, {{ h.x, -h.y,  h.z}, {-h.x, -h.y,  h.z}, {-h.x,  h.y,  h.z}, { h.x,  h.y,  h.z}}},
            {{ 0, 0,-1}, {{-h.x, -h.y, -h.z}, { h.x, -h.y, -h.z}, { h.x,  h.y, -h.z}, {-h.x,  h.y, -h.z}}},
        };
        std::vector<float> v;
        std::vector<uint32_t> i;
        for (int face = 0; face < 6; ++face)
        {
            uint32_t base = (uint32_t)(v.size() / 8);
            for (int k = 0; k < 4; ++k)
            {
                // Perimeter-order UVs: (0,0), (1,0), (1,1), (0,1). The old
                // (k&1, (k>>1)&1) produced a bow-tie because the vertices are
                // in perimeter order, not row-major order.
                float u = (float)((k & 1) ^ ((k >> 1) & 1));
                float vv = (float)((k >> 1) & 1);
                v.insert(v.end(), {f[face].p[k].x, f[face].p[k].y, f[face].p[k].z,
                                   f[face].n.x, f[face].n.y, f[face].n.z,
                                   u, vv});
            }
            // ±Y faces need the opposite winding from ±X / ±Z faces to face
            // outward (see the winding note above RegisterCube).
            if (f[face].n.y != 0.0f)
                i.insert(i.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
            else
                i.insert(i.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
        }

        int id = RegisterRaw(v.data(), v.size() / 8, i.data(), i.size());
        if (id < 0)
            return -1;
        g_cache[key] = id;
        return id;
    }

    // Cylinder centred at origin: side quad strip + two triangle-fan caps
    int RegisterCylinder(float r, float h, int segs)
    {
        if (segs < 3)
            segs = 3;

        size_t key = HashCombine(HashCombine(HashCombine(HashString("cyl"),
                                                         QuantizeKey(r)),
                                             QuantizeKey(h)),
                                 (size_t)segs);
        auto it = g_cache.find(key);
        if (it != g_cache.end())
        {
            g_refCount[it->second]++;
            return it->second;
        }

        std::vector<float> v;
        std::vector<uint32_t> i;
        float hy = h * 0.5f;

        // Side
        for (int s = 0; s <= segs; ++s)
        {
            float a = 2.0f * PI * s / segs;
            float nx = cosf(a), nz = sinf(a);
            v.insert(v.end(), {nx * r, -hy, nz * r, nx, 0, nz, (float)s / segs, 0});
            v.insert(v.end(), {nx * r,  hy, nz * r, nx, 0, nz, (float)s / segs, 1});
        }
        for (int s = 0; s < segs; ++s)
        {
            uint32_t a = s * 2, b = a + 1, c = a + 2, d = a + 3;
            i.insert(i.end(), {a, b, c, c, b, d});
        }

        // Top cap
        uint32_t topC = (uint32_t)(v.size() / 8);
        v.insert(v.end(), {0, hy, 0, 0, 1, 0, 0.5f, 0.5f});
        for (int s = 0; s <= segs; ++s)
        {
            float a = 2.0f * PI * s / segs;
            v.insert(v.end(), {cosf(a) * r, hy, sinf(a) * r, 0, 1, 0,
                               cosf(a) * 0.5f + 0.5f, sinf(a) * 0.5f + 0.5f});
        }
        for (int s = 1; s <= segs; ++s)
            i.insert(i.end(), {topC, topC + (uint32_t)(s + 1), topC + (uint32_t)s});

        // Bottom cap
        uint32_t botC = (uint32_t)(v.size() / 8);
        v.insert(v.end(), {0, -hy, 0, 0, -1, 0, 0.5f, 0.5f});
        for (int s = 0; s <= segs; ++s)
        {
            float a = 2.0f * PI * s / segs;
            v.insert(v.end(), {cosf(a) * r, -hy, sinf(a) * r, 0, -1, 0,
                               cosf(a) * 0.5f + 0.5f, sinf(a) * 0.5f + 0.5f});
        }
        for (int s = 1; s <= segs; ++s)
            i.insert(i.end(), {botC, botC + (uint32_t)s, botC + (uint32_t)(s + 1)});

        int id = RegisterRaw(v.data(), v.size() / 8, i.data(), i.size());
        if (id < 0)
            return -1;
        g_cache[key] = id;
        return id;
    }

    // Capsule: cylinder body + two hemispherical caps, all stitched into one
    // surface
    int RegisterCapsule(float r, float h, int segs, int capRings)
    {
        if (segs < 3)
            segs = 3;
        if (capRings < 1)
            capRings = 1;

        size_t key = HashCombine(HashCombine(HashCombine(HashCombine(
                                        HashString("capsule"), QuantizeKey(r)),
                                        QuantizeKey(h)),
                                        (size_t)segs),
                                 (size_t)capRings);
        auto it = g_cache.find(key);
        if (it != g_cache.end())
        {
            g_refCount[it->second]++;
            return it->second;
        }

        std::vector<float> v;
        std::vector<uint32_t> i;
        float hy = h * 0.5f;

        // Emits a ring at height `y` with normal Y-component `ny`
        auto addRing = [&](float y, float ny) -> uint32_t
        {
            uint32_t base = (uint32_t)(v.size() / 8);
            float sp = sqrtf(std::max(0.0f, 1.0f - ny * ny));
            float rr = r * sp;
            for (int s = 0; s <= segs; ++s)
            {
                float a = 2.0f * PI * s / segs;
                float nx = cosf(a), nz = sinf(a);
                v.insert(v.end(), {nx * rr, y, nz * rr, sp * nx, ny, sp * nz,
                                   (float)s / segs, 0.5f});
            }
            return base;
        };
        // Winding chosen so surfaces face outward when A is the "lower-Y" ring
        auto stitch = [&](uint32_t A, uint32_t B)
        {
            for (int s = 0; s < segs; ++s)
            {
                i.insert(i.end(), {A + s, B + s, A + s + 1, A + s + 1, B + s, B + s + 1});
            }
        };

        // Body between the two equator rings
        uint32_t botEq = addRing(-hy, 0.0f);
        uint32_t topEq = addRing(hy, 0.0f);
        stitch(botEq, topEq);

        // Top hemisphere: equator → pole
        uint32_t prev = topEq;
        for (int k = 1; k <= capRings; ++k)
        {
            float t = (float)k / capRings;
            uint32_t cur = addRing(hy + r * t, t);
            stitch(prev, cur);
            prev = cur;
        }

        // Bottom hemisphere: equator → pole, winding reversed so normals face down
        prev = botEq;
        for (int k = 1; k <= capRings; ++k)
        {
            float t = (float)k / capRings;
            uint32_t cur = addRing(-hy - r * t, -t);
            stitch(cur, prev);
            prev = cur;
        }

        int id = RegisterRaw(v.data(), v.size() / 8, i.data(), i.size());
        if (id < 0)
            return -1;
        g_cache[key] = id;
        return id;
    }

} // namespace Meshes