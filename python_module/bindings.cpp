#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include <pybind11/gil.h>
#include <string>
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
        
        Example:
            >>> import stellar
            >>> sim = stellar.Simulation(headless=True)
            >>> obj_id = sim.add_object(x=0, y=0, mass=1.0)
            >>> sim.set_equation(obj_id, "0.1*mass*(target.x - x)/distance^3")
            >>> sim.update(dt=0.016)
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
        .def("__repr__", [](const ObjectState& p) {
        return "<ObjectState pos=(" + std::to_string(p.x) + ", " +
            std::to_string(p.y) + ") vel=(" + std::to_string(p.vx) +
            ", " + std::to_string(p.vy) + ") mass=" + std::to_string(p.mass) + ">";
            });

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
        .def("__repr__", [](const ObjectConfig& p) {
        return "<ObjectConfig pos=(" + std::to_string(p.x) + ", " +
            std::to_string(p.y) + ") mass=" + std::to_string(p.mass) + ">";
            });

    py::class_<ConstraintConfig>(m, "ConstraintConfig", "Constraint configuration for batch mode")
        .def(py::init<>(), "Create default constraint config")
        .def_readwrite("type", &ConstraintConfig::type, "Constraint type")
        .def_readwrite("target", &ConstraintConfig::target, "Target object ID")
        .def_readwrite("param1", &ConstraintConfig::param1, "Parameter 1")
        .def_readwrite("param2", &ConstraintConfig::param2, "Parameter 2")
        .def_readwrite("param3", &ConstraintConfig::param3, "Parameter 3")
        .def_readwrite("param4", &ConstraintConfig::param4, "Parameter 4");

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
        .def_readwrite("width", &BatchUpdateData::width)
        .def_readwrite("height", &BatchUpdateData::height)
        .def_readwrite("r", &BatchUpdateData::r)
        .def_readwrite("g", &BatchUpdateData::g)
        .def_readwrite("b", &BatchUpdateData::b)
        .def_readwrite("a", &BatchUpdateData::a);

    // =========================================================================
    // CONSTRAINT TYPES
    // =========================================================================
    py::class_<DistanceConstraint>(m, "DistanceConstraint", R"pbdoc(
        Maintain distance between two objects.
        
        Args:
            target_object (int): ID of target object to maintain distance with
            rest_length (float): Desired distance between objects
            stiffness (float): Constraint stiffness (higher = stronger)
        )pbdoc")
        .def(py::init<int, float, float>(),
            py::arg("target_object") = 0,
            py::arg("rest_length") = 5.0f,
            py::arg("stiffness") = 100.0f,
            "Create distance constraint")
        .def_readwrite("target_object", &DistanceConstraint::target_object, "Target object ID")
        .def_readwrite("rest_length", &DistanceConstraint::rest_length, "Desired distance")
        .def_readwrite("stiffness", &DistanceConstraint::stiffness, "Constraint stiffness")
        .def("__repr__", [](const DistanceConstraint& c) {
        return "<DistanceConstraint target=" + std::to_string(c.target_object) +
            " length=" + std::to_string(c.rest_length) + ">";
            });

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
        .def("__repr__", [](const BoundaryConstraint& c) {
        return "<BoundaryConstraint x=[" + std::to_string(c.min_x) + "," +
            std::to_string(c.max_x) + "] y=[" + std::to_string(c.min_y) +
            "," + std::to_string(c.max_y) + "]>";
            });

    // =========================================================================
    // MAIN SIMULATION CLASS
    // =========================================================================
    py::class_<SimulationWrapper>(m, "Simulation", R"pbdoc(
        Main physics simulation class.
        
        Provides real-time physics simulation with GPU acceleration.
        Can run in headless mode (no window) or with OpenGL visualization.
        )pbdoc")
        .def(py::init<bool, int, int, std::string, bool>(),
            py::arg("headless") = true,
            py::arg("width") = 1280,
            py::arg("height") = 720,
            py::arg("title") = "Physics Simulation",
            py::arg("enable_grid") = true,  // New parameter
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
        .def("process_input", &SimulationWrapper::process_input,
            "Process window input and camera controls (visual mode only)")
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

        // Object management
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
             )pbdoc")

        .def("update_object", &SimulationWrapper::update_object,
            py::arg("index"),
            py::arg("x"), py::arg("y"),
            py::arg("vx"), py::arg("vy"),
            py::arg("mass"), py::arg("charge"),
            py::arg("rotation"), py::arg("angular_velocity"),
            py::arg("width"), py::arg("height"),
            py::arg("r"), py::arg("g"), py::arg("b"), py::arg("a"),
            R"pbdoc(
             Update all properties of an existing object.
             
             Args:
                 index (int): Object ID
                 x,y,vx,vy,mass,charge,rotation,angular_velocity: Updated properties
                 width,height: Updated dimensions
                 r,g,b,a: Updated color
             )pbdoc")

        // Batch operations
        .def("batch_get", &SimulationWrapper::batch_get,
            py::arg("indices"),
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

        .def("batch_update", &SimulationWrapper::batch_update,
            py::arg("updates"),
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

        .def("remove_object", &SimulationWrapper::remove_object,
            py::arg("index"),
            "Remove an object by ID")

        .def("object_count", &SimulationWrapper::object_count,
            "Get number of objects in simulation")

        .def("get_object", &SimulationWrapper::get_object,
            py::arg("index"),
            R"pbdoc(
             Get complete object state.
             
             Args:
                 index (int): Object ID
                 
             Returns:
                 ObjectState: Complete object state
             )pbdoc")

        // Convenience methods
        .def("set_rotation", &SimulationWrapper::set_rotation,
            py::arg("index"), py::arg("rotation"),
            "Set rotation angle in radians")

        .def("set_angular_velocity", &SimulationWrapper::set_angular_velocity,
            py::arg("index"), py::arg("angular_velocity"),
            "Set angular velocity in rad/s")

        .def("set_dimensions", &SimulationWrapper::set_dimensions,
            py::arg("index"), py::arg("width"), py::arg("height"),
            "Set width and height for rectangle objects")

        .def("set_radius", &SimulationWrapper::set_radius,
            py::arg("index"), py::arg("radius"),
            "Set radius for circle/polygon objects")

        .def("get_rotation", &SimulationWrapper::get_rotation,
            py::arg("index"),
            "Get rotation angle in radians")

        .def("get_angular_velocity", &SimulationWrapper::get_angular_velocity,
            py::arg("index"),
            "Get angular velocity in rad/s")

        // Equations
        .def("set_equation", &SimulationWrapper::set_equation,
            py::arg("object_index"), py::arg("equation_string"),
            R"pbdoc(
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
             )pbdoc")

        // Constraints
        .def("add_distance_constraint", &SimulationWrapper::add_distance_constraint,
            py::arg("object_index"), py::arg("constraint"),
            "Add distance constraint between objects")

        .def("add_boundary_constraint", &SimulationWrapper::add_boundary_constraint,
            py::arg("object_index"), py::arg("constraint"),
            "Add boundary constraint to object")

        .def("clear_constraints", &SimulationWrapper::clear_constraints,
            py::arg("object_index"),
            "Clear all constraints from object")

        .def("clear_all_constraints", &SimulationWrapper::clear_all_constraints,
            "Clear all constraints from all objects")

        // Batch processing
        .def("run_batch", [](SimulationWrapper& self, const std::vector<BatchConfig>& configs, py::object callback)
            {
                py::gil_scoped_release release;

                if (callback.is_none()) {
                    self.run_batch(configs, nullptr);
                }
                else {
                    self.run_batch(configs,
                        [callback](int batch_idx, const std::vector<ObjectState>& results) {
                            py::gil_scoped_acquire acquire;
                            callback(batch_idx, results);
                        });
                }
            },
            py::arg("configs"), py::arg("callback") = py::none(),
            R"pbdoc(
             Run multiple simulations in batch mode.
             
             Args:
                 configs (list[BatchConfig]): List of simulation configurations
                 callback (callable): Optional callback for progress/results
                     Called as: callback(batch_index, results)
                     
             Note: Only works in headless mode.
             )pbdoc")

        // Parameters
        .def("set_parameter", &SimulationWrapper::set_parameter,
            py::arg("name"), py::arg("value"),
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

        .def("get_parameter", &SimulationWrapper::get_parameter,
            py::arg("name"),
            "Get global parameter value by name")

        // Simulation control
        .def("set_paused", &SimulationWrapper::set_paused,
            py::arg("paused"),
            "Pause or resume simulation")

        .def("is_paused", &SimulationWrapper::is_paused,
            "Check if simulation is paused")

        .def("update_shader_loading", [](SimulationWrapper& self)
            {
                py::gil_scoped_release release;
                self.update_shader_loading();
            },
            "Update shader loading status")

        .def("are_all_shaders_ready", [](const SimulationWrapper& self)
            {
                py::gil_scoped_release release;
                return self.are_all_shaders_ready();
            },
            "Check if all shaders are loaded")

        .def("get_shader_load_progress", [](const SimulationWrapper& self)
            {
                py::gil_scoped_release release;
                return self.get_shader_load_progress();
            },
            "Get shader loading progress (0.0 to 1.0)")

        .def("get_shader_load_status", [](const SimulationWrapper& self)
            {
                py::gil_scoped_release release;
                return self.get_shader_load_status();
            },
            "Get current shader loading status message")

        .def("reset", &SimulationWrapper::reset,
            "Reset simulation to initial state (keeps objects)")

        .def("cleanup", &SimulationWrapper::cleanup,
            "Explicitly cleanup resources")

        // File I/O
        .def("save_to_file", &SimulationWrapper::save_to_file,
            py::arg("filename"),
            py::arg("title") = "",
            py::arg("author") = "",
            py::arg("description") = "",
            R"pbdoc(
             Save simulation state to .stellar file.
             
             Args:
                 filename (str): Output file path
                 title (str): Simulation title
                 author (str): Author name
                 description (str): Simulation description
             )pbdoc")

        .def("load_from_file", &SimulationWrapper::load_from_file,
            py::arg("filename"),
            R"pbdoc(
             Load simulation state from .stellar file.
             
             Args:
                 filename (str): Input file path
             )pbdoc")

        // Properties
        .def_property_readonly("is_headless", &SimulationWrapper::is_headless,
            "Check if simulation is running in headless mode")
        .def_property_readonly("is_initialized", &SimulationWrapper::is_initialized,
            "Check if simulation is fully initialized");
}