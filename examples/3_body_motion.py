#!/usr/bin/env python3
import hyperstellar as se
import time

sim = se.Simulation(headless=False, enable_grid=False)

while not sim.are_all_shaders_ready():
    sim.update_shader_loading()
    time.sleep(0.01)

sim.set_paint_resolution(80, 60)

G, M = 1.0, 1.0

bodies = [
    (-0.97000436, 0.24308753,  0.46620368,  0.43236573,  1.0, 0.3, 0.3),
    ( 0.97000436,-0.24308753,  0.46620368,  0.43236573,  0.3, 1.0, 0.3),
    ( 0.0,        0.0,        -0.93240737, -0.86473146,  0.3, 0.6, 1.0),
]

ids = []
for x, y, vx, vy, r, g, b in bodies:
    obj = sim.add_object(
        x=x, y=y, vx=vx, vy=vy, mass=M,
        skin=se.SkinType.CIRCLE, size=0.1,
        r=r, g=g, b=b, a=1.0
    )
    ids.append(obj)

# ============================================================================
# JIT physics scripts for each body (hardcoded indices)
# ============================================================================

# Body 0: influenced by 1 and 2
@sim.script()
def grav_script_0():
    dx_a = p[1].x - x
    dy_a = p[1].y - y
    dx_b = p[2].x - x
    dy_b = p[2].y - y

    r2_a = dx_a*dx_a + dy_a*dy_a
    r2_b = dx_b*dx_b + dy_b*dy_b

    denom_a = r2_a ** 1.5
    denom_b = r2_b ** 1.5

    ax = G * M * dx_a / denom_a + G * M * dx_b / denom_b
    ay = G * M * dy_a / denom_a + G * M * dy_b / denom_b
    angular = 0.0

# Body 1: influenced by 0 and 2
@sim.script()
def grav_script_1():
    dx_a = p[0].x - x
    dy_a = p[0].y - y
    dx_b = p[2].x - x
    dy_b = p[2].y - y

    r2_a = dx_a*dx_a + dy_a*dy_a
    r2_b = dx_b*dx_b + dy_b*dy_b

    denom_a = r2_a ** 1.5
    denom_b = r2_b ** 1.5

    ax = G * M * dx_a / denom_a + G * M * dx_b / denom_b
    ay = G * M * dy_a / denom_a + G * M * dy_b / denom_b
    angular = 0.0

# Body 2: influenced by 0 and 1
@sim.script()
def grav_script_2():
    dx_a = p[0].x - x
    dy_a = p[0].y - y
    dx_b = p[1].x - x
    dy_b = p[1].y - y

    r2_a = dx_a*dx_a + dy_a*dy_a
    r2_b = dx_b*dx_b + dy_b*dy_b

    denom_a = r2_a ** 1.5
    denom_b = r2_b ** 1.5

    ax = G * M * dx_a / denom_a + G * M * dx_b / denom_b
    ay = G * M * dy_a / denom_a + G * M * dy_b / denom_b
    angular = 0.0

sim.set_script(ids[0], grav_script_0._script_id)
sim.set_script(ids[1], grav_script_1._script_id)
sim.set_script(ids[2], grav_script_2._script_id)

print("Assigned JIT physics scripts to all three bodies.")

# ============================================================================
# JIT paint script for gravity field visualisation
# ============================================================================
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

sim.set_paint_script(gravity_field._script_id)
sim.set_speed(50.0)

print("Running three-body simulation with JIT scripts.")
print("Controls: WASD to pan, scroll to zoom. Close to exit.")

# ============================================================================
# Main loop
# ============================================================================
while not sim.should_close():
    sim.process_input()
    sim.update(0.001)
    sim.render()

sim.cleanup()