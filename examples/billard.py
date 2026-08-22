#!/usr/bin/env python3
"""
Glowing Billiard Trails – Fast Fade
====================================
Four particles move in straight lines, bouncing off a ±3 boundary box.
The paint shader produces glowing trails that fade quickly.
"""

import hyperstellar as se
import time

sim = se.Simulation(
    headless=False,
    width=1400,
    height=900,
    title="Billiard Trails",
    enable_grid=False
)

while not sim.are_all_shaders_ready():
    sim.update_shader_loading()
    time.sleep(0.01)

while sim.object_count() > 0:
    sim.remove_object(0)  # Clear default objects

sim.set_paint_resolution(512, 512)

# ---- Initialise paint background ----
sim.paint("""
    color.r = 0.0;
    color.g = 0.0;
    color.b = 0.0;
    color.a = 1.0;
""")
sim.update(0.001)
sim.render()

# ---- Create particles (different colours) ----
p0 = sim.add_object(x=-1, y=1, vx=5, vy=3, mass=1, skin=se.SkinType.CIRCLE, size=0.12, r=0.1, g=1.0, b=0.1)
p1 = sim.add_object(x=1, y=-1, vx=-4, vy=6, mass=1, skin=se.SkinType.CIRCLE, size=0.12, r=0.1, g=0.1, b=1.0)
p2 = sim.add_object(x=-1, y=-1, vx=6, vy=-5, mass=1, skin=se.SkinType.CIRCLE, size=0.12, r=1.0, g=0.2, b=0.1)
p3 = sim.add_object(x=1, y=1, vx=-3, vy=-4, mass=1, skin=se.SkinType.CIRCLE, size=0.12, r=1.0, g=0.8, b=0.2)

for i in range(4):
    sim.set_collision_enabled(i, False)
    sim.set_equation(i, "ax = 0; ay = 0; angular = 0;")

# ---- Boundary constraint (bounce box) ----
boundary = se.BoundaryConstraint(min_x=-2.0, max_x=2.0, min_y=-2.0, max_y=2.0)
for pid in (p0, p1, p2, p3):
    sim.add_boundary_constraint(pid, boundary)

# ---- Paint JIT script (fast fade) ----
@sim.script(mode='paint')
def glow_trails():
    radius = 0.12
    u_r = sample_prev_r(radius)
    u_g = sample_prev_g(radius)
    u_b = sample_prev_b(radius)
    D = 0.02
    r_new = prev_r + D * (u_r - prev_r)
    g_new = prev_g + D * (u_g - prev_g)
    b_new = prev_b + D * (u_b - prev_b)

    decay = 0.96
    r_new *= decay
    g_new *= decay
    b_new *= decay

    strength = 0.7
    falloff = 6.0

    dx = p[0].x - px; dy = p[0].y - py
    source = strength * exp(-(dx*dx + dy*dy) * falloff)
    r_new = min(1.0, r_new + source)

    dx = p[1].x - px; dy = p[1].y - py
    source = strength * exp(-(dx*dx + dy*dy) * falloff)
    g_new = min(1.0, g_new + source)

    dx = p[2].x - px; dy = p[2].y - py
    source = strength * exp(-(dx*dx + dy*dy) * falloff)
    b_new = min(1.0, b_new + source)

    dx = p[3].x - px; dy = p[3].y - py
    source = strength * 0.8 * exp(-(dx*dx + dy*dy) * falloff)
    r_new = min(1.0, r_new + source * 0.9)
    g_new = min(1.0, g_new + source * 0.6)

    color.r = clamp(r_new, 0.0, 1.0)
    color.g = clamp(g_new, 0.0, 1.0)
    color.b = clamp(b_new, 0.0, 1.0)
    color.a = 1.0

sim.set_paint_script(glow_trails._script_id)

sim.set_speed(1.5)
print("Billiard demo running – particles bounce, no spring forces.")
while not sim.should_close():
    sim.process_input()
    sim.update(0.016)
    sim.render()

sim.cleanup()