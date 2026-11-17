#!/usr/bin/env python3
"""
Piston Gas
==========
Air molecules in a box with a movable right wall (a piston).

  Left / Up     push the piston in   -> volume shrinks, gas heats up (turns red)
  Right / Down  pull the piston out  -> volume grows, gas cools down (turns blue)

Colour shows speed, i.e. temperature: slow = blue, medium = white, fast = red.

How it works
  * Python integrates the piston position and uploads [position, velocity] to
    a scratchpad every frame.
  * Each molecule script reads it. When a molecule hits the piston it bounces
    off a MOVING wall: vx -> 2*piston_v - vx. A wall moving in adds speed,
    a wall moving out removes it. That is the whole heating/cooling mechanism.
  * Molecules also collide with each other (engine collisions), so energy
    spreads through the gas instead of staying with the molecules that hit
    the piston.
"""
import math
import random

import hyperstellar as se
from hyperstellar import *

NUM_MOLECULES = 160
RADIUS = 0.2

HALF_W = 10.0          # left wall at x = -HALF_W
HALF_H = 6.0
PISTON_MIN = -3.0      # most compressed position of the piston face
PISTON_MAX = HALF_W    # fully open
PISTON_SPEED = 3.5     # units per second while a key is held
PLATE_W = 0.4
ROD_LEN = 7.0

SPEED_FOR_RED = 14.0   # speed that maps to full red

sim = se.Simulation(
    headless=False,
    enable_grid=False,
    width=1400,
    height=900,
    title="Piston Gas",
)

while not sim.are_all_shaders_ready():
    sim.update_shader_loading()

sim.set_collision_parameters(True, 10)

# [piston face x, piston velocity], written by Python every frame.
PISTON = sim.create_scratchpad(2)


@sim.script()
def molecule():
    piston_x = scratchpad_read(PISTON, 0)
    piston_v = scratchpad_read(PISTON, 1)

    # Piston (right wall, moving). Reflect in the wall's frame.
    if x > piston_x - RADIUS:
        x = piston_x - RADIUS
        if vx > piston_v:
            vx = 2.0 * piston_v - vx

    # Fixed walls.
    if x < -HALF_W + RADIUS:
        x = -HALF_W + RADIUS
        vx = abs(vx)

    if y > HALF_H - RADIUS:
        y = HALF_H - RADIUS
        vy = -abs(vy)
    elif y < -HALF_H + RADIUS:
        y = -HALF_H + RADIUS
        vy = abs(vy)

    # Temperature colour: blue (slow) -> white -> red (fast).
    t = clamp(sqrt(vx * vx + vy * vy) / SPEED_FOR_RED, 0.0, 1.0)
    color.r = clamp(2.0 * t, 0.0, 1.0)
    color.g = 1.0 - abs(2.0 * t - 1.0)
    color.b = clamp(2.0 * (1.0 - t), 0.0, 1.0)
    color.a = 1.0


@sim.script()
def piston_plate():
    x = scratchpad_read(PISTON, 0) + PLATE_W * 0.5
    vx = 0.0
    vy = 0.0
    ax = 0.0
    ay = 0.0
    angular = 0.0


@sim.script()
def piston_rod():
    x = scratchpad_read(PISTON, 0) + PLATE_W + ROD_LEN * 0.5
    vx = 0.0
    vy = 0.0
    ax = 0.0
    ay = 0.0
    angular = 0.0


@sim.script()
def fixed():
    ax = 0.0
    ay = 0.0
    angular = 0.0


# ---------------------------------------------------------------------
# Scene
# ---------------------------------------------------------------------
random.seed(11)

for _ in range(NUM_MOLECULES):
    angle = random.uniform(0.0, 2.0 * math.pi)
    speed = random.uniform(3.0, 7.0)
    m = sim.add_object(
        x=random.uniform(-HALF_W + 1.0, HALF_W - 1.0),
        y=random.uniform(-HALF_H + 1.0, HALF_H - 1.0),
        vx=speed * math.cos(angle),
        vy=speed * math.sin(angle),
        mass=1.0,
        skin=se.SkinType.CIRCLE,
        size=RADIUS,
        r=0.3, g=0.5, b=1.0,
    )
    sim.set_collision(
        m, enabled=True, shape=se.CollisionShape.CIRCLE,
        restitution=1.0, friction=0.0,
    )
    sim.set_script(m, molecule._script_id)


def add_static(x, y, w, h, script, color):
    o = sim.add_object(
        x=x, y=y, mass=1e12,
        skin=se.SkinType.RECTANGLE, width=w, height=h,
        r=color[0], g=color[1], b=color[2],
    )
    sim.set_collision_enabled(o, False)
    sim.set_script(o, script._script_id)


wall_color = (0.7, 0.7, 0.75)
span = 2.0 * HALF_W + 0.8
add_static(0.0, HALF_H + 0.1, span, 0.2, fixed, wall_color)            # top
add_static(0.0, -HALF_H - 0.1, span, 0.2, fixed, wall_color)           # bottom
add_static(-HALF_W - 0.1, 0.0, 0.2, 2.0 * HALF_H, fixed, wall_color)   # left
add_static(0.0, 0.0, PLATE_W, 2.0 * HALF_H, piston_plate, (0.9, 0.75, 0.3))
add_static(0.0, 0.0, ROD_LEN, 0.8, piston_rod, (0.55, 0.55, 0.6))

sim.set_camera_position(3.0, 0.0)
sim.set_camera_zoom(9.5)

print("Left/Up: compress   Right/Down: expand")

# ---------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------
DT = 0.016
piston_x = PISTON_MAX
frame = 0

while not sim.should_close():
    sim.process_input()

    direction = 0.0
    if sim.keyboard.Left.pressed or sim.keyboard.Up.pressed:
        direction -= 1.0
    if sim.keyboard.Right.pressed or sim.keyboard.Down.pressed:
        direction += 1.0

    new_x = min(PISTON_MAX, max(PISTON_MIN, piston_x + direction * PISTON_SPEED * DT))
    piston_v = (new_x - piston_x) / DT
    piston_x = new_x

    sim.upload_scratchpad(PISTON, [piston_x, piston_v])
    sim.update(DT)
    sim.render()

    frame += 1
    if frame % 60 == 0 and direction != 0.0:
        volume = (piston_x + HALF_W) * 2.0 * HALF_H
        print(f"volume = {volume:6.1f}")

sim.cleanup()