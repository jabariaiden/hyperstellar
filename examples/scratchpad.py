#!/usr/bin/env python3
"""
Agents & Scratchpads Demo  (object + agent + paint all active together)
=======================================================================
Particles bounce inside a box. When a particle hits a wall it emits a signal.
An agent processes the signals and increments a wall-hit counter in a scratchpad.
A paint shader visualises the counters as coloured bars.

Controls: WASD to pan, scroll to zoom.
"""

import hyperstellar as se
import random
#This doesnt work, the paint script desnt draw the counters, make a new, better demo, dont copy this one
sim = se.Simulation(headless=False, enable_grid=False)

while not sim.are_all_shaders_ready():
    sim.update_shader_loading()

sim.set_paint_resolution(512, 512)

# Scratchpad: 4 floats = hit counters for left, right, top, bottom
scratch_id = sim.create_scratchpad(4)
sim.upload_scratchpad(scratch_id, [0.0, 0.0, 0.0, 0.0])
sim.set_signal_queue_capacity(1024)

NUM_PARTICLES = 20
BOUNDARY = 2.5

particles = []
for _ in range(NUM_PARTICLES):
    pid = sim.add_object(
        x=random.uniform(-BOUNDARY, BOUNDARY), y=random.uniform(-BOUNDARY, BOUNDARY),
        vx=random.uniform(-3, 3), vy=random.uniform(-3, 3),
        mass=1.0, skin=se.SkinType.CIRCLE, size=0.1,
        r=random.uniform(0.2, 0.8), g=random.uniform(0.2, 0.8), b=random.uniform(0.2, 0.8), a=1.0,
    )
    sim.set_collision_enabled(pid, False)
    particles.append(pid)

# ----------------------------------------------------------------------------
# AGENT FIRST.
# signal(agent_id, payload) is routed by script ID, and the agent only processes
# signals whose agent_id equals its own script ID (shader: s.agentID != uAgentID).
# Script IDs are handed out in registration order, so the agent must be registered
# BEFORE the object script so AGENT_ID exists when the object script is compiled.
# (The original demo hard-coded signal(0, ...), but the agent was ID 1 -> every
#  signal was addressed to the wrong script and the counters never moved.)
# ----------------------------------------------------------------------------
@sim.script(mode='agent')
def wall_counter():
    idx = int(signal_payload)
    val = scratchpad_read(0, idx)
    val += 1.0
    scratchpad_write(0, idx, val)

AGENT_ID = wall_counter._script_id


@sim.script(mode='object')
def bounce_and_signal():
    ax = 0.0
    ay = 0.0
    angular = 0.0

    # payload: 0=left, 1=right, 2=top, 3=bottom
    if x <= -BOUNDARY:
        vx = abs(vx)
        signal(AGENT_ID, 0.0)
    elif x >= BOUNDARY:
        vx = -abs(vx)
        signal(AGENT_ID, 1.0)
    if y <= -BOUNDARY:
        vy = abs(vy)
        signal(AGENT_ID, 2.0)
    elif y >= BOUNDARY:
        vy = -abs(vy)
        signal(AGENT_ID, 3.0)


@sim.script(mode='paint')
def show_counters():
    left = scratchpad_read(0, 0)
    right = scratchpad_read(0, 1)
    top = scratchpad_read(0, 2)
    bottom = scratchpad_read(0, 3)

    max_val = 100.0
    left = min(1.0, left / max_val) * BOUNDARY
    right = min(1.0, right / max_val) * BOUNDARY
    top = min(1.0, top / max_val) * BOUNDARY
    bottom = min(1.0, bottom / max_val) * BOUNDARY

    if px < -2.8 and abs(py) < left:
        color.r = 1.0
        color.g = 0.2
        color.b = 0.2
    elif px > 2.8 and abs(py) < right:
        color.r = 0.2
        color.g = 1.0
        color.b = 0.2
    elif py > 2.8 and abs(px) < top:
        color.r = 0.2
        color.g = 0.2
        color.b = 1.0
    elif py < -2.8 and abs(px) < bottom:
        color.r = 1.0
        color.g = 1.0
        color.b = 0.2
    else:
        color.r = 0.05
        color.g = 0.05
        color.b = 0.08
    color.a = 1.0


# Paint runs as a background layer; objects are drawn on top of it.
sim.paint(show_counters._script_id)

for pid in particles:
    sim.set_script(pid, bounce_and_signal._script_id)

print("Agents & Scratchpads Demo")
print(f"script ids -> agent={AGENT_ID}, object={bounce_and_signal._script_id}, paint={show_counters._script_id}")
print("Close the window to exit.\n")

sim.set_speed(2.0)

while not sim.should_close():
    sim.default_input()
    sim.update(0.016)                              # objects run, emit signals
    sim.dispatch_agent(AGENT_ID, clear_after=True) # agent consumes signals -> scratchpad
    sim.render()                                   # paint reads scratchpad, objects drawn on top
