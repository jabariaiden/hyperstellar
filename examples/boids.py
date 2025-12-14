#!/usr/bin/env python3
import hyperstellar as se
import math
import random

sim = se.Simulation(headless=False, width=1280, height=720, title="Boid Slalom Squad")

while not sim.are_all_shaders_ready():
    sim.update_shader_loading()

while sim.object_count() > 0:
    sim.remove_object(0)

TARGET_ID, OBSTACLE_COUNT, BOID_COUNT = 0, 4, 7
OBSTACLE_START_ID = 1
BOID_START_ID = OBSTACLE_START_ID + OBSTACLE_COUNT

sim.add_object(0, 0, 0, 0, 1.0, 0, 0, 0, se.SkinType.CIRCLE, 0.15, 0, 0, 1.0, 0.2, 0.2, 1.0)

target_eq_x = "10.0 * (6.0 * sin(t * 0.8) - x) - 2.0 * vx"
target_eq_y = "10.0 * (3.5 * sin(t * 1.6) - y) - 2.0 * vy"
sim.set_equation(TARGET_ID, f"{target_eq_x}, {target_eq_y}")

obs_data = [
    (3.5, 0.5), (-3.5, -0.5), (0.0, 2.8), (0.0, -2.8)
]
OBSTACLE_SIZE = 1.0

obstacles = []
for i, (ox, oy) in enumerate(obs_data):
    oid = sim.add_object(
        ox, oy, 0, 0, 
        50.0, 0, 0, 0, 
        se.SkinType.RECTANGLE, 
        1.0, OBSTACLE_SIZE, OBSTACLE_SIZE,
        1.0, 0.1, 0.1, 1.0,
        0
    )
    obstacles.append(oid)
    sim.set_equation(oid, "0, 0")

boids = []
for i in range(BOID_COUNT):
    start_x = -6.0 + (i % 3) * 0.5
    start_y = (i * 0.5) - 1.5
    rand_vy = (random.random() - 0.5) * 1.0

    bid = sim.add_object(
        start_x, start_y, 
        0.5, rand_vy, 1.0, 0.0, 0, 0,
        se.SkinType.POLYGON, 0.25, 0, 0,
        0.2, 0.8, 1.0, 1.0, 3
    )
    boids.append(bid)

for i in range(BOID_COUNT):
    me = boids[i]
    
    fx_seek = f"12.0 * (p[{TARGET_ID}].x - x)"
    fy_seek = f"12.0 * (p[{TARGET_ID}].y - y)"
    
    fx_obs, fy_obs = [], []
    for obs in obstacles:
        d2 = f"((x - p[{obs}].x)*(x - p[{obs}].x) + (y - p[{obs}].y)*(y - p[{obs}].y) + 0.1)"
        fx_obs.append(f"45.0 * (x - p[{obs}].x) / {d2}") 
        fy_obs.append(f"45.0 * (y - p[{obs}].y) / {d2}")
    
    fx_avoid_obs, fy_avoid_obs = " + ".join(fx_obs), " + ".join(fy_obs)

    fx_sep, fy_sep = [], []
    for other in boids:
        if other == me: continue
        d2 = f"((x - p[{other}].x)*(x - p[{other}].x) + (y - p[{other}].y)*(y - p[{other}].y) + 0.05)"
        fx_sep.append(f"3.0 * (x - p[{other}].x) / {d2}")
        fy_sep.append(f"3.0 * (y - p[{other}].y) / {d2}")

    fx_separation, fy_separation = " + ".join(fx_sep), " + ".join(fy_sep)

    fx_drag, fy_drag = "-2.0 * vx", "-2.0 * vy"

    acc_x = f"{fx_seek} + {fx_avoid_obs} + {fx_separation} + {fx_drag}"
    acc_y = f"{fy_seek} + {fy_avoid_obs} + {fy_separation} + {fy_drag}"

    torque = "30.0 * sin(atan2(vy, vx) - theta) - 8.0 * omega"

    color_r, color_g, color_b = ("0.2", "0.9", "1.0") if i % 3 == 0 else (
        ("0.8", "0.4", "1.0") if i % 3 == 1 else ("0.4", "1.0", "0.6")
    )

    sim.set_equation(me, f"{acc_x}, {acc_y}, {torque}, {color_r}, {color_g}, {color_b}, 1.0")

sim.set_parameter("damping", 0.0)

while not sim.should_close():
    sim.process_input()
    sim.update(0.016)
    sim.render()

sim.cleanup()