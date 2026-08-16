import hyperstellar as se
import random

# --------------------------------------------------------------------------
# Simulation setup
# --------------------------------------------------------------------------
sim = se.Simulation(
    headless=False,
    enable_grid=False,
    width=1400,
    height=1000,
    title="Ball Pit – Collision Test"
)

# Wait for shaders to load
while not sim.are_all_shaders_ready():
    sim.update_shader_loading()

# Clear default objects
while sim.object_count() > 0:
    sim.remove_object(0)

# Enable warm starting for better collision stability
sim.set_collision_parameters(True, 20)

# --------------------------------------------------------------------------
# Generate falling balls
# --------------------------------------------------------------------------
NUM_BALLS = 500           # depending on performance and your computer, can increase for more stress test
COLS = 10                # Number of columns in the initial grid
SPACING = 2           # Gap between balls
X_OFFSET = -15            # Center the grid

for i in range(NUM_BALLS):
    col = i % COLS
    row = i // COLS

    # Slight random jitter for natural look
    jitter_x = random.uniform(-0.1, 0.1)
    jitter_y = random.uniform(-0.1, 0.1)

    x = X_OFFSET + (col * SPACING) + jitter_x
    y = 40 + (row * SPACING) + jitter_y  # Spawn high up, fall into frame

    # Random colour and size
    r, g, b = random.uniform(0, 1), random.uniform(0, 1), random.uniform(0, 1)
    radius = random.gauss(2, 1)

    ball = sim.add_object(x=x, y=y, vy=0,mass=0.1,skin=se.SkinType.CIRCLE,size=radius,r=1, g=g, b=b, a=1.0)

    # Perfect elasticity, zero friction for pure bouncing
    sim.set_collision_properties(ball, restitution=1.0, friction=0.0)
    sim.set_collision_shape(ball, se.CollisionShape.CIRCLE)

    # Gravity only (no horizontal forces)
    sim.set_equation(ball, "ax = 0; ay = -19.8;")

# Boundaries (floor, left wall, right wall)
# Floor
floor = sim.add_object(
    x=0, y=0,
    mass=1e12, skin=se.SkinType.RECTANGLE,
    height=4.0, width=70, rotation=0.0,
    r=1.0, g=1.0, b=1.0, a=1.0
)
sim.set_collision_properties(floor, restitution=0.7, friction=0.5)
sim.set_collision_shape(floor, se.CollisionShape.AABB)
sim.set_equation(floor, "ax = 0; ay = 0;")  # Static

# Left wall
left_wall = sim.add_object(
    x=-25, y=24,
    mass=1e12, skin=se.SkinType.RECTANGLE,
    height=70, width=4.0,
    r=0.3, g=1.0, b=1.0, a=1.0
)
sim.set_collision_properties(left_wall, restitution=0.7, friction=0.5)
sim.set_collision_shape(left_wall, se.CollisionShape.AABB)
sim.set_equation(left_wall, "ax = 0; ay = 0;")

# Right wall
right_wall = sim.add_object(
    x=25, y=24,
    mass=1e12, skin=se.SkinType.RECTANGLE,
    height=70, width=4.0,
    r=0.3, g=1.0, b=1.0, a=1.0
)
sim.set_collision_properties(right_wall, restitution=0.7, friction=0.5)
sim.set_collision_shape(right_wall, se.CollisionShape.AABB)
sim.set_equation(right_wall, "ax = 0; ay = 0;")

sim.set_camera_zoom(100)

print("Running ball pit demo. Close the window to exit.")
print("Controls: WASD to pan, scroll to zoom.")

while not sim.should_close():
    sim.update(0.016)      # ~60 FPS physics step
    sim.render()
    sim.process_input()

sim.cleanup()