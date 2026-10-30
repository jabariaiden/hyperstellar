#include "camera.h"
#include "globals.h"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

// ============================================================================
// Camera (2D, orthographic)
//
// The 2D camera is still used by:
//   - Camera::ScreenToWorld (via SimulationWrapper::screen_to_world for
//     legacy scripts that haven't migrated to the 3D view).
//   - Camera::GetProjectionMatrix (via SimulationWrapper::get_full_frame,
//     which currently ignores the result — see note there).
//
// New 3D code should use Camera3D (below). The 2D Camera's ProcessInput was
// dead code (WASD/QE on g_camera3d is handled by
// SimulationWrapper::default_input) and has been removed along with its
// ImGui dependency.
// ============================================================================

glm::mat4 Camera::GetProjectionMatrix(float viewportWidth, float viewportHeight) const
{
    if (viewportWidth <= 0 || viewportHeight <= 0)
    {
        return glm::mat4(1.0f);
    }

    float aspect = viewportWidth / viewportHeight;
    float halfHeight = zoom;
    float halfWidth = halfHeight * aspect;

    return glm::ortho(
        position.x - halfWidth,
        position.x + halfWidth,
        position.y - halfHeight,
        position.y + halfHeight,
        -1.0f, 1.0f);
}

glm::vec2 Camera::ScreenToWorld(glm::vec2 screenPos, glm::vec2 viewportSize) const
{
    if (viewportSize.x <= 0 || viewportSize.y <= 0)
    {
        return glm::vec2(0.0f);
    }

    glm::vec2 normalized = screenPos / viewportSize;
    glm::vec2 ndc = (normalized * 2.0f) - 1.0f;
    ndc.y = -ndc.y;

    float aspect = viewportSize.x / viewportSize.y;
    float halfHeight = zoom;
    float halfWidth = halfHeight * aspect;

    glm::vec2 worldPos;
    worldPos.x = position.x + ndc.x * halfWidth;
    worldPos.y = position.y + ndc.y * halfHeight;

    return worldPos;
}

void Camera::Reset()
{
    // Note: Camera3D::Reset uses zoom = 20.0f (a wider default view for 3D).
    // The 2D camera keeps 2.0f (a tighter default appropriate for 2D scenes).
    // If you ever switch camera paths mid-scene, expect a scale jump.
    zoom = 2.0f;
    position = glm::vec2(0.0f, 0.0f);
}


// ============================================================================
// Camera3D
// ============================================================================

Camera3D g_camera3d;

// Computes the view matrix from eye / target / up.
//
// Guards against two degenerate inputs that would otherwise produce a NaN
// view matrix (and blank out the entire scene):
//   - eye == target  -> forward vector is zero.
//   - forward ∥ up   -> cross(forward, up) is zero, normalize() -> NaN.
//
// The second case is reachable from LookAt({0,20,0}, {0,0,0}) with the
// default up, and from Orbit at its pitch clamp. When detected, fall back
// to whichever world axis is least aligned with the view direction.
glm::mat4 Camera3D::View() const
{
    glm::vec3 fwd = target - eye;
    float len = glm::length(fwd);
    if (len < 1e-6f)
        return glm::mat4(1.0f);

    glm::vec3 fwdN = fwd / len;
    glm::vec3 upN  = (glm::length(up) > 1e-6f) ? glm::normalize(up)
                                               : glm::vec3(0.0f, 1.0f, 0.0f);

    // If the chosen up is nearly parallel to the view direction, glm::lookAt
    // will produce NaN. Pick a fallback up that isn't.
    if (std::abs(glm::dot(fwdN, upN)) > 0.9999f)
    {
        glm::vec3 fallback = (std::abs(fwdN.y) < 0.9f)
                             ? glm::vec3(0.0f, 1.0f, 0.0f)
                             : glm::vec3(1.0f, 0.0f, 0.0f);
        upN = fallback;
    }

    return glm::lookAt(eye, target, upN);
}

// Switches between ortho (uses zoom) and perspective (uses fov).
glm::mat4 Camera3D::Projection(int w, int h) const
{
    if (w <= 0 || h <= 0)
        return glm::mat4(1.0f);
    float aspect = (float)w / (float)h;
    if (use_ortho)
    {
        float hh = zoom, hw = hh * aspect;
        return glm::ortho(-hw, hw, -hh, hh, near_z, far_z);
    }
    return glm::perspective(glm::radians(fov), aspect, near_z, far_z);
}

// Combined view-projection for shaders that take one matrix.
glm::mat4 Camera3D::ViewProj(int w, int h) const
{
    return Projection(w, h) * View();
}

// World-space half-height of the view at the target plane; lets paint shaders
// compute pixel size without needing to know the projection mode.
//
// In perspective mode this uses |target - eye|. If the two coincide (bad
// LookAt, or a Dolly that somehow collapsed the distance), the result would
// be 0 and the paint shader would divide by it. Fall back to a small but
// non-zero value so paint uniforms stay finite.
float Camera3D::ApparentHalfHeight() const
{
    if (use_ortho)
        return zoom;

    float dist = glm::length(target - eye);
    if (dist < 1e-4f)
        return 0.1f;

    return std::tan(glm::radians(fov) * 0.5f) * dist;
}

void Camera3D::LookAt(glm::vec3 e, glm::vec3 t)
{
    // Reject degenerate calls: a zero forward vector produces an unusable
    // camera state everywhere downstream (View, Dolly, Orbit, paint).
    if (glm::length(t - e) < 1e-6f)
        return;
    eye = e;
    target = t;
}

void Camera3D::LookAt(glm::vec3 e, glm::vec3 t, glm::vec3 u)
{
    if (glm::length(t - e) < 1e-6f)
        return;
    if (glm::length(u) < 1e-6f)
        return;
    eye = e;
    target = t;
    up = glm::normalize(u);
}

// Spherical orbit around the current target.
//
// Assumes the world up axis is Y; Orbit uses +Y as its reconstruction pole.
// If `up` has been set to something else, use LookAt with an explicit eye
// rather than Orbit.
//
// Pitch is clamped ~5 degrees from the poles so the resulting view direction
// stays comfortably non-parallel to `up` (see View()'s NaN guard above).
void Camera3D::Orbit(float dyaw, float dpitch)
{
    glm::vec3 d = eye - target;
    float r = glm::length(d);
    if (r < 1e-6f)
        return;

    float yaw   = std::atan2(d.z, d.x) + dyaw;
    float pitch = std::asin(glm::clamp(d.y / r, -1.0f, 1.0f)) + dpitch;

    const float pitchLimit = glm::radians(85.0f);
    pitch = glm::clamp(pitch, -pitchLimit, pitchLimit);

    eye = target + r * glm::vec3(
                           std::cos(pitch) * std::cos(yaw),
                           std::sin(pitch),
                           std::cos(pitch) * std::sin(yaw));
}

// Translates eye and target together in the camera plane.
//
// Extracts the camera's right/up vectors from the view matrix. glm stores
// mat4 column-major, so v[col][row]; row 0 of the underlying matrix holds
// the right vector, row 1 holds up. Extracting {v[0][0], v[1][0], v[2][0]}
// reads row 0 (right.x, right.y, right.z) — verified correct.
//
// dx/dy are in world units. Callers that want screen-space pan should scale
// by ApparentHalfHeight() themselves (default_input currently doesn't wire
// pan at all — WASD orbits, Q/E dollies).
void Camera3D::Pan(float dx, float dy)
{
    glm::mat4 v = View();
    glm::vec3 right = {v[0][0], v[1][0], v[2][0]};
    glm::vec3 upv   = {v[0][1], v[1][1], v[2][1]};
    eye    += right * dx + upv * dy;
    target += right * dx + upv * dy;
}

// Moves eye along the view direction without changing target.
//
// In perspective mode, delta > 0 reduces the eye-to-target distance (zoom in)
// and delta < 0 increases it (zoom out). Distance is clamped to keep the eye
// on the correct side of the target — without this, holding a Dolly key
// would drive the eye through the target, flip the view direction, and render
// the scene from behind.
//
// In orthographic mode, eye position along the view direction has no visible
// effect (parallel projection). Change `zoom` instead, which is what the
// shaders consume.
void Camera3D::Dolly(float delta)
{
    if (use_ortho)
    {
        zoom = glm::max(0.1f, zoom - delta);
        return;
    }

    glm::vec3 fwd = target - eye;
    float dist = glm::length(fwd);
    if (dist < 1e-6f)
        return;

    float newDist = glm::max(0.1f, dist - delta);
    eye = target - (fwd / dist) * newDist;
}

void Camera3D::Reset()
{
    eye      = {0.0f, 0.0f, 20.0f};
    target   = {0.0f, 0.0f, 0.0f};
    up       = {0.0f, 1.0f, 0.0f};   // Orbit assumes +Y is up.
    use_ortho = true;
    zoom     = 20.0f;
    fov      = 50.0f;
}