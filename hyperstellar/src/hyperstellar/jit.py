import ast
import inspect
import textwrap
from turtle import mode

# =============================================================================
# GLSLGenerator – translates Python AST to GLSL compute shader code
# =============================================================================
class GLSLGenerator(ast.NodeVisitor):
    """
    Translates a decorated Python function into a GLSL compute shader.
    Supports three modes:
      - 'object' (default): runs per particle, updates Object SSBOs.
      - 'paint': runs per pixel, reads/writes textures (double‑buffered).
      - 'agent': runs over the signal queue (one workgroup per signal).

    Features:
      - All arithmetic, comparisons, logical ops
      - if/else, for/while, continue/break, ternary
      - Tensor literals and indexing (vec/mat)
      - Derivatives, noise, rand (unique seed)
      - Complex numbers (i, real/imag/conj/arg, cAdd, etc.)
      - sum(), min(), max() for vectors and binary scalars
      - pow(), int(), float() mapped to GLSL
      - p.x / p[index].property access
      - len() for vectors/matrices
      - Full physics: constraints (inline and SSBO‑based), collisions, warm starting
      - Inline constraints: spring, distance, boundary, angle (direct code generation)
      - SSBO‑based constraints: apply_constraints() reads pre‑defined constraints
      - Paint mode with pan/zoom compensation
      - Scratchpad read/write (objects read only, agents read/write)
      - Signal queue enqueue (objects) and dequeue (agents)
    """
    def __init__(self, debug=False, mode='object'):
        self.debug = debug
        self.mode = mode
        self.lines = []
        self.indent = 0
        self.globals = {}                   # user's global namespace
        self.assigned_vars = set()          # variables that need declaration
        self.var_types = {}                 # variable -> GLSL type
        self.complex_vars = set()           # names known to be complex
        self.inline_depth = 0
        self.max_inline_depth = 10
        self.inlined_functions = set()
        self.rand_call_counter = 0          # unique seed for each rand()
        self.user_handles_collisions = False
        self.user_applies_constraints = False

    def indent_str(self):
        return "    " * self.indent

    # ------------------------------------------------------------------------
    # Main entry point: parse the Python function and generate GLSL
    # ------------------------------------------------------------------------
    def generate(self, func):
        """
        Parse the given Python function, visit its AST, and produce the
        complete GLSL shader source.
        """
        self.globals = func.__globals__
        self.lines = []
        self.assigned_vars.clear()
        self.var_types.clear()
        self.complex_vars.clear()
        self.inline_depth = 0
        self.inlined_functions.clear()
        self.rand_call_counter = 0
        self.user_handles_collisions = False
        self.user_applies_constraints = False

        tree = ast.parse(inspect.getsource(func))
        if not isinstance(tree.body[0], ast.FunctionDef):
            raise ValueError("Not a function definition")
        func_node = tree.body[0]

        collector = AssignCollector()
        collector.visit(func_node)

        # Detect if user calls collision or SSBO‑constraint functions
        special_funcs = {'detect_collision', 'resolve_collision', 'apply_constraints'}
        for node in ast.walk(func_node):
            if isinstance(node, ast.Call) and isinstance(node.func, ast.Name):
                if node.func.id == 'apply_constraints':
                    self.user_applies_constraints = True
                elif node.func.id in ('detect_collision', 'resolve_collision'):
                    self.user_handles_collisions = True

        # Predefined variables depend on mode (they are not declared in user code)
        if self.mode == 'paint':
            predefined = {'px', 'py', 'prev_r', 'prev_g', 'prev_b', 'prev_a',
                          't', 'color', 'idx', 'num_objects', 'dt', 'time',
                          'group_count', 'i'}
        elif self.mode == 'agent':
            predefined = {'signal_object_idx', 'signal_payload', 'num_objects', 'dt',
                          'time', 'idx', 'group_count', 'i'}
        else:
            predefined = {'x', 'y', 'vx', 'vy', 'mass', 'charge', 'theta', 'omega',
                          'color', 'pos', 'vel', 'num_objects', 'dt', 'time', 'idx',
                          'group_count', 'i', 'ax', 'ay', 'angular'}
        self.assigned_vars = collector.vars - predefined

        # For paint mode, 'color' is a built‑in vec4 that the user can write to.
        if self.mode == 'paint':
            self.assigned_vars.add('color')

        # Walk the AST and collect GLSL statements
        self.visit(func_node)
        body = "\n".join(self.lines)

        # Wrap with the appropriate shader skeleton
        if self.mode == 'paint':
            shader = self._wrap_paint_shader(body)
        elif self.mode == 'agent':
            shader = self._wrap_agent_shader(body)
        else:
            shader = self._wrap_object_shader(body)

        if self.debug:
            print("[JIT Debug] Generated shader:\n", shader)
        return shader

    # ------------------------------------------------------------------------
    # Object shader wrapper (full physics, symplectic Euler, constraints/collisions off by default)
    # ------------------------------------------------------------------------
    def _wrap_object_shader(self, body):
        """
        Build the complete compute shader for object‑mode scripts.
        Includes (conditionally):
          - Object, Constraint, Collision structs
          - All SSBO bindings (constraints and collisions only if used)
          - Utility functions (safePow, noise, complex arithmetic)
          - Constraint solvers and collision detection
          - User‑exposed APIs (apply_constraints, detect_collision, ...)
          - Main() that reads the object, executes user code (including inline constraints),
            then optionally applies SSBO constraints and/or collisions, and writes back.
        Inline constraints (spring, distance, boundary, angle) are generated directly
        in the user code and do not require additional shader functions.
        """
        decls = []
        for v in sorted(self.assigned_vars):
            typ = self.var_types.get(v, "float")
            decls.append(f"    {typ} {v};")
        decls_str = "\n".join(decls)

        # Include automatically unless user explicitly took control.
        include_constraints = 0 if self.user_applies_constraints else 1
        include_collisions = 0 if self.user_handles_collisions else 1

        return f'''#version 430 core
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

float rand(vec2 seed) {{
    return fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);
}}
float smoothNoise(vec2 p) {{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = rand(i);
    float b = rand(i + vec2(1.0, 0.0));
    float c = rand(i + vec2(0.0, 1.0));
    float d = rand(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}}
float noise(vec2 p) {{
    float sum = 0.0;
    float amp = 0.5;
    float freq = 4.0;
    for (int i = 0; i < 3; ++i) {{
        sum += amp * smoothNoise(p * freq);
        amp *= 0.5;
        freq *= 2.0;
    }}
    return sum;
}}

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
'''

    # ------------------------------------------------------------------------
    # Paint shader wrapper (unchanged except for scratchpad read)
    # ------------------------------------------------------------------------
    def _wrap_paint_shader(self, body):
        """
        Build the compute shader for paint‑mode scripts.
        Includes:
          - Double‑buffered texture read/write
          - Object SSBO binding for p[index]
          - Pan/zoom compensation uniforms and functions
          - Previous‑frame sampling (sample_prev_*, avg_prev_*)
          - Scratchpad read (read‑only)
        """
        decls = []
        for v in sorted(self.assigned_vars):
            if v == 'color':
                typ = 'vec4'
            else:
                typ = self.var_types.get(v, "float")
            decls.append(f"    {typ} {v};")
        decls_str = "\n".join(decls)

        return f'''#version 430 core
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

float rand(vec2 seed) {{
    return fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);
}}
float smoothNoise(vec2 p) {{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = rand(i);
    float b = rand(i + vec2(1.0, 0.0));
    float c = rand(i + vec2(0.0, 1.0));
    float d = rand(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}}
float noise(vec2 p) {{
    float sum = 0.0;
    float amp = 0.5;
    float freq = 4.0;
    for (int i = 0; i < 3; ++i) {{
        sum += amp * smoothNoise(p * freq);
        amp *= 0.5;
        freq *= 2.0;
    }}
    return sum;
}}

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
'''

    # ------------------------------------------------------------------------
    # Agent shader wrapper (new for agent mode)
    # ------------------------------------------------------------------------
    def _wrap_agent_shader(self, body):
        """
        Build the compute shader for agent‑mode scripts.
        Runs one thread per pending signal.
        Includes:
          - Object SSBO (read‑only)
          - Scratchpad read/write
          - Signal queue read (read‑only)
          - Built‑ins: signal_object_idx, signal_payload
          - Uniforms: uNumObjects, uAgentID, uSignalCount
        """
        decls = []
        for v in sorted(self.assigned_vars):
            typ = self.var_types.get(v, "float")
            decls.append(f"    {typ} {v};")
        decls_str = "\n".join(decls)

        return f'''#version 430 core
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
'''

    # ------------------------------------------------------------------------
    # AST Visitors
    # ------------------------------------------------------------------------
    def visit_FunctionDef(self, node):
        for stmt in node.body:
            self.visit(stmt)

    def visit_Expr(self, node):
        if isinstance(node.value, ast.Call) and isinstance(node.value.func, ast.Name):
            func_name = node.value.func.id
            if func_name == 'apply_constraints':
                self.lines.append(self.indent_str() + "apply_constraints();")
                return
            if func_name == 'resolve_collision':
                args = [self._expr_to_glsl(a) for a in node.value.args]
                self.lines.append(self.indent_str() + f"resolve_collision({', '.join(args)});")
                return
            # Inline constraint functions
            if func_name == 'spring':
                self._handle_spring(node.value)
                return
            if func_name == 'distance':
                self._handle_distance(node.value)
                return
            if func_name == 'boundary':
                self._handle_boundary(node.value)
                return
            if func_name == 'angle':
                self._handle_angle(node.value)
                return
        self.generic_visit(node)

    # ------------------------------------------------------------------------
    # Inline constraint handlers (unchanged)
    # ------------------------------------------------------------------------
    def _handle_spring(self, node):
        kwargs = {kw.arg: kw.value for kw in node.keywords}
        target = self._expr_to_glsl(kwargs.get('target', ast.Constant(value=0)))
        stiffness = self._expr_to_glsl(kwargs.get('stiffness', ast.Constant(value=1.0)))
        damping = self._expr_to_glsl(kwargs.get('damping', ast.Constant(value=0.0)))
        rest = self._expr_to_glsl(kwargs.get('rest_length', ast.Constant(value=1.0)))

        code = f'''
        {{
            int other = int({target});
            if (other >= 0 && other < uNumObjects && other != self) {{
                vec2 otherPos = objectsIn[other].position;
                vec2 otherVel = objectsIn[other].velocity;
                float otherMass = max(EPSILON, objectsIn[other].mass);
                vec2 delta = pos - otherPos;
                float dist = length(delta);
                if (dist > EPSILON) {{
                    vec2 dir = delta / dist;
                    float relVel = dot(vel - otherVel, dir);
                    float forceMag = {stiffness} * (dist - {rest}) + {damping} * relVel;
                    vec2 force = -dir * forceMag;
                    ax += force.x / mass;
                    ay += force.y / mass;
                }}
            }}
        }}
        '''
        self.lines.append(self.indent_str() + code.strip())

    def _handle_distance(self, node):
        kwargs = {kw.arg: kw.value for kw in node.keywords}
        target = self._expr_to_glsl(kwargs.get('target', ast.Constant(value=0)))
        length = self._expr_to_glsl(kwargs.get('length', ast.Constant(value=1.0)))
        beta = 0.2
        gamma = 0.0

        code = f'''
        {{
            int other = int({target});
            if (other >= 0 && other < uNumObjects && other != self) {{
                vec2 otherPos = objectsIn[other].position;
                vec2 otherVel = objectsIn[other].velocity;
                float otherMass = max(EPSILON, objectsIn[other].mass);
                vec2 delta = pos - otherPos;
                float dist = length(delta);
                if (dist > EPSILON) {{
                    vec2 n = delta / dist;
                    float C = dist - {length};
                    float vn = dot(vel - otherVel, n);
                    float invMassSum = (1.0 / mass) + (1.0 / otherMass);
                    float lambda = -(vn + {beta} * C / uDt) / (invMassSum + {gamma});
                    vec2 impulse = lambda * n;
                    vel += impulse / mass;
                    pos += n * ({beta} * C);
                }}
            }}
        }}
        '''
        self.lines.append(self.indent_str() + code.strip())

    def _handle_boundary(self, node):
        kwargs = {kw.arg: kw.value for kw in node.keywords}
        min_x = self._expr_to_glsl(kwargs.get('min_x', ast.Constant(value=-1e10)))
        max_x = self._expr_to_glsl(kwargs.get('max_x', ast.Constant(value=1e10)))
        min_y = self._expr_to_glsl(kwargs.get('min_y', ast.Constant(value=-1e10)))
        max_y = self._expr_to_glsl(kwargs.get('max_y', ast.Constant(value=1e10)))
        elasticity = 0.7
        friction = 0.95

        code = f'''
        {{
            const float elasticity = {elasticity};
            const float friction = {friction};
            if (pos.x < {min_x}) {{ pos.x = {min_x}; vel.x = abs(vel.x) * elasticity; vel.y *= friction; }}
            else if (pos.x > {max_x}) {{ pos.x = {max_x}; vel.x = -abs(vel.x) * elasticity; vel.y *= friction; }}
            if (pos.y < {min_y}) {{ pos.y = {min_y}; vel.y = abs(vel.y) * elasticity; vel.x *= friction; }}
            else if (pos.y > {max_y}) {{ pos.y = {max_y}; vel.y = -abs(vel.y) * elasticity; vel.x *= friction; }}
        }}
        '''
        self.lines.append(self.indent_str() + code.strip())

    def _handle_angle(self, node):
        kwargs = {kw.arg: kw.value for kw in node.keywords}
        target1 = self._expr_to_glsl(kwargs.get('target1', ast.Constant(value=0)))
        target2 = self._expr_to_glsl(kwargs.get('target2', ast.Constant(value=0)))
        target_angle = self._expr_to_glsl(kwargs.get('angle', ast.Constant(value=0.0)))
        beta = 0.2
        gamma = 0.0

        code = f'''
        {{
            int other1 = int({target1});
            int other2 = int({target2});
            if (other1 >= 0 && other1 < uNumObjects && other1 != self &&
                other2 >= 0 && other2 < uNumObjects && other2 != self &&
                other1 != other2) {{

                vec2 B = objectsIn[other1].position;
                vec2 C = objectsIn[other2].position;
                vec2 r1 = B - pos;
                vec2 r2 = C - pos;
                float dotProd = dot(r1, r2);
                float crossProd = r1.x*r2.y - r1.y*r2.x;
                float currentAngle = atan(crossProd, dotProd);

                // Error
                float C_err = currentAngle - {target_angle};

                // Compute gradient of angle w.r.t. pos (self) using finite differences
                float eps = EPSILON;
                vec2 pos_xp = pos + vec2(eps, 0.0);
                vec2 pos_xm = pos - vec2(eps, 0.0);
                vec2 pos_yp = pos + vec2(0.0, eps);
                vec2 pos_ym = pos - vec2(0.0, eps);

                float angle_xp = atan( (B-pos_xp).x*(C-pos_xp).y - (B-pos_xp).y*(C-pos_xp).x,
                                       dot(B-pos_xp, C-pos_xp) );
                float angle_xm = atan( (B-pos_xm).x*(C-pos_xm).y - (B-pos_xm).y*(C-pos_xm).x,
                                       dot(B-pos_xm, C-pos_xm) );
                float angle_yp = atan( (B-pos_yp).x*(C-pos_yp).y - (B-pos_yp).y*(C-pos_yp).x,
                                       dot(B-pos_yp, C-pos_yp) );
                float angle_ym = atan( (B-pos_ym).x*(C-pos_ym).y - (B-pos_ym).y*(C-pos_ym).x,
                                       dot(B-pos_ym, C-pos_ym) );

                vec2 g = vec2( (angle_xp - angle_xm) / (2.0*eps),
                               (angle_yp - angle_ym) / (2.0*eps) );

                float g2 = dot(g, g);
                if (g2 > 1e-10) {{
                    float vn = dot(vel, g);
                    float lambda = -(vn + {beta} * C_err / uDt) / (g2 / mass + {gamma});
                    vel += lambda * g / mass;
                    float posCorr = -{beta} * C_err / g2;
                    pos += posCorr * g;
                }}
            }}
        }}
        '''
        self.lines.append(self.indent_str() + code.strip())

    # ------------------------------------------------------------------------
    # Other AST visitors (unchanged)
    # ------------------------------------------------------------------------
    def visit_Assign(self, node):
        if len(node.targets) != 1:
            raise SyntaxError("Only single target assignments supported")
        target = node.targets[0]
        expr_str = self._expr_to_glsl(node.value)

        if isinstance(target, ast.Name):
            varname = target.id
            typ = self._infer_expr_type(node.value)
            if typ:
                self.var_types[varname] = typ
            if self._is_complex_expr(node.value):
                self.complex_vars.add(varname)
                if not typ or typ == "float":
                    self.var_types[varname] = "vec2"
            self.lines.append(self.indent_str() + f"{varname} = {expr_str};")
        elif isinstance(target, ast.Attribute):
            lvalue = self._visit_attribute(target)
            self.lines.append(self.indent_str() + f"{lvalue} = {expr_str};")
        else:
            raise SyntaxError("Unsupported assignment target")

    def visit_AugAssign(self, node):
        if isinstance(node.target, ast.Name):
            target = node.target.id
            op = self._aug_op(node.op)
            expr = self._expr_to_glsl(node.value)
            if self._is_complex_expr(node.value):
                self.complex_vars.add(target)
                if self.var_types.get(target) in (None, "float"):
                    self.var_types[target] = "vec2"
            self.lines.append(self.indent_str() + f"{target} {op}= {expr};")
        else:
            raise SyntaxError("Only simple augmented assignments")

    def visit_If(self, node):
        cond = self._expr_to_glsl(node.test)
        self.lines.append(self.indent_str() + f"if ({cond}) {{")
        self.indent += 1
        for stmt in node.body:
            self.visit(stmt)
        self.indent -= 1
        if node.orelse:
            self.lines.append(self.indent_str() + "} else {")
            self.indent += 1
            for stmt in node.orelse:
                self.visit(stmt)
            self.indent -= 1
        self.lines.append(self.indent_str() + "}")

    def visit_For(self, node):
        if not isinstance(node.iter, ast.Call) or not isinstance(node.iter.func, ast.Name) or node.iter.func.id != 'range':
            raise SyntaxError("Only 'for i in range(...)' supported")
        args = node.iter.args
        if not (1 <= len(args) <= 3):
            raise SyntaxError("range() requires 1 to 3 arguments")
        var = node.target.id if isinstance(node.target, ast.Name) else "_i"

        start = self._expr_to_glsl(args[0]) if len(args) >= 1 else "0"
        stop = self._expr_to_glsl(args[1]) if len(args) >= 2 else self._expr_to_glsl(args[0])
        step = self._expr_to_glsl(args[2]) if len(args) == 3 else "1"

        if len(args) == 1:
            loop = f"for (int {var} = 0; {var} < int({stop}); {var}++)"
        elif len(args) == 2:
            loop = f"for (int {var} = int({start}); {var} < int({stop}); {var}++)"
        else:
            loop = f"for (int {var} = int({start}); {var} < int({stop}); {var} += int({step}))"

        self.lines.append(self.indent_str() + loop + " {")
        self.indent += 1
        for stmt in node.body:
            self.visit(stmt)
        self.indent -= 1
        self.lines.append(self.indent_str() + "}")

    def visit_While(self, node):
        cond = self._expr_to_glsl(node.test)
        self.lines.append(self.indent_str() + f"while ({cond}) {{")
        self.indent += 1
        for stmt in node.body:
            self.visit(stmt)
        self.indent -= 1
        self.lines.append(self.indent_str() + "}")

    def visit_Continue(self, node):
        self.lines.append(self.indent_str() + "continue;")

    def visit_Break(self, node):
        self.lines.append(self.indent_str() + "break;")

    def visit_IfExp(self, node):
        cond = self._expr_to_glsl(node.test)
        body = self._expr_to_glsl(node.body)
        orelse = self._expr_to_glsl(node.orelse)
        return f"(({cond}) ? ({body}) : ({orelse}))"

    def visit_List(self, node):
        rank = self._list_rank(node)
        if rank == 1:
            elems = [self._expr_to_glsl(e) for e in node.elts]
            n = len(elems)
            if n <= 4:
                return f"vec{n}({', '.join(elems)})"
            else:
                return f"float[{n}]({', '.join(elems)})"
        elif rank == 2:
            rows = node.elts
            if not all(isinstance(r, ast.List) for r in rows):
                raise SyntaxError("Invalid matrix literal: rows must be lists")
            n_rows = len(rows)
            n_cols = len(rows[0].elts)
            elems = []
            for row in rows:
                if len(row.elts) != n_cols:
                    raise SyntaxError("Inconsistent matrix row lengths")
                for e in row.elts:
                    elems.append(self._expr_to_glsl(e))
            if n_rows == n_cols and n_rows in (2,3,4):
                return f"mat{n_rows}({', '.join(elems)})"
            else:
                raise NotImplementedError("Only square 2x2, 3x3, and 4x4 matrices are supported")
        else:
            raise NotImplementedError("Tensor rank > 2 not supported")

    def _list_rank(self, node):
        if not isinstance(node, ast.List):
            return 0
        if not node.elts:
            return 1
        first = node.elts[0]
        if isinstance(first, ast.List):
            return 1 + self._list_rank(first)
        return 1

    def _infer_list_type(self, node):
        rank = self._list_rank(node)
        if rank == 1:
            n = len(node.elts)
            if n <= 4:
                return f"vec{n}"
            else:
                return "float[]"
        elif rank == 2:
            rows = node.elts
            if not rows:
                return None
            n_rows = len(rows)
            n_cols = len(rows[0].elts) if isinstance(rows[0], ast.List) else 0
            if n_rows == n_cols and n_rows in (2,3,4):
                return f"mat{n_rows}"
        return None

    def _is_complex_expr(self, node):
        if isinstance(node, ast.Constant):
            return isinstance(node.value, complex)
        if isinstance(node, ast.Name):
            return node.id in self.complex_vars
        if isinstance(node, ast.Call):
            func_name = node.func.id if isinstance(node.func, ast.Name) else None
            if func_name in ('cAdd', 'cSub', 'cMul', 'cDiv', 'cLog', 'cExp',
                             'cPow', 'cSin', 'cCos', 'cTan', 'conj'):
                return True
            return False
        if isinstance(node, ast.BinOp):
            return self._is_complex_expr(node.left) or self._is_complex_expr(node.right)
        if isinstance(node, ast.UnaryOp):
            return self._is_complex_expr(node.operand)
        if isinstance(node, ast.Attribute):
            return False
        return False

    def _infer_expr_type(self, node):
        if isinstance(node, ast.Name):
            return self.var_types.get(node.id)
        if isinstance(node, ast.Constant):
            if isinstance(node.value, (int, float)):
                return "float"
            if isinstance(node.value, complex):
                return "vec2"
            return None
        if isinstance(node, ast.List):
            return self._infer_list_type(node)
        if isinstance(node, ast.BinOp):
            left_type = self._infer_expr_type(node.left)
            right_type = self._infer_expr_type(node.right)
            if left_type and right_type and left_type == right_type:
                return left_type
            if left_type and left_type.startswith('mat') and right_type and right_type.startswith('vec'):
                size = left_type[3]
                if right_type == f'vec{size}':
                    return f'vec{size}'
            if left_type and left_type.startswith('vec') and right_type and right_type.startswith('mat'):
                size = right_type[3]
                if left_type == f'vec{size}':
                    return f'vec{size}'
            if left_type == "float" and right_type:
                return right_type
            if right_type == "float" and left_type:
                return left_type
            if self._is_complex_expr(node):
                return "vec2"
            return None
        if isinstance(node, ast.Call):
            func_name = node.func.id if isinstance(node.func, ast.Name) else None
            if func_name in ('sin', 'cos', 'tan', 'sqrt', 'log', 'exp', 'abs',
                             'floor', 'ceil', 'frac', 'sign', 'step', 'noise',
                             'rand', 'length'):
                return "float"
            if func_name in ('min', 'max', 'clamp', 'mod', 'atan2', 'dot'):
                return "float"
            if func_name == 'cross':
                return "vec3"
            if func_name == 'diff':
                return "float"
            if func_name == 'len':
                return "float"
            if func_name in ('cAdd', 'cSub', 'cMul', 'cDiv', 'cLog', 'cExp',
                             'cPow', 'cSin', 'cCos', 'cTan', 'conj'):
                return "vec2"
            if func_name in ('real', 'imag', 'arg', 'sum', 'min', 'max'):
                return "float"
            return None
        if isinstance(node, ast.Subscript):
            value_type = self._infer_expr_type(node.value)
            if value_type and value_type.startswith('vec'):
                return "float"
            if value_type and value_type.startswith('mat'):
                size = value_type[3]
                return f"vec{size}"
            return None
        if isinstance(node, ast.Attribute):
            return None
        if isinstance(node, ast.IfExp):
            body_type = self._infer_expr_type(node.body)
            orelse_type = self._infer_expr_type(node.orelse)
            if body_type == orelse_type:
                return body_type
            return None
        return None

    def _visit_subscript(self, node):
        if isinstance(node.value, ast.Name) and node.value.id == 'p':
            slice_expr = self._get_slice_expr(node.slice)
            idx = self._expr_to_glsl(slice_expr)
            return f"objectsIn[{idx}]"
        if isinstance(node.value, ast.Name) and node.value.id in self.var_types:
            var = node.value.id
            typ = self.var_types[var]
            slice_expr = self._get_slice_expr(node.slice)
            idx = self._expr_to_glsl(slice_expr)
            if typ.startswith('vec'):
                return f"{var}[{idx}]"
            elif typ.startswith('mat'):
                return f"{var}[{idx}]"
        if isinstance(node.value, ast.Subscript):
            base = self._visit_subscript(node.value)
            slice_expr = self._get_slice_expr(node.slice)
            idx = self._expr_to_glsl(slice_expr)
            return f"{base}[{idx}]"
        value = self._expr_to_glsl(node.value)
        slice_expr = self._get_slice_expr(node.slice)
        idx = self._expr_to_glsl(slice_expr)
        return f"{value}[{idx}]"

    def _get_slice_expr(self, slice_node):
        if isinstance(slice_node, ast.Index):
            return slice_node.value
        return slice_node

    def _visit_attribute(self, node):
        if isinstance(node.value, ast.Name) and node.value.id == 'p':
            mapping = {
                'x': 'position.x', 'y': 'position.y',
                'vx': 'velocity.x', 'vy': 'velocity.y',
                'mass': 'mass', 'charge': 'charge',
                'theta': 'visualData.z', 'omega': 'visualData.w',
                'r': 'color.r', 'g': 'color.g', 'b': 'color.b', 'a': 'color.a'
            }
            if node.attr in mapping:
                return mapping[node.attr]
            raise SyntaxError(f"Unknown p attribute: {node.attr}")

        if isinstance(node.value, ast.Subscript):
            if isinstance(node.value.value, ast.Name) and node.value.value.id == 'p':
                slice_expr = self._get_slice_expr(node.value.slice)
                idx_expr = self._expr_to_glsl(slice_expr)
                obj_ref = f"objectsIn[{idx_expr}]"
                field_map = {
                    'x': 'position.x', 'y': 'position.y',
                    'vx': 'velocity.x', 'vy': 'velocity.y',
                    'mass': 'mass', 'charge': 'charge',
                    'theta': 'visualData.z', 'omega': 'visualData.w',
                    'r': 'color.r', 'g': 'color.g', 'b': 'color.b', 'a': 'color.a'
                }
                if node.attr in field_map:
                    return f"{obj_ref}.{field_map[node.attr]}"
                raise SyntaxError(f"Unknown property for p[index]: {node.attr}")

        base_str = self._expr_to_glsl(node.value)
        if self._is_complex_expr(node.value):
            if node.attr == 'real':
                return f"real({base_str})"
            if node.attr == 'imag':
                return f"imag({base_str})"
            if node.attr == 'conjugate':
                return f"conj({base_str})"

        if isinstance(node.value, ast.Name):
            obj = node.value.id
            field_map = {
                'x': 'position.x', 'y': 'position.y',
                'vx': 'velocity.x', 'vy': 'velocity.y',
                'mass': 'mass', 'charge': 'charge',
                'theta': 'visualData.z', 'omega': 'visualData.w',
                'r': 'color.r', 'g': 'color.g', 'b': 'color.b', 'a': 'color.a'
            }
            if node.attr in field_map:
                if obj == 'color':
                    return field_map[node.attr]
                else:
                    return f"{obj}.{field_map[node.attr]}"

        raise SyntaxError(f"Unsupported attribute access: {node.attr}")

    def _expr_to_glsl(self, node):
        return self._expr_to_glsl_with_subst(node, {})

    def _expr_to_glsl_with_subst(self, node, subst):
        if isinstance(node, ast.Constant):
            if isinstance(node.value, bool):
                return "true" if node.value else "false"
            if isinstance(node.value, complex):
                return f"vec2({node.value.real}, {node.value.imag})"
            return str(node.value)
        elif isinstance(node, ast.Name):
            name = node.id
            if name in subst:
                return subst[name]
            if name == 'i':
                self.complex_vars.add(name)
                self.var_types[name] = "vec2"
                return 'i'
            if name == 'num_objects':
                return 'uNumObjects'
            if name == 'dt':
                return 'uDt'
            if name == 'time':
                return 'uTime'
            if name == 'idx':
                return 'objectIndex' if self.mode != 'agent' else 'int(gl_GlobalInvocationID.x)'
            if name == 'group_count':
                return 'uGroupCount'
            # Agent built‑ins
            if self.mode == 'agent' and name in ('signal_object_idx', 'signal_payload'):
                return name
            if name in ('x', 'y', 'vx', 'vy', 'mass', 'charge', 'theta', 'omega',
                        'color', 'pos', 'vel'):
                return name
            if self.mode == 'paint':
                if name == 'px':
                    return 'px'
                if name == 'py':
                    return 'py'
                if name == 'prev_r':
                    return 'prev_r'
                if name == 'prev_g':
                    return 'prev_g'
                if name == 'prev_b':
                    return 'prev_b'
                if name == 'prev_a':
                    return 'prev_a'
                if name == 't':
                    return 'uTime'
            if name in self.globals and isinstance(self.globals[name], (int, float)):
                return str(self.globals[name])
            return name
        elif isinstance(node, ast.UnaryOp):
            operand = self._expr_to_glsl_with_subst(node.operand, subst)
            if isinstance(node.op, ast.USub):
                return f"(-{operand})"
            elif isinstance(node.op, ast.Not):
                return f"(!{operand})"
            elif isinstance(node.op, ast.UAdd):
                return f"(+{operand})"
            elif isinstance(node.op, ast.Abs):
                if self._is_complex_expr(node.operand):
                    return f"length({operand})"
                return f"abs({operand})"
            else:
                raise SyntaxError("Unsupported unary operator")
        elif isinstance(node, ast.BinOp):
            left = self._expr_to_glsl(node.left)
            right = self._expr_to_glsl(node.right)
            left_is_c = self._is_complex_expr(node.left)
            right_is_c = self._is_complex_expr(node.right)

            if isinstance(node.op, ast.Pow):
                if left_is_c or right_is_c:
                    left_arg = left if left_is_c else f"vec2({left}, 0.0)"
                    right_arg = right if right_is_c else f"vec2({right}, 0.0)"
                    return f"cPow({left_arg}, {right_arg})"
                return f"safePow(float({left}), float({right}))"

            if isinstance(node.op, ast.Mult) and left_is_c and right_is_c:
                return f"cMul({left}, {right})"
            if isinstance(node.op, ast.Div) and left_is_c and right_is_c:
                return f"cDiv({left}, {right})"

            op = self._bin_op(node.op)
            if isinstance(node.op, ast.Div):
                return f"(float({left}) / float({right}))"
            return f"({left} {op} {right})"
        elif isinstance(node, ast.Compare):
            if len(node.ops) != 1:
                raise SyntaxError("Chained comparisons not supported")
            left = self._expr_to_glsl_with_subst(node.left, subst)
            right = self._expr_to_glsl_with_subst(node.comparators[0], subst)
            op = self._compare_op(node.ops[0])
            return f"({left} {op} {right})"
        elif isinstance(node, ast.BoolOp):
            if isinstance(node.op, ast.And):
                op = "&&"
            elif isinstance(node.op, ast.Or):
                op = "||"
            else:
                raise SyntaxError("Unknown boolean operator")
            values = [self._expr_to_glsl_with_subst(v, subst) for v in node.values]
            return f"({f' {op} '.join(values)})"
        elif isinstance(node, ast.Call):
            func_name = node.func.id if isinstance(node.func, ast.Name) else None
            args = [self._expr_to_glsl_with_subst(a, subst) for a in node.args]

            # --- NEW: scratchpad read/write and signal ---
            if func_name == 'scratchpad_read':
                if len(args) != 2:
                    raise SyntaxError("scratchpad_read(id, index)")
                return f"scratchpad_read(int({args[0]}), int({args[1]}))"
            if func_name == 'scratchpad_write':
                if len(args) != 3:
                    raise SyntaxError("scratchpad_write(id, index, value)")
                return f"scratchpad_write(int({args[0]}), int({args[1]}), {args[2]})"
            if func_name == 'signal':
                if len(args) != 2:
                    raise SyntaxError("signal(agent_id, payload)")
                return f"signal_enqueue(int({args[0]}), {args[1]})"

            if func_name == 'diff':
                return self._emit_diff(node, subst)

            if func_name == 'rand' and len(args) == 0:
                offset = self.rand_call_counter
                self.rand_call_counter += 1
                return f"rand(vec2(uTime, float(objectIndex) + {offset}.0))"

            if func_name == 'len':
                if len(args) != 1:
                    raise SyntaxError("len() takes exactly one argument")
                arg = node.args[0]
                if isinstance(arg, ast.Name) and arg.id in self.var_types:
                    typ = self.var_types[arg.id]
                    if typ.startswith('vec'):
                        return f"{arg.id}.length()"
                    elif typ.startswith('mat'):
                        size = int(typ[3])
                        return str(size * size)
                if isinstance(arg, ast.List):
                    return str(len(arg.elts))
                return "float(-1.0)"

            if func_name == 'sum':
                if len(args) != 1:
                    raise SyntaxError("sum() takes exactly one argument")
                arg = node.args[0]
                arg_str = self._expr_to_glsl(arg)
                typ = self._infer_expr_type(arg)
                if typ and typ.startswith('vec'):
                    n = int(typ[3])
                    comps = [f"{arg_str}.{chr(120+i)}" for i in range(n)]
                    return f"({' + '.join(comps)})"
                raise SyntaxError("sum() only works on vectors (vec2/vec3/vec4)")

            if func_name == 'min':
                if len(args) == 1:
                    arg = node.args[0]
                    arg_str = self._expr_to_glsl(arg)
                    typ = self._infer_expr_type(arg)
                    if typ and typ.startswith('vec'):
                        n = int(typ[3])
                        comps = [f"{arg_str}.{chr(120+i)}" for i in range(n)]
                        res = comps[0]
                        for c in comps[1:]:
                            res = f"min({res}, {c})"
                        return res
                    raise SyntaxError("min() with one argument only works on vectors")
                elif len(args) == 2:
                    return f"min({args[0]}, {args[1]})"
                else:
                    raise SyntaxError("min() takes 1 or 2 arguments")

            if func_name == 'max':
                if len(args) == 1:
                    arg = node.args[0]
                    arg_str = self._expr_to_glsl(arg)
                    typ = self._infer_expr_type(arg)
                    if typ and typ.startswith('vec'):
                        n = int(typ[3])
                        comps = [f"{arg_str}.{chr(120+i)}" for i in range(n)]
                        res = comps[0]
                        for c in comps[1:]:
                            res = f"max({res}, {c})"
                        return res
                    raise SyntaxError("max() with one argument only works on vectors")
                elif len(args) == 2:
                    return f"max({args[0]}, {args[1]})"
                else:
                    raise SyntaxError("max() takes 1 or 2 arguments")

            if func_name == 'pow':
                if len(args) != 2:
                    raise SyntaxError("pow() takes exactly two arguments")
                return f"safePow({args[0]}, {args[1]})"

            if func_name == 'int':
                if len(args) != 1:
                    raise SyntaxError("int() takes exactly one argument")
                return f"int({args[0]})"

            if func_name == 'float':
                if len(args) != 1:
                    raise SyntaxError("float() takes exactly one argument")
                return f"float({args[0]})"

            if self.mode == 'paint':
                if func_name == 'sample_prev_r':
                    return f"sample_prev_r(vec2(px, py), {args[0]})"
                if func_name == 'sample_prev_g':
                    return f"sample_prev_g(vec2(px, py), {args[0]})"
                if func_name == 'sample_prev_b':
                    return f"sample_prev_b(vec2(px, py), {args[0]})"
                if func_name == 'sample_prev_a':
                    return f"sample_prev_a(vec2(px, py), {args[0]})"
                if func_name == 'avg_prev_r':
                    return "avg_prev_r()"
                if func_name == 'avg_prev_g':
                    return "avg_prev_g()"
                if func_name == 'avg_prev_b':
                    return "avg_prev_b()"
                if func_name == 'avg_prev_a':
                    return "avg_prev_a()"

            complex_funcs = {
                'cAdd': 'cAdd', 'cSub': 'cSub', 'cMul': 'cMul', 'cDiv': 'cDiv',
                'cLog': 'cLog', 'cExp': 'cExp', 'cPow': 'cPow',
                'cSin': 'cSin', 'cCos': 'cCos', 'cTan': 'cTan',
                'real': 'real', 'imag': 'imag', 'conj': 'conj', 'arg': 'arg'
            }
            if func_name in complex_funcs:
                return f"{complex_funcs[func_name]}({', '.join(args)})"

            if func_name and func_name in self.globals:
                inlined = self._inline_function_call(func_name, args, node)
                if inlined is not None:
                    return inlined

            if func_name in self._glsl_func_map:
                return f"{self._glsl_func_map[func_name]}({', '.join(args)})"

            return f"{func_name}({', '.join(args)})"
        elif isinstance(node, ast.Subscript):
            return self._visit_subscript(node)
        elif isinstance(node, ast.Attribute):
            return self._visit_attribute(node)
        elif isinstance(node, ast.List):
            return self.visit_List(node)
        elif isinstance(node, ast.IfExp):
            return self.visit_IfExp(node)
        else:
            raise NotImplementedError(f"Unsupported expression: {type(node)}")

    _glsl_func_map = {
        'sin': 'sin', 'cos': 'cos', 'tan': 'tan',
        'sqrt': 'sqrt', 'log': 'log', 'exp': 'exp',
        'abs': 'abs', 'floor': 'floor', 'ceil': 'ceil',
        'frac': 'frac', 'sign': 'sign', 'step': 'step',
        'min': 'min', 'max': 'max', 'clamp': 'clamp',
        'mod': 'mod', 'atan2': 'atan2',
        'dot': 'dot', 'cross': 'cross', 'length': 'length',
        'noise': 'noise',
        'conjugate': 'conj'
    }

    def _emit_diff(self, node, subst):
        if len(node.args) < 2:
            raise SyntaxError("diff() needs expr and wrt variable")
        expr_node = node.args[0]
        wrt_node = node.args[1]
        if not isinstance(wrt_node, ast.Name):
            raise SyntaxError("wrt must be a variable name")
        wrt = wrt_node.id
        order = 1
        if len(node.args) >= 3:
            order_arg = node.args[2]
            if isinstance(order_arg, ast.Constant) and isinstance(order_arg.value, int):
                order = order_arg.value
            else:
                raise SyntaxError("Order must be an integer constant")
        if order not in (1,2):
            raise SyntaxError("Only order 1 and 2 supported")
        plus_subst = dict(subst)
        plus_subst[wrt] = f"({wrt} + EPSILON)"
        minus_subst = dict(subst)
        minus_subst[wrt] = f"({wrt} - EPSILON)"
        expr_plus = self._expr_to_glsl_with_subst(expr_node, plus_subst)
        expr_minus = self._expr_to_glsl_with_subst(expr_node, minus_subst)
        if order == 1:
            return f"(({expr_plus} - {expr_minus}) / (2.0 * EPSILON))"
        else:
            expr_center = self._expr_to_glsl_with_subst(expr_node, subst)
            return f"(({expr_plus} - 2.0*{expr_center} + {expr_minus}) / (EPSILON*EPSILON))"

    def _inline_function_call(self, func_name, args, node):
        if func_name not in self.globals:
            return None
        func_obj = self.globals[func_name]
        if not callable(func_obj):
            return None
        if func_name in self.inlined_functions:
            return None
        if self.inline_depth >= self.max_inline_depth:
            return None
        try:
            source = inspect.getsource(func_obj)
        except (TypeError, OSError):
            return None
        tree = ast.parse(textwrap.dedent(source))
        if not isinstance(tree.body[0], ast.FunctionDef):
            return None
        func_def = tree.body[0]
        if len(func_def.body) != 1 or not isinstance(func_def.body[0], ast.Return):
            return None
        param_names = [arg.arg for arg in func_def.args.args]
        if len(args) != len(param_names):
            return None
        subst = dict(zip(param_names, args))
        self.inlined_functions.add(func_name)
        self.inline_depth += 1
        result = self._expr_to_glsl_with_subst(func_def.body[0].value, subst)
        self.inline_depth -= 1
        self.inlined_functions.remove(func_name)
        return result

    def _bin_op(self, op):
        if isinstance(op, ast.Add): return "+"
        if isinstance(op, ast.Sub): return "-"
        if isinstance(op, ast.Mult): return "*"
        if isinstance(op, ast.Div): return "/"
        if isinstance(op, ast.FloorDiv): return "/"
        raise SyntaxError("Unsupported binary operator")

    def _aug_op(self, op):
        if isinstance(op, ast.Add): return "+"
        if isinstance(op, ast.Sub): return "-"
        if isinstance(op, ast.Mult): return "*"
        if isinstance(op, ast.Div): return "/"
        raise SyntaxError("Unsupported augmented assignment")

    def _compare_op(self, op):
        if isinstance(op, ast.Lt): return "<"
        if isinstance(op, ast.LtE): return "<="
        if isinstance(op, ast.Gt): return ">"
        if isinstance(op, ast.GtE): return ">="
        if isinstance(op, ast.Eq): return "=="
        if isinstance(op, ast.NotEq): return "!="
        raise SyntaxError("Unsupported comparison operator")


# =============================================================================
# AssignCollector – collects target variables assigned within Python AST
# =============================================================================
class AssignCollector(ast.NodeVisitor):
    def __init__(self):
        self.vars = set()
        self._is_root = True

    def _collect_target(self, target):
        if isinstance(target, ast.Name):
            self.vars.add(target.id)
        elif isinstance(target, (ast.Tuple, ast.List)):
            for elt in target.elts:
                self._collect_target(elt)

    def visit_Assign(self, node):
        for target in node.targets:
            self._collect_target(target)
        self.generic_visit(node)

    def visit_AugAssign(self, node):
        self._collect_target(node.target)
        self.generic_visit(node)

    def visit_For(self, node):
        self._collect_target(node.target)
        self.generic_visit(node)


# =============================================================================
# User‑facing decorator
# =============================================================================
def script(sim, debug=False, mode='object'):
    def decorator(func):
        generator = GLSLGenerator(debug=debug, mode=mode)
        shader_source = generator.generate(func)
        
        if mode == 'agent':
            script_id = sim.register_agent(shader_source)   # marks as agent
        else:
            script_id = sim.register_script(shader_source)
        func._script_id = script_id
        func._mode = mode
        return func
    return decorator
