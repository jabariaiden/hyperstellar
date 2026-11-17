#!/usr/bin/env python3
import math

import hyperstellar as se
from hyperstellar import *

sim = se.Simulation(headless=False, width=800, height=600, title="Pendulum", enable_grid=False)
while not sim.are_all_shaders_ready(): sim.update_shader_loading()

# Physical parameters
L = 2.0        # Rod length (meters)
g = 9.81       # Gravity (m/s^2)
damping = 0.1  # Reduced air resistance

# Initial angle
theta_init = math.pi / 3  # Start at 60 degrees

# Pivot (fixed point) - Index 0
pivot = sim.add_object(
    x=0, y=2, vx=0, vy=0, mass=1e12, skin=se.SkinType.CIRCLE, size=0.15,
    r=0.3, g=0.3, b=0.3
)

# Bob (swinging mass) - Index 1
bob = sim.add_object(
    x=L*math.sin(theta_init), y=2 - L*math.cos(theta_init), vx=0, vy=0, mass=10.0,
    skin=se.SkinType.CIRCLE, size=0.25, r=0.2, g=0.6, b=1.0
)

# Rod (visual connector) - Index 2, drawn last so it renders at the back
rod = sim.add_object(
    x=L*math.sin(theta_init)/2, y=2 - L*math.cos(theta_init)/2,
    vx=0, vy=0, mass=0.01, skin=se.SkinType.RECTANGLE, width=L, height=0.08,
    r=0.6, g=0.6, b=0.6
)
sim.set_collision(rod, enabled=False)

@sim.script()
def pivot_jit():
    ax = 0.0
    ay = 0.0
    angular = 0.0

@sim.script()
def bob_jit():
    ax = -damping * vx
    ay = -damping * vy - g
    angular = 0.0

@sim.script()
def rod_jit():
    # Kinematically lock the rod to the midpoint and angle (pivot=0, bob=1)
    x = (p[0].x + p[1].x) / 2.0
    y = (p[0].y + p[1].y) / 2.0
    theta = atan2(p[1].y - p[0].y, p[1].x - p[0].x)
    vx = 0.0
    vy = 0.0
    ax = 0.0
    ay = 0.0
    angular = 0.0
    omega = 0.0

sim.set_script(pivot, pivot_jit._script_id)
sim.set_script(bob, bob_jit._script_id)
sim.set_script(rod, rod_jit._script_id)

sim.add_distance_constraint(bob, pivot, L)

# Speed up the simulation
sim.set_speed(500.0)

while not sim.should_close():
    sim.default_input()
    sim.update(0.0067)
    sim.render()