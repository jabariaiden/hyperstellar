#pragma once
#include <string>
#include <vector>
#include <map>
#include "constraints.h"

struct ConstraintWidget {
    int type = 1;
    int targetObjectID = -1;
    float param1 = 0.0f;
    float param2 = 0.0f;
    float param3 = 0.0f;
    float param4 = 0.0f;
};

class UIManager
{
public:
    static void Initialize();
    static void RenderMainUI();
    static void RenderControlPanel();
    static void RenderSimulationView();
    static void RenderPhaseSpaceView();
    static void RenderObjectsTab();
    static void RenderPropertiesTab();
    static void RenderPhysicsTab();
    static void RenderViewTab();
    static void RenderFileDialogs();
    static void RenderProjectTab();
    static std::map<int, std::vector<ConstraintWidget>> objectConstraintWidgets;

private:
    static void RenderObjectList();
    static void RenderObjectProperties(int selectedIndex);
    static void RenderEquationControls();
};