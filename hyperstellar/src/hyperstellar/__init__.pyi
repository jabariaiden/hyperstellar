# Type stubs for hyperstellar package
import typing
from typing import Any, List, Dict, Optional, Union, Callable, ClassVar, Tuple, overload

# ============================================================================
# ENUMS
# ============================================================================

class SkinType:
    """Visual representation type for objects."""
    CIRCLE: int
    RECTANGLE: int
    POLYGON: int

class CollisionShape:
    """Collision shape type for physics objects."""
    NONE: int       # No collision
    CIRCLE: int     # Circular collision shape
    AABB: int       # Axis-aligned bounding box
    POLYGON: int    # Polygon collision shape

class ConstraintType:
    """Type of physics constraint."""
    DISTANCE: int   # Distance constraint between objects
    BOUNDARY: int   # Boundary/box constraint

# ============================================================================
# DATA CLASSES
# ============================================================================

class ObjectState:
    """Complete state of a physics object.

    Attributes:
        x (float): X position
        y (float): Y position
        vx (float): X velocity
        vy (float): Y velocity
        mass (float): Mass of object
        charge (float): Electric charge
        rotation (float): Rotation angle in radians
        angular_velocity (float): Angular velocity in rad/s
        width (float): Width for rectangles
        height (float): Height for rectangles
        radius (float): Radius for circles/polygons
        polygon_sides (int): Number of sides for polygons
        skin_type (SkinType): Visual type
        r,g,b,a (float): Color components (0.0-1.0)
    """
    x: float
    y: float
    vx: float
    vy: float
    mass: float
    charge: float
    rotation: float
    angular_velocity: float
    width: float
    height: float
    radius: float
    polygon_sides: int
    skin_type: SkinType
    r: float
    g: float
    b: float
    a: float

    def __init__(self) -> None: ...
    def __repr__(self) -> str: ...

class CollisionConfig:
    """Collision configuration for an object.

    Attributes:
        enabled (bool): Whether collision detection is enabled
        shape (CollisionShape): Collision shape type
        restitution (float): Bounciness (0.0-1.0)
        friction (float): Surface friction (0.0-1.0)
    """
    enabled: bool
    shape: CollisionShape
    restitution: float
    friction: float

    def __init__(self) -> None: ...
    def __repr__(self) -> str: ...

class KeyState:
    """Represents the state of a single keyboard key for the current frame.

    Attributes:
        pressed (bool): True if the key is currently held down.
        released (bool): True if the key was just released this frame.
        held (bool): Alias for pressed.
    """
    @property
    def pressed(self) -> bool: ...
    @property
    def released(self) -> bool: ...
    @property
    def held(self) -> bool: ...
    def __repr__(self) -> str: ...

class KeyboardMonitor:
    """
    Provides access to keyboard states via attributes or indexing.

    Example:
        if sim.keyboard.Z.pressed: ...
        if sim.keyboard["Space"].released: ...

    Supported key names include: A-Z, 0-9, Space, Shift, Control, Alt,
    Escape, Enter, Tab, Backspace, Delete, Home, End, PageUp, PageDown,
    Insert, Up, Down, Left, Right, F1-F12.
    """
    def __init__(self, sim: "Simulation") -> None: ...
    def __getattr__(self, name: str) -> KeyState: ...
    def __getitem__(self, name: str) -> KeyState: ...
    def get_key_state(self, key_name: str) -> KeyState:
        """Get the state of a specific key by name."""
        ...

class ObjectConfig:
    """Configuration for creating objects in batch mode.

    Attributes:
        x,y,vx,vy,mass,charge,rotation,angular_velocity: Same as ObjectState
        skin (SkinType): Visual type
        size (float): General size parameter
        width,height (float): Dimensions for rectangles
        r,g,b,a (float): Color
        polygon_sides (int): Polygon sides
        equation (str): Physics equation string
        constraints (List[ConstraintConfig]): List of constraints
    """
    x: float
    y: float
    vx: float
    vy: float
    mass: float
    charge: float
    rotation: float
    angular_velocity: float
    skin: SkinType
    size: float
    width: float
    height: float
    r: float
    g: float
    b: float
    a: float
    polygon_sides: int
    equation: str
    constraints: List["ConstraintConfig"]

    def __init__(self) -> None: ...
    def __repr__(self) -> str: ...

class ConstraintConfig:
    """Constraint configuration for batch mode.

    Attributes:
        type (ConstraintType): Constraint type (DISTANCE or BOUNDARY)
        target (int): Target object ID
        param1 (float): Distance: rest_length, Boundary: min_x, Angle: min_angle
        param2 (float): Boundary: max_x, Angle: max_angle
        param3 (float): Boundary: min_y
        param4 (float): Boundary: max_y
    """
    type: ConstraintType
    target: int
    param1: float
    param2: float
    param3: float
    param4: float

    def __init__(self) -> None: ...

class BatchConfig:
    """Configuration for batch simulations.

    Attributes:
        objects (list[ObjectConfig]): List of object configurations
        duration (float): Simulation duration in seconds
        dt (float): Time step per update
        output_file (str): Optional output file path
    """
    objects: List[ObjectConfig]
    duration: float
    dt: float
    output_file: str

    def __init__(self) -> None: ...

class BatchGetData:
    """Batch get data structure."""
    x: float
    y: float
    vx: float
    vy: float
    mass: float
    charge: float
    rotation: float
    angular_velocity: float
    width: float
    height: float
    radius: float
    polygon_sides: int
    skin_type: int
    r: float
    g: float
    b: float
    a: float

    def __init__(self) -> None: ...

class BatchUpdateData:
    """Batch update data structure."""
    index: int
    x: float
    y: float
    vx: float
    vy: float
    mass: float
    charge: float
    rotation: float
    angular_velocity: float
    size: float          # radius for circle/polygon
    width: float
    height: float
    r: float
    g: float
    b: float
    a: float
    polygon_sides: int

    def __init__(self) -> None: ...

class DistanceConstraint:
    """Maintain distance between two objects.

    Args:
        target_object (int): ID of target object to maintain distance with
        rest_length (float): Desired distance between objects
    """
    target_object: int
    rest_length: float

    def __init__(self, target_object: int = 0, rest_length: float = 5.0) -> None: ...
    def __repr__(self) -> str: ...

class BoundaryConstraint:
    """Keep object within a rectangular boundary.

    Args:
        min_x (float): Minimum X boundary
        max_x (float): Maximum X boundary
        min_y (float): Minimum Y boundary
        max_y (float): Maximum Y boundary
    """
    min_x: float
    max_x: float
    min_y: float
    max_y: float

    def __init__(self, min_x: float = -10.0, max_x: float = 10.0, min_y: float = -10.0, max_y: float = 10.0) -> None: ...
    def __repr__(self) -> str: ...

# ============================================================================
# MAIN SIMULATION CLASS
# ============================================================================

class Simulation:
    """Main physics simulation class.

    Provides real-time physics simulation with GPU acceleration.
    Can run in headless mode (no window) or with OpenGL visualization.
    """

    def __init__(self,
                 headless: bool = True,
                 width: int = 1280,
                 height: int = 720,
                 title: str = "Physics Simulation",
                 enable_grid: bool = True) -> None:
        """
        Create a new simulation instance.

        Args:
            headless (bool): Run without graphical window. Default: True.
            width (int): Window width in pixels. Default: 1280.
            height (int): Window height in pixels. Default: 720.
            title (str): Window title. Default: "Physics Simulation".
            enable_grid (bool): Enable grid/axis rendering. Default: True.

        Example:
            >>> sim = Simulation(headless=True, enable_grid=False)
            >>> sim = Simulation(width=1920, height=1080, title="My Simulation", enable_grid=True)
        """
        ...

    # ========================================================================
    # WINDOW MANAGEMENT
    # ========================================================================

    def render(self) -> None:
        """Render the current frame to the window (visual mode only)."""
        ...

    def paint(self, arg: Union[str, int]) -> None:
        """
        Unified paint method.

        If arg is a string, it's treated as a DSL equation.
        If arg is an int, it's treated as a JIT script ID (from register_script).

        DSL equation variables: px, py, t, p[id].x etc.
        Assign color.r, color.g, color.b.
        """
        ...

    def set_paint_resolution(self, width: int, height: int) -> None:
        """Set internal resolution for paint shader (default = window size)."""
        ...

    def process_input(self) -> None:
        """Process window input and camera controls (visual mode only)."""
        ...

    def should_close(self) -> bool:
        """Check if window should close (visual mode only)."""
        ...

    # ========================================================================
    # GRID CONTROL
    # ========================================================================

    def set_grid_enabled(self, enabled: bool) -> None:
        """
        Enable or disable grid/axis rendering.

        Args:
            enabled (bool): True to enable grid, False to disable
        """
        ...

    def get_grid_enabled(self) -> bool:
        """
        Check if grid/axis rendering is enabled.

        Returns:
            bool: True if grid is enabled, False otherwise
        """
        ...

    # ========================================================================
    # CORE SIMULATION
    # ========================================================================

    def update(self, dt: float = 0.016) -> None:
        """
        Update physics simulation by dt seconds.

        Args:
            dt (float): Time step in seconds. Default: 0.016 (approx 60 FPS).

        Example:
            >>> sim.update(dt=0.01)  # Update with 10ms time step
        """
        ...

    def set_speed(self, speed: float) -> None:
        """
        Set speed multiplier (1.0 = normal).
        Higher values make simulation faster without affecting stability.
        """
        ...

    def get_speed(self) -> float:
        """Get current speed multiplier."""
        ...

    # ========================================================================
    # OBJECT MANAGEMENT
    # ========================================================================

    def add_object(
        self,
        x: float = 0.0,
        y: float = 0.0,
        vx: float = 0.0,
        vy: float = 0.0,
        mass: float = 1.0,
        charge: float = 0.0,
        rotation: float = 0.0,
        angular_velocity: float = 0.0,
        skin: SkinType = SkinType.CIRCLE,
        size: float = 0.3,
        width: float = 0.5,
        height: float = 0.3,
        r: float = 1.0,
        g: float = 1.0,
        b: float = 1.0,
        a: float = 1.0,
        polygon_sides: int = 6
    ) -> int:
        """
        Add an object with full property control.

        Args:
            x,y (float): Initial position
            vx,vy (float): Initial velocity
            mass (float): Object mass. Default: 1.0
            charge (float): Electric charge. Default: 0.0
            rotation (float): Initial rotation in radians. Default: 0.0
            angular_velocity (float): Angular velocity in rad/s. Default: 0.0
            skin (SkinType): Visual type. Default: CIRCLE
            size (float): General size. Default: 0.3
            width,height (float): Dimensions for rectangles. Default: 0.5, 0.3
            r,g,b,a (float): Color (0.0-1.0). Default: white (1,1,1,1)
            polygon_sides (int): Polygon sides. Default: 6

        Returns:
            int: Object ID for future reference

        Example:
            >>> obj_id = sim.add_object(x=10, y=5, mass=50, skin=SkinType.CIRCLE)
        """
        ...

    # Overload for full update (all positional args)
    @overload
    def update_object(
        self,
        index: int,
        x: float, y: float,
        vx: float, vy: float,
        mass: float, charge: float,
        rotation: float, angular_velocity: float,
        size: float,
        width: float, height: float,
        r: float, g: float, b: float, a: float,
        polygon_sides: int = 0
    ) -> None: ...

    # Overload for partial update with kwargs
    @overload
    def update_object(
        self,
        index: int,
        *,
        x: Optional[float] = None,
        y: Optional[float] = None,
        vx: Optional[float] = None,
        vy: Optional[float] = None,
        mass: Optional[float] = None,
        charge: Optional[float] = None,
        rotation: Optional[float] = None,
        angular_velocity: Optional[float] = None,
        size: Optional[float] = None,
        width: Optional[float] = None,
        height: Optional[float] = None,
        r: Optional[float] = None,
        g: Optional[float] = None,
        b: Optional[float] = None,
        a: Optional[float] = None,
        polygon_sides: Optional[int] = None
    ) -> None: ...

    def update_object(self, index: int, *args, **kwargs) -> None:
        """
        Update all or partial properties of an existing object.

        If called with all positional arguments, updates all properties.
        If called with keyword arguments, only specified properties are updated.

        Example:
            # Full update
            sim.update_object(0, 1.0, 2.0, 0.1, 0.2, 1.0, 0.0, 0.0, 0.0, 0.3, 0.5, 0.3, 1.0, 1.0, 1.0, 1.0)
            # Partial update
            sim.update_object(1, x=1.0, r=0.5)
        """
        ...

    # ========================================================================
    # BATCH OPERATIONS
    # ========================================================================

    def batch_get(self, indices: List[int]) -> List[BatchGetData]:
        """
        Get properties for multiple objects at once.

        Args:
            indices (list[int]): List of object indices to fetch

        Returns:
            list: List of BatchGetData objects for each index

        Example:
            >>> states = sim.batch_get([0, 1, 2, 3])
            >>> for state in states:
            >>>     print(f"x={state.x}, y={state.y}")
        """
        ...

    def batch_update(self, updates: List[BatchUpdateData]) -> None:
        """
        Update multiple objects at once.

        Args:
            updates (list[BatchUpdateData]): List of update data objects

        Example:
            >>> updates = [
            >>>     BatchUpdateData(index=0, x=1.0, y=2.0, ...),
            >>>     BatchUpdateData(index=1, x=3.0, y=4.0, ...)
            >>> ]
            >>> sim.batch_update(updates)
        """
        ...

    def remove_object(self, index: int) -> None:
        """Remove an object by ID."""
        ...

    def object_count(self) -> int:
        """Get number of objects in simulation."""
        ...

    def get_object(self, index: int) -> ObjectState:
        """
        Get complete object state.

        Args:
            index (int): Object ID

        Returns:
            ObjectState: Complete object state
        """
        ...

    # ========================================================================
    # CONVENIENCE METHODS
    # ========================================================================

    def set_rotation(self, index: int, rotation: float) -> None:
        """Set rotation angle in radians."""
        ...

    def set_angular_velocity(self, index: int, angular_velocity: float) -> None:
        """Set angular velocity in rad/s."""
        ...

    def set_dimensions(self, index: int, width: float, height: float) -> None:
        """Set width and height for rectangle objects."""
        ...

    def set_radius(self, index: int, radius: float) -> None:
        """Set radius for circle/polygon objects."""
        ...

    def get_rotation(self, index: int) -> float:
        """Get rotation angle in radians."""
        ...

    def get_angular_velocity(self, index: int) -> float:
        """Get angular velocity in rad/s."""
        ...

    # ========================================================================
    # EQUATIONS AND SCRIPTS
    # ========================================================================

    def set_equation(self, object_index: int, equation_string: str) -> None:
        """
        Set physics equation for object.

        Args:
            object_index (int): Object ID
            equation_string (str): Physics equation

        Equation syntax supports:
            - Variables: x, y, vx, vy, mass, charge, time
            - Object references: p[ID].x, p[ID].y, p[ID].mass
            - Functions: sin, cos, tan, sqrt, exp, log
            - Operators: +, -, *, /, ^ (power)

        Example:
            >>> sim.set_equation(0, "0.1*mass*(p[1].x - x)/distance^3")
        """
        ...

    def register_script(self, source: str) -> int:
        """Compile a Python script (source) and return script ID."""
        ...

    def set_script(self, object_index: int, script_id: int) -> None:
        """
        Assign a JIT script to an object (use -1 to revert to default DSL).
        """
        ...

    # ========================================================================
    # CONSTRAINTS
    # ========================================================================

    def add_distance_constraint(self, object_index: int, constraint: DistanceConstraint) -> None:
        """Add distance constraint between objects."""
        ...

    def add_boundary_constraint(self, object_index: int, constraint: BoundaryConstraint) -> None:
        """Add boundary constraint to object."""
        ...

    def clear_constraints(self, object_index: int) -> None:
        """Clear all constraints from object."""
        ...

    def clear_all_constraints(self) -> None:
        """Clear all constraints from all objects."""
        ...

    # ========================================================================
    # COLLISION SYSTEM
    # ========================================================================

    def set_collision_enabled(self, index: int, enabled: bool) -> None:
        """
        Enable or disable collision detection for an object.

        Args:
            index (int): Object ID
            enabled (bool): True to enable collisions, False to disable

        Example:
            >>> sim.set_collision_enabled(0, False)  # Disable collisions for object 0
        """
        ...

    def set_collision_shape(self, index: int, shape: CollisionShape) -> None:
        """
        Set collision shape for an object.

        Args:
            index (int): Object ID
            shape (CollisionShape): Collision shape type

        Note: Shape is automatically set based on visual skin when adding objects.

        Example:
            >>> sim.set_collision_shape(0, CollisionShape.CIRCLE)
        """
        ...

    def set_collision_properties(self, index: int, restitution: float, friction: float) -> None:
        """
        Set collision physical properties.

        Args:
            index (int): Object ID
            restitution (float): Bounciness (0.0-1.0)
                0.0 = no bounce (inelastic collision)
                1.0 = perfect bounce (elastic collision)
            friction (float): Surface friction (0.0-1.0)
                0.0 = frictionless (ice)
                1.0 = maximum friction (rubber)

        Example:
            >>> sim.set_collision_properties(0, restitution=0.9, friction=0.1)
            >>> # Makes object 0 very bouncy with low friction
        """
        ...

    @overload
    def set_collision(
        self,
        index: int,
        *,
        enabled: Optional[bool] = None,
        shape: Optional[CollisionShape] = None,
        restitution: Optional[float] = None,
        friction: Optional[float] = None
    ) -> None: ...

    def set_collision(self, index: int, **kwargs) -> None:
        """
        Set collision properties for an object in a single call.

        Args:
            index (int): Object ID
            enabled (bool, optional): Enable/disable collisions
            shape (CollisionShape, optional): Collision shape type
            restitution (float, optional): Bounciness (0.0-1.0)
            friction (float, optional): Friction (0.0-1.0)

        Example:
            >>> sim.set_collision(0, shape=CollisionShape.CIRCLE, restitution=0.9, friction=0.1)
            >>> sim.set_collision(1, enabled=False)
        """
        ...

    def get_collision_config(self, index: int) -> CollisionConfig:
        """
        Get collision configuration for an object.

        Args:
            index (int): Object ID

        Returns:
            CollisionConfig: Current collision settings

        Example:
            >>> config = sim.get_collision_config(0)
            >>> print(f"Restitution: {config.restitution}")
        """
        ...

    def enable_collision_between(self, obj1: int, obj2: int, enable: bool) -> None:
        """
        Enable or disable collision detection between two specific objects.

        Args:
            obj1 (int): First object ID
            obj2 (int): Second object ID
            enable (bool): True to enable, False to disable

        Useful for creating collision layers or groups.

        Example:
            >>> sim.enable_collision_between(0, 1, False)
            >>> # Objects 0 and 1 will pass through each other
        """
        ...

    def is_collision_enabled(self, index: int) -> bool:
        """
        Check if collision detection is enabled for an object.

        Args:
            index (int): Object ID

        Returns:
            bool: True if collisions are enabled
        """
        ...

    def set_collision_parameters(self, enable_warm_start: bool, max_contact_iterations: int) -> None:
        """
        Set global collision parameters (warm start and iteration count).

        Args:
            enable_warm_start (bool): Enable warm starting for contacts
            max_contact_iterations (int): Maximum iterations for contact resolution (1-20)
        """
        ...

    def get_collision_parameters(self) -> Tuple[bool, int]:
        """
        Get global collision parameters.

        Returns:
            tuple: (enable_warm_start, max_contact_iterations)
        """
        ...

    # ========================================================================
    # BATCH PROCESSING
    # ========================================================================

    def run_batch(
        self,
        configs: List[BatchConfig],
        callback: Optional[Callable[[int, List[ObjectState]], None]] = None
    ) -> None:
        """
        Run multiple simulations in batch mode.

        Args:
            configs (list[BatchConfig]): List of simulation configurations
            callback (callable): Optional callback for progress/results
                Called as: callback(batch_index, results)

        Note: Only works in headless mode.
        """
        ...

    # ========================================================================
    # PARAMETERS
    # ========================================================================

    def set_parameter(self, name: str, value: float) -> None:
        """
        Set global simulation parameter.

        Args:
            name (str): Parameter name
            value (float): Parameter value

        Available parameters:
            - "gravity": Global gravity strength
            - "damping": Velocity damping (0-1)
            - "stiffness": Default constraint stiffness
        """
        ...

    def get_parameter(self, name: str) -> float:
        """Get global parameter value by name."""
        ...

    # ========================================================================
    # SIMULATION CONTROL
    # ========================================================================

    def set_paused(self, paused: bool) -> None:
        """Pause or resume simulation."""
        ...

    def is_paused(self) -> bool:
        """Check if simulation is paused."""
        ...

    def update_shader_loading(self) -> None:
        """Update shader loading status."""
        ...

    def are_all_shaders_ready(self) -> bool:
        """Check if all shaders are loaded."""
        ...

    def get_shader_load_progress(self) -> float:
        """Get shader loading progress (0.0 to 1.0)."""
        ...

    def get_shader_load_status(self) -> str:
        """Get current shader loading status message."""
        ...

    def reset(self) -> None:
        """Reset simulation to initial state (keeps objects)."""
        ...

    def cleanup(self) -> None:
        """Explicitly cleanup resources."""
        ...

    # ========================================================================
    # FILE I/O
    # ========================================================================

    def save_to_file(
        self,
        filename: str,
        title: str = "",
        author: str = "",
        description: str = ""
    ) -> None:
        """
        Save simulation state to .stellar file.

        Args:
            filename (str): Output file path
            title (str): Simulation title
            author (str): Author name
            description (str): Simulation description
        """
        ...

    def load_from_file(self, filename: str) -> None:
        """
        Load simulation state from .stellar file.

        Args:
            filename (str): Input file path
        """
        ...

    # ========================================================================
    # KEYBOARD AND CAMERA
    # ========================================================================

    @property
    def keyboard(self) -> KeyboardMonitor:
        """
        Keyboard state monitor (e.g., sim.keyboard.Z.pressed, sim.keyboard.Space.released).
        """
        ...

    def set_camera_position(self, x: float, y: float) -> None:
        """Set the camera position in world coordinates."""
        ...

    def get_camera_position(self) -> Tuple[float, float]:
        """Return the current camera position as a tuple (x, y)."""
        ...

    def set_camera_zoom(self, zoom: float) -> None:
        """Set the camera zoom level (1.0 = default)."""
        ...

    def get_camera_zoom(self) -> float:
        """Return the current camera zoom level."""
        ...

    # ========================================================================
    # INPUT HELPERS
    # ========================================================================

    def default_input(self) -> None:
        """
        Default WASD + Q/E zoom + ESC close camera controls.
        Call this in your loop for standard behavior.
        """
        ...

    def get_mouse_position(self) -> Tuple[float, float]:
        """
        Returns the current mouse position in world coordinates.
        """
        ...

    def get_mouse_delta(self) -> Tuple[float, float]:
        """
        Returns the mouse movement delta (in pixels) since the last call.
        """
        ...

    # ========================================================================
    # PROPERTIES
    # ========================================================================

    @property
    def is_headless(self) -> bool:
        """Check if simulation is running in headless mode."""
        ...

    @property
    def is_initialized(self) -> bool:
        """Check if simulation is fully initialized."""
        ...

    # ========================================================================
    # SCRATCHPAD
    # ========================================================================

    def create_scratchpad(self, size: int) -> int:
        """Create a scratchpad buffer with given number of floats."""
        ...

    def destroy_scratchpad(self, id: int) -> None:
        """Destroy a scratchpad."""
        ...

    def upload_scratchpad(self, id: int, data: List[float]) -> None:
        """Upload a list of floats to scratchpad."""
        ...

    def map_scratchpad(self, id: int) -> List[float]:
        """Return a list of floats from scratchpad (CPU copy)."""
        ...

    def scratchpad_size(self, id: int) -> int:
        """Get number of elements in scratchpad."""
        ...

    def is_valid_scratchpad(self, id: int) -> bool:
        """Check if scratchpad ID is valid."""
        ...

    # ========================================================================
    # SIGNAL QUEUE
    # ========================================================================

    def set_signal_queue_capacity(self, capacity: int) -> None:
        """Set the maximum number of pending signals."""
        ...

    def set_signal_queue_overflow_policy(self, policy: int) -> None:
        """Set overflow policy: 0=drop, 1=block."""
        ...

    def clear_signal_queue(self) -> None:
        """Clear all pending signals."""
        ...

    def get_signal_queue_count(self) -> int:
        """Get the number of pending signals."""
        ...

    # ========================================================================
    # AGENT DISPATCH
    # ========================================================================

    def dispatch_agent(self, agent_id: int, clear_after: bool = True) -> None:
        """
        Dispatch a specific agent over all pending signals for that agent.
        """
        ...

    def dispatch_all_agents(self, clear_after: bool = True) -> None:
        """
        Dispatch all registered agents over pending signals
        (each agent processes its own).
        """
        ...

    def register_agent(self, source: str) -> int:
        """Compile an agent shader and return its ID."""
        ...

    def get_agent_ids(self) -> List[int]:
        """Return a list of all registered agent IDs."""
        ...

# ============================================================================
# MODULE-LEVEL EXPORTS
# ============================================================================

# Version information
__version__: str