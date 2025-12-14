#define NO_TEXT_RENDERING
#include "axis.h"
#include "camera.h"
#include "text_renderer.h"
#include "buffer_helpers.h"
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <algorithm>

// Static member initialization
GLuint Axis::VAO = 0;
GLuint Axis::VBO = 0;
GLuint Axis::ColorVBO = 0;
GLuint Axis::WidthVBO = 0;
GLuint Axis::AxisVAO = 0;
GLuint Axis::AxisVBO = 0;
GLuint Axis::AxisColorVBO = 0;
GLuint Axis::AxisWidthVBO = 0;
std::vector<glm::vec2> Axis::vertices;
std::vector<glm::vec3> Axis::colors;
std::vector<float> Axis::widths;
std::vector<glm::vec2> Axis::axisVertices;
std::vector<glm::vec3> Axis::axisColors;
std::vector<float> Axis::axisWidths;
std::vector<Axis::GridLabel> Axis::labels;
Axis::Style Axis::style;
// ============================================================================
// Init / Cleanup
// ============================================================================
void Axis::Init() {
    std::cout << "[Axis::Init] Initializing Axis system" << std::endl;

    // Grid VAO
    if (VAO == 0) glGenVertexArrays(1, &VAO);
    if (VBO == 0) glGenBuffers(1, &VBO);
    if (ColorVBO == 0) glGenBuffers(1, &ColorVBO);
    if (WidthVBO == 0) glGenBuffers(1, &WidthVBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, ColorVBO);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, WidthVBO);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);

    // Axis VAO (separate for rendering order control)
    if (AxisVAO == 0) glGenVertexArrays(1, &AxisVAO);
    if (AxisVBO == 0) glGenBuffers(1, &AxisVBO);
    if (AxisColorVBO == 0) glGenBuffers(1, &AxisColorVBO);
    if (AxisWidthVBO == 0) glGenBuffers(1, &AxisWidthVBO);

    glBindVertexArray(AxisVAO);

    glBindBuffer(GL_ARRAY_BUFFER, AxisVBO);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, AxisColorVBO);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, AxisWidthVBO);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);

    std::cout << "[Axis::Init] VAOs and VBOs created successfully" << std::endl;
}

void Axis::Cleanup() {
    std::cout << "[Axis::Cleanup] Cleaning up Axis system" << std::endl;

    if (VAO) { glDeleteVertexArrays(1, &VAO); VAO = 0; }
    if (VBO) { glDeleteBuffers(1, &VBO); VBO = 0; }
    if (ColorVBO) { glDeleteBuffers(1, &ColorVBO); ColorVBO = 0; }
    if (WidthVBO) { glDeleteBuffers(1, &WidthVBO); WidthVBO = 0; }
    if (AxisVAO) { glDeleteVertexArrays(1, &AxisVAO); AxisVAO = 0; }
    if (AxisVBO) { glDeleteBuffers(1, &AxisVBO); AxisVBO = 0; }
    if (AxisColorVBO) { glDeleteBuffers(1, &AxisColorVBO); AxisColorVBO = 0; }
    if (AxisWidthVBO) { glDeleteBuffers(1, &AxisWidthVBO); AxisWidthVBO = 0; }

    vertices.clear();
    colors.clear();
    widths.clear();
    axisVertices.clear();
    axisColors.clear();
    axisWidths.clear();
    labels.clear();

    std::cout << "[Axis::Cleanup] Cleanup completed" << std::endl;
}

// ============================================================================
// Smart Spacing Calculation
// ============================================================================
float Axis::CalculateOptimalSpacing(float zoom, int level) {
    // Desmos-like logarithmic spacing
    float viewportUnits = zoom * 2.0f;

    // Target: 8-15 major lines across viewport for good density
    float idealLines = 12.0f;
    float idealMajorSpacing = viewportUnits / idealLines;

    // Snap to 1, 2, 5, 10, 20, 50 pattern
    float exponent = std::floor(std::log10(idealMajorSpacing));
    float fraction = idealMajorSpacing / std::pow(10.0f, exponent);

    float niceFraction;
    if (fraction < 1.5f) niceFraction = 1.0f;
    else if (fraction < 3.0f) niceFraction = 2.0f;
    else if (fraction < 7.0f) niceFraction = 5.0f;
    else niceFraction = 10.0f;

    float majorSpacing = niceFraction * std::pow(10.0f, exponent);

    // Consistent spacing ratios
    switch (level) {
    case 0: return majorSpacing;
    case 1: return majorSpacing / 5.0f;  // Always 5 minor divisions
    case 2: return majorSpacing / 25.0f; // Always 25 sub-minor divisions
    default: return majorSpacing;
    }
}

float Axis::CalculateLineOpacity(float zoom, float spacing, int level) {
    if (!style.fadeLines) return 1.0f;

    float viewportUnits = zoom * 2.0f;
    float linesPerViewport = viewportUnits / spacing;

    float opacity = 1.0f;

    switch (level) {
    case 0: // Major lines
        if (linesPerViewport > 25) opacity = 0.4f;
        else if (linesPerViewport < 5) opacity = 0.7f;
        break;
    case 1: // Minor lines
        if (linesPerViewport > 50) opacity = 0.2f;
        else if (linesPerViewport < 15) opacity = 0.5f;
        else opacity = 0.3f;
        break;
    case 2: // Sub-minor lines
        opacity = std::min(linesPerViewport / 80.0f, 0.15f);
        break;
    }

    return glm::clamp(opacity, 0.05f, 1.0f);
}

// ============================================================================
// Grid Generation
// ============================================================================
void Axis::GenerateGridLines(const Camera& camera, float viewportWidth, float viewportHeight) {
    float aspect = viewportWidth / viewportHeight;
    float halfHeight = camera.zoom;
    float halfWidth = halfHeight * aspect;

    // ONLY generate what's visible + small buffer
    // This is the key to infinite grid - we regenerate based on camera position every frame
    float buffer = 1.2f; // Small buffer to prevent popping at edges
    float left = camera.position.x - halfWidth * buffer;
    float right = camera.position.x + halfWidth * buffer;
    float bottom = camera.position.y - halfHeight * buffer;
    float top = camera.position.y + halfHeight * buffer;

    // Generate lines for each level
    for (int level = 0; level < 3; level++) {
        if (level == 2 && !style.showSubMinorGrid) continue;
        if (level == 1 && !style.showMinorGrid) continue;

        float spacing = CalculateOptimalSpacing(camera.zoom, level);
        if (spacing <= 0) continue;

        // Choose style based on level
        glm::vec3 color;
        float width;
        switch (level) {
        case 0: color = style.majorGridColor; width = style.majorGridWidth; break;
        case 1: color = style.minorGridColor; width = style.minorGridWidth; break;
        case 2: color = style.subMinorGridColor; width = style.subMinorGridWidth; break;
        }

        float opacity = CalculateLineOpacity(camera.zoom, spacing, level);
        glm::vec3 finalColor = color * opacity;

        // Snap to grid spacing - this ensures lines stay in same world positions
        float startX = std::floor(left / spacing) * spacing;
        float startY = std::floor(bottom / spacing) * spacing;

        // Vertical lines - skip x=0 (main Y-axis)
        for (float x = startX; x <= right; x += spacing) {
            if (std::abs(x) < spacing * 0.001f) continue; // Skip Y-axis

            vertices.push_back(glm::vec2(x, bottom));
            vertices.push_back(glm::vec2(x, top));
            colors.push_back(finalColor);
            colors.push_back(finalColor);
            widths.push_back(width);
            widths.push_back(width);
        }

        // Horizontal lines - skip y=0 (main X-axis)
        for (float y = startY; y <= top; y += spacing) {
            if (std::abs(y) < spacing * 0.001f) continue; // Skip X-axis

            vertices.push_back(glm::vec2(left, y));
            vertices.push_back(glm::vec2(right, y));
            colors.push_back(finalColor);
            colors.push_back(finalColor);
            widths.push_back(width);
            widths.push_back(width);
        }
    }
}

void Axis::GenerateAxes(const Camera& camera, float viewportWidth, float viewportHeight) {
    float aspect = viewportWidth / viewportHeight;
    float halfHeight = camera.zoom;
    float halfWidth = halfHeight * aspect;

    // Axes extend across entire visible viewport + buffer
    float buffer = 1.2f;
    float left = camera.position.x - halfWidth * buffer;
    float right = camera.position.x + halfWidth * buffer;
    float bottom = camera.position.y - halfHeight * buffer;
    float top = camera.position.y + halfHeight * buffer;

    // X-axis (horizontal line at y = 0)
    axisVertices.push_back(glm::vec2(left, 0.0f));
    axisVertices.push_back(glm::vec2(right, 0.0f));
    axisColors.push_back(style.axisColor);
    axisColors.push_back(style.axisColor);
    axisWidths.push_back(style.axisWidth);
    axisWidths.push_back(style.axisWidth);

    // Y-axis (vertical line at x = 0)
    axisVertices.push_back(glm::vec2(0.0f, bottom));
    axisVertices.push_back(glm::vec2(0.0f, top));
    axisColors.push_back(style.axisColor);
    axisColors.push_back(style.axisColor);
    axisWidths.push_back(style.axisWidth);
    axisWidths.push_back(style.axisWidth);
}

// ============================================================================
// Label Generation (Text rendering optional)
// ============================================================================
std::string Axis::FormatLabel(float value, float spacing) {
    if (std::abs(value) < 0.0001f) return "0";

    // More precise decimal handling
    int decimalPlaces = 0;
    if (spacing < 0.01f) decimalPlaces = 4;
    else if (spacing < 0.1f) decimalPlaces = 3;
    else if (spacing < 1.0f) decimalPlaces = 2;
    else if (spacing < 10.0f) decimalPlaces = 1;

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(decimalPlaces) << value;

    std::string result = ss.str();

    // Remove trailing zeros and decimal point if not needed
    size_t dotPos = result.find('.');
    if (dotPos != std::string::npos) {
        result = result.erase(result.find_last_not_of('0') + 1);
        if (result.back() == '.') result.pop_back();
    }

    return result;
}

void Axis::GenerateLabels(const Camera& camera, float viewportWidth, float viewportHeight) {
    labels.clear();

    // Labels are optional - skip if text rendering is disabled
    // We'll still generate label data, but DrawLabels will decide whether to render them
    float majorSpacing = CalculateOptimalSpacing(camera.zoom, 0);

    float aspect = viewportWidth / viewportHeight;
    float halfHeight = camera.zoom;
    float halfWidth = halfHeight * aspect;

    // Generate labels only for visible area
    float buffer = 1.2f;
    float left = camera.position.x - halfWidth * buffer;
    float right = camera.position.x + halfWidth * buffer;
    float bottom = camera.position.y - halfHeight * buffer;
    float top = camera.position.y + halfHeight * buffer;

    // Snap to grid
    float startX = std::floor(left / majorSpacing) * majorSpacing;
    float startY = std::floor(bottom / majorSpacing) * majorSpacing;

    float opacity = CalculateLineOpacity(camera.zoom, majorSpacing, 0);

    // X-axis labels
    for (float x = startX; x <= right; x += majorSpacing) {
        if (std::abs(x) < majorSpacing * 0.01f) continue; // Skip origin

        GridLabel label;
        label.text = FormatLabel(x, majorSpacing);
        label.position = glm::vec2(x, 0.0f);
        label.isXAxis = true;
        label.opacity = opacity;
        label.level = 0;
        labels.push_back(label);
    }

    // Y-axis labels
    for (float y = startY; y <= top; y += majorSpacing) {
        if (std::abs(y) < majorSpacing * 0.01f) continue; // Skip origin

        GridLabel label;
        label.text = FormatLabel(y, majorSpacing);
        label.position = glm::vec2(0.0f, y);
        label.isXAxis = false;
        label.opacity = opacity;
        label.level = 0;
        labels.push_back(label);
    }

    // Origin label
    GridLabel originLabel;
    originLabel.text = "0";
    originLabel.position = glm::vec2(0.0f, 0.0f);
    originLabel.isXAxis = false;
    originLabel.opacity = 1.0f;
    originLabel.level = 0;
    labels.push_back(originLabel);
}

// ============================================================================
// Main Update
// ============================================================================
void Axis::Update(const Camera& camera, float viewportWidth, float viewportHeight) {
    std::cout << "[Axis::Update] Starting update" << std::endl;

    // Clear all buffers
    vertices.clear();
    colors.clear();
    widths.clear();
    axisVertices.clear();
    axisColors.clear();
    axisWidths.clear();
    labels.clear();

    std::cout << "[Axis::Update] Buffers cleared" << std::endl;

    // Generate grid and axes based on CURRENT camera position
    GenerateGridLines(camera, viewportWidth, viewportHeight);
    GenerateAxes(camera, viewportWidth, viewportHeight);
    GenerateLabels(camera, viewportWidth, viewportHeight);

    std::cout << "[Axis::Update] Generated "
        << vertices.size() << " vertices, "
        << axisVertices.size() << " axis vertices, "
        << labels.size() << " labels" << std::endl;

    // =====================================================================
    // CRITICAL FIX: Validate buffer sizes BEFORE uploading to GPU
    // Intel iGPU rejects zero-sized buffers with GL_INVALID_VALUE
    // =====================================================================

    // Upload grid to GPU (only if we have data)
    glBindVertexArray(VAO);

    if (!vertices.empty()) {
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec2),
            vertices.data(), GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ARRAY_BUFFER, ColorVBO);
        glBufferData(GL_ARRAY_BUFFER, colors.size() * sizeof(glm::vec3),
            colors.data(), GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ARRAY_BUFFER, WidthVBO);
        glBufferData(GL_ARRAY_BUFFER, widths.size() * sizeof(float),
            widths.data(), GL_DYNAMIC_DRAW);

        std::cout << "[Axis::Update] Uploaded grid data to GPU" << std::endl;
    }
    else {
        // Upload minimal valid buffers to prevent GL errors
        // Intel drivers need buffers to exist even if empty
        glm::vec2 dummyVert(0.0f, 0.0f);
        glm::vec3 dummyColor(0.0f, 0.0f, 0.0f);
        float dummyWidth = 0.0f;

        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec2), &dummyVert, GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ARRAY_BUFFER, ColorVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec3), &dummyColor, GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ARRAY_BUFFER, WidthVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float), &dummyWidth, GL_DYNAMIC_DRAW);

        std::cout << "[Axis::Update] Uploaded dummy grid buffers" << std::endl;
    }

    glBindVertexArray(0);

    // Upload axes to GPU (only if we have data)
    glBindVertexArray(AxisVAO);

    if (!axisVertices.empty()) {
        glBindBuffer(GL_ARRAY_BUFFER, AxisVBO);
        glBufferData(GL_ARRAY_BUFFER, axisVertices.size() * sizeof(glm::vec2),
            axisVertices.data(), GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ARRAY_BUFFER, AxisColorVBO);
        glBufferData(GL_ARRAY_BUFFER, axisColors.size() * sizeof(glm::vec3),
            axisColors.data(), GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ARRAY_BUFFER, AxisWidthVBO);
        glBufferData(GL_ARRAY_BUFFER, axisWidths.size() * sizeof(float),
            axisWidths.data(), GL_DYNAMIC_DRAW);

        std::cout << "[Axis::Update] Uploaded axis data to GPU" << std::endl;
    }
    else {
        // Upload minimal valid buffers
        glm::vec2 dummyVert(0.0f, 0.0f);
        glm::vec3 dummyColor(0.0f, 0.0f, 0.0f);
        float dummyWidth = 0.0f;

        glBindBuffer(GL_ARRAY_BUFFER, AxisVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec2), &dummyVert, GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ARRAY_BUFFER, AxisColorVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec3), &dummyColor, GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ARRAY_BUFFER, AxisWidthVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float), &dummyWidth, GL_DYNAMIC_DRAW);

        std::cout << "[Axis::Update] Uploaded dummy axis buffers" << std::endl;
    }

    glBindVertexArray(0);

    // Clear any errors that might have occurred
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::cerr << "[Axis::Update] OpenGL error after buffer upload: " << err << std::endl;
    }
    else {
        std::cout << "[Axis::Update] Update completed successfully" << std::endl;
    }
}

// ============================================================================
// Drawing
// ============================================================================
void Axis::Draw(GLuint program, const glm::mat4& projView) {
    std::cout << "[Axis::Draw] Starting draw" << std::endl;

    glUseProgram(program);

    // Set projection matrix
    GLuint projViewLoc = glGetUniformLocation(program, "uProjView");
    if (projViewLoc != GLuint(-1)) {
        glUniformMatrix4fv(projViewLoc, 1, GL_FALSE, glm::value_ptr(projView));
    }

    // Draw grid FIRST
    if (!vertices.empty()) {
        glBindVertexArray(VAO);
        std::cout << "[Axis::Draw] Drawing " << vertices.size() << " grid vertices" << std::endl;
        glDrawArrays(GL_LINES, 0, (GLsizei)vertices.size());
        glBindVertexArray(0);
    }
    else {
        std::cout << "[Axis::Draw] No grid vertices to draw" << std::endl;
    }

    // Draw axes SECOND (on top of grid)
    if (!axisVertices.empty()) {
        glBindVertexArray(AxisVAO);
        std::cout << "[Axis::Draw] Drawing " << axisVertices.size() << " axis vertices" << std::endl;
        glDrawArrays(GL_LINES, 0, (GLsizei)axisVertices.size());
        glBindVertexArray(0);
    }
    else {
        std::cout << "[Axis::Draw] No axis vertices to draw" << std::endl;
    }

    std::cout << "[Axis::Draw] Draw completed" << std::endl;
}

// ============================================================================
// Utility Functions
// ============================================================================
float Axis::WorldToScreenX(float worldX, const Camera& camera, float viewportWidth, float viewportHeight) {
    float aspect = viewportWidth / viewportHeight;
    float halfWidth = camera.zoom * aspect;
    float ndcX = (worldX - camera.position.x) / halfWidth;
    return (ndcX * 0.5f + 0.5f) * viewportWidth;
}

float Axis::WorldToScreenY(float worldY, const Camera& camera, float viewportHeight) {
    float halfHeight = camera.zoom;
    float ndcY = (worldY - camera.position.y) / halfHeight;
    return (1.0f - (ndcY * 0.5f + 0.5f)) * viewportHeight;
}

glm::vec2 Axis::ScreenToWorld(float screenX, float screenY, const Camera& camera,
    float viewportWidth, float viewportHeight) {
    float aspect = viewportWidth / viewportHeight;
    float halfHeight = camera.zoom;
    float halfWidth = halfHeight * aspect;

    float ndcX = (screenX / viewportWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (screenY / viewportHeight) * 2.0f;

    return glm::vec2(
        camera.position.x + ndcX * halfWidth,
        camera.position.y + ndcY * halfHeight
    );
}

bool Axis::IsNearYAxis(float xWorld, const Camera& cam, float vw, float vh, float pixelThreshold) {
    float xScreen = WorldToScreenX(xWorld, cam, vw, vh);
    float yAxisScreen = WorldToScreenX(0.0f, cam, vw, vh);
    return std::abs(xScreen - yAxisScreen) <= pixelThreshold;
}

bool Axis::IsNearXAxis(float yWorld, const Camera& cam, float vw, float vh, float pixelThreshold) {
    float yScreen = WorldToScreenY(yWorld, cam, vh);
    float xAxisScreen = WorldToScreenY(0.0f, cam, vh);
    return std::abs(yScreen - xAxisScreen) <= pixelThreshold;
}

// ============================================================================
// Label Drawing (Text rendering optional)
// ============================================================================
void Axis::DrawLabels(TextRenderer& textRenderer, const Camera& camera,
    float viewportWidth, float viewportHeight) {

    std::cout << "[Axis::DrawLabels] Function called" << std::endl;

    // Check if we should skip text rendering
    // This can be controlled by a compile-time flag or runtime flag
#ifdef NO_TEXT_RENDERING
    std::cout << "[Axis::DrawLabels] Text rendering disabled (NO_TEXT_RENDERING defined)" << std::endl;
    return;
#endif

    // Mark parameters as used to avoid warnings
    (void)textRenderer;
    (void)camera;
    (void)viewportWidth;
    (void)viewportHeight;

    std::cout << "[Axis::DrawLabels] Would draw " << labels.size() << " labels if text rendering enabled" << std::endl;

    // For now, just return - text rendering is optional
    // To enable text rendering, remove NO_TEXT_RENDERING from compiler flags
    // and implement the actual text rendering code below

    return;

    // ============================================================================
    // ORIGINAL TEXT RENDERING CODE BELOW (preserved for later re-enabling)
    // ============================================================================
    /*
    const float bottomMargin = 25.0f;    // Distance from bottom for x-axis labels
    const float leftMargin = 35.0f;      // Distance from left for y-axis labels
    const float edgeBuffer = 60.0f;      // Buffer from screen edges

    // Calculate where the actual axes are on screen
    float xAxisScreenY = WorldToScreenY(0.0f, camera, viewportHeight);
    float yAxisScreenX = WorldToScreenX(0.0f, camera, viewportWidth, viewportHeight);

    for (const auto& label : labels) {
        glm::vec2 screenPos;

        if (label.isXAxis) {
            // X-axis labels: position at bottom but horizontally aligned with grid line
            screenPos.x = WorldToScreenX(label.position.x, camera, viewportWidth, viewportHeight);
            screenPos.y = bottomMargin; // Fixed at bottom

            // Skip if too close to screen edges or if the grid line is off-screen
            if (screenPos.x < edgeBuffer || screenPos.x > viewportWidth - edgeBuffer) continue;

            // Center the label horizontally under the grid line
            screenPos.x -= 6.0f * label.text.length();

        }
        else {
            // Y-axis labels: position at left but vertically aligned with grid line
            screenPos.x = leftMargin; // Fixed at left
            screenPos.y = WorldToScreenY(label.position.y, camera, viewportHeight);

            // Skip if too close to screen edges or if the grid line is off-screen
            if (screenPos.y < edgeBuffer || screenPos.y > viewportHeight - edgeBuffer) continue;

            // Center the label vertically aligned with grid line
            screenPos.y -= 8.0f;
        }

        // Style settings
        glm::vec3 color = glm::vec3(0.9f, 0.9f, 0.9f);
        float fontSize = 0.35f;

        // Special styling for origin - position it at the actual axis intersection
        if (label.text == "0") {
            color = glm::vec3(1.0f, 1.0f, 1.0f);
            fontSize = 0.4f;

            // Position origin at the actual axis intersection point
            screenPos.x = yAxisScreenX - 8.0f; // Left of Y-axis
            screenPos.y = xAxisScreenY + 20.0f; // Below X-axis

            // Clamp to ensure it stays visible
            screenPos.x = glm::clamp(screenPos.x, leftMargin + 10.0f, viewportWidth - 30.0f);
            screenPos.y = glm::clamp(screenPos.y, bottomMargin + 5.0f, viewportHeight - 30.0f);
        }

        textRenderer.RenderText(label.text, screenPos.x, screenPos.y,
            fontSize, color, label.opacity,
            viewportWidth, viewportHeight);
    }
    */
    // ============================================================================
    // END OF TEXT RENDERING CODE
    // ============================================================================
}