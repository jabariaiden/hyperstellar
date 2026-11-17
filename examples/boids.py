#!/usr/bin/env python3
"""
3D Boids Chase
==============
A swarm of small dart-shaped boids chases a glowing target through 3D space.

Every boid runs the same GPU (JIT) script each step:
  * separation / alignment / cohesion against nearby boids
  * pursuit of a point on a slowly rotating shell around the target
    (so the swarm wraps around it instead of piling onto one point),
    with lead prediction based on the target's velocity
  * a soft speed band and a soft flight box
  * nose alignment: the boid's +X axis is steered toward its heading by
    accelerating the spin vector (angular_x / angular_y / angular_z)

The target follows an analytic Lissajous-style path, also in a JIT script.

Controls: WASD pan, Q/E zoom, left-mouse drag to look around, ESC to quit.
"""
import math
import random

import hyperstellar as se
from hyperstellar import *

# ---------------------------------------------------------------------
# Tunables. Plain numbers defined at module level are inlined into the
# generated shader, so they can be used freely inside @sim.script().
# ---------------------------------------------------------------------
NUM_BOIDS = 400
TARGET = NUM_BOIDS          # target is created right after the boids -> index NUM_BOIDS

MAX_SPEED = 13.0
MIN_SPEED = 6.0

NEIGHBOR_R = 4.0
NEIGHBOR_R2 = NEIGHBOR_R * NEIGHBOR_R
SEP_R = 1.1
SEP_R2 = SEP_R * SEP_R

W_COHESION = 0.55
W_ALIGN = 1.3
W_SEPARATION = 5.0
W_SEEK = 2.2

SHELL_RADIUS = 3.2          # radius of the shell the swarm tries to wrap around the target
BOUND_X = 34.0
BOUND_Y = 22.0
BOUND_Z = 28.0

TURN_RATE = 10.0            # how fast the nose is steered toward the heading (rad/s per rad of error)
SPIN_DAMP = 25.0            # how quickly the spin vector follows the desired one

BOID_SCALE = 1.3

# ---------------------------------------------------------------------
# Simulation setup
# ---------------------------------------------------------------------
sim = se.Simulation(
    headless=False,
    enable_grid=False,
    width=1400,
    height=900,
    title="3D Boids Chase",
)

while not sim.are_all_shaders_ready():
    sim.update_shader_loading()

sim.set_camera_projection(False)      # perspective
sim.set_camera_zoom(75.0)             # in perspective mode this is the eye distance
sim.set_directional_light_enabled(True)
sim.set_light_direction((0.35, -0.8, -0.45))


# ---------------------------------------------------------------------
# JIT scripts
# ---------------------------------------------------------------------
@sim.script()
def target_path():
    # Position and velocity come straight from the analytic path, so the
    # boids can read a clean velocity from p[TARGET] for lead prediction.
    x = 16.0 * sin(0.41 * time) + 4.0 * sin(1.30 * time)
    y = 8.0 * sin(0.57 * time + 1.0) + 2.0 * cos(1.10 * time)
    z = 14.0 * sin(0.31 * time + 2.1)

    vx = 16.0 * 0.41 * cos(0.41 * time) + 4.0 * 1.30 * cos(1.30 * time)
    vy = 8.0 * 0.57 * cos(0.57 * time + 1.0) - 2.0 * 1.10 * sin(1.10 * time)
    vz = 14.0 * 0.31 * cos(0.31 * time + 2.1)

    ax = 0.0
    ay = 0.0
    az = 0.0
    angular_x = 0.0
    angular_y = 0.0
    angular_z = 0.0


@sim.script()
def boid_flock():
    # ---- flocking: one pass over every boid ----
    cx = 0.0
    cy = 0.0
    cz = 0.0
    avx = 0.0
    avy = 0.0
    avz = 0.0
    sx = 0.0
    sy = 0.0
    sz = 0.0
    neighbors = 0.0

    for j in range(NUM_BOIDS):
        if j != idx:
            dx = p[j].x - x
            dy = p[j].y - y
            dz = p[j].z - z
            d2 = dx * dx + dy * dy + dz * dz

            if d2 < NEIGHBOR_R2:
                cx += p[j].x
                cy += p[j].y
                cz += p[j].z
                avx += p[j].vx
                avy += p[j].vy
                avz += p[j].vz
                neighbors += 1.0

                if d2 < SEP_R2:
                    inv = 1.0 / (d2 + 0.05)
                    sx -= dx * inv
                    sy -= dy * inv
                    sz -= dz * inv

    if neighbors > 0.0:
        inv_n = 1.0 / neighbors
        ax += (cx * inv_n - x) * W_COHESION + (avx * inv_n - vx) * W_ALIGN
        ay += (cy * inv_n - y) * W_COHESION + (avy * inv_n - vy) * W_ALIGN
        az += (cz * inv_n - z) * W_COHESION + (avz * inv_n - vz) * W_ALIGN

    ax += sx * W_SEPARATION
    ay += sy * W_SEPARATION
    az += sz * W_SEPARATION

    # ---- per-boid personality: slightly different top speeds ----
    pace = 0.85 + 0.3 * fract(sin(float(idx) * 12.9898) * 43758.5453)
    top_speed = MAX_SPEED * pace

    # ---- pursue a point on a rotating shell around the target ----
    # Fibonacci-sphere slot for this boid, spun slowly around the Y axis.
    shell_y = 1.0 - 2.0 * (float(idx) + 0.5) / NUM_BOIDS
    shell_ring = sqrt(max(0.0, 1.0 - shell_y * shell_y))
    shell_ang = float(idx) * 2.3999632 + time * 0.6

    gx = p[TARGET].x + cos(shell_ang) * shell_ring * SHELL_RADIUS
    gy = p[TARGET].y + shell_y * SHELL_RADIUS
    gz = p[TARGET].z + sin(shell_ang) * shell_ring * SHELL_RADIUS

    # Lead: aim where the target will be after roughly the travel time.
    to_tx = p[TARGET].x - x
    to_ty = p[TARGET].y - y
    to_tz = p[TARGET].z - z
    target_dist = sqrt(to_tx * to_tx + to_ty * to_ty + to_tz * to_tz)
    lead = clamp(target_dist / top_speed, 0.0, 1.2)

    gx += p[TARGET].vx * lead
    gy += p[TARGET].vy * lead
    gz += p[TARGET].vz * lead

    gdx = gx - x
    gdy = gy - y
    gdz = gz - z
    gdist = sqrt(gdx * gdx + gdy * gdy + gdz * gdz) + 0.0001

    # Ease off when close to the goal point so they circle rather than overshoot wildly.
    want = top_speed * clamp(gdist / 6.0, 0.45, 1.0)

    ax += (gdx / gdist * want - vx) * W_SEEK
    ay += (gdy / gdist * want - vy) * W_SEEK
    az += (gdz / gdist * want - vz) * W_SEEK

    # ---- soft flight box ----
    if x < -BOUND_X:
        ax += (-BOUND_X - x) * 4.0
    elif x > BOUND_X:
        ax -= (x - BOUND_X) * 4.0

    if y < -BOUND_Y:
        ay += (-BOUND_Y - y) * 4.0
    elif y > BOUND_Y:
        ay -= (y - BOUND_Y) * 4.0

    if z < -BOUND_Z:
        az += (-BOUND_Z - z) * 4.0
    elif z > BOUND_Z:
        az -= (z - BOUND_Z) * 4.0

    # ---- speed band (acts as a force, so it stays smooth) ----
    speed = sqrt(vx * vx + vy * vy + vz * vz) + 0.0001

    if speed > top_speed:
        over = (speed - top_speed) / speed * 8.0
        ax -= vx * over
        ay -= vy * over
        az -= vz * over

    if speed < MIN_SPEED:
        under = (MIN_SPEED - speed) / speed * 4.0
        ax += vx * under
        ay += vy * under
        az += vz * under

    # ---- nose alignment ----
    # Current nose direction = orientation quaternion applied to +X.
    qx = p[idx].qx
    qy = p[idx].qy
    qz = p[idx].qz
    qw = p[idx].qw

    fwd_x = 1.0 - 2.0 * (qy * qy + qz * qz)
    fwd_y = 2.0 * (qx * qy + qz * qw)
    fwd_z = 2.0 * (qx * qz - qy * qw)

    # Heading = velocity nudged by the acceleration, so boids lean into turns.
    hx = vx + ax * 0.1
    hy = vy + ay * 0.1
    hz = vz + az * 0.1
    hlen = sqrt(hx * hx + hy * hy + hz * hz) + 0.0001
    hx = hx / hlen
    hy = hy / hlen
    hz = hz / hlen

    # Spin axis that rotates the nose toward the heading is fwd x heading.
    want_wx = (fwd_y * hz - fwd_z * hy) * TURN_RATE
    want_wy = (fwd_z * hx - fwd_x * hz) * TURN_RATE
    want_wz = (fwd_x * hy - fwd_y * hx) * TURN_RATE

    angular_x = (want_wx - wx) * SPIN_DAMP
    angular_y = (want_wy - wy) * SPIN_DAMP
    angular_z = (want_wz - omega) * SPIN_DAMP


# ---------------------------------------------------------------------
# Meshes and materials
# ---------------------------------------------------------------------
def build_boid_mesh(scale):
    """
    Small convex dart: nose along +X, two swept wing tips, a dorsal and a
    ventral ridge. Triangles are wound outward automatically (the shape is
    convex), because the renderer culls back faces.
    """
    verts = [
        (0.50, 0.00, 0.00),    # 0 nose
        (-0.25, 0.14, 0.00),   # 1 dorsal ridge
        (-0.30, -0.07, 0.00),  # 2 ventral ridge
        (-0.38, 0.02, 0.34),   # 3 left wing tip
        (-0.38, 0.02, -0.34),  # 4 right wing tip
    ]
    tris = [
        (0, 1, 3), (0, 4, 1),      # upper front
        (0, 3, 2), (0, 2, 4),      # lower front
        (1, 4, 3), (2, 3, 4),      # rear
    ]

    cx = sum(v[0] for v in verts) / len(verts)
    cy = sum(v[1] for v in verts) / len(verts)
    cz = sum(v[2] for v in verts) / len(verts)

    indices = []
    for a, b, c in tris:
        va, vb, vc = verts[a], verts[b], verts[c]
        e1 = (vb[0] - va[0], vb[1] - va[1], vb[2] - va[2])
        e2 = (vc[0] - va[0], vc[1] - va[1], vc[2] - va[2])
        n = (
            e1[1] * e2[2] - e1[2] * e2[1],
            e1[2] * e2[0] - e1[0] * e2[2],
            e1[0] * e2[1] - e1[1] * e2[0],
        )
        mid = (
            (va[0] + vb[0] + vc[0]) / 3.0 - cx,
            (va[1] + vb[1] + vc[1]) / 3.0 - cy,
            (va[2] + vb[2] + vc[2]) / 3.0 - cz,
        )
        if n[0] * mid[0] + n[1] * mid[1] + n[2] * mid[2] < 0.0:
            b, c = c, b
        indices.extend([a, b, c])

    scaled = [(v[0] * scale, v[1] * scale, v[2] * scale) for v in verts]
    return scaled, indices


# A few cool colours so the flock reads as individuals. Each colour gets its
# own mesh because a material is attached to a mesh, not to an object.
BOID_COLORS = [
    (0.20, 0.70, 1.00, 1.0),
    (0.30, 0.90, 0.85, 1.0),
    (0.55, 0.60, 1.00, 1.0),
    (0.85, 0.95, 1.00, 1.0),
    (0.35, 0.80, 0.55, 1.0),
]

boid_meshes = []
for k, rgba in enumerate(BOID_COLORS):
    material = se.create_material(se.MaterialType.SOLID_COLOR, rgba)
    verts, indices = build_boid_mesh(BOID_SCALE * (0.9 + 0.08 * k))
    boid_meshes.append(se.register_mesh(verts, indices, material_id=material))

target_material = se.create_material(
    se.MaterialType.EMISSIVE, (1.0, 0.35, 0.06), intensity=2.5
)
target_mesh = se.register_sphere(
    radius=0.9, segs=24, rings=14, material_id=target_material
)


# ---------------------------------------------------------------------
# Objects: boids first (slots 0..NUM_BOIDS-1), then the target
# ---------------------------------------------------------------------
def nose_to_direction(dx, dy, dz):
    """Quaternion (x, y, z, w) rotating +X onto the unit vector (dx, dy, dz)."""
    w = 1.0 + dx
    if w < 1e-6:                       # pointing straight back: half-turn about Z
        return (0.0, 0.0, 1.0, 0.0)
    qx, qy, qz = 0.0, -dz, dy          # cross(+X, d)
    n = math.sqrt(qx * qx + qy * qy + qz * qz + w * w)
    return (qx / n, qy / n, qz / n, w / n)


random.seed(7)

for i in range(NUM_BOIDS):
    px = random.uniform(-32.0, -20.0)
    py = random.uniform(-8.0, 8.0)
    pz = random.uniform(-10.0, 10.0)

    vx0 = random.uniform(7.0, 11.0)
    vy0 = random.uniform(-2.0, 2.0)
    vz0 = random.uniform(-2.0, 2.0)
    speed0 = math.sqrt(vx0 * vx0 + vy0 * vy0 + vz0 * vz0)

    boid = sim.add_object(
        x=px, y=py, z=pz,
        vx=vx0, vy=vy0, vz=vz0,
        mass=1.0,
        mesh_id=boid_meshes[i % len(boid_meshes)],
        orientation=nose_to_direction(vx0 / speed0, vy0 / speed0, vz0 / speed0),
    )
    if boid.slot != i:
        raise RuntimeError(f"boid {i} landed in slot {boid.slot}; the scene was not empty")
    sim.set_collision_enabled(boid, False)
    sim.set_script(boid, boid_flock._script_id)

target = sim.add_object(
    x=0.0, y=0.0, z=0.0,
    mass=1.0,
    mesh_id=target_mesh,
)
if target.slot != TARGET:
    raise RuntimeError(f"target landed in slot {target.slot}, expected {TARGET}")
sim.set_collision_enabled(target, False)
sim.set_script(target, target_path._script_id)

print(f"3D boids chase: {NUM_BOIDS} boids pursuing one target")
print("Controls: WASD pan, Q/E zoom, left-mouse drag to look, ESC to quit")

# ---------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------
while not sim.should_close():
    sim.default_input()
    sim.update(0.016)
    sim.render()

sim.cleanup()