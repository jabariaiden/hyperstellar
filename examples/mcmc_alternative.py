#!/usr/bin/env python3
import hyperstellar as se
import numpy as np
import time
import math

# --------------------------------------------------------------------------
# Constants (captured by JIT)
# --------------------------------------------------------------------------
pi = math.pi
step_size = 0.5                     # smaller step for better mixing
burn_in_frames = 200                # discard early samples

# --------------------------------------------------------------------------
# Simulation setup
# --------------------------------------------------------------------------
sim = se.Simulation(headless=False, width=800, height=600, enable_grid=False)

while not sim.are_all_shaders_ready():
    sim.update_shader_loading()
    time.sleep(0.01)

while sim.object_count() > 0:
    sim.remove_object(0)

num_walkers = 10000
initial_time = 0.01

# --------------------------------------------------------------------------
# Create walkers with initial positions and probabilities
# --------------------------------------------------------------------------
walker_ids = []
for _ in range(num_walkers):
    angle = 2 * pi * np.random.random()
    r = np.random.exponential(1.0)
    x0 = r * np.cos(angle)
    y0 = r * np.sin(angle)

    prob0 = 3.0 * math.exp(-(1.0/7.0) * pi * (x0*x0 + y0*y0) / initial_time)

    pid = sim.add_object(
        x=x0, y=y0, vx=0, vy=0,
        mass=1.0,
        charge=prob0,               # store current probability in `charge`
        skin=se.SkinType.CIRCLE,
        size=0.15,
        r=0.2, g=0.5, b=0.9, a=0.8
    )
    sim.set_collision_enabled(pid, False)
    walker_ids.append(pid)

# --------------------------------------------------------------------------
# Define the MCMC script – runs entirely on the GPU
# --------------------------------------------------------------------------
@sim.script(mode='object', debug=True)
def mcmc_walker():
    # ---- propose a Gaussian step (Box‑Muller) ----
    u1 = rand()
    u2 = rand()
    u1 = max(u1, 1e-10)
    z = sqrt(-2.0 * log(u1)) * cos(2.0 * pi * u2)

    u3 = rand()
    u4 = rand()
    u3 = max(u3, 1e-10)
    z2 = sqrt(-2.0 * log(u3)) * cos(2.0 * pi * u4)

    delta_x = z * step_size
    delta_y = z2 * step_size

    prop_x = x + delta_x
    prop_y = y + delta_y

    # ---- evaluate the wavefunction at the proposed position ----
    t = time
    if t < 1e-10:
        t = 1e-10
    new_prob = 3.0 * exp(-(1.0/7.0) * pi * (prop_x*prop_x + prop_y*prop_y) / t)

    old_prob = charge

    # ---- Metropolis‑Hastings acceptance ratio ----
    alpha = new_prob / old_prob
    if alpha > 1.0:
        alpha = 1.0

    u = rand()

    if u < alpha:
        # ACCEPT: teleport directly to proposal
        x = prop_x
        y = prop_y
        vx = 0.0
        vy = 0.0
        ax = 0.0
        ay = 0.0
        charge = new_prob
    else:
        # REJECT: stay at current position
        vx = 0.0
        vy = 0.0
        ax = 0.0
        ay = 0.0
        # charge remains unchanged

    # ---- update visualisation ----
    color.b = charge
    color.r = 0.2
    color.g = 0.5
    color.a = 0.8

    angular = 0.0

# --------------------------------------------------------------------------
# Assign the script to all walkers
# --------------------------------------------------------------------------
for pid in walker_ids:
    sim.set_script(pid, mcmc_walker._script_id)

sim.update(initial_time)

# --------------------------------------------------------------------------
# Main loop – all MCMC logic is on the GPU
# --------------------------------------------------------------------------
print(f"Running MCMC with {num_walkers} walkers entirely on GPU...")
frame = 0
while not sim.should_close():
    sim.process_input()
    sim.update(0.001)               # each step advances time and runs the script

    # Burn‑in: discard early samples, but still render them
    if frame > burn_in_frames and frame % 10 == 0:
        # Optional: print acceptance rate (could be computed via reduction)
        pass

    sim.render()
    frame += 1

sim.cleanup()