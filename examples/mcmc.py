#!/usr/bin/env python3
import hyperstellar as se
import numpy as np
import time

sim = se.Simulation(headless=False, width=800, height=600, enable_grid=False)

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
walker_ids = []

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
    walker_ids.append(pid)

print(f"Created {num_walkers} independent MCMC walkers using 1 equation")

# Initial GPU probability computation
sim.update(0.01)

# BATCH GET: Get all initial probabilities at once
batch_states = sim.batch_get(walker_ids)
for i, state in enumerate(batch_states):
    walkers[i]['prob'] = max(0.001, state.b)

# MCMC parameters
step_size = 0.5
steps_per_frame = 1  # Only 1 step per walker per frame

frame = 0
while not sim.should_close():
    sim.process_input()
    
    # Single GPU update for all walkers
    sim.update(0.001)
    
    # Prepare BATCH UPDATE for all proposed positions
    updates = []
    for w in walkers:
        # Propose new position
        prop_x = w['x'] + np.random.normal(0, step_size)
        prop_y = w['y'] + np.random.normal(0, step_size)
        
        # Create update data
        update = se.BatchUpdateData()
        update.index = w['pid']
        update.x = prop_x
        update.y = prop_y
        update.vx = 0
        update.vy = 0
        update.mass = 1
        update.charge = 0
        update.rotation = 0
        update.angular_velocity = 0
        update.width = 0.15
        update.height = 0.15
        update.r = 0.2
        update.g = 0.5
        update.b = 0.9
        update.a = 0.8
        
        updates.append(update)
    
    # BATCH UPDATE: Teleport ALL walkers to proposed positions at once
    sim.batch_update(updates)
    
    # ONE GPU update for all particles at new positions
    sim.update(0.001)
    
    # BATCH GET: Read ALL probabilities at new positions at once
    batch_states = sim.batch_get(walker_ids)
    
    # Prepare reject updates (for walkers that need to go back)
    reject_updates = []
    
    # Process accept/reject for all walkers
    for i, (w, state) in enumerate(zip(walkers, batch_states)):
        prop_prob = max(0.001, state.b)
        
        # Metropolis-Hastings
        if np.random.random() < min(1.0, prop_prob / w['prob']):
            # ACCEPT: update state (already at new position)
            w['x'] = state.x
            w['y'] = state.y
            w['prob'] = prop_prob
        else:
            # REJECT: prepare to teleport back
            update = se.BatchUpdateData()
            update.index = w['pid']
            update.x = w['x']  # Original position
            update.y = w['y']
            update.vx = 0
            update.vy = 0
            update.mass = 1
            update.charge = 0
            update.rotation = 0
            update.angular_velocity = 0
            update.width = 0.15
            update.height = 0.15
            update.r = 0.2
            update.g = 0.5
            update.b = w['prob']  # Use probability for blue channel
            update.a = 0.8
            
            reject_updates.append(update)
    
    # BATCH UPDATE: Teleport rejected walkers back at once
    if reject_updates:
        sim.batch_update(reject_updates)
    
    frame += 1
    if frame % 60 == 0:
        accepted = num_walkers - len(reject_updates)
        print(f"Frame {frame}: {accepted}/{num_walkers} accepted ({(accepted/num_walkers)*100:.1f}%)")
    
    sim.render()

sim.cleanup()