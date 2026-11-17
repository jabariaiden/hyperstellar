#!/usr/bin/env python3
# This demo simulates the classic three-body problem, specifically the figure-eight orbit.
# Three bodies of equal mass chase each other in a stable loop due to their mutual gravity.
import hyperstellar as se
from hyperstellar import *

# Create the simulation window without the grid
sim = se.Simulation(headless=False, enable_grid=False, title="Three-Body Motion Demo", width=1400, height=900)

# Wait for shaders to load
while not sim.are_all_shaders_ready():
    sim.update_shader_loading()

# Set a low resolution for the paint shader to keep the background simple
sim.set_paint_resolution(80, 60)

# Gravitational constant and mass for all bodies
G, M = 1.0, 1.0
EPSILON = 1e-6

# Create three bodies explicitly with specific initial positions and velocities.
# These exact values produce the figure-eight orbit. Colors are passed here.
body_0 = sim.add_object(
    x=-0.97000436, y=0.24308753, 
    vx=0.46620368, vy=0.43236573, 
    mass=M, skin=se.SkinType.CIRCLE, size=0.1, 
    r=1.0, g=0.3, b=0.3, a=1.0
)
sim.set_collision_enabled(body_0, False)  
body_1 = sim.add_object(
    x=0.97000436, y=-0.24308753, 
    vx=0.46620368, vy=0.43236573, 
    mass=M, skin=se.SkinType.CIRCLE, size=0.1, 
    r=0.3, g=1.0, b=0.3, a=1.0
)
sim.set_collision_enabled(body_1, False)  
body_2 = sim.add_object(
    x=0.0, y=0.0, 
    vx=-0.93240737, vy=-0.86473146, 
    mass=M, skin=se.SkinType.CIRCLE, size=0.1, 
    r=0.3, g=0.6, b=1.0, a=1.0
)
sim.set_collision_enabled(body_2, False)  

# JIT physics scripts for each body
# Body 0 is pulled by bodies 1 and 2
@sim.script()
def grav_script_0():
    dx_a = p[1].x - x
    dy_a = p[1].y - y
    dx_b = p[2].x - x
    dy_b = p[2].y - y

    r2_a = dx_a*dx_a + dy_a*dy_a
    r2_b = dx_b*dx_b + dy_b*dy_b

    denom_a = (r2_a + EPSILON) ** 1.5
    denom_b = (r2_b + EPSILON) ** 1.5

    ax = G * M * dx_a / denom_a + G * M * dx_b / denom_b
    ay = G * M * dy_a / denom_a + G * M * dy_b / denom_b
    angular = 0.0

# Body 1 is pulled by bodies 0 and 2
@sim.script()
def grav_script_1():
    dx_a = p[0].x - x
    dy_a = p[0].y - y
    dx_b = p[2].x - x
    dy_b = p[2].y - y

    r2_a = dx_a*dx_a + dy_a*dy_a
    r2_b = dx_b*dx_b + dy_b*dy_b

    denom_a = (r2_a + EPSILON) ** 1.5
    denom_b = (r2_b + EPSILON) ** 1.5

    ax = G * M * dx_a / denom_a + G * M * dx_b / denom_b
    ay = G * M * dy_a / denom_a + G * M * dy_b / denom_b
    angular = 0.0

# Body 2 is pulled by bodies 0 and 1
@sim.script()
def grav_script_2():
    dx_a = p[0].x - x
    dy_a = p[0].y - y
    dx_b = p[1].x - x
    dy_b = p[1].y - y

    r2_a = dx_a*dx_a + dy_a*dy_a
    r2_b = dx_b*dx_b + dy_b*dy_b

    denom_a = (r2_a + EPSILON) ** 1.5
    denom_b = (r2_b + EPSILON) ** 1.5

    ax = G * M * dx_a / denom_a + G * M * dx_b / denom_b
    ay = G * M * dy_a / denom_a + G * M * dy_b / denom_b
    angular = 0.0

# Attach the physics scripts to the corresponding object handles
sim.set_script(body_0, grav_script_0._script_id)
sim.set_script(body_1, grav_script_1._script_id)
sim.set_script(body_2, grav_script_2._script_id)

print("Assigned JIT physics scripts to all three bodies.")

# JIT paint script to visualize the combined gravitational field
@sim.script(mode='paint', debug=True)
def gravity_field():
    dx0 = p[0].x - px
    dy0 = p[0].y - py
    dx1 = p[1].x - px
    dy1 = p[1].y - py
    dx2 = p[2].x - px
    dy2 = p[2].y - py

    d0 = sqrt(dx0*dx0 + dy0*dy0 + 0.01)
    d1 = sqrt(dx1*dx1 + dy1*dy1 + 0.01)
    d2 = sqrt(dx2*dx2 + dy2*dy2 + 0.01)

    field = 0.4 / d0 + 0.4 / d1 + 0.4 / d2

    color.r = field * 0.3
    color.b = field * 0.15
    color.g = field * 0.6
    color.a = 1.0 

sim.paint(gravity_field._script_id)

# Speed up the simulation and zoom out to see the full orbit
sim.set_speed(50.0)
sim.set_camera_zoom(3.0)

print("Running three-body simulation with JIT scripts.")
print("Controls: WASD to pan, scroll to zoom. Close to exit.")

# Main loop
while not sim.should_close():
    sim.default_input()
    sim.update(0.001)
    sim.render()

sim.cleanup()