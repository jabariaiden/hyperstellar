# Type stubs for hyperstellar package
import typing
from typing import Any, List, Dict, Optional, Union, Callable

class SkinType:
    """Visual representation type for objects."""
    CIRCLE: int
    RECTANGLE: int
    POLYGON: int

class ConstraintType:
    """Type of physics constraint."""
    DISTANCE: int
    BOUNDARY: int

class ObjectState:
    """Complete state of a physics object."""
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

class ObjectConfig:
    """Configuration for creating objects in batch mode."""
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
    
    def __init__(self) -> None: ...
    def __repr__(self) -> str: ...

class ConstraintConfig:
    """Constraint configuration for batch mode."""
    type: ConstraintType
    target: int
    param1: float
    param2: float
    param3: float
    param4: float
    
    def __init__(self) -> None: ...

class BatchConfig:
    """Configuration for batch simulations."""
    objects: List[ObjectConfig]
    duration: float
    dt: float
    output_file: str
    
    def __init__(self) -> None: ...

class DistanceConstraint:
    """Maintain distance between two objects."""
    target_object: int
    rest_length: float
    stiffness: float
    
    def __init__(self, target_object: int = 0, rest_length: float = 5.0, stiffness: float = 100.0) -> None: ...
    def __repr__(self) -> str: ...

class BoundaryConstraint:
    """Keep object within a rectangular boundary."""
    min_x: float
    max_x: float
    min_y: float
    max_y: float
    
    def __init__(self, min_x: float = -10.0, max_x: float = 10.0, min_y: float = -10.0, max_y: float = 10.0) -> None: ...
    def __repr__(self) -> str: ...

class Simulation:
    """Main physics simulation class."""
    
    def __init__(self, headless: bool = True, width: int = 1280, height: int = 720, title: str = "Physics Simulation") -> None: ...
    
    # Window management
    def render(self) -> None: ...
    def process_input(self) -> None: ...
    def should_close(self) -> bool: ...
    
    # Core simulation
    def update(self, dt: float = 0.016) -> None: ...
    
    # Object management
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
    ) -> int: ...
    
    def update_object(
        self,
        index: int,
        x: float, y: float,
        vx: float, vy: float,
        mass: float, charge: float,
        rotation: float, angular_velocity: float,
        width: float, height: float,
        r: float, g: float, b: float, a: float
    ) -> None: ...
    
    def remove_object(self, index: int) -> None: ...
    def object_count(self) -> int: ...
    def get_object(self, index: int) -> ObjectState: ...
    
    # Convenience methods
    def set_rotation(self, index: int, rotation: float) -> None: ...
    def set_angular_velocity(self, index: int, angular_velocity: float) -> None: ...
    def set_dimensions(self, index: int, width: float, height: float) -> None: ...
    def set_radius(self, index: int, radius: float) -> None: ...
    def get_rotation(self, index: int) -> float: ...
    def get_angular_velocity(self, index: int) -> float: ...
    
    # Equations
    def set_equation(self, object_index: int, equation_string: str) -> None: ...
    
    # Constraints
    def add_distance_constraint(self, object_index: int, constraint: DistanceConstraint) -> None: ...
    def add_boundary_constraint(self, object_index: int, constraint: BoundaryConstraint) -> None: ...
    def clear_constraints(self, object_index: int) -> None: ...
    def clear_all_constraints(self) -> None: ...
    
    # Batch processing
    def run_batch(
        self,
        configs: List[BatchConfig],
        callback: Optional[Callable[[int, List[ObjectState]], None]] = None
    ) -> None: ...
    
    # Parameters
    def set_parameter(self, name: str, value: float) -> None: ...
    def get_parameter(self, name: str) -> float: ...
    
    # Simulation control
    def set_paused(self, paused: bool) -> None: ...
    def is_paused(self) -> bool: ...
    
    def update_shader_loading(self) -> None: ...
    def are_all_shaders_ready(self) -> bool: ...
    def get_shader_load_progress(self) -> float: ...
    def get_shader_load_status(self) -> str: ...
    
    def reset(self) -> None: ...
    def cleanup(self) -> None: ...
    
    # File I/O
    def save_to_file(
        self,
        filename: str,
        title: str = "",
        author: str = "",
        description: str = ""
    ) -> None: ...
    
    def load_from_file(self, filename: str) -> None: ...
    
    # Properties
    @property
    def is_headless(self) -> bool: ...
    
    @property
    def is_initialized(self) -> bool: ...

# Module-level exports
__version__: str