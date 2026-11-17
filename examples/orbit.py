#!/usr/bin/env python3
import math

import hyperstellar as se
from hyperstellar import *

sim = se.Simulation(headless=False, width=1920, height=1080, title="Two-Body Orbit", enable_grid=False)
while not sim.are_all_shaders_ready(): sim.update_shader_loading()

# Physics setup
M_star, M_planet = 50.0, 1.0
G, sep = 1.0, 3.0
total_mass = M_star + M_planet

# Center of mass initial conditions
v_orbit = math.sqrt(G * total_mass / sep)
star = sim.add_object(
    x=-M_planet*sep/total_mass, y=0, vx=0, vy=M_planet*v_orbit/total_mass,
    mass=M_star, skin=se.SkinType.CIRCLE, size=0.8, r=1.0, g=0.9, b=0.3
)
planet = sim.add_object(
    x=M_star*sep/total_mass, y=0, vx=0, vy=-M_star*v_orbit/total_mass, 
    mass=M_planet, skin=se.SkinType.CIRCLE, size=0.25, r=0.3, g=0.6, b=1.0
)

# Newtonian gravity equations (JIT)
@sim.script()
def star_jit():
    dx = p[1].x - x
    dy = p[1].y - y
    r2 = dx*dx + dy*dy + 0.001
    f = G * M_planet / (r2 ** 1.5)
    ax = f * dx
    ay = f * dy

@sim.script()
def planet_jit():
    dx = p[0].x - x
    dy = p[0].y - y
    r2 = dx*dx + dy*dy + 0.001
    f = G * M_star / (r2 ** 1.5)
    ax = f * dx
    ay = f * dy

sim.set_script(star, star_jit._script_id)
sim.set_script(planet, planet_jit._script_id)

# Run
while not sim.should_close():
    sim.process_input()
    sim.update(0.016)
    sim.render()

sim.cleanup()