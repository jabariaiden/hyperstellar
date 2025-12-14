// python_module/simulation_wrapper.h
#ifndef SIMULATION_WRAPPER_H
#define SIMULATION_WRAPPER_H

#include <string>
#include <functional>
#include <fstream>
#include <vector>

// Forward declarations to avoid including all headers
struct ObjectState;
struct DistanceConstraint;
struct BoundaryConstraint;

// Python-friendly enums
enum class PySkinType
{
    PY_SKIN_CIRCLE = 0,
    PY_SKIN_RECTANGLE = 1,
    PY_SKIN_POLYGON = 2
};

enum class PyConstraintType
{
    PY_CONSTRAINT_DISTANCE = 0,
    PY_CONSTRAINT_BOUNDARY = 1
};

// ENHANCED Object state for Python - NOW INCLUDES ROTATION AND DIMENSIONS
struct ObjectState
{
    float x, y;
    float vx, vy;
    float mass, charge;
    float rotation;         // NEW: rotation angle in radians
    float angular_velocity; // NEW: angular velocity in radians/sec
    float width, height;    // NEW: for rectangles
    float radius;           // NEW: for circles/polygons
    int polygon_sides;      // NEW: for polygons
    int skin_type;          // NEW: so users know what shape it is
    float r, g, b, a;
};

// Constraint types for Python
struct DistanceConstraint
{
    int target_object;
    float rest_length;
    float stiffness;

    DistanceConstraint(int target = 0, float length = 5.0f, float stiff = 100.0f)
        : target_object(target), rest_length(length), stiffness(stiff) {}
};

struct BoundaryConstraint
{
    float min_x, max_x;
    float min_y, max_y;

    BoundaryConstraint(float minx = -10.0f, float maxx = 10.0f,
                       float miny = -10.0f, float maxy = 10.0f)
        : min_x(minx), max_x(maxx), min_y(miny), max_y(maxy) {}
};

// Batch mode structures - ENHANCED
struct ConstraintConfig
{
    int type;
    int target;
    float param1, param2, param3, param4;
};

struct ObjectConfig
{
    float x = 0.0f, y = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    float mass = 1.0f;
    float charge = 0.0f;
    float rotation = 0.0f;         // NEW
    float angular_velocity = 0.0f; // NEW
    PySkinType skin = PySkinType::PY_SKIN_CIRCLE;
    float size = 0.3f;
    float width = 0.5f;  // NEW: for rectangles
    float height = 0.3f; // NEW: for rectangles
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    int polygon_sides = 6;
    std::string equation = "";
    std::vector<ConstraintConfig> constraints;
};

struct BatchConfig
{
    std::vector<ObjectConfig> objects;
    float duration = 10.0f;
    float dt = 0.016f;
    std::string output_file = "";
};

class SimulationWrapper
{
private:
    bool m_headless;
    bool m_initialized;
    bool m_paused;
    void *m_window;
    int m_currentBuffer;
    std::string m_title;
    int m_width, m_height;
    float m_simulationTime = 0.0f;

    bool init_headless();
    bool init_windowed(int width, int height, const std::string &title);
    void ensure_initialized() const;

    // Helper for batch mode
    void save_results(const std::string &filename, const std::vector<ObjectState> &states);

public:
    SimulationWrapper(bool headless = true, int width = 1280, int height = 720,
                      std::string title = "Physics Simulation");
    ~SimulationWrapper();

    // Core simulation
    void update(float dt);

    // ENHANCED object creation with FULL property control including rotation and dimensions
    int add_object(
        float x = 0.0f, float y = 0.0f,
        float vx = 0.0f, float vy = 0.0f,
        float mass = 1.0f, float charge = 0.0f,
        float rotation = 0.0f, float angular_velocity = 0.0f,
        PySkinType skin = PySkinType::PY_SKIN_CIRCLE,
        float size = 0.3f,
        float width = 0.5f, float height = 0.3f,
        float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f,
        int polygon_sides = 6);

    void remove_object(int index);
    int object_count() const;
    ObjectState get_object(int index) const;

    // ENHANCED update with rotation and dimensions
    void update_object(
        int index,
        float x, float y,
        float vx, float vy,
        float mass, float charge,
        float rotation, float angular_velocity,
        float width, float height,
        float r, float g, float b, float a);

    // NEW: Convenience methods for specific properties
    void set_rotation(int index, float rotation);
    void set_angular_velocity(int index, float angular_velocity);
    void set_dimensions(int index, float width, float height);
    void set_radius(int index, float radius);
    float get_rotation(int index) const;
    float get_angular_velocity(int index) const;

    // Equations
    void set_equation(int object_index, const std::string &equation_string);

    // Constraints
    void add_distance_constraint(int object_index, const DistanceConstraint &constraint);
    void add_boundary_constraint(int object_index, const BoundaryConstraint &constraint);
    void clear_constraints(int object_index);
    void clear_all_constraints();

    // System parameters
    void set_parameter(const std::string &name, float value);
    float get_parameter(const std::string &name) const;

    // Simulation control
    void set_paused(bool paused);
    bool is_paused() const;
    void reset();
    void cleanup();
    void process_input();
    bool should_close() const;
    void render();

    // Batch processing
    void run_batch(
        const std::vector<BatchConfig> &configs,
        std::function<void(int, const std::vector<ObjectState> &)> callback = nullptr);

    // Save/Load simulation state
    void save_to_file(const std::string &filename, const std::string &title = "",
                      const std::string &author = "", const std::string &description = "");
    void load_from_file(const std::string &filename);

    // Properties
    bool is_headless() const { return m_headless; }
    bool is_initialized() const { return m_initialized; }

    // Shader loading status
    void update_shader_loading();
    bool are_all_shaders_ready() const;
    float get_shader_load_progress() const;
    std::string get_shader_load_status() const;
};

#endif // SIMULATION_WRAPPER_H