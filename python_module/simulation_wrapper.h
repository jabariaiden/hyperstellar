// python_module/simulation_wrapper.h
#ifndef SIMULATION_WRAPPER_H
#define SIMULATION_WRAPPER_H

#include <string>
#include <functional>
#include <fstream>
#include <vector>
#include <array>
#include "script_manager.h"

// Forward declarations
class SimulationWrapper;

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

// Object state for Python
struct ObjectState
{
    float x, y;
    float vx, vy;
    float mass, charge;
    float rotation;         // rotation angle in radians
    float angular_velocity; // angular velocity in radians/sec
    float width, height;    // for rectangles
    float radius;           // for circles/polygons
    int polygon_sides;      // for polygons
    int skin_type;          // so users know what shape it is
    float r, g, b, a;
};

// Batch update data structure
struct BatchUpdateData
{
    int index;
    float x, y, vx, vy, mass, charge, rotation, angular_velocity;
    float size; // radius for circle/polygon
    float width, height;
    float r, g, b, a;
    int polygon_sides; // 0 = keep existing for polygon
};

// Batch get data structure
struct BatchGetData
{
    float x;
    float y;
    float vx;
    float vy;
    float mass;
    float charge;
    float rotation;
    float angular_velocity;
    float width;
    float height;
    float radius;
    int polygon_sides;
    int skin_type;
    float r;
    float g;
    float b;
    float a;
};

// Constraint types for Python
struct DistanceConstraint
{
    int target_object;
    float rest_length; // Desired distance between objects

    DistanceConstraint(int target = 0, float length = 5.0f)
        : target_object(target), rest_length(length)
    {
    }
};

struct BoundaryConstraint
{
    float min_x, max_x;
    float min_y, max_y;

    BoundaryConstraint(float minx = -10.0f, float maxx = 10.0f,
                       float miny = -10.0f, float maxy = 10.0f)
        : min_x(minx), max_x(maxx), min_y(miny), max_y(maxy) {}
};

// Batch mode structures
struct ConstraintConfig
{
    int type;
    int target;
    float param1, param2, param3, param4;
};

enum class PyCollisionShape
{
    NONE = 0,
    CIRCLE = 1,
    AABB = 2,
    POLYGON = 3
};

// Collision property struct
struct CollisionConfig
{
    bool enabled = true;
    PyCollisionShape shape = PyCollisionShape::NONE;
    float restitution = 0.7f;
    float friction = 0.3f;
};

struct ObjectConfig
{
    float x = 0.0f, y = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    float mass = 1.0f;
    float charge = 0.0f;
    float rotation = 0.0f;
    float angular_velocity = 0.0f;
    PySkinType skin = PySkinType::PY_SKIN_CIRCLE;
    float size = 0.3f;
    float width = 0.5f;  // for rectangles
    float height = 0.3f; // for rectangles
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

// Helper class for key state
class KeyState
{
    bool m_pressed;
    bool m_released;
public:
    KeyState(bool pressed, bool released) : m_pressed(pressed), m_released(released) {}
    bool pressed() const { return m_pressed; }
    bool released() const { return m_released; }
};

// Helper class for keyboard monitoring
class KeyboardMonitor
{
    SimulationWrapper *m_sim;

public:
    KeyboardMonitor(SimulationWrapper *sim) : m_sim(sim) {}
    KeyState get_key_state(const std::string &name) const;
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
    bool m_enable_grid;
    float m_speed = 1.0f;
    
    ScriptManager scriptManager;

    // Keyboard state tracking
    static constexpr int MAX_KEYS = 512;
    std::array<bool, MAX_KEYS> m_currentKeys{};
    std::array<bool, MAX_KEYS> m_previousKeys{};

    void update_keyboard_state();

    bool init_headless();
    bool init_windowed(int width, int height, const std::string &title);
    void ensure_initialized() const;

    // Helper for batch mode
    void save_results(const std::string &filename, const std::vector<ObjectState> &states);

public:
    SimulationWrapper(bool headless = true, int width = 1280, int height = 720,
                      std::string title = "Physics Simulation", bool enable_grid = true);
    ~SimulationWrapper();

    void set_grid_enabled(bool enabled) { m_enable_grid = enabled; }
    bool get_grid_enabled() const { return m_enable_grid; }

    // Keyboard queries
    bool is_key_pressed(int key) const;
    bool is_key_just_pressed(int key) const;
    bool is_key_just_released(int key) const;

    // script management
    int register_script(const std::string &source);
    void set_script(int objectIndex, int scriptId);

    // Camera control
    void set_camera_position(float x, float y);
    std::pair<float, float> get_camera_position() const;
    void set_camera_zoom(float zoom);
    float get_camera_zoom() const;

    // Core simulation
    void update(float dt);
    float get_speed() const { return m_speed; }
    void set_speed(float speed) { m_speed = speed; }

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

    void update_object(
        int index,
        float x, float y,
        float vx, float vy,
        float mass, float charge,
        float rotation, float angular_velocity,
        float size, // radius for circle/polygon (ignored for rectangle)
        float width, float height,
        float r, float g, float b, float a,
        int polygon_sides = 0 // only used for polygons; 0 = keep existing
    );

    // Batch get and update
    std::vector<BatchGetData> batch_get(const std::vector<int> &indices) const;
    void batch_update(const std::vector<BatchUpdateData> &updates);

    // paint
    void paint(const std::string &equation);
    void set_paint_resolution(int width, int height);
    void set_paint_script(int script_id);

    // Convenience methods for specific properties
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

    // Collision
    void set_collision_enabled(int index, bool enabled);
    void set_collision_shape(int index, PyCollisionShape shape);
    void set_collision_properties(int index, float restitution, float friction);
    CollisionConfig get_collision_config(int index);
    void enable_collision_between(int obj1, int obj2, bool enable);
    bool is_collision_enabled(int index);
    void set_collision_parameters(bool enable_warm_start, int max_contact_iterations);
    std::pair<bool, int> get_collision_parameters() const;

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

    // ---- Scratchpad, Signal Queue, Agents ----
    int create_scratchpad(size_t size);
    void destroy_scratchpad(int id);
    void upload_scratchpad(int id, const std::vector<float> &data);
    std::vector<float> map_scratchpad(int id);
    size_t scratchpad_size(int id);
    bool is_valid_scratchpad(int id);

    void set_signal_queue_capacity(size_t capacity);
    void set_signal_queue_overflow_policy(int policy);
    void clear_signal_queue();
    size_t get_signal_queue_count();

    void dispatch_agent(int agent_id, bool clear_after = true);
    void dispatch_all_agents(bool clear_after = true);

    int register_agent(const std::string &source);
    std::vector<int> get_agent_ids();
};

#endif // SIMULATION_WRAPPER_H