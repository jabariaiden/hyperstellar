#!/usr/bin/env python3
import hyperstellar as se
import time
import math

sim = se.Simulation(headless=False, width=800, height=600, title="Two-Body System")
while not sim.are_all_shaders_ready():
    sim.update_shader_loading()

while sim.object_count() > 0:
    sim.remove_object(0)

# Parameters
star_mass = 50.0
planet_mass = 1.0
G = 0.1
separation = 0.8
total_mass = star_mass + planet_mass

# Center of mass positions
star_x = -planet_mass * separation / total_mass
planet_x = star_mass * separation / total_mass

# Circular orbit velocities in COM frame
relative_v = math.sqrt(G * total_mass / separation)
star_v = planet_mass * relative_v / total_mass  # Small, in +y direction
planet_v = -star_mass * relative_v / total_mass  # Large, in -y direction

# Star (yellow, small wobble)
star = sim.add_object(star_x, 0, 0, star_v, star_mass, 0, 0, 0,
                     se.SkinType.CIRCLE, 0.25, 0, 0,
                     1.0, 0.9, 0.3, 1.0)

# Planet (blue, large orbit)
planet = sim.add_object(planet_x, 0, 0, planet_v, planet_mass, 0, 0, 0,
                       se.SkinType.CIRCLE, 0.15, 0, 0,
                       0.2, 0.5, 0.9, 1.0)

# Mutual gravity using p[index].value references
sim.set_equation(star,
    f"{G}*{planet_mass}*(p[1].x - x)/(((p[1].x - x)*(p[1].x - x) + (p[1].y - y)*(p[1].y - y))*sqrt((p[1].x - x)*(p[1].x - x) + (p[1].y - y)*(p[1].y - y))), "
    f"{G}*{planet_mass}*(p[1].y - y)/(((p[1].x - x)*(p[1].x - x) + (p[1].y - y)*(p[1].y - y))*sqrt((p[1].x - x)*(p[1].x - x) + (p[1].y - y)*(p[1].y - y)))")

sim.set_equation(planet,
    f"{G}*{star_mass}*(p[0].x - x)/(((p[0].x - x)*(p[0].x - x) + (p[0].y - y)*(p[0].y - y))*sqrt((p[0].x - x)*(p[0].x - x) + (p[0].y - y)*(p[0].y - y))), "
    f"{G}*{star_mass}*(p[0].y - y)/(((p[0].x - x)*(p[0].x - x) + (p[0].y - y)*(p[0].y - y))*sqrt((p[0].x - x)*(p[0].x - x) + (p[0].y - y)*(p[0].y - y)))")

for _ in range(3600):
    sim.process_input()
    sim.update(0.0167)
    sim.render()
    if sim.should_close():
        break

sim.cleanup()