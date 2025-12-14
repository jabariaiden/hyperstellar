#ifndef AXIS_H
#define AXIS_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

// Forward declarations
struct Camera;
class TextRenderer;

class Axis
{
public:
    struct GridLabel
    {
        std::string text;
        glm::vec2 position;
        bool isXAxis;
        float opacity;
        int level; // 0 = major, 1 = minor, 2 = sub-minor
    };

    // Style settings
    struct Style
    {
        glm::vec3 majorGridColor = glm::vec3(0.3f, 0.3f, 0.3f);
        glm::vec3 minorGridColor = glm::vec3(0.2f, 0.2f, 0.2f);
        glm::vec3 subMinorGridColor = glm::vec3(0.15f, 0.15f, 0.15f);
        glm::vec3 axisColor = glm::vec3(0.9f, 0.9f, 0.9f);

        float majorGridWidth = 1.5f;
        float minorGridWidth = 1.0f;
        float subMinorGridWidth = 0.5f;
        float axisWidth = 2.0f;

        bool showMajorGrid = true;
        bool showMinorGrid = true;
        bool showSubMinorGrid = false; // Only at high zoom
        bool smoothZoom = true;
        bool fadeLines = true;

        float majorSpacingBase = 1.0f;
        float minorDivisions = 5.0f;
        float subMinorDivisions = 5.0f;
    };

    // Static member variables
    static GLuint VAO, VBO, ColorVBO, WidthVBO;
    static GLuint AxisVAO, AxisVBO, AxisColorVBO, AxisWidthVBO;
    static std::vector<glm::vec2> vertices;
    static std::vector<glm::vec3> colors;
    static std::vector<float> widths;
    static std::vector<glm::vec2> axisVertices;
    static std::vector<glm::vec3> axisColors;
    static std::vector<float> axisWidths;
    static std::vector<GridLabel> labels;

    // Static functions
    static void Init();
    static void Cleanup();
    static void Update(const Camera& camera, float viewportWidth, float viewportHeight);
    static void Draw(GLuint program, const glm::mat4& projView);
    static void DrawLabels(TextRenderer& textRenderer, const Camera& camera,
        float viewportWidth, float viewportHeight);

    // Style control - TWO OPTIONS:
    // Option 1: Direct access to style (KEEP THIS FOR YOUR EXISTING CODE)
    static Style style;

    // Option 2: Getter/setter (for compatibility)
    static Style& GetStyle() { return style; }
    static void SetStyle(const Style& newStyle) { style = newStyle; }

    // Utility functions
    static float WorldToScreenX(float worldX, const Camera& camera, float viewportWidth, float viewportHeight);
    static float WorldToScreenY(float worldY, const Camera& camera, float viewportHeight);
    static glm::vec2 ScreenToWorld(float screenX, float screenY, const Camera& camera,
        float viewportWidth, float viewportHeight);

    static bool IsNearYAxis(float xWorld, const Camera& cam, float vw, float vh, float pixelThreshold);
    static bool IsNearXAxis(float yWorld, const Camera& cam, float vw, float vh, float pixelThreshold);

private:
    // Private static helper functions
    static void GenerateGridLines(const Camera& camera, float viewportWidth, float viewportHeight);
    static void GenerateAxes(const Camera& camera, float viewportWidth, float viewportHeight);
    static void GenerateLabels(const Camera& camera, float viewportWidth, float viewportHeight);

    static float CalculateOptimalSpacing(float zoom, int level);
    static float CalculateLineOpacity(float zoom, float spacing, int level);
    static std::string FormatLabel(float value, float spacing);
};

#endif