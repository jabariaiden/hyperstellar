# hyperstellar/dsl.py
"""
Stub definitions for the Hyperstellar DSL.

Import this module with `from hyperstellar.dsl import *` at the top of your
simulation script to make linters (Pyright, mypy, Pylance) happy about
the injected variables (x, y, p, ax, ay, angular, color, etc.).

These are dummy values and are never used at runtime – they only serve
to satisfy static analysis.
"""

# Object array (used as p[index])
p = None  # type: ignore

# Object state aliases
x = 0.0
y = 0.0
vx = 0.0
vy = 0.0
mass = 0.0
charge = 0.0
theta = 0.0
omega = 0.0
color = None  # type: ignore

# Acceleration targets (user-assigned)
ax = 0.0
ay = 0.0
angular = 0.0

# Time constant (available in JIT scripts)
uTime = 0.0

# Other constants (inlined from globals, but dummy here for linting)
G = 1.0
k = 1.0
b = 0.1
g = 9.81
# ... add any other global constants you commonly use  ¯\_(ツ)_/¯, if it works, it works

sqrt = None  