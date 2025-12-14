<<<<<<< HEAD
# Create README.md in hyperstellar-public
@"
# Hyperstellar

GPU-accelerated real-time physics engine with custom equations.

## Quick Install
\`\`\`bash
pip install hyperstellar
\`\`\`

## Quick Start
\`\`\`python
import hyperstellar as hs

sim = hs.Simulation(headless=True)
obj_id = sim.add_object(x=0, y=0, mass=1.0, skin=hs.SkinType.CIRCLE)
sim.set_equation(obj_id, \"sin(time), cos(time)\")

for _ in range(100):
    sim.update(dt=0.016)
\`\`\`

## Features
- Real-time GPU acceleration
- Custom mathematical equations
- Visual and headless modes
- Multiple object types
- Object constraints

## Examples
See \`examples/\` folder.

## Building from Source
\`\`\`bash
mkdir build && cd build
cmake .. -DBUILD_PYTHON=ON
cmake --build .
\`\`\`

## License
MIT License
"@ | Out-File -FilePath "README.md" -Encoding UTF8
=======
# hyperstellar
A GPU-accelerated physics simulation engine for defining and integrating mathematical dynamical systems (including complex-valued expressions) in Python, with real-time and headless execution.
>>>>>>> 3fd83288e1496a5a3d4c3e29bdef651eba07de35
