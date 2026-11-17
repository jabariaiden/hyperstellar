import random

import hyperstellar as se
from hyperstellar import *

"""
Kernel mode demo: Conway's Game of Life with ZERO objects.

  kernel  -> general compute, no objects / collisions / constraints / signals
  scratchpad -> the data (two buffers, ping-pong)
  paint   -> reads the scratchpad and draws it

Per frame: two kernel dispatches (A->B, then B->A), then render.
Threads inside one dispatch must not read cells another thread of the same
dispatch writes, hence the two scratchpads.
"""
W = 64
CELLS = W * W

sim = se.Simulation(headless=False, enable_grid=False)
while not sim.are_all_shaders_ready():
    sim.update_shader_loading()
sim.set_paint_resolution(512, 512)

pad_a = sim.create_scratchpad(CELLS)
pad_b = sim.create_scratchpad(CELLS)
assert (pad_a, pad_b) == (0, 1), "kernels below hard-code scratchpad ids 0 and 1"
sim.upload_scratchpad(pad_a, [1.0 if random.random() < 0.3 else 0.0 for _ in range(CELLS)])
sim.upload_scratchpad(pad_b, [0.0] * CELLS)


@sim.script(mode='kernel')
def life_a_to_b():
    cx = mod(float(idx), 64.0)
    cy = floor(float(idx) / 64.0)
    n = 0.0
    for dy in range(-1, 2):
        for dx in range(-1, 2):
            if dx != 0 or dy != 0:
                nx = mod(cx + dx + 64.0, 64.0)
                ny = mod(cy + dy + 64.0, 64.0)
                n += scratchpad_read(0, int(ny * 64.0 + nx))
    me = scratchpad_read(0, idx)
    alive = 0.0
    if me > 0.5 and (n == 2.0 or n == 3.0):
        alive = 1.0
    elif me < 0.5 and n == 3.0:
        alive = 1.0
    scratchpad_write(1, idx, alive)


@sim.script(mode='kernel')
def life_b_to_a():
    cx = mod(float(idx), 64.0)
    cy = floor(float(idx) / 64.0)
    n = 0.0
    for dy in range(-1, 2):
        for dx in range(-1, 2):
            if dx != 0 or dy != 0:
                nx = mod(cx + dx + 64.0, 64.0)
                ny = mod(cy + dy + 64.0, 64.0)
                n += scratchpad_read(1, int(ny * 64.0 + nx))
    me = scratchpad_read(1, idx)
    alive = 0.0
    if me > 0.5 and (n == 2.0 or n == 3.0):
        alive = 1.0
    elif me < 0.5 and n == 3.0:
        alive = 1.0
    scratchpad_write(0, idx, alive)


@sim.script(mode='paint')
def show_grid():
    gx = floor((px + 3.2) / 6.4 * 64.0)
    gy = floor((py + 3.2) / 6.4 * 64.0)
    v = 0.0
    if gx >= 0.0 and gx < 64.0 and gy >= 0.0 and gy < 64.0:
        v = scratchpad_read(0, int(gy * 64.0 + gx))
    color.r = 0.05 + 0.2 * v
    color.g = 0.05 + 0.9 * v
    color.b = 0.08 + 0.4 * v
    color.a = 1.0


sim.paint(show_grid._script_id)

while not sim.should_close():
    sim.default_input()
    sim.dispatch_kernel(life_a_to_b._script_id, CELLS)   # pad 0 -> pad 1
    sim.dispatch_kernel(life_b_to_a._script_id, CELLS)   # pad 1 -> pad 0
    sim.render()                                          # paint reads pad 0