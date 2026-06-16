# Hyperstellar
### Write math Equations in python

Buckle up, because this isn't just another preset physics engine. Hyperstellar gives you the mathematical language to define *any* dynamical system, then GPU accelerates it to thousands of frames per second. From orbital mechanics to fluid dynamics, if you can write the equation, you can simulate it.

<p align="center">
  <img src="media/orbit.gif" alt="Two-Body Orbital System">
</p>

## Why Hyperstellar?
Most Python simulation tools make you choose between ease and performance. CPU-based libraries (NumPy, SciPy) are easy but slow. GPU tools (CUDA, Taichi, Warp) are fast but require learning new languages or shader programming.
Hyperstellar gives you both: write plain Python, get GPU performance.

| | Hyperstellar | NumPy (CPU) | Taichi | NVIDIA Warp |
|---|---|---|---|---|
| Write in Python | yes | yes | no (own lang) | yes (decorators) |
| No shader code | yes | yes | yes | yes |
| Real-time visualization | yes built-in | no | partial | no |
| Visual editor app | yes(in testing) | no | no | no |
| Collision system | yes | no | no | partial |
| pip install | yes | yes | yes | yes |

## Performance

Tested on integrated graphics(thats iGPU, no dedicated GPU):

| Objects | FPS |
|---|---|
| 1,000 | ~60 fps |
| 5,000 | ~52 fps |

Each object is running a per-frame force equation on the GPU. This is not pre-baked animation; it's live physics computation.

## Installation
```bash
pip install hyperstellar
```
Platform Note: The current release (0.1.x) supports Windows 10/11 (64-bit). Linux support is in development - Linux users can build from source using the project files.

## Quick Start
### planetary rotation
```python
import hyperstellar as se
import math

sim = se.Simulation(headless=False, enable_grid=False)
while not sim.are_all_shaders_ready(): # A one time GPU initialization (required before simulation)
    sim.update_shader_loading()
while sim.object_count() > 0: # Remove default object
    sim.remove_object(0)

G, M_star, M_planet, sep = 1.0, 50.0, 1.0, 3.0
v_orbit = math.sqrt(G * (M_star + M_planet) / sep)

# Create star and planet
star = sim.add_object(x=0, y=0, vy=M_planet*v_orbit/(M_star+M_planet),
                      mass=M_star, skin=se.SkinType.CIRCLE, size=0.8)
planet = sim.add_object(x=sep, y=0, vy=-M_star*v_orbit/(M_star+M_planet),
                        mass=M_planet, skin=se.SkinType.CIRCLE, size=0.25)

# Gravitational force equations (ax, ay, angular, r, g, b, a)
sim.set_equation(star,
    f"{G}*{M_planet}*(p[1].x-x)/((p[1].x-x)^2+(p[1].y-y)^2)^1.5," # Newtonian gravity equation using object reference p[index]
    f"{G}*{M_planet}*(p[1].y-y)/((p[1].x-x)^2+(p[1].y-y)^2)^1.5,"
    "0, 1.0, 0.9, 0.3, 1.0"  # Yellow
)
sim.set_equation(planet,
    f"{G}*{M_star}*(p[0].x-x)/((p[0].x-x)^2+(p[0].y-y)^2)^1.5,"
    f"{G}*{M_star}*(p[0].y-y)/((p[0].x-x)^2+(p[0].y-y)^2)^1.5,"
    "0, 0.3, 0.6, 1.0, 1.0"  # Blue
)



while not sim.should_close():
    sim.update(0.016)
    sim.render()
    sim.process_input() 
```

### Bouncing Ball
```python
import hyperstellar as se

sim = se.Simulation(headless=False, enable_grid=False, width=1400, height=1000, title="Collision Test")
while not sim.are_all_shaders_ready():
    sim.update_shader_loading() # Wait for shaders to load before proceeding
while sim.object_count() > 0:
    sim.remove_object(0) # Clear default objects

ball = sim.add_object(x=0, y=20, vy=0,
                      mass=0.1, skin=se.SkinType.CIRCLE, size=0.8)
platform = sim.add_object(x=0, y=-1, vy=0,
                        mass=1e12, skin=se.SkinType.RECTANGLE, height=3.0, width=10.0)

sim.set_collision_properties(ball, restitution=0.8, friction=0.5)
sim.set_collision_properties(platform, restitution=0.7, friction=0.5)
sim.set_collision_shape(platform, se.CollisionShape.AABB)
sim.set_collision_shape(ball, se.CollisionShape.CIRCLE)

sim.set_equation(ball, f"0, -9.8, 0, 1.0, 0.3, 0.3, 1.0")
sim.set_equation(platform, f"0, 0, 0, 0.3, 1.0, 1.0, 1.0")

while not sim.should_close():
    sim.update(0.067) 
    sim.render()
    sim.process_input() #use wsad to move the camera left and right

  ```

### 50,000 Ring of death (GPU benchmark)

```python
import hyperstellar as se
import math

N = 50000
dt = 0.0006
sim = se.Simulation(headless=False)
while not sim.are_all_shaders_ready():
    sim.update_shader_loading()
while sim.object_count() > 0:
    sim.remove_object(0)

spacing = 0.4
R = (N * spacing) / (2 * math.pi)
K, V = 1.5, math.sqrt(1.5 * R)
offset_x = R

for i in range(N):
    angle = (i / N) * 2 * math.pi
    x = math.cos(angle) * R + offset_x
    y = math.sin(angle) * R
    vx = -math.sin(angle) * V
    vy = math.cos(angle) * V
    obj = sim.add_object(x=x, y=y, vx=vx, vy=vy, size=0.15)
    sim.set_collision_enabled(obj, False)
    sim.set_equation(obj, f"-(x-{offset_x})*{K/R}, -y*{K/R}, 0, 0.5, 0.2, 1.0, 1.0")

while not sim.should_close():
    sim.update(dt)
    sim.render()
    sim.process_input()
```



## Core Concepts


### Equation Format

Every object's behavior is defined by a comma-separated equation string with 7 components:

```
"ax, ay, angular, r, g, b, a"
```

Only `ax` and `ay` are required. All other components are optional and default to `0` (for angular) or `1.0` (for color channels).

---

#### Available Variables

These variables represent the current object's state and can be used directly in any equation:

| Variable | Meaning |
|---|---|
| `x`, `y` | Position |
| `vx`, `vy` | Velocity |
| `ax`, `ay` | Acceleration |
| `theta` | Rotation angle |
| `omega` | Angular velocity |
| `alpha` | Angular acceleration |
| `mass` | Object mass |
| `charge` | Object charge |
| `r`, `g`, `b`, `a` | Current color (RGBA) |
| `h`, `s`, `v` | Color in HSV space |
| `t` | Simulation time |
| `i` | Imaginary unit (complex numbers supported) |
| `pi`, `e` | Mathematical constants |
| `k` | Spring/coupling constant |
| `damping` | Damping coefficient |
| `gravity` | Gravitational constant |
| `coupling` | Coupling strength |
| `freq`, `amp` | Frequency, amplitude |

---

#### Object References

Reference other objects in the simulation using `p[index].property`:

```python
# Gravity from object 0 pulling on the current object
sim.set_equation(planet,
    f"{G}*(p[0].x - x) / ((p[0].x-x)^2 + (p[0].y-y)^2)^1.5,"
    f"{G}*(p[0].y - y) / ((p[0].x-x)^2 + (p[0].y-y)^2)^1.5,"
    "0, 0.3, 0.6, 1.0, 1.0"
)
```

All readable properties on `p[index]`:

| Property | Meaning |
|---|---|
| `p[i].x`, `p[i].y` | Position |
| `p[i].vx`, `p[i].vy` | Velocity |
| `p[i].ax`, `p[i].ay` | Acceleration |
| `p[i].mass` | Mass |
| `p[i].charge` | Charge |
| `p[i].data.x` | Rotation |
| `p[i].data.y` | Angular velocity |
| `p[i].color.r`, `p[i].color.g`, `p[i].color.b`, `p[i].color.a` | Color state |

---

#### Built-in Functions

All functions run natively on the GPU:

**Single-argument:**
`sin`, `cos`, `tan`, `sqrt`, `log`, `exp`, `abs`, `floor`, `ceil`, `frac`, `sign`, `step`

**Two-argument:**
`min(a, b)`, `max(a, b)`, `mod(a, b)`, `atan2(y, x)`

**Three-argument:**
`clamp(x, min, max)`

**Complex number:**
`real(z)`, `imag(z)`, `conj(z)`, `arg(z)`

**Operators:**
`+`, `-`, `*`, `/`, `^` (power, right-associative: `2^3^2` = `2^(3^2)`)

---

#### Derivatives

Compute numerical derivatives of any expression on the GPU using `D(expr, variable, order)`:

```python
# First derivative of x^2 with respect to x
sim.set_equation(obj, "D(x^2, x, 1), 0, 0, 1.0, 1.0, 1.0, 1.0")

# Second derivative (order defaults to 1 if omitted)
sim.set_equation(obj, "D(sin(x), x, 2), 0, 0, 1.0, 1.0, 1.0, 1.0")
```

Valid differentiation variables: `x`, `y`, `theta`

Order must be between 1 and 4.

---

#### Color as Simulation State

Color channels (`r, g, b, a`) are not just rendering — they are part of the live simulation state per object, updated every frame on the GPU. You can make color depend on velocity, position, or any other variable:

```python
# Color shifts from blue to red based on speed
speed = "sqrt(vx^2 + vy^2)"
sim.set_equation(obj, f"0, -3, 0, {speed}/10, 0.3, 1.0-{speed}/10, 1.0")
```

### Headless Mode

Run without a window for data collection or batch processing:

```python
sim = se.Simulation(headless=True)
# ... setup ...
while True:
    sim.update(dt)
    height = sim.get_object(ball).y  # read state back to Python
```

### Collision System

```python
sim.set_collision_parameters(enabled=True, iterations=20)
sim.set_collision_shape(obj, se.CollisionShape.CIRCLE)   # or AABB
sim.set_collision_properties(obj, restitution=0.9, friction=0.5)
sim.set_collision_enabled(obj, True)
```

### Constraints
Lock relationships between objects.
- `sim.add_boundary_constraint(object_index, se.BoundaryConstraint(min_x, max_x, min_y, max_y))`
- `sim.add_distance_constraint(object_index, se.DistanceConstraint(target_object, rest_length))`

### Shapes / Skins
- `se.SkinType.CIRCLE`
- `se.SkinType.RECTANGLE`
- `se.SkinType.POLYGON`

### Collision Shapes
- `se.CollisionShape.POLYGON`
- `se.CollisionShape.CIRCLE`
- `se.CollisionShape.AABB`

## The App

Hyperstellar ships with a **visual editor** built in ImGui. You can build and configure simulations visually, save your project, and load it back — similar to how Unity and Visual Studio relate to each other. The Python API and the app work on the same project format. Though it is publicly available through the building of project files.

## Examples

| Example | Description |
|---|---|
| `examples/orbit.py` | Two-body Newtonian gravity |
| `examples/pendulum.py` | Spring-based harmonic motion |
| `examples/boids.py` | Emergent flocking with obstacle avoidance |
| `examples/mcmc.py` | Metropolis-Hastings sampling on GPU |

## Roadmap

- [ ] Linux official release
- [ ] More collision shapes (OBB, convex polygon)
- [ ] Full constraints system
- [ ] API reference documentation
- [ ] More examples

## Contributing

Contributions welcome — code, documentation, examples, and bug reports all help. See the source in `src/bindings.cpp` for the current full API surface while formal docs are in progress.

## License

See [LICENSE](LICENSE).
