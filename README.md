# Hyperstellar
### Write math Equations in python

Buckle up, coz this isn't just another preset physics engine. Hyperstellar gives you the mathematical language to define *any* dynamical system, then GPU accelerates it to thousands of frames per second. From orbital mechanics to fluid dynamics, if you can write the equation, you can simulate it.

<p align="center">
  <img src="media/orbit.gif" alt="Two-Body Orbital System">
</p>

## Installation
```bash
pip install hyperstellar
```
Platform Note: The current release (0.1.x) supports Windows 10/11 (64-bit). Linux support is in development - Linux users can build from source using the project files.

## Quick Start
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
```
See the full version in [examples/orbit.py](examples/orbit.py)

### What you can actually do with this

- Write your own equations (real or complex) and run them directly on the GPU.
- Reference other objects inside equations — positions, velocities, even colors. "```p[i].x```, ```p[i].y```, ```p[i].vx```, etc."
- Define derivatives and run them in the GPU.  ```D(expression, variable, order)```
- Most Math functions are available in DSL. Full support for trig, exponentials, powers, statistics, and complex numbers
- Run the engine with a window, or completely headless if you just want numbers.
- Batch operations for crunching numbers and cpu to gpu uploads in mass.
- Use color as part of the simulation state, not just rendering.
- Lock things together with constraints instead of hardcoding relationships. -experimental
- Run thousands of objects or many simulations at once without touching the CPU.

## Examples

- [**Two-Body Orbit**](examples/orbit.py) - Newtonian gravity simulation
- [**Pendulum**](examples/pendulum.py) - Spring-based harmonic motion
- [**Boids**](examples/boids.py) - Emergent flocking behavior with obstacle avoidance
- [**MCMC Sampling**](examples/mcmc.py) - Metropolis-Hastings algorithm on GPU

## Documentation

Coming soon - for now see the [examples](examples/) folder and the code documentation in [bindings.cpp](src/bindings.cpp).

---

*Built to make physics equations visual and interactive.*
