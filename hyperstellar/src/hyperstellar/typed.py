"""
Type stubs for the Hyperstellar JIT environment.

This module provides type annotations for names understood by GLSLGenerator
and for uniforms injected by the object, paint, and agent shader wrappers.
It is intended to be imported conditionally (e.g. under ``if TYPE_CHECKING:``)
to satisfy linters and IDEs without affecting runtime behaviour or the AST
traversal performed by the JIT compiler.

The JIT translates the function body rather than inlining this module. Names
in the script-facing sections correspond to the mode-specific built-ins in
NameChecker. The ``u...`` names are lower-level GLSL uniforms supplied by the
shader wrappers; not every one is accepted as a bare Python-script name by
NameChecker.
"""

import builtins as _builtins
from typing import Any, List, Optional, Protocol, Tuple, TypeAlias, overload


# ---- Vector and matrix types ----
Vec2: TypeAlias = Tuple[float, float]
Vec3: TypeAlias = Tuple[float, float, float]
Vec4: TypeAlias = Tuple[float, float, float, float]
Mat2: TypeAlias = Tuple[Vec2, Vec2]
Mat3: TypeAlias = Tuple[Vec3, Vec3, Vec3]
Mat4: TypeAlias = Tuple[Vec4, Vec4, Vec4, Vec4]
Vector: TypeAlias = Vec2 | Vec3 | Vec4
Matrix: TypeAlias = Mat2 | Mat3 | Mat4


class Color(Protocol):
    r: float
    g: float
    b: float
    a: float


# ---- Built-in variables common to object, paint, and agent scripts ----
num_objects: int
dt: float
time: float
idx: int
group_count: int
i: int
p: "_ObjectArray"


# ---- Object script variables ----
x: float
y: float
z: float
vx: float
vy: float
vz: float
mass: float
charge: float
theta: float
omega: float
ax: float
ay: float
az: float
angular: float
angular_x: float
angular_y: float
angular_z: float
wx: float
wy: float
wz: float
pos: Vec2
vel: Vec2
color: Color


# ---- Paint script variables ----
px: float
py: float
prev_r: float
prev_g: float
prev_b: float
prev_a: float
t: float


# ---- Agent script variables ----
signal_object_idx: int
signal_payload: float


# ---- Shader-wrapper uniforms ----
# Object wrapper uniforms.
uNumObjects: int
uGroupCount: int
uDt: float
uTime: float
uDerivativeEpsilon: float
k: float
b: float
g: float
uGravityDir: Vec2
uRestitution: float
uCoupling: float
uExternalForce: Vec2
uDriveFreq: float
uDriveAmp: float

# Paint wrapper uniforms.
uCameraX: float
uCameraY: float
uHalfWidth: float
uHalfHeight: float
uScreenWidth: int
uScreenHeight: int
uTexWidth: int
uTexHeight: int
uTexSize: Vec2
uPanDelta: Vec2
uZoomRatio: float
uScale: float

# Agent wrapper uniforms.
uAgentID: int
uSignalCount: int

# Shared storage-buffer offset/capacity uniforms.
uScratchpadOffsets: Tuple[int, ...]
uSignalQueueCapacity: int


# ---- Object array p ----
class _Object:
    # p[index].position/velocity aliases supported by GLSLGenerator.
    x: float
    y: float
    z: float
    vx: float
    vy: float
    vz: float
    mass: float
    charge: float
    theta: float
    omega: float
    wx: float
    wy: float
    wz: float
    qx: float
    qy: float
    qz: float
    qw: float
    spin_x: float
    spin_y: float
    spin_z: float
    mesh_id: int
    r: float
    g: float
    b: float
    a: float


class _ObjectArray(_Object):
    # The current object is also addressable as p.x / p.y / p.vx / etc.
    def __getitem__(self, index: int) -> _Object: ...


# ---- JIT-exposed math functions ----
@overload
def sqrt(x: float) -> float: ...
@overload
def sqrt(x: Vector) -> Vector: ...
def inversesqrt(x: float) -> float: ...

@overload
def sin(x: float) -> float: ...
@overload
def sin(x: Vector) -> Vector: ...
@overload
def cos(x: float) -> float: ...
@overload
def cos(x: Vector) -> Vector: ...
@overload
def tan(x: float) -> float: ...
@overload
def tan(x: Vector) -> Vector: ...
@overload
def asin(x: float) -> float: ...
@overload
def asin(x: Vector) -> Vector: ...
@overload
def acos(x: float) -> float: ...
@overload
def acos(x: Vector) -> Vector: ...
@overload
def atan(y_over_x: float) -> float: ...
@overload
def atan(y: float, x: float) -> float: ...
@overload
def atan(x: Vector) -> Vector: ...

def atan2(y: float, x: float) -> float: ...

def sinh(x: float) -> float: ...
def cosh(x: float) -> float: ...
def tanh(x: float) -> float: ...
def asinh(x: float) -> float: ...
def acosh(x: float) -> float: ...
def atanh(x: float) -> float: ...

@overload
def exp(x: float) -> float: ...
@overload
def exp(x: Vector) -> Vector: ...
@overload
def exp2(x: float) -> float: ...
@overload
def exp2(x: Vector) -> Vector: ...
@overload
def log(x: float) -> float: ...
@overload
def log(x: Vector) -> Vector: ...
@overload
def log2(x: float) -> float: ...
@overload
def log2(x: Vector) -> Vector: ...
@overload
def pow(x: float, y: float) -> float: ...
@overload
def pow(x: Vector, y: float | Vector) -> Vector: ...

@overload
def abs(x: float) -> float: ...
@overload
def abs(x: Vector) -> Vector: ...
@overload
def floor(x: float) -> float: ...
@overload
def floor(x: Vector) -> Vector: ...
@overload
def ceil(x: float) -> float: ...
@overload
def ceil(x: Vector) -> Vector: ...
@overload
def fract(x: float) -> float: ...
@overload
def fract(x: Vector) -> Vector: ...
# GLSLGenerator aliases; import these from hyperstellar.glsl when needed.
@overload
def frac(x: float) -> float: ...
@overload
def frac(x: Vector) -> Vector: ...
@overload
def sign(x: float) -> float: ...
@overload
def sign(x: Vector) -> Vector: ...

@overload
def step(edge: float, x: float) -> float: ...
@overload
def step(edge: float, x: Vector) -> Vector: ...
@overload
def smoothstep(edge0: float, edge1: float, x: float) -> float: ...
@overload
def smoothstep(edge0: float, edge1: float, x: Vector) -> Vector: ...
@overload
def mix(x: float, y: float, a: float) -> float: ...
@overload
def mix(x: Vector, y: Vector, a: float | Vector) -> Vector: ...
@overload
def clamp(x: float, min_val: float, max_val: float) -> float: ...
@overload
def clamp(x: Vector, min_val: float | Vector, max_val: float | Vector) -> Vector: ...
@overload
def mod(x: float, y: float) -> float: ...
@overload
def mod(x: Vector, y: float | Vector) -> Vector: ...
@overload
def min(x: float, y: float) -> float: ...
@overload
def min(x: Vector, y: float | Vector) -> Vector: ...
@overload
def min(x: Vector) -> float: ...
@overload
def max(x: float, y: float) -> float: ...
@overload
def max(x: Vector, y: float | Vector) -> Vector: ...
@overload
def max(x: Vector) -> float: ...


def dot(a: Vector, b: Vector) -> float: ...
def cross(a: Vec3, b: Vec3) -> Vec3: ...
def length(v: Vector) -> float: ...
@overload
def distance(a: Vector, b: Vector) -> float: ...
@overload
def distance(*, target: int = 0, length: float = 1.0) -> None: ...
def normalize(v: Vector) -> Vector: ...
def reflect(inc: Vector, normal: Vector) -> Vector: ...
def refract(inc: Vector, normal: Vector, eta: float) -> Vector: ...
def faceforward(n: Vector, i: Vector, nref: Vector) -> Vector: ...
def transpose(m: Matrix) -> Matrix: ...
def inverse(m: Matrix) -> Matrix: ...
def determinant(m: Matrix) -> float: ...
def outerProduct(c: Vector, r: Vector) -> Matrix: ...
def matrixCompMult(a: Matrix, b: Matrix) -> Matrix: ...

def radians(deg: float) -> float: ...
def degrees(rad: float) -> float: ...
@overload
def saturate(x: float) -> float: ...
@overload
def saturate(x: Vector) -> Vector: ...
@overload
def lerp(a: float, b: float, t: float) -> float: ...
@overload
def lerp(a: Vector, b: Vector, t: float | Vector) -> Vector: ...
def fbm(p: Vec2, octaves: int) -> float: ...
def noise(p: Vec2) -> float: ...
@overload
def rand() -> float: ...
@overload
def rand(seed: Vec2) -> float: ...
def random(seed: Vec2) -> float: ...


# ---- GLSL vector and matrix constructors ----
def vec2(x: float | Vec2 = 0.0, y: Optional[float] = None) -> Vec2: ...
def vec3(x: float | Vec2 | Vec3 = 0.0, y: Optional[float] = None, z: Optional[float] = None) -> Vec3: ...
def vec4(x: float | Vec2 | Vec3 | Vec4 = 0.0, y: Optional[float] = None, z: Optional[float] = None, w: Optional[float] = None) -> Vec4: ...
def mat2(*values: Any) -> Mat2: ...
def mat3(*values: Any) -> Mat3: ...
def mat4(*values: Any) -> Mat4: ...


# ---- Complex-number functions ----
def cAdd(a: Vec2, b: Vec2) -> Vec2: ...
def cSub(a: Vec2, b: Vec2) -> Vec2: ...
def cMul(a: Vec2, b: Vec2) -> Vec2: ...
def cDiv(a: Vec2, b: Vec2) -> Vec2: ...
def cLog(z: Vec2) -> Vec2: ...
def cExp(z: Vec2) -> Vec2: ...
def cPow(base: Vec2, exponent: Vec2) -> Vec2: ...
def cSin(z: Vec2) -> Vec2: ...
def cCos(z: Vec2) -> Vec2: ...
def cTan(z: Vec2) -> Vec2: ...
def real(z: Vec2) -> float: ...
def imag(z: Vec2) -> float: ...
def conj(z: Vec2) -> Vec2: ...
def conjugate(z: Vec2) -> Vec2: ...
def arg(z: Vec2) -> float: ...


# ---- JIT-specific API ----
def signal(agent_id: int, payload: float) -> None: ...
def scratchpad_read(id: int, index: int) -> float: ...
def scratchpad_write(id: int, index: int, value: float) -> None: ...
def apply_constraints() -> None: ...
def detect_collision(other: int) -> bool: ...
def resolve_collision(other: int) -> None: ...


# ---- Constraint helpers ----
def spring(
    *,
    target: int = 0,
    stiffness: float = 1.0,
    damping: float = 0.0,
    rest_length: float = 1.0,
) -> None: ...
def boundary(
    *,
    min_x: float = -1e10,
    max_x: float = 1e10,
    min_y: float = -1e10,
    max_y: float = 1e10,
) -> None: ...
def angle(*, target1: int = 0, target2: int = 0, angle: float = 0.0) -> None: ...
def get_constraint_count() -> int: ...
def get_constraint_type(index: int) -> int: ...
def get_constraint_target(index: int) -> int: ...
def get_constraint_param(index: int, parameter: int) -> float: ...


# ---- Ray-tracing structs ----
class Ray(Protocol):
    origin: Vec3
    direction: Vec3


class Hit(Protocol):
    hit: bool
    t: float
    point: Vec3
    normal: Vec3
    uv: Vec2
    material: int


class GeoObject(Protocol):
    type: int
    data: Vec4


class CameraBasis(Protocol):
    right: Vec3
    up: Vec3
    forward: Vec3


def sphere(center: Vec3, radius: float) -> GeoObject: ...
def plane(normal: Vec3, d: float) -> GeoObject: ...
def camera_ray(
    eye: Vec3, target: Vec3, up: Vec3, fov: float
) -> Ray: ...
def camera_ray_ndc(
    ndc_x: float,
    ndc_y: float,
    eye: Vec3,
    target: Vec3,
    up: Vec3,
    fov: float,
    aspect: float,
) -> Ray: ...
def look_at(eye: Vec3, target: Vec3, up: Vec3) -> CameraBasis: ...
def intersect(ray: Ray, obj: GeoObject) -> Hit: ...
def miss_hit() -> Hit: ...
def closest_hit(a: Hit, b: Hit) -> Hit: ...
def offset_ray_origin(position: Vec3, normal: Vec3) -> Vec3: ...
def raymarch(
    ray: Ray,
    sdf: Any,
    t_min: float,
    t_max: float,
    steps: int,
    eps: float = 1e-4,
) -> Hit: ...


# ---- Paint helpers ----
def sample_prev_r(radius: float) -> float: ...
def sample_prev_g(radius: float) -> float: ...
def sample_prev_b(radius: float) -> float: ...
def sample_prev_a(radius: float) -> float: ...
def avg_prev_r() -> float: ...
def avg_prev_g() -> float: ...
def avg_prev_b() -> float: ...
def avg_prev_a() -> float: ...


# ---- Explicit builtin aliases handled by the AST translator ----
def diff(expression: Any, wrt: Any, order: int = 1) -> float: ...
def len(value: Any) -> int: ...
def sum(value: Vector) -> float: ...
def range(*args: int) -> _builtins.range: ...
def int(value: Any = 0) -> _builtins.int: ...
def float(value: Any = 0.0) -> _builtins.float: ...


__all__ = [
    # Types
    "Vec2", "Vec3", "Vec4", "Mat2", "Mat3", "Mat4", "Vector", "Matrix", "Color",
    "Ray", "Hit", "GeoObject", "CameraBasis",
    # Script-facing variables
    "x", "y", "z", "vx", "vy", "vz", "mass", "charge", "theta", "omega",
    "ax", "ay", "az", "angular", "angular_x", "angular_y", "angular_z",
    "wx", "wy", "wz", "pos", "vel", "color",
    "px", "py", "prev_r", "prev_g", "prev_b", "prev_a", "t",
    "signal_object_idx", "signal_payload",
    "num_objects", "dt", "time", "idx", "group_count", "i", "p",
    # Shader uniforms
    "uNumObjects", "uGroupCount", "uDt", "uTime", "uDerivativeEpsilon",
    "k", "b", "g", "uGravityDir", "uRestitution", "uCoupling",
    "uExternalForce", "uDriveFreq", "uDriveAmp",
    "uCameraX", "uCameraY", "uHalfWidth", "uHalfHeight",
    "uScreenWidth", "uScreenHeight", "uTexWidth", "uTexHeight",
    "uTexSize", "uPanDelta", "uZoomRatio", "uScale",
    "uAgentID", "uSignalCount", "uScratchpadOffsets", "uSignalQueueCapacity",
    # Math functions
    "sqrt", "inversesqrt", "sin", "cos", "tan", "asin", "acos", "atan", "atan2",
    "sinh", "cosh", "tanh", "asinh", "acosh", "atanh",
    "exp", "exp2", "log", "log2", "pow", "abs", "floor", "ceil",
    "fract", "frac", "sign", "step", "smoothstep", "mix", "clamp", "mod",
    "min", "max", "dot", "cross", "length", "distance", "normalize",
    "reflect", "refract", "faceforward", "transpose", "inverse", "determinant",
    "outerProduct", "matrixCompMult", "radians", "degrees", "saturate", "lerp",
    "fbm", "noise", "rand", "random",
    # Constructors and complex functions
    "vec2", "vec3", "vec4", "mat2", "mat3", "mat4",
    "cAdd", "cSub", "cMul", "cDiv", "cLog", "cExp", "cPow",
    "cSin", "cCos", "cTan", "real", "imag", "conj", "conjugate", "arg",
    # JIT and constraint functions
    "signal", "scratchpad_read", "scratchpad_write", "apply_constraints",
    "detect_collision", "resolve_collision", "spring", "boundary", "angle",
    "get_constraint_count", "get_constraint_type", "get_constraint_target",
    "get_constraint_param", "diff", "len", "sum", "range", "int", "float",
    # Ray-tracing and paint functions
    "sphere", "plane", "camera_ray", "camera_ray_ndc", "look_at",
    "intersect", "miss_hit", "closest_hit", "offset_ray_origin", "raymarch",
    "sample_prev_r", "sample_prev_g", "sample_prev_b", "sample_prev_a",
    "avg_prev_r", "avg_prev_g", "avg_prev_b", "avg_prev_a",
]
