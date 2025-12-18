#ifndef AXIS_H
#define AXIS_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

// Forward declaration
class TextRenderer;
class Camera;

class Axis {
public:
    // ============================================================================
    // Structures
    // ============================================================================
    struct GridLabel {
        std::string text;
        glm::vec2 position;
        bool isXAxis;
        float opacity;
        int level;
    };

    struct Style {
        glm::vec3 majorGridColor = glm::vec3(0.4f, 0.4f, 0.6f);
        glm::vec3 minorGridColor = glm::vec3(0.25f, 0.25f, 0.35f);
        glm::vec3 subMinorGridColor = glm::vec3(0.15f, 0.15f, 0.25f);
        glm::vec3 axisColor = glm::vec3(1.0f, 1.0f, 1.0f);

        float majorGridWidth = 1.5f;
        float minorGridWidth = 1.0f;
        float subMinorGridWidth = 0.5f;
        float axisWidth = 2.0f;

        bool showMajorGrid = true;
        bool showMinorGrid = true;
        bool showSubMinorGrid = true;
        bool smoothZoom = true;
        bool fadeLines = true;

        float minorDivisions = 5.0f;
        float subMinorDivisions = 5.0f;
    };

    // ============================================================================
    // Public API
    // ============================================================================
    static void Init();
    static void Cleanup();
    static void Update(const Camera& camera, float viewportWidth, float viewportHeight);
    static void Draw(GLuint program, const glm::mat4& projView);

    // Label rendering (optional)
    static void DrawLabels(TextRenderer& textRenderer, const Camera& camera,
        float viewportWidth, float viewportHeight);

    // Utility functions
    static float WorldToScreenX(float worldX, const Camera& camera,
        float viewportWidth, float viewportHeight);
    static float WorldToScreenY(float worldY, const Camera& camera,
        float viewportHeight);
    static glm::vec2 ScreenToWorld(float screenX, float screenY, const Camera& camera,
        float viewportWidth, float viewportHeight);

    static bool IsNearYAxis(float xWorld, const Camera& cam,
        float vw, float vh, float pixelThreshold = 5.0f);
    static bool IsNearXAxis(float yWorld, const Camera& cam,
        float vw, float vh, float pixelThreshold = 5.0f);

    // Style access
    static Style& GetStyle() { return style; }

    // ============================================================================
    // Debug/Performance
    // ============================================================================
    static size_t GetVertexCount() { return vertices.size(); }
    static size_t GetAxisVertexCount() { return axisVertices.size(); }
    static size_t GetLabelCount() { return labels.size(); }

private:
    // ============================================================================
    // Static Members (GPU resources)
    // ============================================================================
    static GLuint VAO;
    static GLuint VBO;
    static GLuint ColorVBO;
    static GLuint WidthVBO;

    static GLuint AxisVAO;
    static GLuint AxisVBO;
    static GLuint AxisColorVBO;
    static GLuint AxisWidthVBO;

    // ============================================================================
    // Static Members (CPU data)
    // ============================================================================
    static std::vector<glm::vec2> vertices;
    static std::vector<glm::vec3> colors;
    static std::vector<float> widths;

    static std::vector<glm::vec2> axisVertices;
    static std::vector<glm::vec3> axisColors;
    static std::vector<float> axisWidths;

    static std::vector<GridLabel> labels;
    static Style style;

    // ============================================================================
    // Private Helper Functions
    // ============================================================================
    // Grid generation
    static void GenerateGridLines(const Camera& camera,
        float viewportWidth, float viewportHeight);
    static void GenerateAxes(const Camera& camera,
        float viewportWidth, float viewportHeight);
    static void GenerateLabels(const Camera& camera,
        float viewportWidth, float viewportHeight);

    // Spacing calculations
    static float CalculateOptimalSpacing(float zoom, int level);
    static float CalculateLineOpacity(float zoom, float spacing, int level);

    // Label formatting
    static std::string FormatLabel(float value, float spacing);

    // Dynamic buffer calculation - THE KEY FIX
    static float CalculateDynamicBuffer(const Camera& camera,
        float halfWidth, float halfHeight);
};

#endif // AXIS_H