#!/usr/bin/env python3
import hyperstellar as se
import math

sim = se.Simulation(headless=False, width=1920, height=1080, 
                    title="Two-Body Orbit", enable_grid=False)
#it creates one object by defualt, so we remove it
while sim.object_count() > 0:
    sim.remove_object(0)

while not sim.are_all_shaders_ready():
    sim.update_shader_loading()

# Physics setup
M_star, M_planet = 50.0, 1.0
G, sep = 1.0, 3.0 
total_mass = M_star + M_planet

# Center of mass initial conditions
v_orbit = math.sqrt(G * total_mass / sep)
star = sim.add_object(
    x=-M_planet*sep/total_mass, y=0, 
    vx=0, vy=M_planet*v_orbit/total_mass,
    mass=M_star, skin=se.SkinType.CIRCLE, size=0.8,  # Bigger star
    r=1.0, g=0.9, b=0.3
)
planet = sim.add_object(
    x=M_star*sep/total_mass, y=0,
    vx=0, vy=-M_star*v_orbit/total_mass, 
    mass=M_planet, skin=se.SkinType.CIRCLE, size=0.25,  # Bigger planet
    r=0.3, g=0.6, b=1.0
)

# Newtonian gravity equations
for obj, other, mass in [(star, planet, M_planet), (planet, star, M_star)]:
    sim.set_equation(obj,
        f"{G}*{mass}*(p[{other}].x-x)/((p[{other}].x-x)^2+(p[{other}].y-y)^2)^1.5,"
        f"{G}*{mass}*(p[{other}].y-y)/((p[{other}].x-x)^2+(p[{other}].y-y)^2)^1.5"
    )

# Run
while not sim.should_close():
    sim.process_input()
    sim.update(0.016)
    sim.render()

sim.cleanup()