import hyperstellar as se
import math

sim = se.Simulation(headless=False, width=800, height=600, title="Robust Pendulum")

while not sim.are_all_shaders_ready():
    sim.update_shader_loading()

while sim.object_count() > 0:
    sim.remove_object(0)

# DRAMATICALLY REDUCED PARAMETERS
# These survive random initialization without exploding
k = 50.0    # 1/20th of original - limits explosion magnitude
b = 2.0     # Still gives ~3-4 oscillations
L = 2.0
g = 9.81

# 1. Pivot
pivot = sim.add_object(0, 2, 0, 0, 100, 0, 0, 0, se.SkinType.CIRCLE, 0.1)
sim.set_equation(pivot, "0, 0")

# 2. Bob
bob = sim.add_object(2, 2, 0, 0, 1.0, 0, 0, 0, se.SkinType.CIRCLE, 0.2)
eq_bob = f"""
    {k} * (1 - {L}/sqrt((p[0].x - x)^2 + (p[0].y - y)^2 + 0.01)) * (p[0].x - x) - {b}*vx,
    {k} * (1 - {L}/sqrt((p[0].x - x)^2 + (p[0].y - y)^2 + 0.01)) * (p[0].y - y) - {b}*vy - {g}
"""
# Added 0.01 to sqrt to prevent division by zero if bob spawns ON pivot
sim.set_equation(bob, eq_bob)

# 3. Rod
rod = sim.add_object(0, 0, 0, 0, 0.01, 0, 0, 0, se.SkinType.RECTANGLE, 1.0, L, 0.05)
eq_rod = """
    50 * (((p[0].x + p[1].x) / 2) - x) - 50 * vx,
    50 * (((p[0].y + p[1].y) / 2) - y) - 50 * vy,
    50 * sin(atan2(p[1].y - p[0].y, p[1].x - p[0].x) - theta) - 50 * omega
"""
# Reduced rod spring constants too
sim.set_equation(rod, eq_rod)

# Run several "settling" frames with small timesteps
# This lets the spring gently pull the bob to correct position
print("Settling...")
for i in range(50):
    sim.update(0.001)  # Small timesteps during settling
print("Starting simulation...")

while not sim.should_close():
    sim.process_input()
    sim.update(0.016)
    sim.render()

sim.cleanup()