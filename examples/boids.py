#!/usr/bin/env python3
"""3D boids flying between procedurally placed pillars.

Boids use local separation/alignment/cohesion plus pillar avoidance.
When a boid gets close to a pillar it signals one agent. The agent records
that boid's last alert and pillar in a scratchpad; the boid reads that memory
to briefly strengthen its avoidance steering.
"""

import math
import random
import hyperstellar as se

# -----------------------------------------------------------------------------
# Scene
# -----------------------------------------------------------------------------
WIDTH, HEIGHT = 1400, 1000
NUM_BOIDS = 28
NUM_PILLARS = 9
MEMORY_PAD_SIZE = NUM_BOIDS * 2  # last alert time + pillar index per boid

random.seed(12)
sim = se.Simulation(
    headless=False,
    enable_grid=False,
    width=WIDTH,
    height=HEIGHT,
    title="Boids Around Pillars",
)

while not sim.are_all_shaders_ready():
    sim.update_shader_loading()

sim.set_directional_light_enabled(False)
sim.set_camera_zoom(27.0)
sim.set_signal_queue_capacity(512)

memory_pad = sim.create_scratchpad(MEMORY_PAD_SIZE)
sim.upload_scratchpad(memory_pad, [0.0] * MEMORY_PAD_SIZE)

# -----------------------------------------------------------------------------
# Agent: remembers the most recent pillar alert from each boid.
# A signal's object index is the boid's actual object slot.
# -----------------------------------------------------------------------------
@sim.script(mode="agent")
def remember_pillar_alert():
    boid_slot = float(signal_object_idx)
    base = boid_slot * 2.0
    scratchpad_write(memory_pad, base, time)
    scratchpad_write(memory_pad, base + 1, signal_payload)


AGENT_ID = remember_pillar_alert._script_id


# Static objects get an explicit zero-acceleration script.
@sim.script()
def keep_pillar_still():
    ax = 0.0
    ay = 0.0
    az = 0.0
    angular_x = 0.0
    angular_y = 0.0
    angular_z = 0.0


# -----------------------------------------------------------------------------
# Boids: classic separation, alignment and cohesion, plus pillar avoidance.
# Boids are created first, so p[0:NUM_BOIDS] are the flock and the pillars
# occupy p[NUM_BOIDS:NUM_BOIDS + NUM_PILLARS].
# -----------------------------------------------------------------------------
@sim.script()
def flock():
    ax = 0.0
    ay = 0.0
    az = 0.0

    # Read the agent's last alert for this boid.
    memory_base = float(idx) * 2.0
    last_alert = scratchpad_read(memory_pad, memory_base)
    last_pillar = scratchpad_read(memory_pad, memory_base + 1.0)
    alert_age = time - last_alert

    # Separation, alignment and cohesion from nearby flockmates.
    center_x = 0.0
    center_y = 0.0
    center_z = 0.0
    average_vx = 0.0
    average_vy = 0.0
    average_vz = 0.0
    separate_x = 0.0
    separate_y = 0.0
    separate_z = 0.0
    neighbors = 0.0

    for j in range(NUM_BOIDS):
        if j != idx:
            dx = p[j].x - x
            dy = p[j].y - y
            dz = p[j].z - z
            d2 = dx * dx + dy * dy + dz * dz

            if d2 < 36.0 and d2 > 0.0001:
                center_x += p[j].x
                center_y += p[j].y
                center_z += p[j].z
                average_vx += p[j].vx
                average_vy += p[j].vy
                average_vz += p[j].vz
                neighbors += 1.0

                if d2 < 2.25:
                    separate_x -= dx / d2
                    separate_y -= dy / d2
                    separate_z -= dz / d2

    if neighbors > 0.0:
        center_x = center_x / neighbors - x
        center_y = center_y / neighbors - y
        center_z = center_z / neighbors - z
        average_vx = average_vx / neighbors - vx
        average_vy = average_vy / neighbors - vy
        average_vz = average_vz / neighbors - vz

        ax += center_x * 0.18 + average_vx * 0.55 + separate_x * 1.8
        ay += center_y * 0.18 + average_vy * 0.55 + separate_y * 1.8
        az += center_z * 0.18 + average_vz * 0.55 + separate_z * 1.8

    # Steer away from the nearest part of each vertical pillar.
    nearest_distance = 1000.0
    nearest_pillar = -1.0

    for k in range(NUM_PILLARS):
        pillar_x = p[NUM_BOIDS + k].x
        pillar_y = p[NUM_BOIDS + k].y
        pillar_z = p[NUM_BOIDS + k].z
        dx = x - pillar_x
        dz = z - pillar_z
        horizontal_distance = sqrt(dx * dx + dz * dz)
        vertical_distance = abs(y - pillar_y)

        if vertical_distance < 4.8 and horizontal_distance < nearest_distance:
            nearest_distance = horizontal_distance
            nearest_pillar = float(k)

        if vertical_distance < 4.8 and horizontal_distance < 3.8 and horizontal_distance > 0.001:
            strength = (3.8 - horizontal_distance) * 2.0
            if float(k) == last_pillar and last_alert > 0.001 and alert_age < 1.25:
                strength *= 1.8
            ax += (dx / horizontal_distance) * strength
            az += (dz / horizontal_distance) * strength

    # Emit at most one alert per boid per 0.8 seconds. The agent writes the
    # timestamp and pillar index back to this boid's scratchpad memory.
    if nearest_distance < 4.2 and (last_alert < 0.001 or alert_age > 0.8):
        signal(AGENT_ID, float(nearest_pillar))

    # Keep the flock within a simple 3D flight volume.
    if x < -12.0:
        ax += (-12.0 - x) * 1.4
    elif x > 12.0:
        ax -= (x - 12.0) * 1.4

    if y < -4.5:
        ay += (-4.5 - y) * 1.4
    elif y > 4.5:
        ay -= (y - 4.5) * 1.4

    if z < -11.0:
        az += (-11.0 - z) * 1.4
    elif z > 11.0:
        az -= (z - 11.0) * 1.4

    # Mild drag keeps the flock's speed from growing without bound.
    ax -= vx * 0.12
    ay -= vy * 0.12
    az -= vz * 0.12


# -----------------------------------------------------------------------------
# Geometry and materials
# -----------------------------------------------------------------------------
boid_material = se.create_material(
    se.MaterialType.EMISSIVE, (0.15, 0.85, 1.0), intensity=1.8
)
pillar_material = se.create_material(
    se.MaterialType.EMISSIVE, (0.18, 0.32, 0.42), intensity=0.45
)

# The triangular mesh lies in the XY plane and points along local +X.
boid_mesh = se.register_polygon(radius=0.48, sides=3, material_id=boid_material)
pillar_mesh = se.register_cylinder(
    radius=0.65, height=8.0, segs=18, material_id=pillar_material
)

# Spawn a flock moving generally to the right, with small 3D variations.
boids = []
for _ in range(NUM_BOIDS):
    x = random.uniform(-9.0, -5.0)
    y = random.uniform(-2.2, 2.2)
    z = random.uniform(-7.5, 7.5)
    vx = random.uniform(2.0, 3.0)
    vy = random.uniform(-0.5, 0.5)
    vz = random.uniform(-0.8, 0.8)
    heading = math.atan2(vy, vx)
    orientation = (0.0, 0.0, math.sin(heading * 0.5), math.cos(heading * 0.5))

    boid = sim.add_object(
        x=x, y=y, z=z,
        vx=vx, vy=vy, vz=vz,
        mass=1.0,
        mesh_id=boid_mesh,
        orientation=orientation,
        r=0.15, g=0.85, b=1.0, a=1.0,
    )
    sim.set_collision_enabled(boid, False)
    sim.set_script(boid, flock._script_id)
    boids.append(boid)

# Procedurally distribute pillars through the flock's flight volume.
pillar_positions = []
while len(pillar_positions) < NUM_PILLARS:
    candidate = (random.uniform(-8.5, 8.5), random.uniform(-8.5, 8.5))
    if all(
        (candidate[0] - px) ** 2 + (candidate[1] - pz) ** 2 > 20.0
        for px, pz in pillar_positions
    ):
        pillar_positions.append(candidate)

for pillar_x, pillar_z in pillar_positions:
    pillar = sim.add_object(
        x=pillar_x, y=0.0, z=pillar_z,
        mass=1e12,
        mesh_id=pillar_mesh,
        r=0.18, g=0.35, b=0.46, a=1.0,
    )
    sim.set_collision_enabled(pillar, False)
    sim.set_script(pillar, keep_pillar_still._script_id)

print("Boids Around Pillars")
print(f"{NUM_BOIDS} boids | {NUM_PILLARS} procedural pillars")
print("Boids flock together and steer around pillars.")
print("Near-pillar signals are processed by an agent and fed back through scratchpad memory.")
print("Controls: WASD to pan, scroll to zoom. Close the window to exit.")

while not sim.should_close():
    sim.default_input()
    sim.update(0.016)
    sim.dispatch_agent(AGENT_ID, clear_after=True)
    sim.render()

sim.cleanup()
