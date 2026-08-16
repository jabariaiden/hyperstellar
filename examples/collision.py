import hyperstellar as se
import math

# Create simulation (visual mode)
sim = se.Simulation(headless=False, enable_grid=False, width=1400, height=1000, title="Collision Test")

# Wait for shaders to load before proceeding
while not sim.are_all_shaders_ready():
    sim.update_shader_loading()

# Clear default objects
while sim.object_count() > 0:
    sim.remove_object(0)

# Enable warm starting and set contact iterations (global solver parameters)
sim.set_collision_parameters(enable_warm_start=True, max_contact_iterations=20)

# Create 10 falling rectangles with varying rotations
for i in range(10):
    ball = sim.add_object(
        x=i / 2,
        y=(i / 2) ** 2 + 5,
        vy=0,
        mass=0.1,
        skin=se.SkinType.RECTANGLE,
        height=0.8,
        width=0.8,
        rotation=math.radians(i * 10)
    )
    # Unified collision setup – enabled, shape, restitution, friction in one call
    sim.set_collision(
        ball,
        enabled=True,
        shape=se.CollisionShape.AABB,
        restitution=1.0,
        friction=0.5
    )
    # Set physics equation (legacy DSL)
    sim.set_equation(ball, "0, -9.8, 0, 1.0, 0.3, 0.3, 1.0")

# Create a large static platform
platform = sim.add_object(
    x=0,
    y=-1,
    vy=0,
    mass=1e12,
    skin=se.SkinType.RECTANGLE,
    height=3.0,
    width=10.0,
    rotation=0.0
)
sim.set_collision(
    platform,
    enabled=True,
    shape=se.CollisionShape.AABB
)
sim.set_equation(platform, "0, 0, 0, 0.3, 1.0, 1.0, 1.0")

# Adjust camera
sim.set_camera_zoom(10.03)

# Main loop
while not sim.should_close():
    sim.update(0.067)
    sim.render()
    sim.process_input()  # WASD to move camera