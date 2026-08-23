"""
Shader generation for JIT-compiled scripts.

This module contains the wrapping functions that produce the complete GLSL
source for object, paint, and agent shaders. The AST translation is done
by GLSLGenerator in jit.py; this module provides the boilerplate and
mode-specific code.
"""

from typing import Set, Dict, Any

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
# -----------------------------------------------------------------------------
def _wrap_object_shader(body: str, header: str, sdf_defs: str,
                        assigned_vars: Set[str], var_types: Dict[str, str],
                        user_handles_collisions: bool,
                        user_applies_constraints: bool) -> str:
    """
    Produce the full GLSL shader for an 'object' script.
    It includes:
      - Object SSBOs (read input, write output)
      - Index buffer for group dispatch
      - Constraint SSBOs (if not handled by user)
      - Collision properties and contact buffer (if not handled by user)
      - Scratchpad read (objects only read)
      - Signal queue enqueue (objects can signal)
      - Full collision detection and response (optional)
      - Constraint solvers (optional)
      - Symplectic Euler integration
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
{sdf_defs}
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// ----------------------------------------------------------------------------
// DATA STRUCTURES
// ----------------------------------------------------------------------------
struct Object {{
    vec2 position;
    vec2 velocity;
    float mass;
    float charge;
    int visualSkinType;
    int collisionShapeType;
    vec4 visualData;
    vec4 collisionData;
    vec4 color;
    int equationID;
    int scriptID;
    int _pad[2];
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

struct ContactPoint {{
    vec2 normal;
    vec2 position;
    float penetration;
    float accumulatedNormalImpulse;
    float accumulatedTangentImpulse;
    int frameCount;
}};

// ----------------------------------------------------------------------------
// BUFFERS AND UNIFORMS
// ----------------------------------------------------------------------------
layout(std430, binding = 0) readonly buffer ObjectsIn {{ Object objectsIn[]; }};
layout(std430, binding = 1) writeonly buffer ObjectsOut {{ Object objectsOut[]; }};
layout(std430, binding = 9) readonly buffer ObjectIndices {{ int indices[]; }};

#if {include_constraints}
layout(std430, binding = 5) readonly buffer Constraints {{ Constraint constraints[]; }};
layout(std430, binding = 6) readonly buffer ObjectConstraintMappings {{ ObjectConstraints objectConstraints[]; }};
#endif

#if {include_collisions}
layout(std430, binding = 7) readonly buffer CollisionProps {{ CollisionProperties collisionProps[]; }};
layout(std430, binding = 8) buffer ContactBuffer {{ ContactPoint contacts[]; }};
#endif

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

uniform int uEnableWarmStart;
uniform int uMaxContactIterations;

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
uniform int  uSignalQueueOverflowPolicy;

void signal_enqueue(uint agentID, float payload) {{
    uint idx = atomicAdd(count, 1u);
    if (idx < uSignalQueueCapacity) {{
        signals[idx].agentID = agentID;
        signals[idx].objectIdx = uint(gl_GlobalInvocationID.x);
        signals[idx].payload = payload;
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
const int COLLISION_NONE = 0;
const int COLLISION_CIRCLE = 1;
const int COLLISION_AABB = 2;
const int COLLISION_POLYGON = 3;
const int MAX_CONTACTS_PER_OBJECT = 4;
const int MAX_CONTACT_FRAMES = 5;

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
// ROTATION HELPERS
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
// COLLISION DETECTION (only used if user calls detect_collision or resolve_collision)
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

CollisionInfo detectCollision(Object a, Object b, int ib, int self) {{
    CollisionInfo info;
    info.hasCollision = false;
    CollisionProperties pa = collisionProps[self];
    CollisionProperties pb = collisionProps[ib];
    if (pa.enabled == 0 || pb.enabled == 0) return info;
    int sa = pa.shapeType, sb = pb.shapeType;
    if (sa == COLLISION_NONE || sb == COLLISION_NONE) return info;
    if (sa == COLLISION_CIRCLE && sb == COLLISION_CIRCLE)
        return detectCircleCircle(a.position, a.visualData.x, b.position, b.visualData.x, ib);
    if (sa == COLLISION_AABB && sb == COLLISION_AABB) {{
        vec2 ha = a.visualData.xy * 0.5, hb = b.visualData.xy * 0.5;
        return detectAABBAABB(a.position, ha, a.visualData.z, b.position, hb, b.visualData.z, ib);
    }}
    if (sa == COLLISION_POLYGON && sb == COLLISION_POLYGON)
        return detectPolygonPolygon(a.position, a.visualData.x, int(a.visualData.y), a.visualData.z,
                                    b.position, b.visualData.x, int(b.visualData.y), b.visualData.z, ib);
    if (sa == COLLISION_CIRCLE && sb == COLLISION_AABB) {{
        vec2 hb = b.visualData.xy * 0.5;
        return detectCircleAABB(a.position, a.visualData.x, b.position, hb, b.visualData.z, ib);
    }}
    if (sa == COLLISION_AABB && sb == COLLISION_CIRCLE) {{
        vec2 ha = a.visualData.xy * 0.5;
        CollisionInfo ci = detectCircleAABB(b.position, b.visualData.x, a.position, ha, a.visualData.z, ib);
        ci.normal = -ci.normal;
        return ci;
    }}
    if (sa == COLLISION_CIRCLE && sb == COLLISION_POLYGON)
        return detectCirclePolygon(a.position, a.visualData.x, b.position, b.visualData.x, int(b.visualData.y), b.visualData.z, ib);
    if (sa == COLLISION_POLYGON && sb == COLLISION_CIRCLE) {{
        CollisionInfo ci = detectCirclePolygon(b.position, b.visualData.x, a.position, a.visualData.x, int(a.visualData.y), a.visualData.z, ib);
        ci.normal = -ci.normal;
        return ci;
    }}
    if (sa == COLLISION_POLYGON && sb == COLLISION_AABB) {{
        vec2 hb = b.visualData.xy * 0.5;
        return detectPolygonAABB(a.position, a.visualData.x, int(a.visualData.y), a.visualData.z,
                                 b.position, hb, b.visualData.z, ib);
    }}
    if (sa == COLLISION_AABB && sb == COLLISION_POLYGON) {{
        vec2 ha = a.visualData.xy * 0.5;
        CollisionInfo ci = detectPolygonAABB(b.position, b.visualData.x, int(b.visualData.y), b.visualData.z,
                                             a.position, ha, a.visualData.z, ib);
        ci.normal = -ci.normal;
        return ci;
    }}
    return info;
}}

// ----------------------------------------------------------------------------
// COLLISION RESPONSE WITH TORQUE (only used if user calls resolve_collision)
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

// ----------------------------------------------------------------------------
// CONTACT PERSISTENCE (warm starting) – only used if collisions are enabled
// ----------------------------------------------------------------------------
int findPersistentContact(int a, int b, vec2 n) {{
    int base = a * MAX_CONTACTS_PER_OBJECT;
    for (int i = 0; i < MAX_CONTACTS_PER_OBJECT; ++i) {{
        int ci = base + i;
        if (ci >= contacts.length()) break;
        ContactPoint c = contacts[ci];
        if (c.frameCount > 0 && dot(c.normal, n) > 0.9) return ci;
    }}
    return -1;
}}

void updateContactPoint(int a, int b, vec2 n, vec2 pos, float pen) {{
    int base = a * MAX_CONTACTS_PER_OBJECT;
    int oldest = base, oldestFrames = 9999;
    for (int i = 0; i < MAX_CONTACTS_PER_OBJECT; ++i) {{
        int ci = base + i;
        if (ci >= contacts.length()) break;
        ContactPoint c = contacts[ci];
        if (c.frameCount == 0 || dot(c.normal, n) > 0.9) {{
            contacts[ci].normal = n;
            contacts[ci].position = pos;
            contacts[ci].penetration = pen;
            contacts[ci].frameCount = min(c.frameCount + 1, MAX_CONTACT_FRAMES);
            return;
        }}
        if (c.frameCount < oldestFrames) {{
            oldestFrames = c.frameCount;
            oldest = ci;
        }}
    }}
    if (oldest >= 0 && oldest < contacts.length()) {{
        contacts[oldest].normal = n;
        contacts[oldest].position = pos;
        contacts[oldest].penetration = pen;
        contacts[oldest].frameCount = 1;
        contacts[oldest].accumulatedNormalImpulse = 0.0;
        contacts[oldest].accumulatedTangentImpulse = 0.0;
    }}
}}

void ageContacts(int idx) {{
    int base = idx * MAX_CONTACTS_PER_OBJECT;
    for (int i = 0; i < MAX_CONTACTS_PER_OBJECT; ++i) {{
        int ci = base + i;
        if (ci >= contacts.length()) break;
        if (contacts[ci].frameCount > 0) {{
            contacts[ci].frameCount--;
            if (contacts[ci].frameCount == 0) {{
                contacts[ci].accumulatedNormalImpulse = 0.0;
                contacts[ci].accumulatedTangentImpulse = 0.0;
            }}
        }}
    }}
}}

// ----------------------------------------------------------------------------
// USER‑EXPOSED COLLISION FUNCTIONS (only available if user called them)
// ----------------------------------------------------------------------------
bool detect_collision(int other) {{
    if (other < 0 || other >= uNumObjects || other == int(gl_GlobalInvocationID.x)) return false;
    Object self_obj = objectsIn[int(gl_GlobalInvocationID.x)];
    Object other_obj = objectsIn[other];
    CollisionInfo col = detectCollision(self_obj, other_obj, other, int(gl_GlobalInvocationID.x));
    return col.hasCollision;
}}

void resolve_collision(int other) {{
    if (other < 0 || other >= uNumObjects || other == int(gl_GlobalInvocationID.x)) return;
    int self = int(gl_GlobalInvocationID.x);
    Object self_obj = objectsIn[self];
    Object other_obj = objectsIn[other];
    CollisionInfo col = detectCollision(self_obj, other_obj, other, self);
    if (!col.hasCollision) return;
    CollisionProperties propA = collisionProps[self];
    CollisionProperties propB = collisionProps[other];
    float rest = min(propA.restitution, propB.restitution);
    float fric = sqrt(propA.friction * propB.friction);
    float mA = self_obj.mass, mB = max(EPSILON, other_obj.mass);
    float wA = self_obj.visualData.w, wB = other_obj.visualData.w;
    float iA = getMomentOfInertia(mA, propA.shapeType, self_obj.visualData.x, self_obj.visualData.y, self_obj.visualData.x * 0.5);
    float iB = getMomentOfInertia(mB, propB.shapeType, other_obj.visualData.x, other_obj.visualData.y, other_obj.visualData.x * 0.5);
    vec2 pa = self_obj.position, va = self_obj.velocity;
    vec2 pb = other_obj.position, vb = other_obj.velocity;
    resolveCollisionWithTorque(pa, va, wA, mA, iA,
                               pb, vb, wB, mB, iB,
                               col.normal, col.contactPoint, col.penetration,
                               rest, fric);
    objectsOut[self].position = pa;
    objectsOut[self].velocity = va;
    objectsOut[self].visualData.w = wA;
}}

// ----------------------------------------------------------------------------
// CONSTRAINT SOLVERS (only used if user calls apply_constraints)
// ----------------------------------------------------------------------------
void solveDistanceConstraint(inout vec2 pos, inout vec2 vel, Constraint c, int self) {{
    if (c.targetObjectID < 0 || c.targetObjectID >= uNumObjects || c.targetObjectID == self) return;
    Object target = objectsIn[c.targetObjectID];
    vec2 offset = pos - target.position;
    float dist = length(offset);
    if (dist < EPSILON) return;
    vec2 normal = offset / dist;
    vec2 desired = normal * c.param1;
    pos -= (offset - desired) * CONSTRAINT_STIFFNESS;
    vec2 relVel = vel - target.velocity;
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

void applyConstraints(inout vec2 pos, inout vec2 vel, int self, vec2 originalPos) {{
    ObjectConstraints pc = objectConstraints[self];
    if (pc.numConstraints <= 0) return;
    for (int iter = 0; iter < MAX_CONSTRAINT_ITERATIONS; ++iter) {{
        for (int i = 0; i < pc.numConstraints; ++i) {{
            Constraint c = constraints[pc.constraintOffset + i];
            if (c.type == CONSTRAINT_DISTANCE) solveDistanceConstraint(pos, vel, c, self);
            else if (c.type == CONSTRAINT_BOUNDARY) solveBoundaryConstraint(pos, vel, c);
            else if (c.type == CONSTRAINT_ANGLE) solveAngleConstraint(pos, vel, c, originalPos);
        }}
    }}
}}

// ----------------------------------------------------------------------------
// USER‑EXPOSED CONSTRAINT API
// ----------------------------------------------------------------------------
int get_constraint_count() {{
    int self = int(gl_GlobalInvocationID.x);
    return objectConstraints[self].numConstraints;
}}

int get_constraint_type(int idx) {{
    int self = int(gl_GlobalInvocationID.x);
    ObjectConstraints pc = objectConstraints[self];
    if (idx < 0 || idx >= pc.numConstraints) return -1;
    return constraints[pc.constraintOffset + idx].type;
}}

int get_constraint_target(int idx) {{
    int self = int(gl_GlobalInvocationID.x);
    ObjectConstraints pc = objectConstraints[self];
    if (idx < 0 || idx >= pc.numConstraints) return -1;
    return constraints[pc.constraintOffset + idx].targetObjectID;
}}

float get_constraint_param(int idx, int n) {{
    int self = int(gl_GlobalInvocationID.x);
    ObjectConstraints pc = objectConstraints[self];
    if (idx < 0 || idx >= pc.numConstraints) return 0.0;
    Constraint c = constraints[pc.constraintOffset + idx];
    if (n == 1) return c.param1;
    if (n == 2) return c.param2;
    if (n == 3) return c.param3;
    if (n == 4) return c.param4;
    return 0.0;
}}

void apply_constraints() {{
    int self = int(gl_GlobalInvocationID.x);
    Object p = objectsIn[self];
    vec2 pos = p.position;
    vec2 vel = p.velocity;
    vec2 originalPos = pos;
    applyConstraints(pos, vel, self, originalPos);
    objectsOut[self].position = pos;
    objectsOut[self].velocity = vel;
}}

// ----------------------------------------------------------------------------
// MAIN
// ----------------------------------------------------------------------------
void main() {{
    int idx = int(gl_GlobalInvocationID.x);
    if (idx >= uGroupCount) return;
    int objectIndex = indices[idx];

    Object p = objectsIn[objectIndex];
    vec2 pos = p.position;
    vec2 vel = p.velocity;
    float mass = max(EPSILON, p.mass);
    float charge = p.charge;
    float theta = p.visualData.z;
    float omega = p.visualData.w;
    vec4 color = p.color;
    int self = objectIndex;
    vec2 originalPos = pos;

    float ax = 0.0, ay = 0.0, angular = 0.0;

    // ---- user code (x, y, vx, vy are aliases) ----
    float x = pos.x;
    float y = pos.y;
    float vx = vel.x;
    float vy = vel.y;
{decls_str}
{body}

    // ---- sanitize outputs ----
    ax = (isInvalid(ax) ? 0.0 : ax);
    ay = (isInvalid(ay) ? 0.0 : ay);
    angular = (isInvalid(angular) ? 0.0 : angular);
    color = sanitizeVec4(color);

    // ---- apply SSBO constraints (only if user called apply_constraints()) ----
    #if {include_constraints}
    applyConstraints(pos, vel, self, originalPos);
    #endif

    // ---- automatic collision handling (only if user called detect_collision or resolve_collision) ----
    #if {include_collisions}
    bool hadCollision = false;
    for (int i = 0; i < uNumObjects; ++i) {{
        if (i == self) continue;
        Object other = objectsIn[i];
        CollisionProperties propA = collisionProps[self];
        CollisionProperties propB = collisionProps[i];
        if (propA.enabled == 0 || propB.enabled == 0) continue;
        if (propA.shapeType == COLLISION_NONE || propB.shapeType == COLLISION_NONE) continue;

        CollisionInfo col = detectCollision(p, other, i, self);
        if (col.hasCollision) {{
            hadCollision = true;
            float rest = min(propA.restitution, propB.restitution);
            float fric = sqrt(propA.friction * propB.friction);
            float mA = mass, mB = max(EPSILON, other.mass);
            float wA = omega, wB = other.visualData.w;
            float iA = getMomentOfInertia(mA, propA.shapeType, p.visualData.x, p.visualData.y, p.visualData.x * 0.5);
            float iB = getMomentOfInertia(mB, propB.shapeType, other.visualData.x, other.visualData.y, other.visualData.x * 0.5);
            vec2 tempPos = other.position;
            vec2 tempVel = other.velocity;
            float tempOmega = wB;
            resolveCollisionWithTorque(pos, vel, omega, mA, iA,
                                       tempPos, tempVel, tempOmega, mB, iB,
                                       col.normal, col.contactPoint, col.penetration,
                                       rest, fric);
        }}
    }}
    if (hadCollision) {{
        vel = sanitizeVec2(vel);
        omega = clamp(omega, -100.0, 100.0);
    }}
    if (uEnableWarmStart == 1) ageContacts(self);
    #endif

    // ---- symplectic Euler integration (always) ----
    vx += ax * uDt;
    vy += ay * uDt;
    x += vx * uDt;
    y += vy * uDt;
    theta += angular * uDt;

    // ---- sanitize final position/velocity ----
    x = sanitizeVec2(vec2(x, y)).x;
    y = sanitizeVec2(vec2(x, y)).y;
    vx = sanitizeVec2(vec2(vx, vy)).x;
    vy = sanitizeVec2(vec2(vx, vy)).y;
    theta = mod(theta, 2.0*PI);
    if (theta < 0.0) theta += 2.0*PI;

    // ---- write back ----
    objectsOut[objectIndex].position = vec2(x, y);
    objectsOut[objectIndex].velocity = vec2(vx, vy);
    objectsOut[objectIndex].mass = mass;
    objectsOut[objectIndex].charge = charge;
    objectsOut[objectIndex].visualSkinType = p.visualSkinType;
    objectsOut[objectIndex].collisionShapeType = p.collisionShapeType;
    objectsOut[objectIndex].visualData.x = p.visualData.x;
    objectsOut[objectIndex].visualData.y = p.visualData.y;
    objectsOut[objectIndex].visualData.z = theta;
    objectsOut[objectIndex].visualData.w = omega;
    objectsOut[objectIndex].collisionData.x = ax;
    objectsOut[objectIndex].collisionData.y = ay;
    objectsOut[objectIndex].collisionData.z = p.collisionData.z;
    objectsOut[objectIndex].collisionData.w = p.collisionData.w;
    objectsOut[objectIndex].color = color;
    objectsOut[objectIndex].equationID = p.equationID;
    objectsOut[objectIndex].scriptID = p.scriptID;
    objectsOut[objectIndex]._pad[0] = 0;
    objectsOut[objectIndex]._pad[1] = 0;
}}
"""
    return shader


# -----------------------------------------------------------------------------
# Paint mode wrapper
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
{sdf_defs}
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

uniform sampler2D uPrevFrame;
layout(rgba8, binding = 0) writeonly uniform image2D uPaintTarget;

struct Object {{
    vec2 position;
    vec2 velocity;
    float mass;
    float charge;
    int visualSkinType;
    int collisionShapeType;
    vec4 visualData;
    vec4 collisionData;
    vec4 color;
    int equationID;
    int scriptID;
    int _pad[2];
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
uniform int  uSignalQueueOverflowPolicy;

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

float getObjectProperty(int idx, int hash, int self) {{
    if (idx < 0 || idx >= uNumObjects || idx == self) return 0.0;
    Object o = objectsIn[idx];
    int h = hash;
    if (h == 1) return o.position.x;
    if (h == 2) return o.position.y;
    if (h == 3) return o.velocity.x;
    if (h == 4) return o.velocity.y;
    if (h == 5) return o.collisionData.x;
    if (h == 6) return o.collisionData.y;
    if (h == 22) return o.mass;
    if (h == 23) return o.charge;
    if (h == 8) return o.visualData.z;
    if (h == 27) return o.visualData.w;
    if (h == 9) return o.color.r;
    if (h == 10) return o.color.g;
    if (h == 11) return o.color.b;
    if (h == 12) return o.color.a;
    if (h == 100) return o.visualData.x;
    if (h == 101) return o.visualData.y;
    if (h == 102) return o.visualData.x;
    if (h == 103) return o.visualData.x;
    if (h == 104) return o.visualData.y;
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
{sdf_defs}
layout(local_size_x = 64) in;

// ----------------------------------------------------------------------------
// OBJECT SSBO (read‑only)
// ----------------------------------------------------------------------------
struct Object {{
    vec2 position;
    vec2 velocity;
    float mass;
    float charge;
    int visualSkinType;
    int collisionShapeType;
    vec4 visualData;
    vec4 collisionData;
    vec4 color;
    int equationID;
    int scriptID;
    int _pad[2];
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