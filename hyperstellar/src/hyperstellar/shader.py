"""
Shader generation for JIT-compiled scripts.

This module contains the wrapping functions that produce the complete GLSL
source for object, paint, and agent shaders. The AST translation is done
by GLSLGenerator in jit.py; this module provides the boilerplate and
mode-specific code.

A shared block of quaternion helpers (`_QUAT_HELPERS`) is prepended to every
wrapper so that JIT scripts and the generated conversion code can use a single
consistent quaternion convention. The GLSL Object struct mirrors the wider
C++ Object struct (position/velocity/orientation/spin are vec4s, plus mass,
charge, skin/collision/equation/script/mesh ints) so CPU and GPU see exactly
the same byte layout.

Quaternion convention:
    q = (x, y, z, w) with w the scalar part.
    `quat_axis_angle(axis, angle)` produces a unit quaternion for a rotation
    of `angle` radians about `axis`.

2D object rotation:
    Legacy 2D objects live on the XY plane, so their orientation is a pure
    rotation about the Z axis:  Rz(theta) = (0, 0, sin(theta/2), cos(theta/2)).
    The shader therefore reads theta directly from orientation.z/orientation.w
    and writes it back with `quat_axis_angle(vec3(0,0,1), theta)`. Angular
    velocity (omega) for 2D objects is stored in spin.z.

3D object rotation:
    Full 3D orientation and angular velocity are stored in orientation and
    spin. The integration path applies the legacy theta/angular contribution
    as a Z-axis quaternion, then applies the general spin vector
    (wx, wy, wz) as a first-order quaternion derivative step, re-normalising.

3D angular acceleration:
    Scripts accelerate the spin vector with `angular_x`, `angular_y` and
    `angular_z` (rad/s^2 about each world axis). `wz` is an alias of `omega`
    (spin about Z). The legacy scalar `angular` is ALSO a Z-axis angular
    acceleration, so it sums with `angular_z` if both are set. 3D scripts
    should prefer `angular_z`; 2D scripts may keep using `angular`.

User-facing collision / constraint calls:
    `apply_constraints()`, `resolve_collision(i)` and `detect_collision(i)`
    are NOT GLSL functions in this module. They are inlined by
    GLSLGenerator (jit.py) so that they operate on the script's *current*
    position/velocity/spin locals; a GLSL function writing objectsOut[] would
    be silently overwritten by the unconditional write-back at the end of
    main(). This module supplies only the `inout` helpers they expand to
    (applyConstraints, detectCollision, detectCollision3D,
    resolveCollisionWithTorque, resolveCollision3D, getMomentOfInertia[3D])
    and the `_dc_inline` boolean query helper.
"""

from typing import Set, Dict, Any


# -----------------------------------------------------------------------------
# Shared quaternion helpers
#
# These are injected after the caller-supplied `header` in every wrapper so
# that:
#   - The legacy 2D object shader can convert between theta/omega and the
#     new orientation/spin fields.
#   - The 3D object shader can read/write quaternions directly.
#   - Paint and agent scripts may also use quaternion math on object data.
# -----------------------------------------------------------------------------
_QUAT_HELPERS = """
// ----------------------------------------------------------------------------
// QUATERNION HELPERS (shared header)
//
// All vectors are stored as vec4 with .w as the scalar part.
// ----------------------------------------------------------------------------
vec4 quat_mul(vec4 a, vec4 b) {
    return vec4(
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z);
}

vec4 quat_conj(vec4 q) { return vec4(-q.xyz, q.w); }

vec4 quat_axis_angle(vec3 axis, float angle) {
    float h = angle * 0.5;
    return vec4(normalize(axis) * sin(h), cos(h));
}

vec3 quat_rotate(vec4 q, vec3 v) {
    vec3 u = q.xyz; float s = q.w;
    return 2.0*dot(u,v)*u + (s*s - dot(u,u))*v + 2.0*s*cross(u,v);
}

// Extracts the scalar 2D rotation angle (theta) about the world Z axis from
// an object's orientation quaternion. 2D objects live on the XY plane, so
// orientation is Rz(theta) = (0, 0, sin(theta/2), cos(theta/2)); the angle is
// recovered directly from the .z and .w components.
float quat_extract_2d_angle(vec4 orientation) {
    return 2.0 * atan(orientation.z, orientation.w);
}
"""


# -----------------------------------------------------------------------------
# 3D collision primitives (injected only into the object-mode shader)
#
# Shape IDs COLLISION_SPHERE (4) and COLLISION_BOX (5) are declared in the
# main shader's constants block above this block's injection point; this
# block does not redeclare them. The block exposes a CollisionInfo3D struct,
# SAT-based sphere/sphere, sphere/box and box/box detectors, an impulse-based
# resolver with friction and spin, a dispatch helper that handles A/B
# shape pairing, and `_dc_inline`, the read-only query helper that backs the
# user-facing detect_collision(other) call (inlined by jit.py).
# -----------------------------------------------------------------------------
_COLLISION_3D = """
// ----------------------------------------------------------------------------
// 3D COLLISION PRIMITIVES
//
// Each detector returns a CollisionInfo3D with `normal` pointing from A to B.
// The resolver uses the "each thread updates only its own side" pattern that
// the 2D solver uses: only `pa`, `va`, `wa` end up applied to the calling
// object; the B-side values are treated as read-only inputs.
// ----------------------------------------------------------------------------
struct CollisionInfo3D {
    bool  hasCollision;
    vec3  normal;        // unit, A -> B
    float penetration;
    vec3  contactPoint;
};

CollisionInfo3D noHit3D() {
    CollisionInfo3D c;
    c.hasCollision = false; c.normal = vec3(0.0, 0.0, 1.0);
    c.penetration = 0.0;    c.contactPoint = vec3(0.0);
    return c;
}

// Column-major rotation matrix from a quaternion: columns are the world-space
// directions of the box's local X, Y and Z axes.
mat3 quatToMat3(vec4 q) {
    return mat3(quat_rotate(q, vec3(1.0,0.0,0.0)),
                quat_rotate(q, vec3(0.0,1.0,0.0)),
                quat_rotate(q, vec3(0.0,0.0,1.0)));
}

// Scalar inertia approximation. vd = visualData (xyz box extents, w sphere radius).
// For a sphere we use (2/5) m r^2; for a box we use the trace of the box
// inertia tensor scaled into a single scalar. Both are approximations - the
// solver only needs a positive, roughly-correct magnitude to keep impulses
// stable and spin plausible.
float getMomentOfInertia3D(float m, int shape, vec4 vd) {
    if (shape == COLLISION_SPHERE) return max(EPSILON, 0.4 * m * vd.w * vd.w);
    return max(EPSILON, m * dot(vd.xyz, vd.xyz) / 18.0);
}

CollisionInfo3D detectSphereSphere(vec3 pa, float ra, vec3 pb, float rb) {
    CollisionInfo3D c = noHit3D();
    vec3 d = pb - pa;
    float rs = ra + rb, dsq = dot(d, d);
    if (dsq >= rs * rs) return c;
    float dist = sqrt(dsq);
    c.hasCollision = true;
    c.normal = (dist > 1e-6) ? d / dist : vec3(0.0, 0.0, 1.0);
    c.penetration = rs - dist;
    c.contactPoint = pa + c.normal * (ra - c.penetration * 0.5);
    return c;
}

// Sphere vs oriented box. The closest point on the box to the sphere centre
// is found in the box's local frame, then transformed back to world. Normal
// points from the sphere toward the box.
CollisionInfo3D detectSphereBox(vec3 sp, float sr, vec3 bp, vec3 hb, vec4 bq) {
    CollisionInfo3D c = noHit3D();
    vec3 local = quat_rotate(quat_conj(bq), sp - bp);
    vec3 cl = clamp(local, -hb, hb);
    vec3 diff = local - cl;
    float d2 = dot(diff, diff);
    if (d2 > sr * sr) return c;
    vec3 nLocal = vec3(0.0);
    float pen;
    if (d2 > 1e-10) {
        // Sphere centre outside the box: normal is the closest-point direction.
        float d = sqrt(d2);
        nLocal = diff / d;
        pen = sr - d;
    } else {
        // Sphere centre inside the box: pick the nearest face to escape through.
        vec3 over = hb - abs(local);
        if (over.x <= over.y && over.x <= over.z) {
            nLocal.x = (local.x >= 0.0) ? 1.0 : -1.0;
            pen = over.x + sr;
        } else if (over.y <= over.z) {
            nLocal.y = (local.y >= 0.0) ? 1.0 : -1.0;
            pen = over.y + sr;
        } else {
            nLocal.z = (local.z >= 0.0) ? 1.0 : -1.0;
            pen = over.z + sr;
        }
    }
    c.hasCollision = true;
    c.normal = -quat_rotate(bq, nLocal);       // sphere -> box
    c.penetration = pen;
    c.contactPoint = sp + c.normal * (sr - pen * 0.5);
    return c;
}

// Oriented box vs oriented box (SAT, 15 axes: 3 per box + 9 cross products).
// The minimum-overlap axis gives the normal; a small score bias prefers face
// axes over cross-product axes, so face-on contacts resolve more stably.
CollisionInfo3D detectBoxBox(vec3 pa, vec3 ha, vec4 qa, vec3 pb, vec3 hb, vec4 qb) {
    CollisionInfo3D c = noHit3D();
    mat3 Ra = quatToMat3(qa), Rb = quatToMat3(qb);
    vec3 t = pb - pa;
    float bestScore = 1e30, bestOv = 0.0;
    vec3 bestN = vec3(0.0, 0.0, 1.0);
    for (int k = 0; k < 15; ++k) {
        vec3 L;
        if (k < 3)      L = Ra[k];
        else if (k < 6) L = Rb[k - 3];
        else            L = cross(Ra[(k - 6) / 3], Rb[(k - 6) % 3]);
        float len = length(L);
        if (len < 1e-4) continue;   // near-parallel edges: skip this axis
        L /= len;
        float ra = ha.x*abs(dot(Ra[0],L)) + ha.y*abs(dot(Ra[1],L)) + ha.z*abs(dot(Ra[2],L));
        float rb = hb.x*abs(dot(Rb[0],L)) + hb.y*abs(dot(Rb[1],L)) + hb.z*abs(dot(Rb[2],L));
        float dist = dot(t, L);
        float ov = ra + rb - abs(dist);
        if (ov <= 0.0) return c;    // separating axis found
        float score = ov + (k >= 6 ? 0.001 : 0.0);
        if (score < bestScore) { bestScore = score; bestOv = ov; bestN = (dist >= 0.0) ? L : -L; }
    }
    float raN = ha.x*abs(dot(Ra[0],bestN)) + ha.y*abs(dot(Ra[1],bestN)) + ha.z*abs(dot(Ra[2],bestN));
    c.hasCollision = true;
    c.normal = bestN;
    c.penetration = bestOv;
    c.contactPoint = pa + bestN * (raN - bestOv * 0.5);
    return c;
}

// Dispatch by shape pairing. A's and B's positions/orientations are read
// directly from the passed-in Object copies; the caller is responsible for
// having already snapped those copies to the current-frame state.
CollisionInfo3D detectCollision3D(Object a, Object b, int sa, int sb) {
    if (sa == COLLISION_SPHERE && sb == COLLISION_SPHERE)
        return detectSphereSphere(a.position.xyz, a.visualData.w,
                                  b.position.xyz, b.visualData.w);
    if (sa == COLLISION_SPHERE && sb == COLLISION_BOX)
        return detectSphereBox(a.position.xyz, a.visualData.w,
                               b.position.xyz, b.visualData.xyz * 0.5, b.orientation);
    if (sa == COLLISION_BOX && sb == COLLISION_SPHERE) {
        CollisionInfo3D c = detectSphereBox(b.position.xyz, b.visualData.w,
                                            a.position.xyz, a.visualData.xyz * 0.5,
                                            a.orientation);
        c.normal = -c.normal;       // sphere->box -> A->B
        return c;
    }
    if (sa == COLLISION_BOX && sb == COLLISION_BOX)
        return detectBoxBox(a.position.xyz, a.visualData.xyz * 0.5, a.orientation,
                            b.position.xyz, b.visualData.xyz * 0.5, b.orientation);
    return noHit3D();
}

// Impulse-based resolution with friction and spin. Same "each thread updates
// only its own side" pattern as the 2D solver: `pa`/`va`/`wa` are read/write,
// `pb`/`vb`/`wb` are effectively read-only views of the other object's state.
void resolveCollision3D(inout vec3 pa, inout vec3 va, inout vec3 wa, float ma, float ia,
                        inout vec3 pb, inout vec3 vb, inout vec3 wb, float mb, float ib,
                        vec3 n, vec3 cp, float pen, float rest, float fric) {
    const float posCorr = 0.8, slop = 0.001;
    if (pen > slop) {
        float invTot = 1.0 / (ma + mb);
        vec3 corr = (pen - slop) * posCorr * n;
        pa -= corr * (mb * invTot);
        pb += corr * (ma * invTot);
    }
    vec3 ra = cp - pa, rb = cp - pb;
    vec3 rel = (vb + cross(wb, rb)) - (va + cross(wa, ra));
    float vn = dot(rel, n);
    if (vn > 0.0) return;
    vec3 raxn = cross(ra, n), rbxn = cross(rb, n);
    float den = 1.0/ma + 1.0/mb + dot(raxn,raxn)/ia + dot(rbxn,rbxn)/ib;
    if (den < EPSILON) return;
    float j = -(1.0 + clamp(rest, 0.0, 1.0)) * vn / den;
    vec3 imp = j * n;
    va -= imp / ma;            vb += imp / mb;
    wa -= cross(ra, imp) / ia; wb += cross(rb, imp) / ib;

    if (fric > 0.0) {
        rel = (vb + cross(wb, rb)) - (va + cross(wa, ra));
        vec3 tv = rel - n * dot(rel, n);
        float tl = length(tv);
        if (tl > EPSILON) {
            vec3 t = tv / tl;
            vec3 rat = cross(ra, t), rbt = cross(rb, t);
            float dent = 1.0/ma + 1.0/mb + dot(rat,rat)/ia + dot(rbt,rbt)/ib;
            float jt = clamp(-tl / dent, -j * fric, j * fric);
            vec3 fimp = jt * t;
            va -= fimp / ma;            vb += fimp / mb;
            wa -= cross(ra, fimp) / ia; wb += cross(rb, fimp) / ib;
        }
    }
}


"""


def wrap_shader(
    mode: str,
    body: str,
    header: str,
    sdf_defs: str,
    assigned_vars: Set[str],
    var_types: Dict[str, str],
    user_handles_collisions: bool,
    user_applies_constraints: bool,
    debug: bool = False
) -> str:
    """
    Wrap the user's generated GLSL body into a complete shader.

    Args:
        mode: 'object', 'paint', or 'agent'
        body: The GLSL code generated from the user's Python function.
        header: The ray‑tracing and utility header (common to all modes).
        sdf_defs: SDF and raymarch function definitions.
        assigned_vars: Set of variable names that need declaration.
        var_types: Mapping from variable name to GLSL type.
        user_handles_collisions: Whether the user called collision functions.
        user_applies_constraints: Whether the user called apply_constraints.
        debug: If True, prints the generated shader.

    Returns:
        The complete GLSL shader source code.
    """
    if mode == 'paint':
        return _wrap_paint_shader(body, header, sdf_defs, assigned_vars, var_types)
    elif mode == 'agent':
        return _wrap_agent_shader(body, header, sdf_defs, assigned_vars, var_types)
    else:
        return _wrap_object_shader(body, header, sdf_defs, assigned_vars, var_types,
                                   user_handles_collisions, user_applies_constraints)


# -----------------------------------------------------------------------------
# Object mode wrapper
#
# The object shader keeps the user's mental model (x, y, vx, vy, theta, omega)
# but stores physical state in the wide Object struct using quaternion
# orientation + spin. 2D objects live on the XY plane, so orientation is
# Rz(theta) and angular velocity is spin.z. 3D objects (created via
# add_object_3d) use the full quaternion and spin vector.
#
# On entry the shader decodes theta from orientation.z/.w and omega from
# spin.z. On exit it re-encodes theta back into a pure-Z quaternion and
# writes omega into spin.z, while also applying any per-frame spin
# contribution from wx/wy/omega through a first-order quaternion derivative.
#
# Collision handling dispatches to the 2D SAT path for shapes 1-3 and to the
# new 3D detectors for shapes 4 (SPHERE) and 5 (BOX). Mixing 2D and 3D shapes
# is a no-op for that pair, by design. A per-pair CollisionMatrix SSBO
# (binding 13, row-major [objectA * uNumObjects + objectB], 0 = excluded)
# lets the host disable collisions between specific object pairs in the
# automatic path.
#
# Angular acceleration: `angular` (legacy, Z axis) plus `angular_x`,
# `angular_y`, `angular_z` accelerate the spin vector each step. `angular`
# and `angular_z` both act about Z and sum if both are set; 3D scripts should
# prefer `angular_z`. `wz` is a read/write alias of `omega`.
#
# Internal shader state is prefixed with an underscore (_q, _w, _self, etc.)
# so that user scripts may freely assign variables named q, w, self, etc.
# -----------------------------------------------------------------------------
def _wrap_object_shader(body: str, header: str, sdf_defs: str,
                        assigned_vars: Set[str], var_types: Dict[str, str],
                        user_handles_collisions: bool,
                        user_applies_constraints: bool) -> str:
    """
    Produce the full GLSL shader for an 'object' script (2D and 3D modes).
    It includes:
      - Object SSBOs (read input, write output) using the wide Object struct
      - Index buffer for group dispatch
      - Quaternion helpers (shared header) for theta <-> orientation conversion
      - 3D sphere/box collision primitives (via _COLLISION_3D)
      - Constraint SSBOs (always bound; the automatic path is gated by #if)
      - Collision properties and per-pair collision matrix (always bound; ditto)
      - Scratchpad read (objects only read)
      - Signal queue enqueue (objects can signal)
      - Full 2D and 3D collision detection and response (optional)
      - Constraint solvers (optional)
      - Symplectic Euler integration on (x, y, z, theta, spin), including
        3D angular acceleration (angular, angular_x, angular_y, angular_z)
    """
    decls = []
    for v in sorted(assigned_vars):
        typ = var_types.get(v, "float")
        decls.append(f"    {typ} {v};")
    decls_str = "\n".join(decls)

    include_constraints = 0 if user_applies_constraints else 1
    include_collisions = 0 if user_handles_collisions else 1

    shader = f"""#version 430 core
{header}
{_QUAT_HELPERS}
{sdf_defs}
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// ----------------------------------------------------------------------------
// DATA STRUCTURES
//
// The Object struct mirrors the C++ layout in objects.cpp: 7 vec4 members
// followed by 2 floats and 5 ints. It is 160 bytes so that its std430 stride
// matches the C++ sizeof(Object).
// ----------------------------------------------------------------------------
struct Object {{
    vec4 position;           // XYZ position, .w reserved
    vec4 velocity;           // XYZ velocity, .w reserved
    vec4 orientation;        // Unit quaternion (x, y, z, w)
    vec4 spin;               // Angular velocity (xyz), .w reserved
    vec4 visualData;         // Shape/visual parameters: box extents (xyz), sphere radius (w)
    vec4 collisionData;      // Cached acceleration/other collision data
    vec4 color;              // RGBA
    float mass;
    float charge;
    int visualSkinType;
    int collisionShapeType;
    int equationID;
    int scriptID;
    int meshID;              // -1 means invisible to the mesh renderer
    int _pad[5];
}};

struct Constraint {{
    int type;
    int targetObjectID;
    float param1;
    float param2;
    float param3;
    float param4;
    int _pad1;
    int _pad2;
}};

struct ObjectConstraints {{
    int objectID;
    int numConstraints;
    int constraintOffset;
    int _pad;
}};

struct CollisionProperties {{
    int enabled;
    int shapeType;
    float restitution;
    float friction;
    float mass_factor;
    int _pad1;
    int _pad2;
    int _pad3;
}};

struct CollisionInfo {{
    bool hasCollision;
    vec2 normal;
    float penetration;
    int otherObjectID;
    vec2 contactPoint;
}};

// ----------------------------------------------------------------------------
// BUFFERS AND UNIFORMS
//
// All four constraint and collision SSBOs (Constraints, ObjectConstraint-
// Mappings, CollisionProps, CollisionMatrix) are declared unconditionally, so
// that the helpers the jit-inlined user calls expand to (applyConstraints,
// _dc_inline, detectCollision, ...) always compile. The #if gates below only
// wrap the automatic per-step paths inside main(), never the buffer
// declarations. Binding 8 (former contact buffer) is intentionally unused.
// ----------------------------------------------------------------------------
layout(std430, binding = 0) readonly buffer ObjectsIn {{ Object objectsIn[]; }};
layout(std430, binding = 1) writeonly buffer ObjectsOut {{ Object objectsOut[]; }};
layout(std430, binding = 9) readonly buffer ObjectIndices {{ int indices[]; }};

layout(std430, binding = 5) readonly buffer Constraints {{ Constraint constraints[]; }};
layout(std430, binding = 6) readonly buffer ObjectConstraintMappings {{ ObjectConstraints objectConstraints[]; }};

layout(std430, binding = 7) readonly buffer CollisionProps {{ CollisionProperties collisionProps[]; }};

// Per-pair collision filter, row-major: collisionMatrix[a * uNumObjects + b].
// 0 = pair excluded from the automatic collision path, non-zero = collide.
// The host initialises every entry to 1 and updates both (a,b) and (b,a) on
// each EnableCollisionBetween call.

uniform int uNumObjects;
uniform int uGroupCount;
uniform float uDt;
uniform float uTime;
uniform float uDerivativeEpsilon;

uniform float k;
uniform float b;
uniform float g;
uniform vec2 uGravityDir;
uniform float uRestitution;
uniform float uCoupling;
uniform vec2 uExternalForce;
uniform float uDriveFreq;
uniform float uDriveAmp;

// ----------------------------------------------------------------------------
// SCRATCHPAD (read-only for objects)
// ----------------------------------------------------------------------------
layout(std430, binding = 10) buffer ScratchpadPool {{ float scratchpadData[]; }};
uniform int uScratchpadOffsets[16];

float scratchpad_read(int id, int idx) {{
    return scratchpadData[uScratchpadOffsets[id] + idx];
}}

// ----------------------------------------------------------------------------
// SIGNAL QUEUE (objects can only enqueue)
//
// objectIdx records the caller's resolved slot (indices[global_id]), so the
// enqueued signal carries the same object identity the rest of the shader
// uses, even when the dispatch is over a script group.
// ----------------------------------------------------------------------------
struct Signal {{
    uint agentID;
    uint objectIdx;
    float payload;
}};

layout(std430, binding = 11) buffer SignalQueue {{
    uint count;
    Signal signals[];
}};

uniform uint uSignalQueueCapacity;

void signal_enqueue(uint agentID, float payload) {{
    uint _sigIdx = atomicAdd(count, 1u);
    if (_sigIdx < uSignalQueueCapacity) {{
        signals[_sigIdx].agentID = agentID;
        signals[_sigIdx].objectIdx = uint(indices[int(gl_GlobalInvocationID.x)]);
        signals[_sigIdx].payload = payload;
    }}
}}

// ----------------------------------------------------------------------------
// CONSTANTS & UTILITIES
// ----------------------------------------------------------------------------
const float EPSILON = 1e-6;
const float SAFE_MIN_VALUE = 1e-6;
const float SAFE_MAX_EXP = 50.0;
const float PI = 3.14159265359;
const int CONSTRAINT_DISTANCE = 0;
const int CONSTRAINT_BOUNDARY = 1;
const int CONSTRAINT_ANGLE = 2;
const float CONSTRAINT_STIFFNESS = 1.0;
const int MAX_CONSTRAINT_ITERATIONS = 3;

// 2D shapes
const int COLLISION_NONE = 0;
const int COLLISION_CIRCLE = 1;
const int COLLISION_AABB = 2;
const int COLLISION_POLYGON = 3;
// 3D shapes
const int COLLISION_SPHERE = 4;
const int COLLISION_BOX = 5;

float safeDivide(float n, float d) {{
    return (abs(d) < EPSILON) ? 0.0 : n / d;
}}
float safePow(float base, float exp) {{
    if (base < 0.0) {{
        float intPart;
        if (abs(modf(exp, intPart)) < EPSILON) {{
            float result = pow(-base, exp);
            if (int(intPart) % 2 == 1) result = -result;
            return result;
        }}
    }}
    return pow(max(0.0, base), exp);
}}
float safeLog(float v) {{ return log(max(SAFE_MIN_VALUE, v)); }}
float safeExp(float v) {{ return exp(clamp(v, -SAFE_MAX_EXP, SAFE_MAX_EXP)); }}
bool isInvalid(float v) {{ return isinf(v) || isnan(v); }}
vec2 sanitizeVec2(vec2 v) {{
    if (isInvalid(v.x)) v.x = 0.0;
    if (isInvalid(v.y)) v.y = 0.0;
    return v;
}}
vec4 sanitizeVec4(vec4 v) {{
    for (int i = 0; i < 4; ++i)
        if (isInvalid(v[i])) v[i] = 0.0;
    return v;
}}
float signFunc(float x) {{ return (x > 0.0) ? 1.0 : ((x < 0.0) ? -1.0 : 0.0); }}
float stepFunc(float x) {{ return (x >= 0.0) ? 1.0 : 0.0; }}

// ----------------------------------------------------------------------------
// COMPLEX NUMBERS
// ----------------------------------------------------------------------------
vec2 cAdd(vec2 a, vec2 b) {{ return a + b; }}
vec2 cSub(vec2 a, vec2 b) {{ return a - b; }}
vec2 cMul(vec2 a, vec2 b) {{
    return vec2(a.x*b.x - a.y*b.y, a.x*b.y + a.y*b.x);
}}
vec2 cDiv(vec2 a, vec2 b) {{
    float d = b.x*b.x + b.y*b.y;
    if (abs(d) < EPSILON) return vec2(0.0);
    return vec2((a.x*b.x + a.y*b.y)/d, (a.y*b.x - a.x*b.y)/d);
}}
vec2 cLog(vec2 z) {{
    return vec2(safeLog(length(z)), atan(z.y, z.x));
}}
vec2 cExp(vec2 z) {{
    float ea = safeExp(z.x);
    return vec2(ea * cos(z.y), ea * sin(z.y));
}}
vec2 cPow(vec2 b, vec2 e) {{
    if (length(b) < EPSILON) return vec2(0.0);
    return cExp(cMul(e, cLog(b)));
}}
vec2 cSin(vec2 z) {{ return vec2(sin(z.x)*cosh(z.y), cos(z.x)*sinh(z.y)); }}
vec2 cCos(vec2 z) {{ return vec2(cos(z.x)*cosh(z.y), -sin(z.x)*sinh(z.y)); }}
vec2 cTan(vec2 z) {{ return cDiv(cSin(z), cCos(z)); }}

// ----------------------------------------------------------------------------
// ROTATION HELPERS (2D)
// ----------------------------------------------------------------------------
vec2 rotatePoint(vec2 p, float a) {{
    float ca = cos(a), sa = sin(a);
    return vec2(p.x*ca - p.y*sa, p.x*sa + p.y*ca);
}}
vec2 worldToLocal(vec2 wp, vec2 op, float r) {{
    return rotatePoint(wp - op, -r);
}}
vec2 localToWorld(vec2 lp, vec2 op, float r) {{
    return op + rotatePoint(lp, r);
}}
vec2 getSupportPoint(vec2 pos, vec2 halfExt, float rot, vec2 dir) {{
    float cr = cos(rot), sr = sin(rot);
    vec2 localDir = vec2(dot(dir, vec2(cr, sr)), dot(dir, vec2(-sr, cr)));
    vec2 localSupport = vec2(sign(localDir.x) * halfExt.x, sign(localDir.y) * halfExt.y);
    return pos + vec2(localSupport.x*cr - localSupport.y*sr, localSupport.x*sr + localSupport.y*cr);
}}
float getMomentOfInertia(float mass, int shapeType, float w, float h, float r) {{
    if (shapeType == COLLISION_CIRCLE) return 0.5 * mass * r * r;
    if (shapeType == COLLISION_AABB) return (1.0/12.0) * mass * (w*w + h*h);
    return 0.5 * mass * r * r;
}}
// ----------------------------------------------------------------------------
// detect_collision(other) backing helper
//
// jit.py rewrites the user-facing `detect_collision(other)` expression into a
// call to this function. It is a pure query: it reads objectsIn[] /
// collisionProps[] and never touches objectsOut[], so (unlike the removed
// GLSL-level detect_collision) it cannot be overwritten by main()'s final
// write-back. `pos`, `z` and `q` are the caller's CURRENT-frame values and
// `p` supplies the caller's remaining fields (visualData, etc.).
//
// 2D vs 3D selection mirrors the automatic path in main(): both shapes 3D ->
// 3D detector; both 2D -> 2D detector; a mixed 2D/3D pair never collides.
// ----------------------------------------------------------------------------
bool _dc_inline(int other, int selfIdx, vec2 pos, float z, vec4 q, Object p) {{
    if (other < 0 || other >= uNumObjects || other == selfIdx) return false;
    Object _o = objectsIn[other];
    CollisionProperties _pa = collisionProps[selfIdx];
    CollisionProperties _pb = collisionProps[other];
    if (_pa.shapeType == COLLISION_NONE || _pb.shapeType == COLLISION_NONE) return false;
    if (_pa.enabled == 0 || _pb.enabled == 0) return false;
    if (_pa.shapeType >= COLLISION_SPHERE && _pb.shapeType >= COLLISION_SPHERE) {{
        Object _me = p;
        _me.position    = vec4(pos, z, 0.0);
        _me.orientation = q;
        return detectCollision3D(_me, _o, _pa.shapeType, _pb.shapeType).hasCollision;
    }}
    // Mixed 2D/3D pairs report no hit, matching the auto path.
    if (_pa.shapeType >= COLLISION_SPHERE || _pb.shapeType >= COLLISION_SPHERE) return false;
    return detectCollision(p, _o, other, selfIdx).hasCollision;
}}
// ----------------------------------------------------------------------------
// 2D COLLISION DETECTION
//
// All helpers receive vec2 positions; the caller is responsible for
// extracting .xy from the wide Object struct. Rotation angles are extracted
// from the object's orientation quaternion via quat_extract_2d_angle().
// ----------------------------------------------------------------------------
CollisionInfo detectCircleCircle(vec2 pa, float ra, vec2 pb, float rb, int ob) {{
    CollisionInfo info;
    info.hasCollision = false;
    info.otherObjectID = ob;
    vec2 d = pb - pa;
    float dsq = dot(d, d);
    float rs = ra + rb;
    if (dsq < rs*rs && dsq > EPSILON) {{
        float dist = sqrt(dsq);
        info.hasCollision = true;
        info.normal = d / dist;
        info.penetration = rs - dist;
        info.contactPoint = pa + d * (ra / rs);
    }}
    return info;
}}

CollisionInfo detectAABBAABB(vec2 pa, vec2 ha, float ra, vec2 pb, vec2 hb, float rb, int ob) {{
    CollisionInfo info;
    info.hasCollision = false;
    info.otherObjectID = ob;
    info.penetration = 1e10;
    vec2 axes[4];
    axes[0] = vec2(cos(ra), sin(ra));
    axes[1] = vec2(-sin(ra), cos(ra));
    axes[2] = vec2(cos(rb), sin(rb));
    axes[3] = vec2(-sin(rb), cos(rb));
    vec2 d = pb - pa;
    for (int i = 0; i < 4; ++i) {{
        vec2 axis = axes[i];
        float projA_rad = abs(dot(axes[0], axis)) * ha.x + abs(dot(axes[1], axis)) * ha.y;
        float projB_rad = abs(dot(axes[2], axis)) * hb.x + abs(dot(axes[3], axis)) * hb.y;
        float projA_center = 0.0;
        float projB_center = dot(d, axis);
        float sep = abs(projB_center - projA_center) - (projA_rad + projB_rad);
        if (sep > 1e-4) return info;
        float overlap = -sep;
        if (overlap < info.penetration) {{
            info.penetration = overlap;
            info.normal = axis;
            if (dot(d, axis) < 0.0) info.normal = -axis;
        }}
    }}
    info.hasCollision = true;
    info.contactPoint = getSupportPoint(pa, ha, ra, info.normal);
    return info;
}}

vec2 projectPolygon(vec2 center, float radius, int sides, float rot, vec2 axis) {{
    float minP = 1e10, maxP = -1e10;
    float step = 2.0 * PI / float(sides);
    for (int i = 0; i < sides; ++i) {{
        float a = rot + float(i) * step;
        vec2 v = center + radius * vec2(cos(a), sin(a));
        float p = dot(v, axis);
        minP = min(minP, p);
        maxP = max(maxP, p);
    }}
    return vec2(minP, maxP);
}}

vec2 getPolygonNormal(int side, int total, float rot) {{
    float step = 2.0 * PI / float(total);
    float a = rot + float(side) * step;
    vec2 edge = vec2(cos(a + step), sin(a + step)) - vec2(cos(a), sin(a));
    return normalize(vec2(-edge.y, edge.x));
}}

CollisionInfo detectPolygonPolygon(vec2 pa, float ra, int sa, float rta, vec2 pb, float rb, int sb, float rtb, int ob) {{
    CollisionInfo info;
    info.hasCollision = false;
    info.otherObjectID = ob;
    info.penetration = 1e10;
    int total = sa + sb;
    for (int i = 0; i < total; ++i) {{
        vec2 axis = (i < sa) ? getPolygonNormal(i, sa, rta) : getPolygonNormal(i - sa, sb, rtb);
        vec2 projA = projectPolygon(pa, ra, sa, rta, axis);
        vec2 projB = projectPolygon(pb, rb, sb, rtb, axis);
        if (projA.y < projB.x || projB.y < projA.x) return info;
        float overlap = min(projA.y, projB.y) - max(projA.x, projB.x);
        if (overlap < info.penetration) {{
            info.penetration = overlap;
            info.normal = axis;
            if (dot(pb - pa, axis) < 0.0) info.normal = -axis;
        }}
    }}
    info.hasCollision = true;
    return info;
}}

CollisionInfo detectCircleAABB(vec2 cp, float cr, vec2 bp, vec2 he, float brot, int ob) {{
    CollisionInfo info;
    info.hasCollision = false;
    info.otherObjectID = ob;
    vec2 local = worldToLocal(cp, bp, brot);
    vec2 closest = clamp(local, -he, he);
    vec2 d = local - closest;
    float dsq = dot(d, d);
    if (dsq < cr*cr) {{
        float dist = sqrt(dsq);
        info.hasCollision = true;
        if (dist > EPSILON) {{
            vec2 localNormal = -(d / dist);
            info.normal = rotatePoint(localNormal, brot);
            info.penetration = cr - dist;
            info.contactPoint = cp - info.normal * (cr - info.penetration * 0.5);
        }} else {{
            vec2 toEdge = -local;
            vec2 absToEdge = abs(toEdge);
            vec2 edgeDist = he - absToEdge;
            vec2 localNormal;
            if (edgeDist.x < edgeDist.y) {{
                localNormal = vec2(sign(toEdge.x), 0.0);
                info.penetration = cr + edgeDist.x;
            }} else {{
                localNormal = vec2(0.0, sign(toEdge.y));
                info.penetration = cr + edgeDist.y;
            }}
            info.normal = rotatePoint(localNormal, brot);
            info.contactPoint = cp - info.normal * cr;
        }}
    }}
    return info;
}}

CollisionInfo detectCirclePolygon(vec2 cp, float cr, vec2 pp, float pr, int ps, float prot, int ob) {{
    CollisionInfo info;
    info.hasCollision = false;
    info.otherObjectID = ob;
    info.penetration = 1e10;
    float step = 2.0 * PI / float(ps);
    for (int i = 0; i < ps; ++i) {{
        vec2 axis = getPolygonNormal(i, ps, prot);
        float cproj = dot(cp, axis);
        vec2 crange = vec2(cproj - cr, cproj + cr);
        vec2 prange = projectPolygon(pp, pr, ps, prot, axis);
        if (crange.y < prange.x || prange.y < crange.x) return info;
        float overlap = min(crange.y, prange.y) - max(crange.x, prange.x);
        if (overlap < info.penetration) {{
            info.penetration = overlap;
            info.normal = axis;
            if (dot(pp - cp, axis) < 0.0) info.normal = -axis;
        }}
    }}
    vec2 closest = pp;
    float closestDsq = 1e10;
    for (int i = 0; i < ps; ++i) {{
        float a = prot + float(i) * step;
        vec2 v = pp + pr * vec2(cos(a), sin(a));
        float dsq = dot(v - cp, v - cp);
        if (dsq < closestDsq) {{ closestDsq = dsq; closest = v; }}
    }}
    vec2 axis = normalize(cp - closest);
    float cproj = dot(cp, axis);
    vec2 crange = vec2(cproj - cr, cproj + cr);
    vec2 prange = projectPolygon(pp, pr, ps, prot, axis);
    if (!(crange.y < prange.x || prange.y < crange.x)) {{
        float overlap = min(crange.y, prange.y) - max(crange.x, prange.x);
        if (overlap < info.penetration) {{
            info.penetration = overlap;
            info.normal = axis;
            if (dot(pp - cp, axis) < 0.0) info.normal = -axis;
        }}
        info.hasCollision = true;
    }}
    if (info.hasCollision) info.contactPoint = cp - info.normal * cr;
    return info;
}}

CollisionInfo detectPolygonAABB(vec2 pp, float pr, int ps, float prot, vec2 bp, vec2 he, float brot, int ob) {{
    CollisionInfo info;
    info.hasCollision = false;
    info.otherObjectID = ob;
    info.penetration = 1e10;
    vec2 verts[32];
    float step = 2.0 * PI / float(ps);
    for (int i = 0; i < ps && i < 32; ++i) {{
        float a = prot + float(i) * step;
        verts[i] = pp + pr * vec2(cos(a), sin(a));
    }}
    vec2 bax[2];
    bax[0] = vec2(cos(brot), sin(brot));
    bax[1] = vec2(-sin(brot), cos(brot));
    vec2 bc[4] = vec2[](
        vec2(-he.x, -he.y), vec2( he.x, -he.y),
        vec2( he.x,  he.y), vec2(-he.x,  he.y)
    );
    vec2 bv[4];
    for (int i = 0; i < 4; ++i) bv[i] = bp + bc[i].x * bax[0] + bc[i].y * bax[1];
    vec2 axes[34];
    int ac = 0;
    for (int i = 0; i < ps; ++i) {{
        int n = (i + 1) % ps;
        vec2 e = verts[n] - verts[i];
        axes[ac++] = normalize(vec2(-e.y, e.x));
    }}
    axes[ac++] = bax[0];
    axes[ac++] = bax[1];
    for (int i = 0; i < ac; ++i) {{
        vec2 axis = axes[i];
        float pmin = dot(verts[0], axis), pmax = pmin;
        for (int j = 1; j < ps; ++j) {{
            float p = dot(verts[j], axis);
            pmin = min(pmin, p);
            pmax = max(pmax, p);
        }}
        float bmin = dot(bv[0], axis), bmax = bmin;
        for (int j = 1; j < 4; ++j) {{
            float p = dot(bv[j], axis);
            bmin = min(bmin, p);
            bmax = max(bmax, p);
        }}
        if (pmax < bmin - 1e-4 || bmax < pmin - 1e-4) return info;
        float overlap = min(pmax, bmax) - max(pmin, bmin);
        if (overlap < info.penetration) {{
            info.penetration = overlap;
            info.normal = axis;
            vec2 centerDiff = bp - pp;
            if (dot(centerDiff, axis) < 0.0) info.normal = -axis;
        }}
    }}
    info.hasCollision = true;
    info.contactPoint = (pp + bp) * 0.5;
    return info;
}}

// Dispatches to the correct shape-pair helper. Positions are extracted via
// .xy from the wide Object struct; per-object rotation angles are recovered
// from the orientation quaternion using quat_extract_2d_angle().
CollisionInfo detectCollision(Object a, Object b, int ib, int self) {{
    CollisionInfo info;
    info.hasCollision = false;
    CollisionProperties pa = collisionProps[self];
    CollisionProperties pb = collisionProps[ib];
    if (pa.enabled == 0 || pb.enabled == 0) return info;
    int sa = pa.shapeType, sb = pb.shapeType;
    if (sa == COLLISION_NONE || sb == COLLISION_NONE) return info;

    float angle_a = quat_extract_2d_angle(a.orientation);
    float angle_b = quat_extract_2d_angle(b.orientation);

    if (sa == COLLISION_CIRCLE && sb == COLLISION_CIRCLE)
        return detectCircleCircle(a.position.xy, a.visualData.x, b.position.xy, b.visualData.x, ib);
    if (sa == COLLISION_AABB && sb == COLLISION_AABB) {{
        vec2 ha = a.visualData.xy * 0.5, hb = b.visualData.xy * 0.5;
        return detectAABBAABB(a.position.xy, ha, angle_a, b.position.xy, hb, angle_b, ib);
    }}
    if (sa == COLLISION_POLYGON && sb == COLLISION_POLYGON)
        return detectPolygonPolygon(a.position.xy, a.visualData.x, int(a.visualData.y), angle_a,
                                    b.position.xy, b.visualData.x, int(b.visualData.y), angle_b, ib);
    if (sa == COLLISION_CIRCLE && sb == COLLISION_AABB) {{
        vec2 hb = b.visualData.xy * 0.5;
        return detectCircleAABB(a.position.xy, a.visualData.x, b.position.xy, hb, angle_b, ib);
    }}
    if (sa == COLLISION_AABB && sb == COLLISION_CIRCLE) {{
        vec2 ha = a.visualData.xy * 0.5;
        CollisionInfo ci = detectCircleAABB(b.position.xy, b.visualData.x, a.position.xy, ha, angle_a, ib);
        ci.normal = -ci.normal;
        return ci;
    }}
    if (sa == COLLISION_CIRCLE && sb == COLLISION_POLYGON)
        return detectCirclePolygon(a.position.xy, a.visualData.x, b.position.xy, b.visualData.x, int(b.visualData.y), angle_b, ib);
    if (sa == COLLISION_POLYGON && sb == COLLISION_CIRCLE) {{
        CollisionInfo ci = detectCirclePolygon(b.position.xy, b.visualData.x, a.position.xy, a.visualData.x, int(a.visualData.y), angle_a, ib);
        ci.normal = -ci.normal;
        return ci;
    }}
    if (sa == COLLISION_POLYGON && sb == COLLISION_AABB) {{
        vec2 hb = b.visualData.xy * 0.5;
        return detectPolygonAABB(a.position.xy, a.visualData.x, int(a.visualData.y), angle_a,
                                 b.position.xy, hb, angle_b, ib);
    }}
    if (sa == COLLISION_AABB && sb == COLLISION_POLYGON) {{
        vec2 ha = a.visualData.xy * 0.5;
        CollisionInfo ci = detectPolygonAABB(b.position.xy, b.visualData.x, int(b.visualData.y), angle_b,
                                             a.position.xy, ha, angle_a, ib);
        ci.normal = -ci.normal;
        return ci;
    }}
    return info;
}}

// ----------------------------------------------------------------------------
// 2D COLLISION RESPONSE WITH TORQUE
// ----------------------------------------------------------------------------
void resolveCollisionWithTorque(inout vec2 pa, inout vec2 va, inout float wa, float ma, float ia,
                                inout vec2 pb, inout vec2 vb, inout float wb, float mb, float ib,
                                vec2 n, vec2 cp, float pen, float rest, float fric) {{
    const float posCorr = 0.8, slop = 0.001;
    if (pen > slop) {{
        float tot = ma + mb;
        float invTot = 1.0 / tot;
        vec2 corr = (pen - slop) * posCorr * n;
        pa -= corr * (mb * invTot);
        pb += corr * (ma * invTot);
    }}
    vec2 ra = cp - pa;
    vec2 rb = cp - pb;
    vec2 vac = va + vec2(-wa * ra.y, wa * ra.x);
    vec2 vbc = vb + vec2(-wb * rb.y, wb * rb.x);
    vec2 rel = vbc - vac;
    float vn = dot(rel, n);
    if (vn > 0.0) return;
    float e = clamp(rest, 0.0, 1.0);
    float num = -(1.0 + e) * vn;
    float rap = dot(ra, n), rbp = dot(rb, n);
    float den = (1.0/ma) + (1.0/mb) + (rap*rap)/ia + (rbp*rbp)/ib;
    if (abs(den) < EPSILON) return;
    float j = num / den;
    vec2 imp = j * n;
    va -= imp / ma;
    vb += imp / mb;
    wa -= (ra.x * imp.y - ra.y * imp.x) / ia;
    wb += (rb.x * imp.y - rb.y * imp.x) / ib;
    if (fric > 0.0) {{
        vac = va + vec2(-wa * ra.y, wa * ra.x);
        vbc = vb + vec2(-wb * rb.y, wb * rb.x);
        rel = vbc - vac;
        vec2 tan = rel - n * dot(rel, n);
        float tl = length(tan);
        if (tl > EPSILON) {{
            tan /= tl;
            float vt = dot(rel, tan);
            float jt = -vt * fric;
            float dent = (1.0/ma) + (1.0/mb) + (rap*rap)/ia + (rbp*rbp)/ib;
            if (abs(dent) > EPSILON) {{
                jt /= dent;
                float maxJt = abs(j) * fric;
                jt = clamp(jt, -maxJt, maxJt);
                vec2 fimp = jt * tan;
                va -= fimp / ma;
                vb += fimp / mb;
                wa -= (ra.x * fimp.y - ra.y * fimp.x) / ia;
                wb += (rb.x * fimp.y - rb.y * fimp.x) / ib;
            }}
        }}
    }}
}}

{_COLLISION_3D}

// ----------------------------------------------------------------------------
// USER-FACING COLLISION CALLS (intentionally not defined here)
//
// `detect_collision(other)` and `resolve_collision(other)` are NOT GLSL
// functions. A GLSL function cannot safely mutate the caller's locals, and
// writing objectsOut[] from inside one is overwritten by the unconditional
// write-back at the end of main(). jit.py therefore inlines them:
//   - resolve_collision(other) expands to a block that runs the 2D/3D
//     detector + resolver below directly on the caller's pos/vel/z/vz/
//     wx/wy/omega locals.
//   - detect_collision(other) expands to `_dc_inline(...)` (defined with the
//     3D collision primitives above), a read-only query.
// The caller's own object slot is `_self` (resolved through the index
// buffer), so scripts dispatched as a group touch the correct object.
// 2D-vs-3D pairs report no collision, matching the automatic path in main().
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// CONSTRAINT SOLVERS (used by applyConstraints, below)
// ----------------------------------------------------------------------------
void solveDistanceConstraint(inout vec2 pos, inout vec2 vel, Constraint c, int self) {{
    if (c.targetObjectID < 0 || c.targetObjectID >= uNumObjects || c.targetObjectID == self) return;
    Object target = objectsIn[c.targetObjectID];
    vec2 offset = pos - target.position.xy;
    float dist = length(offset);
    if (dist < EPSILON) return;
    vec2 normal = offset / dist;
    vec2 desired = normal * c.param1;
    pos -= (offset - desired) * CONSTRAINT_STIFFNESS;
    vec2 relVel = vel - target.velocity.xy;
    vel -= dot(relVel, normal) * normal;
}}

void solveBoundaryConstraint(inout vec2 pos, inout vec2 vel, Constraint c) {{
    float x1 = c.param1, x2 = c.param2, y1 = c.param3, y2 = c.param4;
    float minX = min(x1, x2), maxX = max(x1, x2);
    float minY = min(y1, y2), maxY = max(y1, y2);
    const float elasticity = 0.7, friction = 0.95;
    if (pos.x < minX) {{ pos.x = minX; vel.x = abs(vel.x) * elasticity; vel.y *= friction; }}
    else if (pos.x > maxX) {{ pos.x = maxX; vel.x = -abs(vel.x) * elasticity; vel.y *= friction; }}
    if (pos.y < minY) {{ pos.y = minY; vel.y = abs(vel.y) * elasticity; vel.x *= friction; }}
    else if (pos.y > maxY) {{ pos.y = maxY; vel.y = -abs(vel.y) * elasticity; vel.x *= friction; }}
}}

void solveAngleConstraint(inout vec2 pos, inout vec2 vel, Constraint c, vec2 originalPos) {{
    vec2 dir = pos - originalPos;
    float radius = length(dir);
    if (radius < EPSILON) return;
    float current = atan(dir.y, dir.x);
    current = mod(current + 2.0*PI, 2.0*PI);
    float minA = mod(c.param1 + 2.0*PI, 2.0*PI);
    float maxA = mod(c.param2 + 2.0*PI, 2.0*PI);
    bool outOfBounds = false;
    float corrected = current;
    if (minA <= maxA) {{
        if (current < minA || current > maxA) {{
            outOfBounds = true;
            corrected = (abs(current - minA) < abs(current - maxA)) ? minA : maxA;
        }}
    }} else {{
        if (current < minA && current > maxA) {{
            outOfBounds = true;
            corrected = (abs(current - minA) < abs(current - maxA)) ? minA : maxA;
        }}
    }}
    if (outOfBounds) {{
        pos = originalPos + vec2(cos(corrected), sin(corrected)) * radius;
        vec2 radial = normalize(pos - originalPos);
        vec2 tangent = vec2(-radial.y, radial.x);
        vel = tangent * dot(vel, tangent) * 0.9;
    }}
}}


// ----------------------------------------------------------------------------
// applyConstraints
//
// Runs every constraint attached to object `self` against the caller-supplied
// `pos`/`vel` (inout), MAX_CONSTRAINT_ITERATIONS times. It never touches
// objectsOut[]: it is used both by the automatic path in main() and by the
// inlined user-facing `apply_constraints()` call (see jit.py), and in both
// cases the result flows through main()'s final write-back.
// ----------------------------------------------------------------------------
void applyConstraints(inout vec2 pos, inout vec2 vel, int self, vec2 originalPos) {{
    ObjectConstraints pc = objectConstraints[self];
    if (pc.numConstraints <= 0) return;
    for (int iter = 0; iter < MAX_CONSTRAINT_ITERATIONS; ++iter) {{
        for (int ci = 0; ci < pc.numConstraints; ++ci) {{
            Constraint c = constraints[pc.constraintOffset + ci];
            if (c.type == CONSTRAINT_DISTANCE)      solveDistanceConstraint(pos, vel, c, self);
            else if (c.type == CONSTRAINT_BOUNDARY) solveBoundaryConstraint(pos, vel, c);
            else if (c.type == CONSTRAINT_ANGLE)    solveAngleConstraint(pos, vel, c, originalPos);
        }}
    }}
}}

// ----------------------------------------------------------------------------
// USER EXPOSED CONSTRAINT API (read-only queries)
//
// Note: `apply_constraints()` is not defined here; it is inlined by jit.py
// against the script's current pos/vel (see applyConstraints above).
// ----------------------------------------------------------------------------
int get_constraint_count() {{
    int _self = indices[int(gl_GlobalInvocationID.x)];
    return objectConstraints[_self].numConstraints;
}}

int get_constraint_type(int idx) {{
    int _self = indices[int(gl_GlobalInvocationID.x)];
    ObjectConstraints pc = objectConstraints[_self];
    if (idx < 0 || idx >= pc.numConstraints) return -1;
    return constraints[pc.constraintOffset + idx].type;
}}

int get_constraint_target(int idx) {{
    int _self = indices[int(gl_GlobalInvocationID.x)];
    ObjectConstraints pc = objectConstraints[_self];
    if (idx < 0 || idx >= pc.numConstraints) return -1;
    return constraints[pc.constraintOffset + idx].targetObjectID;
}}

float get_constraint_param(int idx, int n) {{
    int _self = indices[int(gl_GlobalInvocationID.x)];
    ObjectConstraints pc = objectConstraints[_self];
    if (idx < 0 || idx >= pc.numConstraints) return 0.0;
    Constraint c = constraints[pc.constraintOffset + idx];
    if (n == 1) return c.param1;
    if (n == 2) return c.param2;
    if (n == 3) return c.param3;
    if (n == 4) return c.param4;
    return 0.0;
}}

// ----------------------------------------------------------------------------
// MAIN (2D + 3D object shader)
//
// Read:  theta from orientation.z/.w (Rz rotation); omega from spin.z;
//        z, vz from position/velocity.z; wx, wy from spin.xy.
// User code operates on x, y, z, vx, vy, vz, theta, omega (alias wz), wx, wy,
// and accelerates rotation with angular (Z, legacy), angular_x, angular_y,
// angular_z.
// Write: re-encode theta as quat_axis_angle(vec3(0,0,1), theta), then apply
//        the full spin vector (wx, wy, omega) as a first-order quaternion
//        derivative step and re-normalise.
//
// The object's own slot is resolved through the index buffer (objectIndex)
// so that this works correctly when dispatched as a script group.
//
// Internal state is underscore-prefixed (_q, _w, _self, _originalPos,
// _hadCollision, _dTheta, _zv) so user scripts may assign q, w, self, etc.
// IMPORTANT: x/y/vx/vy are copies of pos/vel used as user-facing aliases.
// They must be synced back into pos/vel before constraints and collisions,
// and read back from pos/vel before integration and write-back, otherwise
// collision and constraint results are silently discarded.
// The jit-inlined user calls (apply_constraints / resolve_collision) operate
// on pos/vel directly, so jit.py must copy x/y/vx/vy into pos/vel before the
// inlined block and back into x/y/vx/vy afterwards; z, vz, wx, wy and omega
// are shared locals and need no copying.
// ----------------------------------------------------------------------------
void main() {{
    int idx = int(gl_GlobalInvocationID.x);
    if (idx >= uGroupCount) return;
    int objectIndex = indices[idx];

    Object p = objectsIn[objectIndex];

    vec2 pos = p.position.xy;
    vec2 vel = p.velocity.xy;
    float z  = p.position.z;
    float vz = p.velocity.z;
    float az = 0.0;

    vec4 _q = p.orientation;
    float theta  = quat_extract_2d_angle(_q);   // legacy 2D view
    float _theta0 = theta;
    float wx = p.spin.x, wy = p.spin.y;
    float omega = p.spin.z;                    // Z-axis spin (legacy name)
    float _wz0 = omega;                        // value of omega on entry
    float wz = omega;                          // 3D-style alias of omega

    float mass = max(EPSILON, p.mass);
    float charge = p.charge;
    vec4 color = p.color;
    int _self = objectIndex;
    vec2 _originalPos = pos;

    float ax = 0.0, ay = 0.0, angular = 0.0;
    // 3D angular acceleration (rad/s^2 about world X/Y/Z). `angular` and
    // `angular_z` are both Z-axis torques and sum; prefer angular_z in 3D.
    float angular_x = 0.0, angular_y = 0.0, angular_z = 0.0;

    float x = pos.x;
    float y = pos.y;
    float vx = vel.x;
    float vy = vel.y;
{decls_str}
{body}

    // ---- fold the wz alias back into omega ----
    // The script may write omega, wz, or both; applying wz's delta keeps both
    // writes (writing only omega leaves wz == _wz0, so the delta is zero).
    omega += wz - _wz0;

    // ---- sanitize outputs ----
    ax = (isInvalid(ax) ? 0.0 : ax);
    ay = (isInvalid(ay) ? 0.0 : ay);
    az = (isInvalid(az) ? 0.0 : az);
    angular = (isInvalid(angular) ? 0.0 : angular);
    angular_x = (isInvalid(angular_x) ? 0.0 : angular_x);
    angular_y = (isInvalid(angular_y) ? 0.0 : angular_y);
    angular_z = (isInvalid(angular_z) ? 0.0 : angular_z);
    color = sanitizeVec4(color);

    // ---- sync scalar aliases into pos/vel before constraints/collisions ----
    pos = vec2(x, y);
    vel = vec2(vx, vy);

    #if {include_constraints}
    applyConstraints(pos, vel, _self, _originalPos);
    #endif

    #if {include_collisions}
    bool _hadCollision = false;
    for (int i = 0; i < uNumObjects; ++i) {{
        if (i == _self) continue;
        // Host-controlled per-pair exclusion (EnableCollisionBetween).
        Object _other = objectsIn[i];
        CollisionProperties propA = collisionProps[_self];
        CollisionProperties propB = collisionProps[i];
        if (propA.enabled == 0 || propB.enabled == 0) continue;
        if (propA.shapeType == COLLISION_NONE || propB.shapeType == COLLISION_NONE) continue;

        // ---- 3D shapes ----
        if (propA.shapeType >= COLLISION_SPHERE || propB.shapeType >= COLLISION_SPHERE) {{
            // Mixed 2D/3D pairs do not collide.
            if (propA.shapeType < COLLISION_SPHERE || propB.shapeType < COLLISION_SPHERE) continue;

            // Snap the current-frame state into a copy of p so the 3D
            // detector sees the user's updated positions, not the pre-update
            // SSBO state.
            Object _me = p;
            _me.position = vec4(pos, z, 0.0);
            _me.orientation = _q;

            CollisionInfo3D _c3 = detectCollision3D(_me, _other, propA.shapeType, propB.shapeType);
            if (_c3.hasCollision) {{
                _hadCollision = true;
                float rest = min(propA.restitution, propB.restitution);
                float fric = sqrt(propA.friction * propB.friction);
                float mA = mass, mB = max(EPSILON, _other.mass);
                float iA = getMomentOfInertia3D(mA, propA.shapeType, p.visualData);
                float iB = getMomentOfInertia3D(mB, propB.shapeType, _other.visualData);
                vec3 pa = vec3(pos, z), va = vec3(vel, vz), wa = vec3(wx, wy, omega);
                vec3 pb = _other.position.xyz, vb = _other.velocity.xyz, wb = _other.spin.xyz;
                resolveCollision3D(pa, va, wa, mA, iA, pb, vb, wb, mB, iB,
                                   _c3.normal, _c3.contactPoint, _c3.penetration, rest, fric);
                pos = pa.xy; z = pa.z;
                vel = va.xy; vz = va.z;
                wx = wa.x; wy = wa.y; omega = wa.z;
            }}
            continue;
        }}

        // ---- 2D shapes (unchanged) ----
        CollisionInfo _col = detectCollision(p, _other, i, _self);
        if (_col.hasCollision) {{
            _hadCollision = true;
            float rest = min(propA.restitution, propB.restitution);
            float fric = sqrt(propA.friction * propB.friction);
            float mA = mass, mB = max(EPSILON, _other.mass);
            float wA = omega, wB = _other.spin.z;
            float iA = getMomentOfInertia(mA, propA.shapeType, p.visualData.x, p.visualData.y, p.visualData.x * 0.5);
            float iB = getMomentOfInertia(mB, propB.shapeType, _other.visualData.x, _other.visualData.y, _other.visualData.x * 0.5);
            vec2 tempPos = _other.position.xy;
            vec2 tempVel = _other.velocity.xy;
            float tempOmega = wB;
            resolveCollisionWithTorque(pos, vel, omega, mA, iA,
                                       tempPos, tempVel, tempOmega, mB, iB,
                                       _col.normal, _col.contactPoint, _col.penetration,
                                       rest, fric);
        }}
    }}
    if (_hadCollision) {{
        vel = sanitizeVec2(vel);
        omega = clamp(omega, -100.0, 100.0);
        wx = clamp(wx, -100.0, 100.0);
        wy = clamp(wy, -100.0, 100.0);
    }}
    #endif

    // ---- pull results back into the scalar aliases ----
    x = pos.x; y = pos.y; vx = vel.x; vy = vel.y;

    // ---- symplectic Euler (3 axes) ----
    vx += ax * uDt;  vy += ay * uDt;  vz += az * uDt;
    x  += vx * uDt;  y  += vy * uDt;  z  += vz * uDt;

    // ---- 3D angular acceleration ----
    // Integrates the spin vector (wx, wy, omega) before the orientation step
    // below consumes it. angular_z acts on omega (Z); the legacy scalar
    // `angular` is applied separately as a Z rotation in the orientation step.
    wx    += angular_x * uDt;
    wy    += angular_y * uDt;
    omega += angular_z * uDt;

    // ---- orientation: legacy theta/angular, then full 3D spin ----
    float _dTheta = (theta - _theta0) + angular * uDt;
    if (_dTheta != 0.0) _q = quat_mul(quat_axis_angle(vec3(0.0, 0.0, 1.0), _dTheta), _q);
    vec3 _w = vec3(wx, wy, omega);
    if (dot(_w, _w) > 1e-12) _q = normalize(_q + 0.5 * uDt * quat_mul(vec4(_w, 0.0), _q));
    if (any(isnan(_q)) || any(isinf(_q))) _q = vec4(0.0, 0.0, 0.0, 1.0);

    // ---- sanitize ----
    x = sanitizeVec2(vec2(x, y)).x;
    y = sanitizeVec2(vec2(x, y)).y;
    vx = sanitizeVec2(vec2(vx, vy)).x;
    vy = sanitizeVec2(vec2(vx, vy)).y;
    vec2 _zv = sanitizeVec2(vec2(z, vz));
    z = _zv.x; vz = _zv.y;

    // ---- write back ----
    objectsOut[objectIndex].position    = vec4(x, y, z, 0.0);
    objectsOut[objectIndex].velocity    = vec4(vx, vy, vz, 0.0);
    objectsOut[objectIndex].orientation = _q;
    objectsOut[objectIndex].spin        = vec4(wx, wy, omega, 0.0);
    objectsOut[objectIndex].mass = mass;
    objectsOut[objectIndex].charge = charge;
    objectsOut[objectIndex].visualSkinType = p.visualSkinType;
    objectsOut[objectIndex].collisionShapeType = p.collisionShapeType;
    objectsOut[objectIndex].visualData = p.visualData;      // preserves xyz extents + w radius
    objectsOut[objectIndex].collisionData.x = ax;
    objectsOut[objectIndex].collisionData.y = ay;
    objectsOut[objectIndex].collisionData.z = az;
    objectsOut[objectIndex].collisionData.w = p.collisionData.w;
    objectsOut[objectIndex].color = color;
    objectsOut[objectIndex].equationID = p.equationID;
    objectsOut[objectIndex].scriptID = p.scriptID;
    objectsOut[objectIndex].meshID = p.meshID;
}}
"""
    return shader


# -----------------------------------------------------------------------------
# Paint mode wrapper
#
# Paint scripts only READ the Object SSBO, so the only change required here is
# updating the struct definition to the wide layout. Field access via
# `o.position.x`, `o.position.y`, `o.velocity.x`, `o.velocity.y` continues to
# work because those are still the first two components of the vec4s.
# -----------------------------------------------------------------------------
def _wrap_paint_shader(body: str, header: str, sdf_defs: str,
                       assigned_vars: Set[str], var_types: Dict[str, str]) -> str:
    """
    Produce the full GLSL shader for a 'paint' script.
    This shader runs per pixel, reads the previous frame texture,
    writes to a target image (double-buffered), and provides access
    to object data (read-only) and scratchpad (read-only).
    It now also includes the signal queue, allowing paint scripts
    to enqueue signals.
    """
    decls = []
    for v in sorted(assigned_vars):
        if v == 'color':
            typ = 'vec4'
        else:
            typ = var_types.get(v, "float")
        decls.append(f"    {typ} {v};")
    decls_str = "\n".join(decls)

    shader = f"""#version 430 core
{header}
{_QUAT_HELPERS}
{sdf_defs}
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

uniform sampler2D uPrevFrame;
layout(rgba8, binding = 0) writeonly uniform image2D uPaintTarget;

// Wide Object struct: mirrors the C++ layout. Paint only reads from it, so
// no writeback changes are needed here – just the field layout.
struct Object {{
    vec4 position;
    vec4 velocity;
    vec4 orientation;
    vec4 spin;
    vec4 visualData;
    vec4 collisionData;
    vec4 color;
    float mass;
    float charge;
    int visualSkinType;
    int collisionShapeType;
    int equationID;
    int scriptID;
    int meshID;
    int _pad[5];
}};
layout(std430, binding = 0) readonly buffer ObjectsIn {{ Object objectsIn[]; }};

uniform int uNumObjects;
uniform float uTime;
uniform float uCameraX;
uniform float uCameraY;
uniform float uHalfWidth;
uniform float uHalfHeight;
uniform int uScreenWidth;
uniform int uScreenHeight;
uniform int uTexWidth;
uniform int uTexHeight;
uniform float uDt;

uniform vec2  uTexSize;
uniform vec2  uPanDelta;
uniform float uZoomRatio;
uniform float uScale;

// Scratchpad (read‑only)
layout(std430, binding = 10) buffer ScratchpadPool {{ float scratchpadData[]; }};
uniform int uScratchpadOffsets[16];

float scratchpad_read(int id, int idx) {{
    return scratchpadData[uScratchpadOffsets[id] + idx];
}}

// Signal queue – paint scripts can enqueue signals
struct Signal {{
    uint agentID;
    uint objectIdx;
    float payload;
}};
layout(std430, binding = 11) buffer SignalQueue {{
    uint count;
    Signal signals[];
}};
uniform uint uSignalQueueCapacity;

void signal_enqueue(uint agentID, float payload) {{
    uint idx = atomicAdd(count, 1u);
    if (idx < uSignalQueueCapacity) {{
        signals[idx].agentID = agentID;
        signals[idx].objectIdx = 0; // paint does not associate with a specific object
        signals[idx].payload = payload;
    }}
}}

const float EPSILON = 1e-6;

float safeDivide(float n, float d) {{
    return (abs(d) < EPSILON) ? 0.0 : n / d;
}}
float safePow(float b, float e) {{
    if (b < 0.0) {{
        float intPart;
        if (abs(modf(e, intPart)) < EPSILON) {{
            float result = pow(-b, e);
            if (int(intPart) % 2 == 1) result = -result;
            return result;
        }}
    }}
    return pow(max(0.0, b), e);
}}
float safeLog(float v) {{ return log(max(EPSILON, v)); }}
float safeExp(float v) {{ return exp(clamp(v, -50.0, 50.0)); }}
bool isInvalid(float v) {{ return isinf(v) || isnan(v); }}
float signFunc(float x) {{ return (x > 0.0) ? 1.0 : ((x < 0.0) ? -1.0 : 0.0); }}
float stepFunc(float x) {{ return (x >= 0.0) ? 1.0 : 0.0; }}

const vec2 i = vec2(0.0, 1.0);
vec2 cAdd(vec2 a, vec2 b) {{ return a + b; }}
vec2 cSub(vec2 a, vec2 b) {{ return a - b; }}
vec2 cMul(vec2 a, vec2 b) {{
    return vec2(a.x*b.x - a.y*b.y, a.x*b.y + a.y*b.x);
}}
vec2 cDiv(vec2 a, vec2 b) {{
    float d = b.x*b.x + b.y*b.y;
    if (abs(d) < EPSILON) return vec2(0.0);
    return vec2((a.x*b.x + a.y*b.y)/d, (a.y*b.x - a.x*b.y)/d);
}}
vec2 cLog(vec2 z) {{
    float r = length(z);
    float theta = atan(z.y, z.x);
    return vec2(safeLog(r), theta);
}}
vec2 cExp(vec2 z) {{
    float r = safeExp(z.x);
    return r * vec2(cos(z.y), sin(z.y));
}}
vec2 cPow(vec2 b, vec2 e) {{
    return cExp(cMul(e, cLog(b)));
}}
vec2 cSin(vec2 z) {{
    return vec2(sin(z.x)*cosh(z.y), cos(z.x)*sinh(z.y));
}}
vec2 cCos(vec2 z) {{
    return vec2(cos(z.x)*cosh(z.y), -sin(z.x)*sinh(z.y));
}}
vec2 cTan(vec2 z) {{
    return cDiv(cSin(z), cCos(z));
}}
float real(vec2 z) {{ return z.x; }}
float imag(vec2 z) {{ return z.y; }}
vec2 conj(vec2 z) {{ return vec2(z.x, -z.y); }}
float arg(vec2 z) {{ return atan(z.y, z.x); }}

vec4 samplePrev(float wx, float wy) {{
    float tx = (wx - (uCameraX - uHalfWidth)) / (2.0 * uHalfWidth);
    float ty = (wy - (uCameraY - uHalfHeight)) / (2.0 * uHalfHeight);
    tx = clamp(tx, 0.0, 1.0);
    ty = clamp(ty, 0.0, 1.0);
    return texture(uPrevFrame, vec2(tx, ty));
}}

float samplePrevBox(vec2 center, float radius, int channel) {{
    const int N = 5;
    float step = radius / float(N - 1);
    float sum = 0.0;
    float count = 0.0;
    for (int i = 0; i < N; ++i) {{
        for (int j = 0; j < N; ++j) {{
            float wx = center.x + (float(i) - float(N-1)/2.0) * step;
            float wy = center.y + (float(j) - float(N-1)/2.0) * step;
            vec4 col = samplePrev(wx, wy);
            if (channel == 0) sum += col.r;
            else if (channel == 1) sum += col.g;
            else if (channel == 2) sum += col.b;
            else sum += col.a;
            count++;
        }}
    }}
    return sum / count;
}}

float avgPrevFrame(int channel) {{
    const int N = 10;
    float sum = 0.0;
    float count = 0.0;
    for (int i = 0; i < N; ++i) {{
        for (int j = 0; j < N; ++j) {{
            float fx = float(i) / float(N - 1);
            float fy = float(j) / float(N - 1);
            float wx = (uCameraX - uHalfWidth) + fx * 2.0 * uHalfWidth;
            float wy = (uCameraY - uHalfHeight) + fy * 2.0 * uHalfHeight;
            vec4 col = samplePrev(wx, wy);
            if (channel == 0) sum += col.r;
            else if (channel == 1) sum += col.g;
            else if (channel == 2) sum += col.b;
            else sum += col.a;
            count++;
        }}
    }}
    return sum / count;
}}

float sample_prev_r(vec2 center, float radius) {{ return samplePrevBox(center, radius, 0); }}
float sample_prev_g(vec2 center, float radius) {{ return samplePrevBox(center, radius, 1); }}
float sample_prev_b(vec2 center, float radius) {{ return samplePrevBox(center, radius, 2); }}
float sample_prev_a(vec2 center, float radius) {{ return samplePrevBox(center, radius, 3); }}
float avg_prev_r() {{ return avgPrevFrame(0); }}
float avg_prev_g() {{ return avgPrevFrame(1); }}
float avg_prev_b() {{ return avgPrevFrame(2); }}
float avg_prev_a() {{ return avgPrevFrame(3); }}

// Hash map for per-object properties:
//    1-2   position x/y          3-4   velocity x/y
//    5-6   collisionData x/y     8     theta (rotation about Z, from orientation)
//    9-12  color r/g/b/a         22-23 mass / charge
//    27    omega (spin about Z)
//    100-104  shape parameters. For 2D objects these are radius / width /
//             height / sides taken from visualData. For 3D objects
//             (visualSkinType == -1) visualData holds AABB extents instead, so
//             these hashes return 0 for them rather than a misleading radius.
//    200-208  3D state: position.z, velocity.z, orientation xyzw, spin xyz.
float getObjectProperty(int idx, int hash, int self) {{
    if (idx < 0 || idx >= uNumObjects || idx == self) return 0.0;
    Object o = objectsIn[idx];
    int h = hash;
    // Position and velocity still live in .x/.y of the vec4s, so existing
    // hash-based accessors keep working unchanged.
    if (h == 1) return o.position.x;
    if (h == 2) return o.position.y;
    if (h == 3) return o.velocity.x;
    if (h == 4) return o.velocity.y;
    if (h == 5) return o.collisionData.x;
    if (h == 6) return o.collisionData.y;
    if (h == 22) return o.mass;
    if (h == 23) return o.charge;
    // Rotation and angular velocity no longer live in visualData in the wide
    // layout. Hashes 8 and 27 therefore return the DERIVED values: theta is
    // recovered from the orientation quaternion (rotation about Z) and omega
    // is spin.z, so scripts reading p[i].theta / p[i].omega keep working.
    if (h == 8) return quat_extract_2d_angle(o.orientation);
    if (h == 27) return o.spin.z;
    if (h == 9) return o.color.r;
    if (h == 10) return o.color.g;
    if (h == 11) return o.color.b;
    if (h == 12) return o.color.a;
    // 3D objects store AABB extents in visualData, not radius/width/height, so
    // the 2D shape-parameter hashes are meaningless for them: report 0.
    if (h >= 100 && h <= 104 && o.visualSkinType == -1) return 0.0;
    if (h == 100) return o.visualData.x;
    if (h == 101) return o.visualData.y;
    if (h == 102) return o.visualData.x;
    if (h == 103) return o.visualData.x;
    if (h == 104) return o.visualData.y;
    // 3D state (additive; existing hashes are unaffected).
    if (h == 200) return o.position.z;
    if (h == 201) return o.velocity.z;
    if (h == 202) return o.orientation.x;
    if (h == 203) return o.orientation.y;
    if (h == 204) return o.orientation.z;
    if (h == 205) return o.orientation.w;
    if (h == 206) return o.spin.x;
    if (h == 207) return o.spin.y;
    if (h == 208) return o.spin.z;
    return 0.0;
}}

float getVariableValue(int hash) {{
    if (hash == 17) return 3.14159265359;
    if (hash == 18) return 2.71828182846;
    if (hash == 7) return uTime;
    return 0.0;
}}

ivec2 getPrevSampleCoord(ivec2 currentCoord) {{
    vec2 uv = (vec2(currentCoord) + vec2(0.5)) / uTexSize;
    vec2 prevUV = (0.5 + (uv - 0.5) * uZoomRatio) - uPanDelta;
    ivec2 prevPixel = ivec2(prevUV * uTexSize);
    prevPixel.y = int(uTexSize.y) - 1 - prevPixel.y;
    return prevPixel;
}}

void main() {{
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    if (coord.x >= uTexWidth || coord.y >= uTexHeight) return;

    float screenX = (float(coord.x) + 0.5) * float(uScreenWidth) / float(uTexWidth);
    float screenY = (float(coord.y) + 0.5) * float(uScreenHeight) / float(uTexHeight);
    float ndcX = (screenX / uScreenWidth) * 2.0 - 1.0;
    float ndcY = 1.0 - (screenY / uScreenHeight) * 2.0;
    float px = uCameraX + ndcX * uHalfWidth;
    float py = uCameraY + ndcY * uHalfHeight;

    ivec2 prevCoord = getPrevSampleCoord(coord);
    vec4 prev = vec4(0.0);
    if (prevCoord.x >= 0 && prevCoord.x < int(uTexSize.x) &&
        prevCoord.y >= 0 && prevCoord.y < int(uTexSize.y)) {{
        prev = texelFetch(uPrevFrame, prevCoord, 0);
    }}

    float prev_r = prev.r;
    float prev_g = prev.g;
    float prev_b = prev.b;
    float prev_a = prev.a;
    float t = uTime;

    // user‑declared variables
{decls_str}

    // user code
{body}

    vec4 outColor = vec4(color.r, color.g, color.b, color.a);
    imageStore(uPaintTarget, ivec2(coord.x, uTexHeight - 1 - coord.y), outColor);
}}
"""
    return shader


# -----------------------------------------------------------------------------
# Agent mode wrapper
#
# Agents only READ the Object SSBO (and read/write scratchpads), so the only
# required change is the wide Object struct definition. No field offsets used
# by agent scripts change.
# -----------------------------------------------------------------------------
def _wrap_agent_shader(body: str, header: str, sdf_defs: str,
                       assigned_vars: Set[str], var_types: Dict[str, str]) -> str:
    """
    Produce the full GLSL shader for an 'agent' script.
    Agents run over each pending signal in the queue.
    They can read the object SSBO, read and write scratchpads,
    and read the signal queue (but not enqueue).
    """
    decls = []
    for v in sorted(assigned_vars):
        typ = var_types.get(v, "float")
        decls.append(f"    {typ} {v};")
    decls_str = "\n".join(decls)

    shader = f"""#version 430 core
{header}
{_QUAT_HELPERS}
{sdf_defs}
layout(local_size_x = 64) in;

// ----------------------------------------------------------------------------
// OBJECT SSBO (read only)
//
// Wide layout matching the C++ struct. Agent scripts read fields via the
// same names (position/velocity/orientation/spin/etc.); no writeback path
// exists for agents, so only the layout itself needs updating.
// ----------------------------------------------------------------------------
struct Object {{
    vec4 position;
    vec4 velocity;
    vec4 orientation;
    vec4 spin;
    vec4 visualData;
    vec4 collisionData;
    vec4 color;
    float mass;
    float charge;
    int visualSkinType;
    int collisionShapeType;
    int equationID;
    int scriptID;
    int meshID;
    int _pad[5];
}};
layout(std430, binding = 0) readonly buffer ObjectsIn {{ Object objectsIn[]; }};

// ----------------------------------------------------------------------------
// SCRATCHPAD (read/write)
// ----------------------------------------------------------------------------
layout(std430, binding = 10) buffer ScratchpadPool {{ float scratchpadData[]; }};
uniform int uScratchpadOffsets[16];

float scratchpad_read(int id, int idx) {{
    return scratchpadData[uScratchpadOffsets[id] + idx];
}}
void scratchpad_write(int id, int idx, float val) {{
    scratchpadData[uScratchpadOffsets[id] + idx] = val;
}}

// ----------------------------------------------------------------------------
// SIGNAL QUEUE (read‑only)
// ----------------------------------------------------------------------------
struct Signal {{ uint agentID; uint objectIdx; float payload; }};
layout(std430, binding = 11) readonly buffer SignalQueue {{
    uint count;
    Signal signals[];
}};

uniform int uNumObjects;
uniform int uAgentID;
uniform int uSignalCount;

// ----------------------------------------------------------------------------
// MAIN
// ----------------------------------------------------------------------------
void main() {{
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= uSignalCount) return;
    Signal s = signals[idx];
    if (s.agentID != uAgentID) return;

    // Built‑ins for the user
    uint signal_object_idx = s.objectIdx;
    float signal_payload = s.payload;

    // Declare user variables
{decls_str}

    // User code
{body}
}}
"""
    return shader