# Type stubs for hyperstellar package
import typing
from typing import Any, List, Dict, Optional, Union, Callable, ClassVar, Tuple

class SkinType:
    """Visual representation type for objects."""
    CIRCLE: int
    RECTANGLE: int
    POLYGON: int

class PyCollisionShape:
    """Collision shape type for physics objects."""
    NONE: int       # No collision
    CIRCLE: int     # Circular collision shape
    AABB: int       # Axis-aligned bounding box
    POLYGON: int    # Polygon collision shape

class ConstraintType:
    """Type of physics constraint."""
    DISTANCE: int   # Distance constraint between objects
    BOUNDARY: int   # Boundary/box constraint

class ObjectState:
    """Complete state of a physics object."""
    x: float                # X position
    y: float                # Y position
    vx: float               # X velocity
    vy: float               # Y velocity
    mass: float             # Object mass
    charge: float           # Electrical charge
    rotation: float         # Rotation angle (radians)
    angular_velocity: float # Angular velocity (radians/second)
    width: float            # Width (for rectangles) or diameter (for circles/polygons)
    height: float           # Height (for rectangles) or diameter (for circles/polygons)
    radius: float           # Radius (for circles/polygons)
    polygon_sides: int      # Number of sides (for polygons, 0 for other shapes)
    skin_type: SkinType     # Visual skin type
    r: float                # Red color component (0.0-1.0)
    g: float                # Green color component (0.0-1.0)
    b: float                # Blue color component (0.0-1.0)
    a: float                # Alpha/transparency component (0.0-1.0)
    
    def __init__(self) -> None: ...
    def __repr__(self) -> str: ...

class CollisionConfig:
    """Collision configuration for an object."""
    enabled: bool               # Whether collisions are enabled
    shape: PyCollisionShape     # Collision shape type
    restitution: float          # Bounciness (0.0-1.0)
    friction: float             # Friction coefficient (0.0-1.0)
    
    def __init__(self) -> None: ...
    def __repr__(self) -> str: ...

class ObjectConfig:
    """Configuration for creating objects in batch mode."""
    x: float                    # Initial X position
    y: float                    # Initial Y position
    vx: float                   # Initial X velocity
    vy: float                   # Initial Y velocity
    mass: float                 # Object mass
    charge: float               # Electrical charge
    rotation: float             # Initial rotation
    angular_velocity: float     # Initial angular velocity
    skin: SkinType              # Visual skin type
    size: float                 # Size parameter (radius for circles/polygons)
    width: float                # Width (for rectangles)
    height: float               # Height (for rectangles)
    r: float                    # Red color (0.0-1.0)
    g: float                    # Green color (0.0-1.0)
    b: float                    # Blue color (0.0-1.0)
    a: float                    # Alpha/transparency (0.0-1.0)
    polygon_sides: int          # Number of sides (for polygons)
    equation: str               # Physics equation string
    constraints: List[ConstraintConfig]  # List of constraints
    
    def __init__(self) -> None: ...
    def __repr__(self) -> str: ...

class ConstraintConfig:
    """Constraint configuration for batch mode."""
    type: ConstraintType    # Type of constraint
    target: int             # Target object index (for distance constraints)
    param1: float           # Parameter 1 (distance, min_x, etc.)
    param2: float           # Parameter 2 (max_x, etc.)
    param3: float           # Parameter 3 (min_y, etc.)
    param4: float           # Parameter 4 (max_y, etc.)
    
    def __init__(self) -> None: ...

class BatchConfig:
    """Configuration for batch simulations."""
    objects: List[ObjectConfig]    # List of objects to create
    duration: float                # Simulation duration (seconds)
    dt: float                      # Time step (seconds)
    output_file: str               # Optional output file path
    
    def __init__(self) -> None: ...

# Batch data structures
class BatchGetData:
    """Batch get data structure for fetching multiple objects at once."""
    x: float                # X position
    y: float                # Y position
    vx: float               # X velocity
    vy: float               # Y velocity
    mass: float             # Object mass
    charge: float           # Electrical charge
    rotation: float         # Rotation angle
    angular_velocity: float # Angular velocity
    width: float            # Width/diameter
    height: float           # Height/diameter
    radius: float           # Radius
    polygon_sides: int      # Polygon sides (0 for other shapes)
    skin_type: int          # Skin type as integer
    r: float                # Red color
    g: float                # Green color
    b: float                # Blue color
    a: float                # Alpha/transparency
    
    def __init__(self) -> None: ...

class BatchUpdateData:
    """Batch update data structure for updating multiple objects at once."""
    index: int              # Object index to update
    x: float                # New X position
    y: float                # New Y position
    vx: float               # New X velocity
    vy: float               # New Y velocity
    mass: float             # New mass
    charge: float           # New charge
    rotation: float         # New rotation
    angular_velocity: float # New angular velocity
    width: float            # New width/radius
    height: float           # New height (for rectangles)
    r: float                # New red color
    g: float                # New green color
    b: float                # New blue color
    a: float                # New alpha/transparency
    
    def __init__(self) -> None: ...

class DistanceConstraint:
    """Maintain distance between two objects."""
    target_object: int      # Index of target object
    rest_length: float      # Desired distance between objects
    stiffness: float        # Constraint stiffness
    
    def __init__(self, target_object: int = 0, rest_length: float = 5.0, stiffness: float = 100.0) -> None: ...
    def __repr__(self) -> str: ...

class BoundaryConstraint:
    """Keep object within a rectangular boundary."""
    min_x: float            # Minimum X boundary
    max_x: float            # Maximum X boundary
    min_y: float            # Minimum Y boundary
    max_y: float            # Maximum Y boundary
    
    def __init__(self, min_x: float = -10.0, max_x: float = 10.0, min_y: float = -10.0, max_y: float = 10.0) -> None: ...
    def __repr__(self) -> str: ...

class Simulation:
    """Main physics simulation class."""
    
    def __init__(self, 
                 headless: bool = True, 
                 width: int = 1280, 
                 height: int = 720, 
                 title: str = "Physics Simulation",
                 enable_grid: bool = True) -> None:
        """
        Initialize physics simulation.
        
        Args:
            headless: Run without graphics window (for batch processing)
            width: Window width in pixels (ignored in headless mode)
            height: Window height in pixels (ignored in headless mode)
            title: Window title (ignored in headless mode)
            enable_grid: Enable coordinate grid display
        """
        ...
    
    # ========================================================================
    # WINDOW MANAGEMENT
    # ========================================================================
    
    def render(self) -> None:
        """Render the current simulation state to screen (non-headless only)."""
        ...
    
    def process_input(self) -> None:
        """Process user input (keyboard, mouse) for camera control."""
        ...
    
    def should_close(self) -> bool:
        """
        Check if window should close.
        
        Returns:
            True if window close requested, False otherwise
        """
        ...
    
    # ========================================================================
    # GRID CONTROL
    # ========================================================================
    
    def set_grid_enabled(self, enabled: bool) -> None:
        """
        Enable or disable coordinate grid display.
        
        Args:
            enabled: True to show grid, False to hide
        """
        ...
    
    def get_grid_enabled(self) -> bool:
        """
        Check if grid is enabled.
        
        Returns:
            True if grid is visible, False otherwise
        """
        ...
    
    # ========================================================================
    # CORE SIMULATION
    # ========================================================================
    
    def update(self, dt: float = 0.016) -> None:
        """
        Update simulation physics.
        
        Args:
            dt: Time step in seconds (default: 1/60th = 0.016)
        """
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
        Add a new physics object to the simulation.
        
        Args:
            x, y: Initial position
            vx, vy: Initial velocity
            mass: Object mass
            charge: Electrical charge
            rotation: Initial rotation angle (radians)
            angular_velocity: Initial angular velocity (radians/second)
            skin: Visual appearance type
            size: Radius for circles/polygons (ignored for rectangles)
            width: Width for rectangles, diameter for circles/polygons
            height: Height for rectangles (ignored for circles/polygons)
            r, g, b, a: Color components (0.0-1.0)
            polygon_sides: Number of sides for polygons (3-12)
            
        Returns:
            Index of the newly created object
            
        Note:
            Collision shape is automatically assigned based on skin type:
            - CIRCLE → PyCollisionShape.CIRCLE
            - RECTANGLE → PyCollisionShape.AABB
            - POLYGON → PyCollisionShape.POLYGON
            Default collision properties: restitution=0.7, friction=0.3
        """
        ...
    
    def update_object(
        self,
        index: int,
        x: float, y: float,
        vx: float, vy: float,
        mass: float, charge: float,
        rotation: float, angular_velocity: float,
        width: float, height: float,
        r: float, g: float, b: float, a: float
    ) -> None:
        """
        Update properties of an existing object.
        
        Args:
            index: Object index to update
            x, y: New position
            vx, vy: New velocity
            mass: New mass
            charge: New charge
            rotation: New rotation angle
            angular_velocity: New angular velocity
            width: New width/radius (interpretation depends on skin type)
            height: New height (for rectangles only)
            r, g, b, a: New color components
        """
        ...
    
    # ========================================================================
    # BATCH OPERATIONS
    # ========================================================================
    
    def batch_get(self, indices: List[int]) -> List[BatchGetData]:
        """
        Efficiently fetch multiple objects' states at once.
        
        Args:
            indices: List of object indices to fetch
            
        Returns:
            List of BatchGetData objects containing object states
            
        Raises:
            RuntimeError: If any index is invalid
        """
        ...
    
    def batch_update(self, updates: List[BatchUpdateData]) -> None:
        """
        Efficiently update multiple objects at once.
        
        Args:
            updates: List of BatchUpdateData objects with update information
            
        Raises:
            RuntimeError: If any object index is invalid
        """
        ...
    
    def remove_object(self, index: int) -> None:
        """
        Remove an object from the simulation.
        
        Args:
            index: Index of object to remove
            
        Raises:
            RuntimeError: If index is invalid
        """
        ...
    
    def object_count(self) -> int:
        """
        Get current number of objects in simulation.
        
        Returns:
            Number of active objects
        """
        ...
    
    def get_object(self, index: int) -> ObjectState:
        """
        Get complete state of a specific object.
        
        Args:
            index: Object index
            
        Returns:
            ObjectState containing all object properties
            
        Raises:
            RuntimeError: If index is invalid
        """
        ...
    
    # ========================================================================
    # CONVENIENCE METHODS
    # ========================================================================
    
    def set_rotation(self, index: int, rotation: float) -> None:
        """
        Set rotation angle of an object.
        
        Args:
            index: Object index
            rotation: New rotation angle in radians
        """
        ...
    
    def set_angular_velocity(self, index: int, angular_velocity: float) -> None:
        """
        Set angular velocity of an object.
        
        Args:
            index: Object index
            angular_velocity: New angular velocity in radians/second
        """
        ...
    
    def set_dimensions(self, index: int, width: float, height: float) -> None:
        """
        Set dimensions of an object.
        
        Args:
            index: Object index
            width: New width (radius for circles/polygons, width for rectangles)
            height: New height (for rectangles only, ignored for circles/polygons)
        """
        ...
    
    def set_radius(self, index: int, radius: float) -> None:
        """
        Set radius of an object.
        
        Args:
            index: Object index
            radius: New radius (for circles/polygons, converts rectangles to squares)
        """
        ...
    
    def get_rotation(self, index: int) -> float:
        """
        Get current rotation angle of an object.
        
        Args:
            index: Object index
            
        Returns:
            Current rotation angle in radians
        """
        ...
    
    def get_angular_velocity(self, index: int) -> float:
        """
        Get current angular velocity of an object.
        
        Args:
            index: Object index
            
        Returns:
            Current angular velocity in radians/second
        """
        ...
    
    # ========================================================================
    # COLLISION SYSTEM
    # ========================================================================
    
    def set_collision_enabled(self, index: int, enabled: bool) -> None:
        """
        Enable or disable collisions for an object.
        
        Args:
            index: Object index
            enabled: True to enable collisions, False to disable
            
        Raises:
            RuntimeError: If index is invalid
        """
        ...
    
    def set_collision_shape(self, index: int, shape: PyCollisionShape) -> None:
        """
        Set collision shape for an object.
        
        Args:
            index: Object index
            shape: Collision shape type (NONE, CIRCLE, AABB, or POLYGON)
            
        Raises:
            RuntimeError: If index is invalid
        """
        ...
    
    def set_collision_properties(self, index: int, restitution: float, friction: float) -> None:
        """
        Set collision material properties.
        
        Args:
            index: Object index
            restitution: Bounciness coefficient (0.0-1.0)
            friction: Friction coefficient (0.0-1.0)
            
        Raises:
            RuntimeError: If index is invalid or parameters are out of range
        """
        ...
    
    def get_collision_config(self, index: int) -> CollisionConfig:
        """
        Get current collision configuration for an object.
        
        Args:
            index: Object index
            
        Returns:
            CollisionConfig with current collision settings
            
        Raises:
            RuntimeError: If index is invalid
        """
        ...
    
    def enable_collision_between(self, obj1: int, obj2: int, enable: bool) -> None:
        """
        Enable or disable collisions between two specific objects.
        
        Args:
            obj1: First object index
            obj2: Second object index
            enable: True to enable collisions between them, False to disable
            
        Raises:
            RuntimeError: If either index is invalid
        """
        ...
    
    def is_collision_enabled(self, index: int) -> bool:
        """
        Check if collisions are enabled for an object.
        
        Args:
            index: Object index
            
        Returns:
            True if collisions are enabled, False otherwise
            
        Raises:
            RuntimeError: If index is invalid
        """
        ...
    
    # ========================================================================
    # NEW: COLLISION PARAMETERS
    # ========================================================================
    
    def set_collision_parameters(self, enable_warm_start: bool, max_contact_iterations: int) -> None:
        """
        Set global collision solver parameters.
        
        Args:
            enable_warm_start: Enable warm starting for contact constraints (improves stability)
            max_contact_iterations: Maximum number of contact resolution iterations (1-20)
            
        Raises:
            RuntimeError: If parameters are out of valid range
        """
        ...
    
    def get_collision_parameters(self) -> Tuple[bool, int]:
        """
        Get current global collision solver parameters.
        
        Returns:
            Tuple of (enable_warm_start, max_contact_iterations)
        """
        ...
    
    # ========================================================================
    # EQUATIONS
    # ========================================================================
    
    def set_equation(self, object_index: int, equation_string: str) -> None:
        """
        Set physics equation for an object.
        
        Args:
            object_index: Index of object to apply equation to
            equation_string: Mathematical equation defining object's physics
            
        Raises:
            RuntimeError: If object index is invalid or equation parsing fails
        """
        ...
    
    # ========================================================================
    # CONSTRAINTS
    # ========================================================================
    
    def add_distance_constraint(self, object_index: int, constraint: DistanceConstraint) -> None:
        """
        Add distance constraint between two objects.
        
        Args:
            object_index: Index of first object
            constraint: DistanceConstraint defining target and rest length
            
        Raises:
            RuntimeError: If object indices are invalid or constraint is invalid
        """
        ...
    
    def add_boundary_constraint(self, object_index: int, constraint: BoundaryConstraint) -> None:
        """
        Add boundary constraint to keep object within a rectangular area.
        
        Args:
            object_index: Index of object to constrain
            constraint: BoundaryConstraint defining bounds
            
        Raises:
            RuntimeError: If object index is invalid or bounds are invalid
        """
        ...
    
    def clear_constraints(self, object_index: int) -> None:
        """
        Clear all constraints from an object.
        
        Args:
            object_index: Index of object to clear constraints from
            
        Raises:
            RuntimeError: If object index is invalid
        """
        ...
    
    def clear_all_constraints(self) -> None:
        """Clear all constraints from all objects."""
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
        Run multiple simulations in batch mode (headless only).
        
        Args:
            configs: List of BatchConfig objects defining simulations to run
            callback: Optional function called after each simulation completes
        
        Raises:
            RuntimeError: If not in headless mode
        """
        ...
    
    # ========================================================================
    # PARAMETERS
    # ========================================================================
    
    def set_parameter(self, name: str, value: float) -> None:
        """
        Set global physics parameter.
        
        Args:
            name: Parameter name ("gravity", "damping", or "stiffness")
            value: New parameter value
            
        Raises:
            RuntimeError: If parameter name is unknown
        """
        ...
    
    def get_parameter(self, name: str) -> float:
        """
        Get current value of a global physics parameter.
        
        Args:
            name: Parameter name ("gravity", "damping", or "stiffness")
            
        Returns:
            Current parameter value
            
        Raises:
            RuntimeError: If parameter name is unknown
        """
        ...
    
    # ========================================================================
    # SIMULATION CONTROL
    # ========================================================================
    
    def set_paused(self, paused: bool) -> None:
        """
        Pause or resume simulation.
        
        Args:
            paused: True to pause, False to resume
        """
        ...
    
    def is_paused(self) -> bool:
        """
        Check if simulation is paused.
        
        Returns:
            True if simulation is paused, False otherwise
        """
        ...
    
    def update_shader_loading(self) -> None:
        """Update shader loading status (for async shader compilation)."""
        ...
    
    def are_all_shaders_ready(self) -> bool:
        """
        Check if all shaders are loaded and ready.
        
        Returns:
            True if shaders are ready, False otherwise
        """
        ...
    
    def get_shader_load_progress(self) -> float:
        """
        Get overall shader loading progress.
        
        Returns:
            Progress from 0.0 (not started) to 1.0 (complete)
        """
        ...
    
    def get_shader_load_status(self) -> str:
        """
        Get human-readable shader loading status.
        
        Returns:
            Status message string
        """
        ...
    
    def reset(self) -> None:
        """Reset simulation to initial state."""
        ...
    
    def cleanup(self) -> None:
        """Clean up all simulation resources."""
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
        Save simulation state to file.
        
        Args:
            filename: Path to save file
            title: Optional simulation title
            author: Optional author name
            description: Optional description text
        """
        ...
    
    def load_from_file(self, filename: str) -> None:
        """
        Load simulation state from file.
        
        Args:
            filename: Path to load file
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
        """Check if simulation is initialized and ready."""
        ...

# ========================================================================
# MODULE-LEVEL EXPORTS
# ========================================================================

# Skin type constants (accessible as hyperstellar.SkinType.CIRCLE, etc.)
class PySkinType:
    """Python wrapper for skin type constants."""
    PY_SKIN_CIRCLE: ClassVar[int]    # Circular visual representation
    PY_SKIN_RECTANGLE: ClassVar[int] # Rectangular visual representation
    PY_SKIN_POLYGON: ClassVar[int]   # Polygonal visual representation

# Collision shape constants (accessible as hyperstellar.PyCollisionShape.CIRCLE, etc.)
class PyCollisionShape:
    """Python wrapper for collision shape constants."""
    NONE: ClassVar[int]    # No collision
    CIRCLE: ClassVar[int]  # Circular collision
    AABB: ClassVar[int]    # Axis-aligned bounding box
    POLYGON: ClassVar[int] # Polygonal collision

# Version information
__version__: str