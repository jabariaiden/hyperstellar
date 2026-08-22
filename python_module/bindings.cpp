#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include <pybind11/gil.h>
#include <string>
#include <vector>
#include <unordered_map>
#include "simulation_wrapper.h"

namespace py = pybind11;

PYBIND11_MODULE(stellar, m)
{
    m.doc() = R"pbdoc(
        Stellar Physics Engine - GPU-accelerated physics with OpenGL
        
        This module provides real-time physics simulation with GPU acceleration.
        Features include:
        - Real-time physics with OpenGL rendering
        - Custom physics equations
        - Batch processing for headless simulations
        - Multiple constraint types
        - Object rotation and angular dynamics
        - Complete collision system (Circle, AABB, SAT)
        - Stable object handles with generation counting
        - Collision callbacks
        - Object tagging and groups
        - Pythonic iteration and context management
        
        Example:
            >>> import stellar
            >>> with stellar.Simulation(headless=True) as sim:
            >>>     obj = sim.add_object(x=0, y=0, mass=1.0)
            >>>     sim.set_equation(obj, "0.1*mass*(target.x - x)/distance^3")
            >>>     for _ in range(100):
            >>>         sim.update(dt=0.016)
    )pbdoc";

    // =========================================================================
    // ENUMS
    // =========================================================================
    py::enum_<PySkinType>(m, "SkinType", R"pbdoc(
        Visual representation type for objects.
        
        Attributes:
            CIRCLE: Circular object (default)
            RECTANGLE: Rectangular object
            POLYGON: Regular polygon with n sides
        )pbdoc")
        .value("CIRCLE", PySkinType::PY_SKIN_CIRCLE, "Circular object")
        .value("RECTANGLE", PySkinType::PY_SKIN_RECTANGLE, "Rectangular object")
        .value("POLYGON", PySkinType::PY_SKIN_POLYGON, "Regular polygon")
        .export_values();

    py::enum_<PyConstraintType>(m, "ConstraintType", R"pbdoc(
        Type of physics constraint.
        
        Attributes:
            DISTANCE: Maintain distance between objects
            BOUNDARY: Keep object within boundary box
        )pbdoc")
        .value("DISTANCE", PyConstraintType::PY_CONSTRAINT_DISTANCE, "Distance constraint")
        .value("BOUNDARY", PyConstraintType::PY_CONSTRAINT_BOUNDARY, "Boundary constraint")
        .export_values();

    py::enum_<PyCollisionShape>(m, "CollisionShape", R"pbdoc(
        Collision shape type for physics interactions.
        
        Attributes:
            NONE: No collision detection
            CIRCLE: Circular collision boundary
            AABB: Axis-aligned bounding box (rectangle)
            POLYGON: Convex polygon collision (uses SAT)
        )pbdoc")
        .value("NONE", PyCollisionShape::NONE, "No collision")
        .value("CIRCLE", PyCollisionShape::CIRCLE, "Circle collision")
        .value("AABB", PyCollisionShape::AABB, "Rectangle AABB collision")
        .value("POLYGON", PyCollisionShape::POLYGON, "Polygon SAT collision")
        .export_values();

    // =========================================================================
    // OBJECT STATE
    // =========================================================================
    py::class_<ObjectState>(m, "ObjectState", R"pbdoc(
        Complete state of a physics object.
        
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
        )pbdoc")
        .def(py::init<>(), "Create default object state")
        .def_readwrite("x", &ObjectState::x, "X position")
        .def_readwrite("y", &ObjectState::y, "Y position")
        .def_readwrite("vx", &ObjectState::vx, "X velocity")
        .def_readwrite("vy", &ObjectState::vy, "Y velocity")
        .def_readwrite("mass", &ObjectState::mass, "Mass")
        .def_readwrite("charge", &ObjectState::charge, "Electric charge")
        .def_readwrite("rotation", &ObjectState::rotation, "Rotation angle (radians)")
        .def_readwrite("angular_velocity", &ObjectState::angular_velocity, "Angular velocity (rad/s)")
        .def_readwrite("width", &ObjectState::width, "Width (for rectangles)")
        .def_readwrite("height", &ObjectState::height, "Height (for rectangles)")
        .def_readwrite("radius", &ObjectState::radius, "Radius (for circles/polygons)")
        .def_readwrite("polygon_sides", &ObjectState::polygon_sides, "Number of polygon sides")
        .def_readwrite("skin_type", &ObjectState::skin_type, "Visual type (SkinType)")
        .def_readwrite("r", &ObjectState::r, "Red color component (0.0-1.0)")
        .def_readwrite("g", &ObjectState::g, "Green color component (0.0-1.0)")
        .def_readwrite("b", &ObjectState::b, "Blue color component (0.0-1.0)")
        .def_readwrite("a", &ObjectState::a, "Alpha/opacity (0.0-1.0)")
        .def("__repr__", [](const ObjectState &p)
             { return "<ObjectState pos=(" + std::to_string(p.x) + ", " +
                      std::to_string(p.y) + ") vel=(" + std::to_string(p.vx) +
                      ", " + std::to_string(p.vy) + ") mass=" + std::to_string(p.mass) + ">"; });

    // =========================================================================
    // OBJECT CONFIG (for batch mode)
    // =========================================================================
    py::class_<ObjectConfig>(m, "ObjectConfig", R"pbdoc(
        Configuration for creating objects in batch mode.
        
        Attributes:
            x,y,vx,vy,mass,charge,rotation,angular_velocity: Same as ObjectState
            skin (SkinType): Visual type
            size (float): General size parameter
            width,height (float): Dimensions for rectangles
            r,g,b,a (float): Color
            polygon_sides (int): Polygon sides
            equation (str): Physics equation string
        )pbdoc")
        .def(py::init<>(), "Create default object config")
        .def_readwrite("x", &ObjectConfig::x, "X position")
        .def_readwrite("y", &ObjectConfig::y, "Y position")
        .def_readwrite("vx", &ObjectConfig::vx, "X velocity")
        .def_readwrite("vy", &ObjectConfig::vy, "Y velocity")
        .def_readwrite("mass", &ObjectConfig::mass, "Mass")
        .def_readwrite("charge", &ObjectConfig::charge, "Electric charge")
        .def_readwrite("rotation", &ObjectConfig::rotation, "Rotation angle")
        .def_readwrite("angular_velocity", &ObjectConfig::angular_velocity, "Angular velocity")
        .def_readwrite("skin", &ObjectConfig::skin, "Visual type")
        .def_readwrite("size", &ObjectConfig::size, "General size")
        .def_readwrite("width", &ObjectConfig::width, "Width (for rectangles)")
        .def_readwrite("height", &ObjectConfig::height, "Height (for rectangles)")
        .def_readwrite("r", &ObjectConfig::r, "Red color")
        .def_readwrite("g", &ObjectConfig::g, "Green color")
        .def_readwrite("b", &ObjectConfig::b, "Blue color")
        .def_readwrite("a", &ObjectConfig::a, "Alpha/opacity")
        .def_readwrite("polygon_sides", &ObjectConfig::polygon_sides, "Polygon sides")
        .def_readwrite("equation", &ObjectConfig::equation, "Physics equation")
        .def("__repr__", [](const ObjectConfig &p)
             { return "<ObjectConfig pos=(" + std::to_string(p.x) + ", " +
                      std::to_string(p.y) + ") mass=" + std::to_string(p.mass) + ">"; });

    py::class_<ConstraintConfig>(m, "ConstraintConfig", R"pbdoc(
        Constraint configuration for batch mode.
        
        Attributes:
            type (int): Constraint type (0=DISTANCE, 1=BOUNDARY)
            target (int): Target object ID
            param1 (float): Distance: rest_length, Boundary: min_x, Angle: min_angle
            param2 (float): Boundary: max_x, Angle: max_angle
            param3 (float): Boundary: min_y
            param4 (float): Boundary: max_y
        )pbdoc")
        .def(py::init<>(), "Create default constraint config")
        .def_readwrite("type", &ConstraintConfig::type, "Constraint type")
        .def_readwrite("target", &ConstraintConfig::target, "Target object ID")
        .def_readwrite("param1", &ConstraintConfig::param1, "Distance: rest_length, Boundary: min_x, Angle: min_angle")
        .def_readwrite("param2", &ConstraintConfig::param2, "Boundary: max_x, Angle: max_angle")
        .def_readwrite("param3", &ConstraintConfig::param3, "Boundary: min_y")
        .def_readwrite("param4", &ConstraintConfig::param4, "Boundary: max_y");

    py::class_<BatchConfig>(m, "BatchConfig", R"pbdoc(
        Configuration for batch simulations.
        
        Attributes:
            objects (list[ObjectConfig]): List of object configurations
            duration (float): Simulation duration in seconds
            dt (float): Time step per update
            output_file (str): Optional output file path
        )pbdoc")
        .def(py::init<>(), "Create default batch config")
        .def_readwrite("objects", &BatchConfig::objects, "List of object configurations")
        .def_readwrite("duration", &BatchConfig::duration, "Simulation duration (seconds)")
        .def_readwrite("dt", &BatchConfig::dt, "Time step per update")
        .def_readwrite("output_file", &BatchConfig::output_file, "Output file path (optional)");

    // =========================================================================
    // BATCH DATA STRUCTURES
    // =========================================================================
    py::class_<BatchGetData>(m, "BatchGetData", "Batch get data structure")
        .def(py::init<>())
        .def_readwrite("x", &BatchGetData::x)
        .def_readwrite("y", &BatchGetData::y)
        .def_readwrite("vx", &BatchGetData::vx)
        .def_readwrite("vy", &BatchGetData::vy)
        .def_readwrite("mass", &BatchGetData::mass)
        .def_readwrite("charge", &BatchGetData::charge)
        .def_readwrite("rotation", &BatchGetData::rotation)
        .def_readwrite("angular_velocity", &BatchGetData::angular_velocity)
        .def_readwrite("width", &BatchGetData::width)
        .def_readwrite("height", &BatchGetData::height)
        .def_readwrite("radius", &BatchGetData::radius)
        .def_readwrite("polygon_sides", &BatchGetData::polygon_sides)
        .def_readwrite("skin_type", &BatchGetData::skin_type)
        .def_readwrite("r", &BatchGetData::r)
        .def_readwrite("g", &BatchGetData::g)
        .def_readwrite("b", &BatchGetData::b)
        .def_readwrite("a", &BatchGetData::a);

    py::class_<BatchUpdateData>(m, "BatchUpdateData", "Batch update data structure")
        .def(py::init<>())
        .def_readwrite("index", &BatchUpdateData::index)
        .def_readwrite("x", &BatchUpdateData::x)
        .def_readwrite("y", &BatchUpdateData::y)
        .def_readwrite("vx", &BatchUpdateData::vx)
        .def_readwrite("vy", &BatchUpdateData::vy)
        .def_readwrite("mass", &BatchUpdateData::mass)
        .def_readwrite("charge", &BatchUpdateData::charge)
        .def_readwrite("rotation", &BatchUpdateData::rotation)
        .def_readwrite("angular_velocity", &BatchUpdateData::angular_velocity)
        .def_readwrite("size", &BatchUpdateData::size) // radius for circle/polygon
        .def_readwrite("width", &BatchUpdateData::width)
        .def_readwrite("height", &BatchUpdateData::height)
        .def_readwrite("r", &BatchUpdateData::r)
        .def_readwrite("g", &BatchUpdateData::g)
        .def_readwrite("b", &BatchUpdateData::b)
        .def_readwrite("a", &BatchUpdateData::a)
        .def_readwrite("polygon_sides", &BatchUpdateData::polygon_sides);

    // =========================================================================
    // STABLE OBJECT HANDLE
    // =========================================================================
    py::class_<ObjectHandle>(m, "ObjectHandle", R"pbdoc(
        A stable handle to an object that remains valid across removals.
        
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
    )pbdoc")
        .def(py::init<>(), "Create an invalid handle")
        .def_readwrite("slot", &ObjectHandle::slot, "Internal object slot")
        .def_readwrite("generation", &ObjectHandle::generation, "Generation counter")
        .def("__repr__", [](const ObjectHandle &h)
             { return "<ObjectHandle slot=" + std::to_string(h.slot) +
                      " gen=" + std::to_string(h.generation) + ">"; })
        .def("__eq__", [](const ObjectHandle &a, const ObjectHandle &b)
             { return a.slot == b.slot && a.generation == b.generation; })
        .def("__hash__", [](const ObjectHandle &h)
             { return std::hash<int>()(h.slot) ^ (std::hash<int>()(h.generation) << 1); })
        .def("is_valid", [](const ObjectHandle &h, SimulationWrapper &sim)
             { return sim.is_handle_valid(h); }, "Check if this handle is still valid");

    // =========================================================================
    // CONSTRAINT TYPES
    // =========================================================================
    py::class_<DistanceConstraint>(m, "DistanceConstraint", R"pbdoc(
        Maintain distance between two objects.
        
        Args:
            target_object (int): ID of target object to maintain distance with
            rest_length (float): Desired distance between objects
        )pbdoc")
        .def(py::init<int, float>(),
             py::arg("target_object") = 0,
             py::arg("rest_length") = 5.0f,
             "Create distance constraint")
        .def_readwrite("target_object", &DistanceConstraint::target_object, "Target object ID")
        .def_readwrite("rest_length", &DistanceConstraint::rest_length, "Desired distance")
        .def("__repr__", [](const DistanceConstraint &c)
             { return "<DistanceConstraint target=" + std::to_string(c.target_object) +
                      " length=" + std::to_string(c.rest_length) + ">"; });

    py::class_<BoundaryConstraint>(m, "BoundaryConstraint", R"pbdoc(
        Keep object within a rectangular boundary.
        
        Args:
            min_x (float): Minimum X boundary
            max_x (float): Maximum X boundary
            min_y (float): Minimum Y boundary
            max_y (float): Maximum Y boundary
        )pbdoc")
        .def(py::init<float, float, float, float>(),
             py::arg("min_x") = -10.0f,
             py::arg("max_x") = 10.0f,
             py::arg("min_y") = -10.0f,
             py::arg("max_y") = 10.0f,
             "Create boundary constraint")
        .def_readwrite("min_x", &BoundaryConstraint::min_x, "Minimum X")
        .def_readwrite("max_x", &BoundaryConstraint::max_x, "Maximum X")
        .def_readwrite("min_y", &BoundaryConstraint::min_y, "Minimum Y")
        .def_readwrite("max_y", &BoundaryConstraint::max_y, "Maximum Y")
        .def("__repr__", [](const BoundaryConstraint &c)
             { return "<BoundaryConstraint x=[" + std::to_string(c.min_x) + "," +
                      std::to_string(c.max_x) + "] y=[" + std::to_string(c.min_y) +
                      "," + std::to_string(c.max_y) + "]>"; });

    // =========================================================================
    // COLLISION CONFIG
    // =========================================================================
    py::class_<CollisionConfig>(m, "CollisionConfig", R"pbdoc(
        Collision configuration for an object.
        
        Attributes:
            enabled (bool): Whether collision detection is enabled
            shape (CollisionShape): Collision shape type
            restitution (float): Bounciness (0.0 = no bounce, 1.0 = perfect bounce)
            friction (float): Surface friction (0.0 = frictionless, 1.0 = maximum friction)
        )pbdoc")
        .def(py::init<>(), "Create default collision config")
        .def_readwrite("enabled", &CollisionConfig::enabled, "Collision enabled")
        .def_readwrite("shape", &CollisionConfig::shape, "Collision shape type")
        .def_readwrite("restitution", &CollisionConfig::restitution, "Bounciness (0.0-1.0)")
        .def_readwrite("friction", &CollisionConfig::friction, "Surface friction (0.0-1.0)")
        .def("__repr__", [](const CollisionConfig &c)
             {
        std::string shapeStr;
        switch (c.shape) {
        case PyCollisionShape::NONE: shapeStr = "NONE"; break;
        case PyCollisionShape::CIRCLE: shapeStr = "CIRCLE"; break;
        case PyCollisionShape::AABB: shapeStr = "AABB"; break;
        case PyCollisionShape::POLYGON: shapeStr = "POLYGON"; break;
        }
        return "<CollisionConfig shape=" + shapeStr +
            " restitution=" + std::to_string(c.restitution) +
            " friction=" + std::to_string(c.friction) + ">"; });

    // =========================================================================
    // COLLISION CALLBACK DATA
    // =========================================================================
    py::class_<CollisionEvent>(m, "CollisionEvent", R"pbdoc(
        Data about a collision event for callback handlers.
        
        Attributes:
            object_a (ObjectHandle): First object in the collision
            object_b (ObjectHandle): Second object in the collision
            normal_x (float): X component of collision normal
            normal_y (float): Y component of collision normal
            penetration (float): How deeply the objects overlap
            contact_x (float): X position of the contact point
            contact_y (float): Y position of the contact point
            impulse (float): Magnitude of the collision impulse
    )pbdoc")
        .def(py::init<>())
        .def_readwrite("object_a", &CollisionEvent::object_a, "First object")
        .def_readwrite("object_b", &CollisionEvent::object_b, "Second object")
        .def_readwrite("normal_x", &CollisionEvent::normal_x, "Collision normal X")
        .def_readwrite("normal_y", &CollisionEvent::normal_y, "Collision normal Y")
        .def_readwrite("penetration", &CollisionEvent::penetration, "Penetration depth")
        .def_readwrite("contact_x", &CollisionEvent::contact_x, "Contact point X")
        .def_readwrite("contact_y", &CollisionEvent::contact_y, "Contact point Y")
        .def_readwrite("impulse", &CollisionEvent::impulse, "Impulse magnitude");

    // =========================================================================
    // KEYBOARD STATE
    // =========================================================================
    py::class_<KeyState>(m, "KeyState", R"pbdoc(
        Represents the state of a single keyboard key for the current frame.
        
        Attributes:
            pressed (bool): True if the key is currently held down.
            released (bool): True if the key was just released this frame.
            held (bool): Alias for pressed.
    )pbdoc")
        .def_property_readonly("pressed", &KeyState::pressed)
        .def_property_readonly("released", &KeyState::released)
        .def_property_readonly("held", &KeyState::pressed)
        .def("__repr__", [](const KeyState &ks)
             { return "<KeyState pressed=" + std::to_string(ks.pressed()) +
                      " released=" + std::to_string(ks.released()) + ">"; });

    py::class_<KeyboardMonitor>(m, "KeyboardMonitor", R"pbdoc(
        Provides access to keyboard states via attributes or indexing.
        
        Example:
            if sim.keyboard.Z.pressed: ...
            if sim.keyboard["Space"].released: ...
        
        Supported key names include: A-Z, 0-9, Space, Shift, Control, Alt,
        Escape, Enter, Tab, Backspace, Delete, Home, End, PageUp, PageDown,
        Insert, Up, Down, Left, Right, F1-F12.
    )pbdoc")
        .def(py::init<SimulationWrapper *>())
        .def("__getattr__", [](KeyboardMonitor &km, const std::string &name)
             { return km.get_key_state(name); })
        .def("__getitem__", [](KeyboardMonitor &km, const std::string &name)
             { return km.get_key_state(name); })
        .def("get_key_state", &KeyboardMonitor::get_key_state,
             py::arg("key_name"),
             "Get the state of a specific key by name.");

    // =========================================================================
    // MAIN SIMULATION CLASS
    // =========================================================================
    py::class_<SimulationWrapper>(m, "Simulation", R"pbdoc(
        Main physics simulation class.
        
        Provides real-time physics simulation with GPU acceleration.
        Can run in headless mode (no window) or with OpenGL visualization.
        
        Supports iteration: for obj in sim: ...
        Context manager: with Simulation(...) as sim: ...
        )pbdoc")
        .def(py::init<bool, int, int, std::string, bool>(),
             py::arg("headless") = true,
             py::arg("width") = 1280,
             py::arg("height") = 720,
             py::arg("title") = "Physics Simulation",
             py::arg("enable_grid") = true,
             R"pbdoc(
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
             )pbdoc")

        // Window management
        .def("render", &SimulationWrapper::render,
             "Render the current frame to the window (visual mode only)")
        .def("set_paint_resolution", &SimulationWrapper::set_paint_resolution,
             py::arg("width"), py::arg("height"),
             "Set internal resolution for paint shader (default = window size)")
        .def("process_input", &SimulationWrapper::process_input,
             "Update keyboard state for this frame. For default camera controls, call default_input().")
        .def("should_close", &SimulationWrapper::should_close,
             "Check if window should close (visual mode only)")

        // Grid control
        .def("set_grid_enabled", &SimulationWrapper::set_grid_enabled,
             py::arg("enabled"),
             R"pbdoc(
             Enable or disable grid/axis rendering.
             
             Args:
                 enabled (bool): True to enable grid, False to disable
             )pbdoc")

        .def("get_grid_enabled", &SimulationWrapper::get_grid_enabled,
             R"pbdoc(
             Check if grid/axis rendering is enabled.
             
             Returns:
                 bool: True if grid is enabled, False otherwise
             )pbdoc")

        // Core simulation
        .def("update", &SimulationWrapper::update,
             py::arg("dt") = 0.016f,
             R"pbdoc(
             Update physics simulation by dt seconds.
             
             Args:
                 dt (float): Time step in seconds. Default: 0.016 (approx 60 FPS).
                 
             Example:
                 >>> sim.update(dt=0.01)  # Update with 10ms time step
             )pbdoc")

        .def("set_speed", &SimulationWrapper::set_speed, py::arg("speed"),
             "Set speed multiplier (1.0 = normal). Higher values make simulation faster without affecting stability.")
        .def("get_speed", &SimulationWrapper::get_speed,
             "Get current speed multiplier.")

        // Object management - returns ObjectHandle for stability
        .def("add_object", &SimulationWrapper::add_object,
             py::arg("x") = 0.0f, py::arg("y") = 0.0f,
             py::arg("vx") = 0.0f, py::arg("vy") = 0.0f,
             py::arg("mass") = 1.0f, py::arg("charge") = 0.0f,
             py::arg("rotation") = 0.0f, py::arg("angular_velocity") = 0.0f,
             py::arg("skin") = PySkinType::PY_SKIN_CIRCLE,
             py::arg("size") = 0.3f,
             py::arg("width") = 0.5f, py::arg("height") = 0.3f,
             py::arg("r") = 1.0f, py::arg("g") = 1.0f,
             py::arg("b") = 1.0f, py::arg("a") = 1.0f,
             py::arg("polygon_sides") = 6,
             R"pbdoc(
             Add an object with full property control.
             
             Returns an ObjectHandle that remains stable across object removals.
             
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
                 ObjectHandle: Stable handle for future reference
                 
             Example:
                 >>> handle = sim.add_object(x=10, y=5, mass=50, skin=SkinType.CIRCLE)
             )pbdoc")

        // Raw index add (kept for backward compatibility)
        .def("add_object_raw", &SimulationWrapper::add_object_raw,
             py::arg("x") = 0.0f, py::arg("y") = 0.0f,
             py::arg("vx") = 0.0f, py::arg("vy") = 0.0f,
             py::arg("mass") = 1.0f, py::arg("charge") = 0.0f,
             py::arg("rotation") = 0.0f, py::arg("angular_velocity") = 0.0f,
             py::arg("skin") = PySkinType::PY_SKIN_CIRCLE,
             py::arg("size") = 0.3f,
             py::arg("width") = 0.5f, py::arg("height") = 0.3f,
             py::arg("r") = 1.0f, py::arg("g") = 1.0f,
             py::arg("b") = 1.0f, py::arg("a") = 1.0f,
             py::arg("polygon_sides") = 6,
             R"pbdoc(
             DEPRECATED: Use add_object() which returns a stable ObjectHandle.
             
             Add an object and return a raw integer index (unstable across removals).
             )pbdoc")

        // Full update with positional args (complete update)
        .def("update_object", &SimulationWrapper::update_object,
             py::arg("index"),
             py::arg("x"), py::arg("y"),
             py::arg("vx"), py::arg("vy"),
             py::arg("mass"), py::arg("charge"),
             py::arg("rotation"), py::arg("angular_velocity"),
             py::arg("size"),
             py::arg("width"), py::arg("height"),
             py::arg("r"), py::arg("g"), py::arg("b"), py::arg("a"),
             py::arg("polygon_sides") = 0,
             R"pbdoc(
                Update all properties of an existing object.
                
                Args:
                    index (int): Object ID (raw index, or ObjectHandle.slot)
                    x, y, vx, vy, mass, charge, rotation, angular_velocity: Updated properties
                    size (float): Radius for circles/polygons (ignored for rectangles)
                    width, height (float): Dimensions for rectangles (ignored otherwise)
                    r, g, b, a (float): Updated color
                    polygon_sides (int): Number of sides for polygons (0 = keep existing)
                )pbdoc")

        // Partial update with kwargs (only specified properties change)
        .def("update_object", [](SimulationWrapper &self, int index, const py::kwargs &kwargs)
             {
            // Fetch current state
            ObjectState state = self.get_object(index);

            // Convert kwargs to dict for easier access
            py::dict kwargs_dict = kwargs;

            // Helper to get float with default
            auto get_float = [&](const char* key, float default_val) -> float {
                if (kwargs_dict.contains(key)) {
                    return py::cast<float>(kwargs_dict[key]);
                }
                return default_val;
            };

            auto get_int = [&](const char* key, int default_val) -> int {
                if (kwargs_dict.contains(key)) {
                    return py::cast<int>(kwargs_dict[key]);
                }
                return default_val;
            };

            float x = get_float("x", state.x);
            float y = get_float("y", state.y);
            float vx = get_float("vx", state.vx);
            float vy = get_float("vy", state.vy);
            float mass = get_float("mass", state.mass);
            float charge = get_float("charge", state.charge);
            float rotation = get_float("rotation", state.rotation);
            float angular_velocity = get_float("angular_velocity", state.angular_velocity);
            float size = get_float("size", state.radius);
            float width = get_float("width", state.width);
            float height = get_float("height", state.height);
            float r = get_float("r", state.r);
            float g = get_float("g", state.g);
            float b = get_float("b", state.b);
            float a = get_float("a", state.a);
            int polygon_sides = get_int("polygon_sides", state.polygon_sides);

            // Call the full update
            self.update_object(index,
                               x, y,
                               vx, vy,
                               mass, charge,
                               rotation, angular_velocity,
                               size,
                               width, height,
                               r, g, b, a,
                               polygon_sides); }, py::arg("index"), R"pbdoc(
            Update only specified properties. Properties not provided keep their current values.

            Example:
                >>> sim.update_object(handle, x=1.0, r=0.5)  # only change x and red component
                >>> sim.update_object(handle, size=0.8)      # change radius of a circle
            )pbdoc")

        // Update by handle
        .def("update_object", [](SimulationWrapper &self, const ObjectHandle &handle, const py::kwargs &kwargs)
             {
            if (!self.is_handle_valid(handle)) {
                throw std::runtime_error("ObjectHandle is invalid (object was removed)");
            }
            int index = self.get_slot_index(handle);
            // Fetch current state
            ObjectState state = self.get_object(index);

            py::dict kwargs_dict = kwargs;

            auto get_float = [&](const char* key, float default_val) -> float {
                if (kwargs_dict.contains(key)) {
                    return py::cast<float>(kwargs_dict[key]);
                }
                return default_val;
            };

            auto get_int = [&](const char* key, int default_val) -> int {
                if (kwargs_dict.contains(key)) {
                    return py::cast<int>(kwargs_dict[key]);
                }
                return default_val;
            };

            float x = get_float("x", state.x);
            float y = get_float("y", state.y);
            float vx = get_float("vx", state.vx);
            float vy = get_float("vy", state.vy);
            float mass = get_float("mass", state.mass);
            float charge = get_float("charge", state.charge);
            float rotation = get_float("rotation", state.rotation);
            float angular_velocity = get_float("angular_velocity", state.angular_velocity);
            float size = get_float("size", state.radius);
            float width = get_float("width", state.width);
            float height = get_float("height", state.height);
            float r = get_float("r", state.r);
            float g = get_float("g", state.g);
            float b = get_float("b", state.b);
            float a = get_float("a", state.a);
            int polygon_sides = get_int("polygon_sides", state.polygon_sides);

            self.update_object(index,
                               x, y,
                               vx, vy,
                               mass, charge,
                               rotation, angular_velocity,
                               size,
                               width, height,
                               r, g, b, a,
                               polygon_sides); }, py::arg("handle"), R"pbdoc(
            Update an object by stable handle.

            Example:
                >>> handle = sim.add_object(x=0, y=0)
                >>> sim.update_object(handle, x=1.0, r=0.5)
            )pbdoc")

        // Remove object by handle
        .def("remove_object", &SimulationWrapper::remove_object_handle,
             py::arg("handle"), "Remove an object by stable ObjectHandle")

        // Remove object by raw index (deprecated)
        .def("remove_object_raw", &SimulationWrapper::remove_object,
             py::arg("index"), "DEPRECATED: Remove an object by raw index (unstable)")

        // Remove multiple objects at once (batched for efficiency)
        .def("remove_objects", &SimulationWrapper::remove_objects,
             py::arg("handles"),
             R"pbdoc(
             Remove multiple objects in one batch operation.
             
             Args:
                 handles (list[ObjectHandle]): List of handles to remove
                 
             This is more efficient than calling remove_object() repeatedly because
             it handles the swap-and-pop bookkeeping once.
             )pbdoc")

        .def("object_count", &SimulationWrapper::object_count, "Get number of objects in simulation")

        .def("get_object", static_cast<ObjectState (SimulationWrapper::*)(int) const>(&SimulationWrapper::get_object), py::arg("index"),
             R"pbdoc(
             Get complete object state.
             
             Args:
                 index (int): Object ID (raw index, or ObjectHandle.slot)
                 
             Returns:
                 ObjectState: Complete object state
             )pbdoc")

        .def("get_object", [](SimulationWrapper &self, const ObjectHandle &handle) -> ObjectState
             {
            if (!self.is_handle_valid(handle)) {
                throw std::runtime_error("ObjectHandle is invalid (object was removed)");
            }
            return self.get_object(self.get_slot_index(handle)); }, py::arg("handle"), R"pbdoc(
             Get object state by stable handle.
             
             Args:
                 handle (ObjectHandle): Object handle
                 
             Returns:
                 ObjectState: Complete object state
             )pbdoc")

        .def("get_handle", &SimulationWrapper::get_handle, py::arg("index"),
             R"pbdoc(
             Get the stable ObjectHandle for a raw index.
             
             Returns the handle for the current object at the given slot.
             If the slot is empty, returns an invalid handle.
             )pbdoc")

        .def("is_handle_valid", &SimulationWrapper::is_handle_valid, py::arg("handle"),
             "Check if an ObjectHandle is still valid")

        // get_all_objects - returns all object states
        .def("get_all_objects", &SimulationWrapper::get_all_objects,
             R"pbdoc(
             Get states for all objects in the simulation.
             
             Returns:
                 list[ObjectState]: Complete state of every object
                 
             Example:
                 >>> states = sim.get_all_objects()
                 >>> for state in states:
                 >>>     print(f"Object at ({state.x:.2f}, {state.y:.2f})")
             )pbdoc")

        // Convenience methods
        .def("set_rotation", &SimulationWrapper::set_rotation, py::arg("index"), py::arg("rotation"), "Set rotation angle in radians")

        .def("set_angular_velocity", &SimulationWrapper::set_angular_velocity, py::arg("index"), py::arg("angular_velocity"), "Set angular velocity in rad/s")

        .def("set_dimensions", &SimulationWrapper::set_dimensions, py::arg("index"), py::arg("width"), py::arg("height"), "Set width and height for rectangle objects")

        .def("set_radius", &SimulationWrapper::set_radius, py::arg("index"), py::arg("radius"), "Set radius for circle/polygon objects")

        .def("get_rotation", &SimulationWrapper::get_rotation, py::arg("index"), "Get rotation angle in radians")

        .def("get_angular_velocity", &SimulationWrapper::get_angular_velocity, py::arg("index"), "Get angular velocity in rad/s")

        // Batch operations
        .def("batch_get", &SimulationWrapper::batch_get, py::arg("indices"),
             R"pbdoc(
             Get properties for multiple objects at once.
             
             Args:
                 indices (list[int]): List of object indices to fetch
                 
             Returns:
                 list: List of BatchGetData objects for each index
                 
             Example:
                 >>> states = sim.batch_get([0, 1, 2, 3])
                 >>> for state in states:
                 >>>     print(f"x={state.x}, y={state.y}")
             )pbdoc")

        .def("batch_update", &SimulationWrapper::batch_update, py::arg("updates"),
             R"pbdoc(
             Update multiple objects at once.
             
             Args:
                 updates (list[BatchUpdateData]): List of update data objects
                 
             Example:
                 >>> updates = [
                 >>>     BatchUpdateData(index=0, x=1.0, y=2.0, ...),
                 >>>     BatchUpdateData(index=1, x=3.0, y=4.0, ...)
                 >>> ]
                 >>> sim.batch_update(updates)
             )pbdoc")

        // =====================================================================
        // CLEAR / SOFT RESET
        // =====================================================================
        .def("clear_all", &SimulationWrapper::clear_all,
             R"pbdoc(
             Remove all objects and clear all constraints, scripts, and equations.
             
             This is a "soft reset" that clears the simulation state but keeps
             the OpenGL context and shaders loaded. It does not destroy the window.
             
             Use this instead of creating a new Simulation object when you want to
             start fresh with the same context.
             
             Example:
                 >>> sim.clear_all()
                 >>> # All objects, constraints, and scripts are gone
             )pbdoc")

        // =====================================================================
        // COLLISION CALLBACK
        // =====================================================================
        .def("set_collision_callback", &SimulationWrapper::set_collision_callback,
             py::arg("callback"),
             R"pbdoc(
             Set a callback that is invoked when a collision occurs.
             
             Args:
                 callback: A function that takes a CollisionEvent object
                 
             The callback is called during update() for each collision detected.
             The callback receives a CollisionEvent containing:
                 - object_a, object_b: ObjectHandles of the colliding objects
                 - normal_x, normal_y: Collision normal vector
                 - penetration: How deeply the objects overlap
                 - contact_x, contact_y: Contact point in world coordinates
                 - impulse: Magnitude of the collision impulse
             
             Example:
                 >>> def on_collision(event):
                 >>>     print(f"Collision! {event.object_a} hit {event.object_b}")
                 >>>     print(f"Impulse: {event.impulse:.2f}")
                 >>> sim.set_collision_callback(on_collision)
             
             To clear the callback, pass None:
                 >>> sim.set_collision_callback(None)
             )pbdoc")

        .def("clear_collision_callback", &SimulationWrapper::clear_collision_callback,
             R"pbdoc(
             Remove the collision callback.
             )pbdoc")

        // =====================================================================
        // SCRATCHPAD ENUMERATION
        // =====================================================================
        .def("get_scratchpad_ids", &SimulationWrapper::get_scratchpad_ids,
             R"pbdoc(
             Get a list of all active scratchpad IDs.
             
             Returns:
                 list[int]: All valid scratchpad IDs currently allocated
                 
             Example:
                 >>> ids = sim.get_scratchpad_ids()
                 >>> for sid in ids:
                 >>>     print(f"Scratchpad {sid} has {sim.scratchpad_size(sid)} elements")
             )pbdoc")

        // =====================================================================
        // AGENT MANAGEMENT
        // =====================================================================
        .def("unregister_agent", &SimulationWrapper::unregister_agent,
             py::arg("agent_id"),
             R"pbdoc(
             Unregister an agent, removing it from the engine.
             
             Args:
                 agent_id (int): Agent ID to remove
                 
             The agent will no longer be dispatched, and its shader resources
             will be freed. Objects that reference this agent will no longer
             have a valid script.
             )pbdoc")

        .def("clear_agents", &SimulationWrapper::clear_agents,
             R"pbdoc(
             Unregister all agents.
             
             This removes all registered agents and frees their resources.
             All objects with scripts will have their script IDs reset to -1.
             )pbdoc")

        // Equations
        .def("set_equation", &SimulationWrapper::set_equation, py::arg("object_index"), py::arg("equation_string"),
             R"pbdoc(
             Set physics equation for object.
             
             Args:
                 object_index (int): Object ID (raw index, or ObjectHandle.slot)
                 equation_string (str): Physics equation
                 
             Equation syntax supports:
             - Variables: x, y, vx, vy, mass, charge, time
             - Object references: p[ID].x, p[ID].y, p[ID].mass
             - Functions: sin, cos, tan, sqrt, exp, log
             - Operators: +, -, *, /, ^ (power)
                 
             Example:
                 >>> sim.set_equation(0, "0.1*mass*(p[1].x - x)/distance^3")
             )pbdoc")

        .def("set_equation", [](SimulationWrapper &self, const ObjectHandle &handle, const std::string &equation_string)
             {
            if (!self.is_handle_valid(handle)) {
                throw std::runtime_error("ObjectHandle is invalid (object was removed)");
            }
            self.set_equation(self.get_slot_index(handle), equation_string); }, py::arg("handle"), py::arg("equation_string"),
             "Set physics equation for an object by stable handle.")

        // Scripting JIT
        .def("register_script", &SimulationWrapper::register_script, py::arg("source"), "Compile a Python script (source) and return script ID")
        .def("set_script", [](SimulationWrapper &self, int object_index, py::object script_or_id)
             {
                    int script_id;
                    if (py::isinstance<py::int_>(script_or_id)) {
                    script_id = py::cast<int>(script_or_id);
                    } 
                    else if (py::hasattr(script_or_id, "_script_id")) {
                        script_id = py::cast<int>(script_or_id.attr("_script_id"));
                    } 
                    else {
                    throw py::type_error("set_script() expects an int or a callable with a '_script_id' attribute");
                    }
            self.set_script(object_index, script_id); }, py::arg("object_index"), py::arg("script"), R"pbdoc(Assign a JIT script to an object. Accepts a script ID or a decorated function.)pbdoc")

        .def("set_script", [](SimulationWrapper &self, const ObjectHandle &handle, py::object script_or_id)
             {
            if (!self.is_handle_valid(handle)) {
                throw std::runtime_error("ObjectHandle is invalid (object was removed)");
            }
            int script_id;
            if (py::isinstance<py::int_>(script_or_id)) {
                script_id = py::cast<int>(script_or_id);
            } 
            else if (py::hasattr(script_or_id, "_script_id")) {
                script_id = py::cast<int>(script_or_id.attr("_script_id"));
            } 
            else {
                throw py::type_error("set_script() expects an int or a callable with a '_script_id' attribute");
            }
            self.set_script(self.get_slot_index(handle), script_id); }, py::arg("handle"), py::arg("script"), "Assign a JIT script to an object by stable handle.")

        // ----- Unified paint: removed set_paint_script, replaced paint with overloaded version -----
        .def("paint", [](SimulationWrapper &self, py::object arg)
             {
            if (py::isinstance<py::str>(arg)) {
                self.paint(py::cast<std::string>(arg));
            } else if (py::isinstance<py::int_>(arg)) {
                self.set_paint_script(py::cast<int>(arg));
            } else {
                throw py::type_error("paint() expects a string (DSL) or an integer (script ID)");
            } }, py::arg("arg"), R"pbdoc(
            Unified paint method.
            If arg is a string, it's treated as a DSL equation.
            If arg is an int, it's treated as a JIT script ID (from register_script).
        )pbdoc")
            
        //get the paint as an image(compressed JPEG)
        .def("get_paint_image", [](SimulationWrapper &self, int quality = 85) {
            std::vector<unsigned char> jpeg_data;
            self.get_paint_image(jpeg_data, quality);
            if (jpeg_data.empty()) return py::bytes("");
            return py::bytes((const char*)jpeg_data.data(), jpeg_data.size());
        }, py::arg("quality") = 85, "Get the paint framebuffer as JPEG bytes.")

        //get the full simulation as a compressed JPEG
        .def("get_full_frame", [](SimulationWrapper &self, int quality = 85) {
            std::vector<unsigned char> jpeg_data;
            self.get_full_frame(jpeg_data, quality);
            if (jpeg_data.empty()) return py::bytes("");
            return py::bytes((const char*)jpeg_data.data(), jpeg_data.size());
        }, py::arg("quality") = 85, "Get the full rendered frame as JPEG.")

        // Constraints
        .def("add_distance_constraint", &SimulationWrapper::add_distance_constraint, py::arg("object_index"), py::arg("constraint"), "Add distance constraint between objects")

        .def("add_boundary_constraint", &SimulationWrapper::add_boundary_constraint, py::arg("object_index"), py::arg("constraint"), "Add boundary constraint to object")

        .def("clear_constraints", &SimulationWrapper::clear_constraints, py::arg("object_index"), "Clear all constraints from object")

        .def("clear_all_constraints", &SimulationWrapper::clear_all_constraints, "Clear all constraints from all objects")

        // Collision management (individual setters for compatibility)
        .def("set_collision_enabled", &SimulationWrapper::set_collision_enabled, py::arg("index"), py::arg("enabled"),
             R"pbdoc(
             Enable or disable collision detection for an object.
             
             Args:
                 index (int): Object ID (raw index, or ObjectHandle.slot)
                 enabled (bool): True to enable collisions, False to disable
                 
             Example:
                 >>> sim.set_collision_enabled(0, False)  # Disable collisions for object 0
             )pbdoc")

        .def("set_collision_enabled", [](SimulationWrapper &self, const ObjectHandle &handle, bool enabled)
             {
            if (!self.is_handle_valid(handle)) {
                throw std::runtime_error("ObjectHandle is invalid (object was removed)");
            }
            self.set_collision_enabled(self.get_slot_index(handle), enabled); }, py::arg("handle"), py::arg("enabled"),
             "Enable or disable collision detection for an object by stable handle.")

        .def("set_collision_shape", &SimulationWrapper::set_collision_shape, py::arg("index"), py::arg("shape"),
             R"pbdoc(
             Set collision shape for an object.
             
             Args:
                 index (int): Object ID (raw index, or ObjectHandle.slot)
                 shape (CollisionShape): Collision shape type
                 
             Note: Shape is automatically set based on visual skin when adding objects.
             
             Example:
                 >>> sim.set_collision_shape(0, CollisionShape.CIRCLE)
             )pbdoc")

        .def("set_collision_shape", [](SimulationWrapper &self, const ObjectHandle &handle, PyCollisionShape shape)
             {
            if (!self.is_handle_valid(handle)) {
                throw std::runtime_error("ObjectHandle is invalid (object was removed)");
            }
            self.set_collision_shape(self.get_slot_index(handle), shape); }, py::arg("handle"), py::arg("shape"),
             "Set collision shape for an object by stable handle.")

        .def("set_collision_properties", &SimulationWrapper::set_collision_properties, py::arg("index"), py::arg("restitution"), py::arg("friction"),
             R"pbdoc(
             Set collision physical properties.
             
             Args:
                 index (int): Object ID (raw index, or ObjectHandle.slot)
                 restitution (float): Bounciness (0.0-1.0)
                     0.0 = no bounce (inelastic collision)
                     1.0 = perfect bounce (elastic collision)
                 friction (float): Surface friction (0.0-1.0)
                     0.0 = frictionless (ice)
                     1.0 = maximum friction (rubber)
                 
             Example:
                 >>> sim.set_collision_properties(0, restitution=0.9, friction=0.1)
                 >>> # Makes object 0 very bouncy with low friction
             )pbdoc")

        .def("set_collision_properties", [](SimulationWrapper &self, const ObjectHandle &handle, float restitution, float friction)
             {
            if (!self.is_handle_valid(handle)) {
                throw std::runtime_error("ObjectHandle is invalid (object was removed)");
            }
            self.set_collision_properties(self.get_slot_index(handle), restitution, friction); }, py::arg("handle"), py::arg("restitution"), py::arg("friction"),
             "Set collision physical properties for an object by stable handle.")

        // unified collision setter
        .def("set_collision", [](SimulationWrapper &self, int index, const py::kwargs &kwargs)
             {
            // Optional: set enabled
            if (kwargs.contains("enabled")) {
                bool enabled = py::cast<bool>(kwargs["enabled"]);
                self.set_collision_enabled(index, enabled);
            }
            // Optional: set shape
            if (kwargs.contains("shape")) {
                PyCollisionShape shape = py::cast<PyCollisionShape>(kwargs["shape"]);
                self.set_collision_shape(index, shape);
            }
            // Optional: set restitution and friction
            if (kwargs.contains("restitution") && kwargs.contains("friction")) {
                float restitution = py::cast<float>(kwargs["restitution"]);
                float friction = py::cast<float>(kwargs["friction"]);
                self.set_collision_properties(index, restitution, friction);
            } else if (kwargs.contains("restitution")) {
                // If only restitution given, keep current friction
                float restitution = py::cast<float>(kwargs["restitution"]);
                CollisionConfig config = self.get_collision_config(index);
                self.set_collision_properties(index, restitution, config.friction);
            } else if (kwargs.contains("friction")) {
                // If only friction given, keep current restitution
                float friction = py::cast<float>(kwargs["friction"]);
                CollisionConfig config = self.get_collision_config(index);
                self.set_collision_properties(index, config.restitution, friction);
            } }, py::arg("index"), R"pbdoc(
            Set collision properties for an object in a single call.
            
            Args:
                index (int): Object ID (raw index, or ObjectHandle.slot)
                enabled (bool, optional): Enable/disable collisions
                shape (CollisionShape, optional): Collision shape type
                restitution (float, optional): Bounciness (0.0-1.0)
                friction (float, optional): Friction (0.0-1.0)
            
            Example:
                >>> sim.set_collision(0, shape=CollisionShape.CIRCLE, restitution=0.9, friction=0.1)
                >>> sim.set_collision(1, enabled=False)
        )")

        .def("set_collision", [](SimulationWrapper &self, const ObjectHandle &handle, const py::kwargs &kwargs)
             {
            if (!self.is_handle_valid(handle)) {
                throw std::runtime_error("ObjectHandle is invalid (object was removed)");
            }
            int index = self.get_slot_index(handle);
            if (kwargs.contains("enabled")) {
                bool enabled = py::cast<bool>(kwargs["enabled"]);
                self.set_collision_enabled(index, enabled);
            }
            if (kwargs.contains("shape")) {
                PyCollisionShape shape = py::cast<PyCollisionShape>(kwargs["shape"]);
                self.set_collision_shape(index, shape);
            }
            if (kwargs.contains("restitution") && kwargs.contains("friction")) {
                float restitution = py::cast<float>(kwargs["restitution"]);
                float friction = py::cast<float>(kwargs["friction"]);
                self.set_collision_properties(index, restitution, friction);
            } else if (kwargs.contains("restitution")) {
                float restitution = py::cast<float>(kwargs["restitution"]);
                CollisionConfig config = self.get_collision_config(index);
                self.set_collision_properties(index, restitution, config.friction);
            } else if (kwargs.contains("friction")) {
                float friction = py::cast<float>(kwargs["friction"]);
                CollisionConfig config = self.get_collision_config(index);
                self.set_collision_properties(index, config.restitution, friction);
            } }, py::arg("handle"), "Set collision properties for an object by stable handle.")

        .def("get_collision_config", &SimulationWrapper::get_collision_config,
             py::arg("index"),
             R"pbdoc(
             Get collision configuration for an object.
             
             Args:
                 index (int): Object ID (raw index, or ObjectHandle.slot)
                 
             Returns:
                 CollisionConfig: Current collision settings
                 
             Example:
                 >>> config = sim.get_collision_config(0)
                 >>> print(f"Restitution: {config.restitution}")
             )pbdoc")

        .def("get_collision_config", [](SimulationWrapper &self, const ObjectHandle &handle)
             {
            if (!self.is_handle_valid(handle)) {
                throw std::runtime_error("ObjectHandle is invalid (object was removed)");
            }
            return self.get_collision_config(self.get_slot_index(handle)); }, py::arg("handle"),
             "Get collision configuration for an object by stable handle.")

        .def("enable_collision_between", &SimulationWrapper::enable_collision_between, py::arg("obj1"), py::arg("obj2"), py::arg("enable"),
             R"pbdoc(
             Enable or disable collision detection between two specific objects.
             
             Args:
                 obj1 (int): First object ID (raw index, or ObjectHandle.slot)
                 obj2 (int): Second object ID (raw index, or ObjectHandle.slot)
                 enable (bool): True to enable, False to disable
                 
             Useful for creating collision layers or groups.
             
             Example:
                 >>> sim.enable_collision_between(0, 1, False)
                 >>> # Objects 0 and 1 will pass through each other
             )pbdoc")

        .def("enable_collision_between", [](SimulationWrapper &self, const ObjectHandle &handle1, const ObjectHandle &handle2, bool enable)
             {
            if (!self.is_handle_valid(handle1)) {
                throw std::runtime_error("ObjectHandle 1 is invalid (object was removed)");
            }
            if (!self.is_handle_valid(handle2)) {
                throw std::runtime_error("ObjectHandle 2 is invalid (object was removed)");
            }
            self.enable_collision_between(self.get_slot_index(handle1), self.get_slot_index(handle2), enable); }, py::arg("handle1"), py::arg("handle2"), py::arg("enable"),
             "Enable or disable collision detection between two objects by stable handles.")

        .def("is_collision_enabled", &SimulationWrapper::is_collision_enabled, py::arg("index"),
             R"pbdoc(
             Check if collision detection is enabled for an object.
             
             Args:
                 index (int): Object ID (raw index, or ObjectHandle.slot)
                 
             Returns:
                 bool: True if collisions are enabled
             )pbdoc")

        .def("is_collision_enabled", [](SimulationWrapper &self, const ObjectHandle &handle) -> bool
             {
            if (!self.is_handle_valid(handle)) {
                throw std::runtime_error("ObjectHandle is invalid (object was removed)");
            }
            return self.is_collision_enabled(self.get_slot_index(handle)); }, py::arg("handle"),
             "Check if collision detection is enabled for an object by stable handle.")

        // Warm start and contact iterations (kept separate)
        .def("set_collision_parameters", &SimulationWrapper::set_collision_parameters, py::arg("enable_warm_start"), py::arg("max_contact_iterations"),
             R"pbdoc(
             Set global collision parameters (warm start and iteration count).
             
             Args:
                 enable_warm_start (bool): Enable warm starting for contacts
                 max_contact_iterations (int): Maximum iterations for contact resolution (1-20)
             )pbdoc")

        .def("get_collision_parameters", &SimulationWrapper::get_collision_parameters,
             R"pbdoc(
             Get global collision parameters.
             
             Returns:
                 tuple: (enable_warm_start, max_contact_iterations)
             )pbdoc")

        // Batch processing
        .def("run_batch", [](SimulationWrapper &self, const std::vector<BatchConfig> &configs, py::object callback)
             {
                py::gil_scoped_release release;
                if (callback.is_none()) {
                    self.run_batch(configs, nullptr);
                } else {
                    self.run_batch(configs,
                        [callback](int batch_idx, const std::vector<ObjectState>& results) {
                            py::gil_scoped_acquire acquire;
                            callback(batch_idx, results);
                        });
                } }, py::arg("configs"), py::arg("callback") = py::none(),
             R"pbdoc(
             Run multiple simulations in batch mode.
             
             Args:
                 configs (list[BatchConfig]): List of simulation configurations
                 callback (callable): Optional callback for progress/results
                     Called as: callback(batch_index, results)
                     
             Note: Only works in headless mode.
             )pbdoc")

        // Parameters
        .def("set_parameter", &SimulationWrapper::set_parameter, py::arg("name"), py::arg("value"),
             R"pbdoc(
             Set global simulation parameter.
             
             Args:
                 name (str): Parameter name
                 value (float): Parameter value
                 
             Available parameters:
             - "gravity": Global gravity strength
             - "damping": Velocity damping (0-1)
             - "stiffness": Default constraint stiffness
             )pbdoc")

        .def("get_parameter", &SimulationWrapper::get_parameter, py::arg("name"), "Get global parameter value by name")

        // Simulation control
        .def("set_paused", &SimulationWrapper::set_paused, py::arg("paused"), "Pause or resume simulation")

        .def("is_paused", &SimulationWrapper::is_paused, "Check if simulation is paused")

        .def("update_shader_loading", [](SimulationWrapper &self)
             {
                py::gil_scoped_release release;
                self.update_shader_loading(); }, "Update shader loading status")

        .def("are_all_shaders_ready", [](const SimulationWrapper &self)
             {
                py::gil_scoped_release release;
                return self.are_all_shaders_ready(); }, "Check if all shaders are loaded")

        .def("get_shader_load_progress", [](const SimulationWrapper &self)
             {
                py::gil_scoped_release release;
                return self.get_shader_load_progress(); }, "Get shader loading progress (0.0 to 1.0)")

        .def("get_shader_load_status", [](const SimulationWrapper &self)
             {
                py::gil_scoped_release release;
                return self.get_shader_load_status(); }, "Get current shader loading status message")

        .def("reset", &SimulationWrapper::reset, "Reset simulation to initial state (keeps objects)")

        .def("cleanup", &SimulationWrapper::cleanup, "Explicitly cleanup resources")

        // File I/O
        .def("save_to_file", &SimulationWrapper::save_to_file, py::arg("filename"), py::arg("title") = "", py::arg("author") = "", py::arg("description") = "",
             R"pbdoc(
             Save simulation state to .stellar file.
             
             Args:
                 filename (str): Output file path
                 title (str): Simulation title
                 author (str): Author name
                 description (str): Simulation description
             )pbdoc")

        .def("load_from_file", &SimulationWrapper::load_from_file, py::arg("filename"),
             R"pbdoc(
             Load simulation state from .stellar file.
             
             Args:
                 filename (str): Input file path
             )pbdoc")

        // Keyboard property
        .def_property_readonly("keyboard", [](SimulationWrapper &self)
                               { return KeyboardMonitor(&self); }, "Keyboard state monitor (e.g., sim.keyboard.Z.pressed, sim.keyboard.Space.released)")

        // Camera controls
        .def("set_camera_position", &SimulationWrapper::set_camera_position, py::arg("x"), py::arg("y"), "Set the camera position in world coordinates.")
        .def("get_camera_position", &SimulationWrapper::get_camera_position, "Return the current camera position as a tuple (x, y).")
        .def("set_camera_zoom", &SimulationWrapper::set_camera_zoom, py::arg("zoom"), "Set the camera zoom level (1.0 = default).")
        .def("get_camera_zoom", &SimulationWrapper::get_camera_zoom, "Return the current camera zoom level.")

        // ----- Input helpers -----
        .def("default_input", &SimulationWrapper::default_input,
             R"pbdoc(
             Default WASD + Q/E zoom + ESC close camera controls.
             Call this in your loop for standard behavior.
             )pbdoc")
        .def("get_mouse_position", &SimulationWrapper::get_mouse_position,
             R"pbdoc(
             Returns the current mouse position in world coordinates.
             )pbdoc")
        .def("get_mouse_delta", &SimulationWrapper::get_mouse_delta,
             R"pbdoc(
             Returns the mouse movement delta (in pixels) since the last call.
             )pbdoc")
        .def("set_key_state", &SimulationWrapper::set_key_state,
             R"pbdoc(
             Sets the keyboard state without having to press on the keyboard
             )pbdoc")

        // =====================================================================
        // SCREEN↔WORLD COORDINATE HELPERS
        // =====================================================================
        .def("screen_to_world", &SimulationWrapper::screen_to_world,
             py::arg("screen_x"), py::arg("screen_y"),
             R"pbdoc(
             Convert screen pixel coordinates to world coordinates.
             
             Args:
                 screen_x (float): X coordinate in pixels (from left)
                 screen_y (float): Y coordinate in pixels (from top)
                 
             Returns:
                 tuple[float, float]: World coordinates (x, y)
                 
             Example:
                 >>> world_x, world_y = sim.screen_to_world(mouse_x, mouse_y)
                 >>> obj = sim.add_object(x=world_x, y=world_y)
             )pbdoc")

        .def("world_to_screen", &SimulationWrapper::world_to_screen,
             py::arg("world_x"), py::arg("world_y"),
             R"pbdoc(
             Convert world coordinates to screen pixel coordinates.
             
             Args:
                 world_x (float): X coordinate in world space
                 world_y (float): Y coordinate in world space
                 
             Returns:
                 tuple[float, float]: Screen coordinates in pixels (from left, from top)
             )pbdoc")

        // =====================================================================
        // CAMERA CONVENIENCE
        // =====================================================================
        .def("fit_camera_to_objects", &SimulationWrapper::fit_camera_to_objects,
             py::arg("padding") = 0.2f,
             R"pbdoc(
             Auto-frame the camera to fit all objects in the view.
             
             Args:
                 padding (float): Extra padding around the bounding box (as a fraction)
                 
             Example:
                 >>> # After adding objects, frame them all
                 >>> sim.fit_camera_to_objects()
             )pbdoc")

        .def("follow_object", static_cast<void (SimulationWrapper::*)(int, float)>(&SimulationWrapper::follow_object),
             py::arg("index"), py::arg("smoothing") = 0.05f,
             R"pbdoc(
             Smoothly move the camera to follow an object.
             
             Args:
                 index (int): Object ID (raw index, or ObjectHandle.slot)
                 smoothing (float): Interpolation factor (0.0 = instant, 1.0 = never move)
                 
             Example:
                 >>> sim.follow_object(obj_handle, smoothing=0.02)
                 >>> # Camera will smoothly follow the object
             )pbdoc")

        .def("follow_object", [](SimulationWrapper &self, const ObjectHandle &handle, float smoothing = 0.05f)
             {
            if (!self.is_handle_valid(handle)) {
                throw std::runtime_error("ObjectHandle is invalid (object was removed)");
            }
            self.follow_object(self.get_slot_index(handle), smoothing); }, py::arg("handle"), py::arg("smoothing") = 0.05f,
             "Smoothly move the camera to follow an object by stable handle.")

        // Properties
        .def_property_readonly("is_headless", &SimulationWrapper::is_headless, "Check if simulation is running in headless mode")
        .def_property_readonly("is_initialized", &SimulationWrapper::is_initialized, "Check if simulation is fully initialized")
        .def_property_readonly("width", &SimulationWrapper::get_width, "Current window/framebuffer width")
        .def_property_readonly("height", &SimulationWrapper::get_height, "Current window/framebuffer height")

        // Scratchpad
        .def("create_scratchpad", &SimulationWrapper::create_scratchpad, py::arg("size"), "Create a scratchpad buffer with given number of floats.")
        .def("destroy_scratchpad", &SimulationWrapper::destroy_scratchpad, py::arg("id"), "Destroy a scratchpad.")
        .def("upload_scratchpad", &SimulationWrapper::upload_scratchpad, py::arg("id"), py::arg("data"), "Upload a list of floats to scratchpad.")
        .def("map_scratchpad", &SimulationWrapper::map_scratchpad, py::arg("id"), "Return a list of floats from scratchpad (CPU copy).")
        .def("scratchpad_size", &SimulationWrapper::scratchpad_size, py::arg("id"), "Get number of elements in scratchpad.")
        .def("is_valid_scratchpad", &SimulationWrapper::is_valid_scratchpad, py::arg("id"), "Check if scratchpad ID is valid.")

        // Signal queue
        .def("set_signal_queue_capacity", &SimulationWrapper::set_signal_queue_capacity, py::arg("capacity"), "Set the maximum number of pending signals.")
        .def("set_signal_queue_overflow_policy", &SimulationWrapper::set_signal_queue_overflow_policy, py::arg("policy"), "Set overflow policy: 0=drop, 1=block.")
        .def("clear_signal_queue", &SimulationWrapper::clear_signal_queue, "Clear all pending signals.")
        .def("get_signal_queue_count", &SimulationWrapper::get_signal_queue_count, "Get the number of pending signals.")

        // Agent dispatch
        .def("dispatch_agent", &SimulationWrapper::dispatch_agent, py::arg("agent_id"), py::arg("clear_after") = true, "Dispatch a specific agent over all pending signals for that agent.")
        .def("dispatch_all_agents", &SimulationWrapper::dispatch_all_agents, py::arg("clear_after") = true, "Dispatch all registered agents over pending signals (each agent processes its own).")

        // Agent registration
        .def("register_agent", &SimulationWrapper::register_agent, py::arg("source"), "Compile an agent shader and return its ID.")
        .def("get_agent_ids", &SimulationWrapper::get_agent_ids, "Return a list of all registered agent IDs.")

        // =====================================================================
        // CONTEXT MANAGER AND ITERATION (bindings)
        // =====================================================================
        .def("__enter__", [](SimulationWrapper &self) -> SimulationWrapper& { return self; })
        .def("__exit__", [](SimulationWrapper &self, py::object, py::object, py::object) {
            self.cleanup();
            return false;
        })
        .def("__len__", &SimulationWrapper::object_count)
        .def("__iter__", [](SimulationWrapper &self) {
            py::list objs = py::cast(self.get_all_objects());
            return objs.attr("__iter__")();
        });
}