#!/usr/bin/env python3
"""
Agents & Scratchpads Demo
=========================
Particles bounce inside a box. When a particle hits a wall, it emits a signal.
An agent processes the signals and increments a wall‑hit counter in a scratchpad.
A paint shader visualises the counters as coloured bars.

Controls: WASD to pan, scroll to zoom.
"""

import hyperstellar as se
import random
import time

# ----------------------------------------------------------------------------
# Simulation setup
# ----------------------------------------------------------------------------
sim = se.Simulation(
    headless=False,
    width=1200,
    height=800,
    title="Agents & Scratchpads Demo",
    enable_grid=False
)

while not sim.are_all_shaders_ready():
    sim.update_shader_loading()
    time.sleep(0.01)

# Clear any default objects
while sim.object_count() > 0:
    sim.remove_object(0)

sim.set_paint_resolution(512, 512)

# ----------------------------------------------------------------------------
# Create scratchpad: 4 floats = hit counters for left, right, top, bottom
# ----------------------------------------------------------------------------
scratch_id = sim.create_scratchpad(4)
sim.upload_scratchpad(scratch_id, [0.0, 0.0, 0.0, 0.0])

# Signal queue capacity (default 1024, but we set explicitly)
sim.set_signal_queue_capacity(1024)

# ----------------------------------------------------------------------------
# Create bouncing particles (objects)
# ----------------------------------------------------------------------------
NUM_PARTICLES = 20
BOUNDARY = 2.5

particles = []
for _ in range(NUM_PARTICLES):
    x = random.uniform(-BOUNDARY, BOUNDARY)
    y = random.uniform(-BOUNDARY, BOUNDARY)
    vx = random.uniform(-3, 3)
    vy = random.uniform(-3, 3)
    r = random.uniform(0.2, 0.8)
    g = random.uniform(0.2, 0.8)
    b = random.uniform(0.2, 0.8)
    pid = sim.add_object(
        x=x, y=y, vx=vx, vy=vy,
        mass=1.0,
        skin=se.SkinType.CIRCLE,
        size=0.1,
        r=r, g=g, b=b, a=1.0
    )
    sim.set_collision_enabled(pid, False)   # no collisions between particles
    particles.append(pid)

# ----------------------------------------------------------------------------
# Object JIT script: free motion + boundary detection with signal emission
# ----------------------------------------------------------------------------
@sim.script(mode='object', debug=True)
def bounce_and_signal():
    # Simple physics: constant velocity
    ax = 0.0
    ay = 0.0
    angular = 0.0

    # Boundary detection – if hit, emit a signal and flip velocity
    # Signal payload: 0=left, 1=right, 2=top, 3=bottom
    if x <= -BOUNDARY:
        vx = abs(vx)      # bounce inward
        signal(0, 0.0)    # agent ID 0, payload 0 (left)
    elif x >= BOUNDARY:
        vx = -abs(vx)
        signal(0, 1.0)    # right
    if y <= -BOUNDARY:
        vy = abs(vy)
        signal(0, 2.0)    # top
    elif y >= BOUNDARY:
        vy = -abs(vy)
        signal(0, 3.0)    # bottom

# Assign the same script to all particles
for pid in particles:
    sim.set_script(pid, bounce_and_signal._script_id)

# ----------------------------------------------------------------------------
# Agent: processes signals, increments scratchpad counters
# ----------------------------------------------------------------------------
@sim.script(mode='agent', debug=True)
def wall_counter():
    # This runs per signal
    idx = int(signal_payload)
    # Read current value from scratchpad
    val = scratchpad_read(0, idx)
    val += 1.0
    scratchpad_write(0, idx, val)

# The agent ID is stored in the function's _script_id attribute
AGENT_ID = wall_counter._script_id

# ----------------------------------------------------------------------------
# Paint JIT: read scratchpad and show counters as bars
# ----------------------------------------------------------------------------
@sim.script(mode='paint', debug=True)
def show_counters():
    # Read the four counters
    left = scratchpad_read(0, 0)
    right = scratchpad_read(0, 1)
    top = scratchpad_read(0, 2)
    bottom = scratchpad_read(0, 3)

    # Normalise to [0,1] for display (cap at 100)
    max_val = 100.0
    left = min(1.0, left / max_val)
    right = min(1.0, right / max_val)
    top = min(1.0, top / max_val)
    bottom = min(1.0, bottom / max_val)

    # Paint bars on the edges
    # Left bar (red)
    if px < -2.8 and abs(py) < left:
        color.r = 1.0
        color.g = 0.2
        color.b = 0.2
    # Right bar (green)
    elif px > 2.8 and abs(py) < right:
        color.r = 0.2
        color.g = 1.0
        color.b = 0.2
    # Top bar (blue)
    elif py > 2.8 and abs(px) < top:
        color.r = 0.2
        color.g = 0.2
        color.b = 1.0
    # Bottom bar (yellow)
    elif py < -2.8 and abs(px) < bottom:
        color.r = 1.0
        color.g = 1.0
        color.b = 0.2
    else:
        # Dark background
        color.r = 0.05
        color.g = 0.05
        color.b = 0.08

    color.a = 1.0

sim.set_paint_script(show_counters._script_id)

# ----------------------------------------------------------------------------
# Main loop
# ----------------------------------------------------------------------------
print("Agents & Scratchpads Demo")
print("Particles bounce. Hit counters are shown as coloured bars at the edges.")
print("Close the window to exit.\n")

sim.set_speed(2.0)

while not sim.should_close():
    sim.process_input()

    # 1. Physics update (objects run their scripts, emit signals)
    sim.update(0.016)

    # 2. Dispatch the agent to process all pending signals
    sim.dispatch_agent(AGENT_ID, clear_after=True)

    # 3. Render (paint shader reads scratchpad)
    sim.render()

sim.cleanup()