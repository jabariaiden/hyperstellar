import hyperstellar as se
import math

sim = se.Simulation(headless=False, width=800, height=600, 
                    title="Pendulum", enable_grid=False)

while not sim.are_all_shaders_ready():
    sim.update_shader_loading()

while sim.object_count() > 0:
    sim.remove_object(0)

# Physical parameters
L = 2.0      # Rod length (meters)
g = 9.81     # Gravity (m/s^2)
k = 500.0    # Spring stiffness
damping = 0.1  # Reduced air resistance (swings longer)

# Initial angle
theta_init = math.pi / 3  # Start at 60 degrees

# Rod (visual connector) - DRAWN FIRST (renders behind)
rod = sim.add_object(
    x=L*math.sin(theta_init)/2, 
    y=2 - L*math.cos(theta_init)/2,
    vx=0, vy=0, mass=0.01,
    skin=se.SkinType.RECTANGLE, 
    width=L, height=0.08,
    r=0.6, g=0.6, b=0.6  # Medium gray
)

# Pivot (fixed point)
pivot = sim.add_object(
    x=0, y=2, vx=0, vy=0, mass=100,
    skin=se.SkinType.CIRCLE, size=0.15,
    r=0.3, g=0.3, b=0.3  # Dark gray
)
sim.set_equation(pivot, "0, 0")  # Stationary

# Bob (swinging mass)
bob = sim.add_object(
    x=L*math.sin(theta_init), 
    y=2 - L*math.cos(theta_init),
    vx=0, vy=0, mass=10.0,
    skin=se.SkinType.CIRCLE, size=0.25,
    r=0.2, g=0.6, b=1.0  # Blue
)
# Spring force toward pivot + gravity + damping
sim.set_equation(bob,
    f"{k}*(1 - {L}/sqrt((p[1].x-x)^2 + (p[1].y-y)^2 + 0.01))*(p[1].x-x) - {damping}*vx,"
    f"{k}*(1 - {L}/sqrt((p[1].x-x)^2 + (p[1].y-y)^2 + 0.01))*(p[1].y-y) - {damping}*vy - {g}"
)

# Track midpoint between pivot and bob, align rotation
sim.set_equation(rod,
    "5000*((p[1].x + p[2].x)/2 - x) - 50*vx,"
    "5000*((p[1].y + p[2].y)/2 - y) - 50*vy,"
    "5000*sin(atan2(p[2].y - p[1].y, p[2].x - p[1].x) - theta) - 50*omega"
)

# Run simulation
while not sim.should_close():
    sim.process_input()
    sim.update(0.0067)
    sim.render()

sim.cleanup()