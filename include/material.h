#pragma once

#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>

// Values are stable and are mirrored by the renderer's GLSL MaterialType.
enum class MaterialType : std::uint32_t
{
    SOLID_COLOR = 0,
    EMISSIVE    = 1
};

// CPU-side material description.
//
// SOLID_COLOR:
//   color = RGBA. Alpha controls opacity/transparency.
//   intensity is unused.
//
// EMISSIVE:
//   color.rgb = emission colour. color.a is unused.
//   intensity scales the emitted RGB radiance.
//
// The field order and 32-byte size are deliberately std430-friendly so this
// same record is uploaded directly to the GLSL std430 storage buffer without repacking.
struct alignas(16) Material
{
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    MaterialType type = MaterialType::SOLID_COLOR;
    std::uint32_t _padding[2] = {0, 0};

    static Material SolidColor(const glm::vec4& rgba) noexcept
    {
        Material material;
        material.type = MaterialType::SOLID_COLOR;
        material.color = rgba;
        material.intensity = 1.0f;
        return material;
    }

    static Material Emissive(const glm::vec3& rgb, float intensity = 1.0f) noexcept
    {
        Material material;
        material.type = MaterialType::EMISSIVE;
        material.color = glm::vec4(rgb, 1.0f);
        material.intensity = intensity;
        return material;
    }
};

static_assert(sizeof(MaterialType) == sizeof(std::uint32_t),
              "MaterialType must match a GLSL uint");
static_assert(offsetof(Material, color) == 0, "Material.color offset mismatch");
static_assert(offsetof(Material, intensity) == 16, "Material.intensity offset mismatch");
static_assert(offsetof(Material, type) == 20, "Material.type offset mismatch");
static_assert(sizeof(Material) == 32, "Material must match the GLSL std430 layout");


// Runtime material registry. Material IDs are stable for the lifetime of the
// registry and are used by Mesh::materialID / the renderer's material SSBO.
namespace Materials
{
    int Create(const Material& material);
    bool Update(int materialID, const Material& material);
    bool Destroy(int materialID);
    bool IsValid(int materialID) noexcept;
    const Material* Get(int materialID) noexcept;
    int Count() noexcept;

    // Called by Objects::Init / Objects::Cleanup while the GL context is current.
    bool InitializeGPU();
    void CleanupGPU();
}
