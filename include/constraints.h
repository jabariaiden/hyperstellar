#ifndef CONSTRAINTS_H
#define CONSTRAINTS_H

#include <glm/glm.hpp>
#include <vector>

enum ConstraintType {
    CONSTRAINT_DISTANCE = 0,
    CONSTRAINT_BOUNDARY = 1,
    CONSTRAINT_ANGLE = 2
};

struct Constraint {
    int type;              // ConstraintType
    int targetObjectID;  // For distance constraints (-1 if none)
    float param1;          // Distance: radius, Boundary: x1, Angle: min_angle
    float param2;          // Distance: unused, Boundary: x2, Angle: max_angle
    float param3;          // Distance: unused, Boundary: y1, Angle: unused
    float param4;          // Distance: unused, Boundary: y2, Angle: unused
    int _pad1;
    int _pad2;
    
    Constraint() : type(0), targetObjectID(-1), 
                   param1(0), param2(0), param3(0), param4(0),
                   _pad1(0), _pad2(0) {}
};

// Each object can have multiple constraints
struct ObjectConstraints {
    int objectID;
    int numConstraints;
    int constraintOffset;  // Offset into global constraint buffer
    int _pad;
    
    ObjectConstraints() : objectID(-1), numConstraints(0), 
                           constraintOffset(0), _pad(0) {}
};

#endif // CONSTRAINTS_H