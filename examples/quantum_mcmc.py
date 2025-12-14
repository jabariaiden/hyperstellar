#!/usr/bin/env python3
import hyperstellar as se
import numpy as np
import time

sim = se.Simulation(headless=False, width=800, height=600)

while not sim.are_all_shaders_ready():
    sim.update_shader_loading()
    time.sleep(0.01)

while sim.object_count() > 0:
    sim.remove_object(0)

# ONE equation shared by all walkers
# GPU computes time-evolving quantum wave function at particle's position
shared_equation = "0,0,0,0.3,0.6,exp(-1*(x*x+y*y)*(1 + 0.5*sin(0.3*t))),1"

# Create multiple independent MCMC walkers
num_walkers = 300  # MORE walkers
walkers = []

for i in range(num_walkers):
    # Random initial position (continuous)
    angle = 2 * np.pi * np.random.random()
    r = np.random.exponential(1.0)
    x0 = r * np.cos(angle)
    y0 = r * np.sin(angle)
    
    pid = sim.add_object(
        x=x0, y=y0, vx=0, vy=0,
        mass=1, charge=0,
        skin=se.SkinType.CIRCLE,
        size=0.15,
        r=0.2, g=0.5, b=0.9, a=0.8
    )
    sim.set_equation(pid, shared_equation)
    
    walkers.append({
        'pid': pid,
        'x': x0,
        'y': y0,
        'prob': 1.0
    })

print(f"Created {num_walkers} independent MCMC walkers using 1 equation")

# Initial GPU probability computation
sim.update(0.01)
for w in walkers:
    obj = sim.get_object(w['pid'])
    w['prob'] = max(0.001, obj.b)

# MCMC parameters
step_size = 0.5
steps_per_frame = 1  # Only 1 step per walker per frame = 300 Python calls instead of 900

frame = 0
while not sim.should_close():
    sim.process_input()
    
    # Single GPU update for all walkers
    sim.update(0.001)
    
    # Each walker does ONE MCMC step
    for w in walkers:
        # Propose new position
        prop_x = w['x'] + np.random.normal(0, step_size)
        prop_y = w['y'] + np.random.normal(0, step_size)
        
        # TELEPORT to proposed position
        sim.update_object(w['pid'],
            x=prop_x, y=prop_y,
            vx=0, vy=0, mass=1, charge=0,
            rotation=0, angular_velocity=0,
            width=0.15, height=0.15,
            r=0.2, g=0.5, b=0.9, a=0.8
        )
    
    # ONE GPU update for all particles
    sim.update(0.001)
    
    # Read probabilities and accept/reject
    for w in walkers:
        obj = sim.get_object(w['pid'])
        prop_prob = max(0.001, obj.b)
        
        # Metropolis-Hastings
        if np.random.random() < min(1.0, prop_prob / w['prob']):
            # ACCEPT: update state (already at new position)
            w['x'] = obj.x
            w['y'] = obj.y
            w['prob'] = prop_prob
        else:
            # REJECT: teleport back
            sim.update_object(w['pid'],
                x=w['x'], y=w['y'],
                vx=0, vy=0, mass=1, charge=0,
                rotation=0, angular_velocity=0,
                width=0.15, height=0.15,
                r=0.2, g=0.5, b=w['prob'], a=0.8
            )
    
    frame += 1
    if frame % 60 == 0:
        print(f"Frame {frame}: {num_walkers} walkers teleporting freely")
    
    sim.render()

sim.cleanup()