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
    CIRCLE: int     # 2D circular collision shape
    AABB: int       # 2D axis-aligned bounding box
    POLYGON: int    # 2D polygon collision shape
    SPHERE: int     # 3D sphere collision shape
    BOX: int        # 3D oriented box collision shape

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
        z (float): Z position (0 for 2D objects)
        vx (float): X velocity
        vy (float): Y velocity
        vz (float): Z velocity (0 for 2D objects)
        mass (float): Mass of object
        charge (float): Electric charge
        rotation (float): Rotation angle in radians (2D projection)
        angular_velocity (float): Angular velocity in rad/s (2D projection)
        qx,qy,qz,qw (float): Orientation quaternion (authoritative for 3D)
        spin_x,spin_y,spin_z (float): Angular velocity vector (authoritative for 3D)
        width (float): Width for rectangles
        height (float): Height for rectangles
        radius (float): Radius for circles/polygons
        polygon_sides (int): Number of sides for polygons
        skin_type (SkinType): Visual type (-1 for 3D mesh objects)
        mesh_id (int): Mesh handle (-1 = no mesh)
        r,g,b,a (float): Color components (0.0-1.0)
    """
    x: float
    y: float
    z: float
    vx: float
    vy: float
    vz: float
    mass: float
    charge: float
    rotation: float
    angular_velocity: float
    qx: float
    qy: float
    qz: float
    qw: float
    spin_x: float
    spin_y: float
    spin_z: float
    width: float
    height: float
    radius: float
    polygon_sides: int
    skin_type: SkinType
    mesh_id: int
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

class ObjectHandle:
    """A stable handle to an object that remains valid across removals.

    Instead of a raw integer index that can silently repoint when objects
    are removed, a handle contains a slot and a generation counter. If the
    object is removed and a new object reuses the slot, the generation
    changes and the handle becomes invalid.

    Attributes:
        slot (int): The internal object slot
        generation (int): Generation counter for detecting stale handles

    Example:
        >>> handle = sim.add_object(x=0, y=0)
        >>> sim.remove_object(handle)
        >>> # handle is now invalid and will raise an error if used
    """
    slot: int
    generation: int

    def __init__(self) -> None: ...
    def __repr__(self) -> str: ...
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...
    def is_valid(self, sim: "Simulation") -> bool:
        """Check if this handle is still valid."""
        ...

class CollisionEvent:
    """Data about a collision event for callback handlers.

    Attributes:
        object_a (ObjectHandle): First object in the collision
        object_b (ObjectHandle): Second object in the collision
        normal_x (float): X component of collision normal
        normal_y (float): Y component of collision normal
        penetration (float): How deeply the objects overlap
        contact_x (float): X position of the contact point
        contact_y (float): Y position of the contact point
        impulse (float): Magnitude of the collision impulse
    """
    object_a: ObjectHandle
    object_b: ObjectHandle
    normal_x: float
    normal_y: float
    penetration: float
    contact_x: float
    contact_y: float
    impulse: float

    def __init__(self) -> None: ...

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
    z: float
    vx: float
    vy: float
    vz: float
    mass: float
    charge: float
    rotation: float
    angular_velocity: float
    qx: float
    qy: float
    qz: float
    qw: float
    spin_x: float
    spin_y: float
    spin_z: float
    width: float
    height: float
    radius: float
    polygon_sides: int
    skin_type: int
    mesh_id: int
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
# MESH REGISTRATION (module-level)
#
# Returns integer handles used by add_object / set_mesh. Meshes are
# reference-counted: each successful Register* call adds a reference, and
# destroy_mesh releases one. Identical primitives share one VBO/IBO.
# ============================================================================

def register_circle(radius: float, segs: int = 32) -> int: ...
def register_square(width: float, height: float) -> int: ...
def register_polygon(radius: float, sides: int = 6) -> int: ...
def register_sphere(radius: float, segs: int = 32, rings: int = 16) -> int: ...
def register_cube(size_x: float, size_y: float, size_z: float) -> int: ...
def register_cylinder(radius: float, height: float, segs: int = 32) -> int: ...
def register_capsule(radius: float, height: float, segs: int = 32, cap_rings: int = 8) -> int: ...
def destroy_mesh(mesh_id: int) -> None: ...
def mesh_count() -> int: ...
def mesh_ids() -> List[int]: ...

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

    def get_paint_image(self, quality: int = 85) -> bytes:
        """Get the paint framebuffer as JPEG bytes."""
        ...

    def get_full_frame(self, quality: int = 85) -> bytes:
        """Get the full rendered frame as JPEG."""
        ...

    def process_input(self) -> None:
        """Update keyboard state for this frame. For default camera controls, call default_input()."""
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

    @overload
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
    ) -> ObjectHandle:
        """
        Add a flat 2D object on the XY plane.

        Use ``skin`` to choose a built-in 2D shape. ``rotation`` and
        ``angular_velocity`` describe rotation about the Z axis.

        Returns:
            ObjectHandle: Stable handle for the new object.
        """
        ...

    @overload
    def add_object(
        self,
        x: float = 0.0,
        y: float = 0.0,
        z: float = 0.0,
        vx: float = 0.0,
        vy: float = 0.0,
        vz: float = 0.0,
        mass: float = 1.0,
        charge: float = 0.0,
        mesh_id: int = -1,
        orientation: Tuple[float, float, float, float] = (0.0, 0.0, 0.0, 1.0),
        spin: Tuple[float, float, float] = (0.0, 0.0, 0.0),
        r: float = 1.0,
        g: float = 1.0,
        b: float = 1.0,
        a: float = 1.0
    ) -> ObjectHandle:
        """
        Add a 3D object using a registered mesh.

        Pass ``z`` and ``mesh_id`` by keyword to select this overload.
        ``orientation`` is a quaternion in (x, y, z, w) order, and
        ``spin`` is an angular-velocity vector.

        ``mesh_id`` defaults to -1, which raises ``ValueError``; you must
        supply a real mesh handle from ``register_sphere`` / ``register_cube``
        / etc. for this overload to succeed.

        The collision shape defaults to ``SPHERE``. Use
        ``set_collision_shape(handle, CollisionShape.BOX)`` after creation
        to switch to box collision.

        Returns:
            ObjectHandle: Stable handle for the new object.

        Example:
            >>> mesh = register_sphere(radius=1.0)
            >>> h = sim.add_object(0, 0, 5, mesh_id=mesh, mass=2.0)
        """
        ...

    def add_object_raw(
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
        DEPRECATED: Use add_object() which returns a stable ObjectHandle.

        Add an object and return a raw integer index (unstable across removals).
        """
        ...

    # ========================================================================
    # MESH / 3D-STATE ACCESSORS
    #
    # These operate on either 2D or 3D objects; the extra z / quaternion /
    # spin arguments are silently stored for 2D objects too (they are simply
    # not read by the 2D simulation path). Defaults for the trailing args
    # mean existing 2D call sites keep working.
    # ========================================================================

    def set_mesh(self, index: int, mesh_id: int) -> None:
        """Swap the mesh assigned to an object. Pass -1 to hide it."""
        ...

    def get_mesh(self, index: int) -> int: ...

    def set_position(self, index: int, x: float, y: float, z: float = 0.0) -> None: ...
    def get_position(self, index: int) -> Tuple[float, float, float]: ...

    def set_velocity(self, index: int, vx: float, vy: float, vz: float = 0.0) -> None: ...
    def get_velocity(self, index: int) -> Tuple[float, float, float]: ...

    def set_orientation(self, index: int, qx: float, qy: float, qz: float, qw: float = 1.0) -> None: ...
    def get_orientation(self, index: int) -> Tuple[float, float, float, float]: ...

    def set_spin(self, index: int, sx: float, sy: float, sz: float = 0.0) -> None: ...
    def get_spin(self, index: int) -> Tuple[float, float, float]: ...

    # ========================================================================
    # UPDATE
    # ========================================================================

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
    ) -> None:
        """
        Update all properties of an existing object.

        Args:
            index (int): Object ID (raw index, or ObjectHandle.slot)
            x, y, vx, vy, mass, charge, rotation, angular_velocity: Updated properties
            size (float): Radius for circles/polygons (ignored for rectangles)
            width, height (float): Dimensions for rectangles (ignored otherwise)
            r, g, b, a (float): Updated color
            polygon_sides (int): Number of sides for polygons (0 = keep existing)
        """
        ...

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

    @overload
    def update_object(
        self,
        handle: ObjectHandle,
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
        polygon_sides: Optional[int] = None,
        z: Optional[float] = None,
        vz: Optional[float] = None,
        qx: Optional[float] = None,
        qy: Optional[float] = None,
        qz: Optional[float] = None,
        qw: Optional[float] = None,
        orientation: Optional[Tuple[float, float, float, float]] = None,
        spin_x: Optional[float] = None,
        spin_y: Optional[float] = None,
        spin_z: Optional[float] = None,
        spin: Optional[Tuple[float, float, float]] = None,
        mesh_id: Optional[int] = None
    ) -> None: ...

    # (int overload with 3D kwargs; kept as a distinct overload for completeness)
    @overload
    def update_object(
        self,
        index: int,
        *,
        z: Optional[float] = None,
        vz: Optional[float] = None,
        qx: Optional[float] = None,
        qy: Optional[float] = None,
        qz: Optional[float] = None,
        qw: Optional[float] = None,
        orientation: Optional[Tuple[float, float, float, float]] = None,
        spin_x: Optional[float] = None,
        spin_y: Optional[float] = None,
        spin_z: Optional[float] = None,
        spin: Optional[Tuple[float, float, float]] = None,
        mesh_id: Optional[int] = None
    ) -> None: ...

    def update_object(self, index: Union[int, ObjectHandle], *args, **kwargs) -> None:
        """
        Update only the properties supplied as keyword arguments.

        For 3D objects, use x/y/z, vx/vy/vz, orientation=(qx, qy, qz, qw),
        spin=(sx, sy, sz), or mesh_id. For 2D objects, use the usual
        position, velocity, shape, color, and rotation properties.

        Example:
            >>> sim.update_object(handle, x=1.0, r=0.5)
            >>> sim.update_object(handle, orientation=(0, 0, 0.707, 0.707))
        """
        ...

    # ========================================================================
    # REMOVE
    # ========================================================================

    def remove_object(self, handle: ObjectHandle) -> None:
        """Remove an object by stable ObjectHandle."""
        ...

    def remove_object_raw(self, index: int) -> None:
        """DEPRECATED: Remove an object by raw index (unstable)."""
        ...

    def remove_objects(self, handles: List[ObjectHandle]) -> None:
        """
        Remove multiple objects in one batch operation.

        Args:
            handles (list[ObjectHandle]): List of handles to remove

        This is more efficient than calling remove_object() repeatedly because
        it handles the swap-and-pop bookkeeping once.
        """
        ...

    def object_count(self) -> int:
        """Get number of objects in simulation."""
        ...

    # ========================================================================
    # OBJECT RETRIEVAL
    # ========================================================================

    @overload
    def get_object(self, index: int) -> ObjectState: ...
    @overload
    def get_object(self, handle: ObjectHandle) -> ObjectState: ...

    def get_object(self, index: Union[int, ObjectHandle]) -> ObjectState:
        """
        Get complete object state.

        Args:
            index (int): Object ID (raw index, or ObjectHandle.slot)

        Returns:
            ObjectState: Complete object state
        """
        ...

    def get_handle(self, index: int) -> ObjectHandle:
        """
        Get the stable ObjectHandle for a raw index.

        Returns the handle for the current object at the given slot.
        If the slot is empty, returns an invalid handle.
        """
        ...

    def is_handle_valid(self, handle: ObjectHandle) -> bool:
        """Check if an ObjectHandle is still valid."""
        ...

    def get_all_objects(self) -> List[ObjectState]:
        """
        Get states for all objects in the simulation.

        Returns:
            list[ObjectState]: Complete state of every object

        Example:
            >>> states = sim.get_all_objects()
            >>> for state in states:
            >>>     print(f"Object at ({state.x:.2f}, {state.y:.2f})")
        """
        ...

    def clear_all(self) -> None:
        """
        Remove all objects and clear all constraints, scripts, and equations.

        This is a "soft reset" that clears the simulation state but keeps
        the OpenGL context and shaders loaded. It does not destroy the window.

        Use this instead of creating a new Simulation object when you want to
        start fresh with the same context.

        Example:
            >>> sim.clear_all()
            >>> # All objects, constraints, and scripts are gone
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

    # ========================================================================
    # EQUATIONS AND SCRIPTS
    # ========================================================================

    @overload
    def set_equation(self, object_index: int, equation_string: str) -> None: ...
    @overload
    def set_equation(self, handle: ObjectHandle, equation_string: str) -> None: ...

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

    @overload
    def set_script(self, object_index: int, script: Union[int, Callable[..., Any]]) -> None: ...
    @overload
    def set_script(self, handle: ObjectHandle, script: Union[int, Callable[..., Any]]) -> None: ...

    def set_script(self, object_index: int, script: Union[int, Callable[..., Any]]) -> None:
        """Assign a JIT script to an object. Accepts a script ID or a decorated function."""
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

    @overload
    def set_collision_enabled(self, index: int, enabled: bool) -> None: ...
    @overload
    def set_collision_enabled(self, handle: ObjectHandle, enabled: bool) -> None: ...

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

    @overload
    def set_collision_shape(self, index: int, shape: CollisionShape) -> None: ...
    @overload
    def set_collision_shape(self, handle: ObjectHandle, shape: CollisionShape) -> None: ...

    def set_collision_shape(self, index: int, shape: CollisionShape) -> None:
        """
        Set collision shape for an object.

        Args:
            index (int): Object ID
            shape (CollisionShape): Collision shape type

        Note: Shape is automatically set based on visual skin when adding 2D
        objects; 3D objects created through add_object default to SPHERE.
        Use this method to switch a 3D object to BOX collision after creation.

        Example:
            >>> sim.set_collision_shape(0, CollisionShape.CIRCLE)
        """
        ...

    @overload
    def set_collision_properties(self, index: int, restitution: float, friction: float) -> None: ...
    @overload
    def set_collision_properties(self, handle: ObjectHandle, restitution: float, friction: float) -> None: ...

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

    @overload
    def set_collision(
        self,
        handle: ObjectHandle,
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

    @overload
    def get_collision_config(self, index: int) -> CollisionConfig: ...
    @overload
    def get_collision_config(self, handle: ObjectHandle) -> CollisionConfig: ...

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

    @overload
    def set_collision_layer(self, index: int, layer: int) -> None: ...
    @overload
    def set_collision_layer(self, handle: ObjectHandle, layer: int) -> None: ...

    def set_collision_layer(self, index: int, layer: int) -> None:
        """
        Bitmask of layers this object belongs to (default 0x01).

        Two objects collide only if each one's layer overlaps the other's mask:
            (A.layer & B.mask) != 0 and (B.layer & A.mask) != 0

        Example:
            >>> sim.set_collision_layer(player_handle, 0x01)  # layer 0
            >>> sim.set_collision_layer(enemy_handle,  0x02)  # layer 1
        """
        ...

    @overload
    def set_collision_mask(self, index: int, mask: int) -> None: ...
    @overload
    def set_collision_mask(self, handle: ObjectHandle, mask: int) -> None: ...

    def set_collision_mask(self, index: int, mask: int) -> None:
        """
        Bitmask of layers this object collides with (default 0xFFFFFFFF).

        Example:
            >>> sim.set_collision_mask(handle, 0x03)  # collide with layers 0 and 1
        """
        ...

    @overload
    def get_collision_layer(self, index: int) -> int: ...
    @overload
    def get_collision_layer(self, handle: ObjectHandle) -> int: ...
    def get_collision_layer(self, index: int) -> int: ...

    @overload
    def get_collision_mask(self, index: int) -> int: ...
    @overload
    def get_collision_mask(self, handle: ObjectHandle) -> int: ...
    def get_collision_mask(self, index: int) -> int: ...

    @overload
    def is_collision_enabled(self, index: int) -> bool: ...
    @overload
    def is_collision_enabled(self, handle: ObjectHandle) -> bool: ...

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

    def set_collision_callback(self, callback: Optional[Callable[[CollisionEvent], None]]) -> None:
        """
        Set a callback that is invoked when a collision occurs.

        Args:
            callback: A function that takes a CollisionEvent object, or None to clear.

        Example:
            >>> def on_collision(event):
            >>>     print(f"Collision! {event.object_a} hit {event.object_b}")
            >>> sim.set_collision_callback(on_collision)
        """
        ...

    def clear_collision_callback(self) -> None:
        """Remove the collision callback."""
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

    def set_key_state(self, key: int, pressed: bool) -> None:
        """Sets the keyboard state without having to press on the keyboard."""
        ...

    # ========================================================================
    # SCREEN ↔ WORLD COORDINATE HELPERS
    # ========================================================================

    def screen_to_world(self, screen_x: float, screen_y: float) -> Tuple[float, float]:
        """
        Convert screen pixel coordinates to world coordinates.

        Args:
            screen_x (float): X coordinate in pixels (from left)
            screen_y (float): Y coordinate in pixels (from top)

        Returns:
            tuple[float, float]: World coordinates (x, y)

        Example:
            >>> world_x, world_y = sim.screen_to_world(mouse_x, mouse_y)
            >>> obj = sim.add_object(x=world_x, y=world_y)
        """
        ...

    def world_to_screen(self, world_x: float, world_y: float) -> Tuple[float, float]:
        """
        Convert world coordinates to screen pixel coordinates.

        Args:
            world_x (float): X coordinate in world space
            world_y (float): Y coordinate in world space

        Returns:
            tuple[float, float]: Screen coordinates in pixels (from left, from top)
        """
        ...

    # ========================================================================
    # CAMERA CONVENIENCE
    # ========================================================================

    def fit_camera_to_objects(self, padding: float = 0.2) -> None:
        """
        Auto-frame the camera to fit all objects in the view.

        Args:
            padding (float): Extra padding around the bounding box (as a fraction)

        Example:
            >>> # After adding objects, frame them all
            >>> sim.fit_camera_to_objects()
        """
        ...

    @overload
    def follow_object(self, index: int, smoothing: float = 0.05) -> None: ...
    @overload
    def follow_object(self, handle: ObjectHandle, smoothing: float = 0.05) -> None: ...

    def follow_object(self, index: int, smoothing: float = 0.05) -> None:
        """
        Smoothly move the camera to follow an object.

        Args:
            index (int): Object ID
            smoothing (float): Interpolation factor (0.0 = instant, 1.0 = never move)

        Example:
            >>> sim.follow_object(obj_handle, smoothing=0.02)
            >>> # Camera will smoothly follow the object
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

    @property
    def width(self) -> int:
        """Current window/framebuffer width."""
        ...

    @property
    def height(self) -> int:
        """Current window/framebuffer height."""
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

    def get_scratchpad_ids(self) -> List[int]:
        """
        Get a list of all active scratchpad IDs.

        Returns:
            list[int]: All valid scratchpad IDs currently allocated

        Example:
            >>> ids = sim.get_scratchpad_ids()
            >>> for sid in ids:
            >>>     print(f"Scratchpad {sid} has {sim.scratchpad_size(sid)} elements")
        """
        ...

    # ========================================================================
    # SIGNAL QUEUE
    # ========================================================================

    def set_signal_queue_capacity(self, capacity: int) -> None:
        """Set the maximum number of pending signals."""
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

    def unregister_agent(self, agent_id: int) -> None:
        """
        Unregister an agent, removing it from the engine.

        Args:
            agent_id (int): Agent ID to remove

        The agent will no longer be dispatched, and its shader resources
        will be freed. Objects that reference this agent will no longer
        have a valid script.
        """
        ...

    def clear_agents(self) -> None:
        """
        Unregister all agents.

        This removes all registered agents and frees their resources.
        All objects with scripts will have their script IDs reset to -1.
        """
        ...

    # ========================================================================
    # PYTHON-LEVEL EXTENSIONS
    # (attached at package import time by hyperstellar/__init__.py)
    # ========================================================================

    def script(self, mode: str = "object", debug: bool = False) -> Callable[[Callable[..., Any]], Callable[..., Any]]:
        """
        Decorator that compiles a Python function into a GPU compute shader
        and registers it with this simulation.

        Modes: "object", "paint", "agent".
        """
        ...

    def duplicate_object(self, obj: Union[int, ObjectHandle], **overrides: Any) -> ObjectHandle:
        """
        Duplicate an existing object with optional property overrides.

        Args:
            obj: Object ID (raw index or ObjectHandle)
            **overrides: Any property to override (x, y, vx, vy, mass, etc.)

        Returns:
            ObjectHandle: Handle to the new object
        """
        ...

    @property
    def tags(self) -> Any:
        """Python-side object tagging system (see _ObjectTags)."""
        ...

    def remove_tagged(self, tag: str) -> None:
        """Remove all objects that have been tagged with `tag`."""
        ...

    def set_equation_named(self, obj: Union[int, ObjectHandle], preset_name: str, **kwargs: Any) -> None:
        """
        Set a physics equation using a named preset.

        Args:
            obj: Object ID or ObjectHandle
            preset_name (str): "gravity", "spring", "damped_spring", or "orbit"
            **kwargs: Parameters to substitute. For orbit, pass `target=other_handle`.
        """
        ...

# ============================================================================
# MODULE-LEVEL EXTENSIONS
# (defined in hyperstellar/__init__.py)
# ============================================================================

class EquationPresets:
    """Named equation-preset generators."""

    @staticmethod
    def gravity(G: float = 1.0, eps: float = 0.001) -> str: ...

    @staticmethod
    def spring(k: float = 1.0) -> str: ...

    @staticmethod
    def damped_spring(k: float = 1.0, damping: float = 0.1) -> str: ...

    @staticmethod
    def orbit(G: float = 1.0, eps: float = 0.001) -> str: ...

def script(
    sim: "Simulation",
    debug: bool = False,
    mode: str = "object",
) -> Callable[[Callable[..., Any]], Callable[..., Any]]:
    """
    Module-level script decorator (equivalent to @sim.script).

    Example:
        >>> @script(sim)
        >>> def my_physics():
        >>>     ax = -x; ay = -y
    """
    ...

# Version information
__version__: str