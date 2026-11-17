#!/usr/bin/env python3
import random

import hyperstellar as se
from hyperstellar import *

# --------------------------------------------------------------------------
# Simulation setup
# --------------------------------------------------------------------------
sim = se.Simulation(headless=False, enable_grid=False, width=1400, height=1000, title="Ball Pit – Collision Test")
while not sim.are_all_shaders_ready(): sim.update_shader_loading()

# Enable warm starting for better collision stability
sim.set_collision_parameters(True, 20)

# --------------------------------------------------------------------------
# Generate falling balls
# --------------------------------------------------------------------------
NUM_BALLS = 500
COLS = 10
SPACING = 2
X_OFFSET = -15

objects = []
colors = []

for i in range(NUM_BALLS):
    col = i % COLS
    row = i // COLS
    
    jitter_x = random.uniform(-0.1, 0.1)
    jitter_y = random.uniform(-0.1, 0.1)
    
    x = X_OFFSET + (col * SPACING) + jitter_x
    y = 40 + (row * SPACING) + jitter_y
    
    radius = max(0.1, random.gauss(2, 1))
    r, g, b = random.uniform(0, 1), random.uniform(0, 1), random.uniform(0, 1)
    
    objects.append({"x": x, "y": y, "vy": 0, "mass": 0.1, "size": radius})
    colors.append((r, g, b, 1.0))

balls = sim.add_objects(objects, se.SkinType.CIRCLE, colors)

for ball in balls:
    # Perfect elasticity, zero friction for pure bouncing
    sim.set_collision(ball, enabled=True, shape=se.CollisionShape.CIRCLE, restitution=1.0, friction=0.0)
    # Gravity only (no horizontal forces)
    sim.set_equation(ball, "ax = 0; ay = -19.8;")

# --------------------------------------------------------------------------
# Boundaries (floor, left wall, right wall)
# --------------------------------------------------------------------------
floor = sim.add_object(x=0, y=0, mass=1e12, skin=se.SkinType.RECTANGLE, height=4.0, width=70.0, rotation=0.0, r=1.0, g=1.0, b=1.0, a=1.0)
left_wall = sim.add_object(x=-25, y=24, mass=1e12, skin=se.SkinType.RECTANGLE, height=70.0, width=4.0, r=0.3, g=1.0, b=1.0, a=1.0)
right_wall = sim.add_object(x=25, y=24, mass=1e12, skin=se.SkinType.RECTANGLE, height=70.0, width=4.0, r=0.3, g=1.0, b=1.0, a=1.0)

for wall in [floor, left_wall, right_wall]:
    sim.set_collision(wall, enabled=True, shape=se.CollisionShape.AABB, restitution=0.7, friction=0.5)
    sim.set_equation(wall, "ax = 0; ay = 0;")  # Static

sim.set_camera_zoom(20)
sim.set_speed(20.0)

print("Running ball pit demo. Close the window to exit.")
print("Controls: WASD to pan, scroll to zoom.")

while not sim.should_close():
    sim.default_input()
    sim.update(0.0016)
    sim.render()

sim.cleanup() #optional