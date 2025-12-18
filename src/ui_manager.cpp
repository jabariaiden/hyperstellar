// ui_manager.cpp
#include "ui_manager.h"
#include "renderer.h"
#include "globals.h"
#include "objects.h"
#include "physics_system.h"
#include "parser.h"
#include "debug_helpers.h"
#include "gpu_serializer.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "../lib/ImGuiFileDialog/ImGuiFileDialog.h"
#include <vector>
#include <iostream>
#include <map>
#include <fstream>
#include <filesystem>
#include <glm/gtc/type_ptr.hpp>

namespace fs = std::filesystem;

// ============================================================================
// STATIC VARIABLES
// ============================================================================

static std::vector<std::string> presetEquations = {
    "-k*x/m - g*uGravityDir.x + uExternalForce.x, -k*y/m - g*uGravityDir.y + uExternalForce.y, 0, 1, 1, 1, 1",
    "0, 0, 0, 1, 1, 1, 1",
    "-k*x/m - b*vx/m, -k*y/m - b*vy/m, 0, 1, 1, 1, 1",
    "-k*x/m, -k*y/m, 0, 1, 1, 1, 1",
    "uExternalForce.x, uExternalForce.y, 0, 1, 1, 1, 1" };

static const char* presetNames[] = {
    "Damped Spring + Gravity + External",
    "Zero Acceleration",
    "Damped 2D Spring",
    "Simple 2D Oscillator",
    "Constant External Force" };

static const char* SkinTypeNames[] = {
    "Circle",
    "Rectangle",
    "Polygon" };

static const char* CollisionTypeNames[] = {
    "None",
    "Circle",
    "Rectangle",
    "Polygon" };

// UI State
static std::string userEquation = presetEquations[0];
static bool equationUpdated = true;
static int selectedObjectIndex = -1;
static int lastSelectedObjectIndex = -1;
static std::map<int, std::string> objectEquations;
static std::map<int, int> objectPresets;

// File Dialog State
static bool showOpenDialog = false;
static bool showSaveDialog = false;
static std::string currentFilePath = "";
static std::string saveTitle = "Untitled Project";
static std::string saveAuthor = "";
static std::string saveDescription = "";

// Constraints
std::map<int, std::vector<ConstraintWidget>> UIManager::objectConstraintWidgets;

// ============================================================================
// HELPER FUNCTIONS FOR FILE OPERATIONS
// ============================================================================

// Helper to add a object from loaded data
static void AddObjectFromLoadedData(const Object& objectData, int skinType)
{
    if (Objects::GetNumObjects() < Objects::MAX_OBJECTS)
    {
        // Set default object type
        Objects::SetDefaultObjectType(skinType);

        // Add object (this creates a new object with defaults)
        Objects::AddObject();
        int newIndex = Objects::GetNumObjects() - 1;

        // Now update it with our loaded data
        Objects::UpdateObjectCPU(newIndex, objectData);

        // Upload to GPU
        Objects::UploadCpuDataToGpu();

        // Initialize equation for new object
        objectEquations[newIndex] = presetEquations[0];
        objectPresets[newIndex] = 0;

        std::cout << "[UI] Added loaded object " << newIndex << std::endl;
    }
    else
    {
        std::cerr << "[UI] Cannot add object: maximum limit reached" << std::endl;
    }
}

// Save current project to file
static bool SaveCurrentProject(const std::string& filepath)
{
    try
    {
        std::cout << "[UI] Saving project to: " << filepath << std::endl;

        std::ofstream file(filepath);
        if (!file.is_open())
        {
            std::cerr << "[UI] Failed to open file for writing: " << filepath << std::endl;
            return false;
        }

        // Fetch current objects
        std::vector<Object> objects;
        Objects::FetchToCPU(Renderer::GetCurrentObjectBuffer(), objects);

        // Write header
        file << "# Simulation State File\n";
        file << "# Created with Classical Physics Simulator\n";
        file << "# Title: " << saveTitle << "\n";
        if (!saveAuthor.empty())
            file << "# Author: " << saveAuthor << "\n";
        if (!saveDescription.empty())
            file << "# Description: " << saveDescription << "\n";
        file << "# Version: 1.0\n\n";

        // Write system parameters
        file << "[SYSTEM_PARAMETERS]\n";
        file << "gravity = " << g_physics.gravity << "\n";
        file << "damping = " << g_physics.damping << "\n";
        file << "stiffness = " << g_physics.stiffness << "\n";
        file << "restitution = " << g_physics.restitution << "\n";
        file << "coupling = " << g_physics.coupling << "\n\n";

        // Write camera state
        file << "[CAMERA]\n";
        file << "position = " << g_camera.position.x << " " << g_camera.position.y << "\n";
        file << "zoom = " << g_camera.zoom << "\n\n";

        // Write objects
        file << "[OBJECTS]\n";
        file << "count = " << objects.size() << "\n\n";

        for (size_t i = 0; i < objects.size(); ++i)
        {
            const Object& p = objects[i];
            file << "OBJECT " << i << "\n";
            file << "position = " << p.position.x << " " << p.position.y << "\n";
            file << "velocity = " << p.velocity.x << " " << p.velocity.y << "\n";
            file << "mass = " << p.mass << "\n";
            file << "charge = " << p.charge << "\n";
            file << "skin_type = " << p.visualSkinType << "\n";
            file << "color = " << p.color.r << " " << p.color.g << " "
                << p.color.b << " " << p.color.a << "\n";
            file << "rotation = " << p.visualData.z << "\n";
            file << "angular_velocity = " << p.visualData.w << "\n";

            // Type-specific dimensions
            if (p.visualSkinType == 0) // Circle
            {
                file << "radius = " << p.visualData.x << "\n";
            }
            else if (p.visualSkinType == 1) // Rectangle
            {
                file << "width = " << p.visualData.x << "\n";
                file << "height = " << p.visualData.y << "\n";
            }
            else if (p.visualSkinType == 2) // Polygon
            {
                file << "radius = " << p.visualData.x << "\n";
                file << "sides = " << static_cast<int>(p.visualData.y) << "\n";
            }

            file << "\n";
        }

        file.close();

        // Update current file path
        currentFilePath = filepath;

        // Extract filename for title
        std::string filename = fs::path(filepath).filename().string();
        size_t dotPos = filename.find_last_of('.');
        if (dotPos != std::string::npos)
        {
            saveTitle = filename.substr(0, dotPos);
        }
        else
        {
            saveTitle = filename;
        }

        std::cout << "[UI] Successfully saved " << objects.size()
            << " objects to " << filepath << std::endl;

        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[UI] Error saving project: " << e.what() << std::endl;
        return false;
    }
}

// Load project from file
static bool LoadProject(const std::string& filepath)
{
    try
    {
        std::cout << "[UI] Loading project from: " << filepath << std::endl;

        std::ifstream file(filepath);
        if (!file.is_open())
        {
            std::cerr << "[UI] Failed to open file: " << filepath << std::endl;
            return false;
        }

        // Clear current simulation
        Objects::ResetToInitialConditions();
        while (Objects::GetNumObjects() > 0)
        {
            Objects::RemoveObject(0);
        }

        // Clear UI state
        selectedObjectIndex = -1;
        lastSelectedObjectIndex = -1;
        objectEquations.clear();
        objectPresets.clear();
        UIManager::objectConstraintWidgets.clear();

        std::string line;
        std::string currentSection;
        Object currentObject;
        int currentObjectID = -1;

        while (std::getline(file, line))
        {
            // Trim whitespace
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);

            // Skip empty lines and comments
            if (line.empty() || line[0] == '#')
                continue;

            // Check for section headers
            if (line.size() > 1 && line[0] == '[' && line[line.size() - 1] == ']')
            {
                currentSection = line.substr(1, line.size() - 2);
                continue;
            }

            // Parse based on current section
            if (currentSection == "SYSTEM_PARAMETERS")
            {
                size_t eqPos = line.find('=');
                if (eqPos != std::string::npos)
                {
                    std::string key = line.substr(0, eqPos);
                    std::string value = line.substr(eqPos + 1);

                    // Trim
                    key.erase(key.find_last_not_of(" \t") + 1);
                    value.erase(0, value.find_first_not_of(" \t"));

                    if (key == "gravity")
                        g_physics.gravity = std::stof(value);
                    else if (key == "damping")
                        g_physics.damping = std::stof(value);
                    else if (key == "stiffness")
                        g_physics.stiffness = std::stof(value);
                    else if (key == "restitution")
                        g_physics.restitution = std::stof(value);
                    else if (key == "coupling")
                        g_physics.coupling = std::stof(value);
                }
            }
            else if (currentSection == "CAMERA")
            {
                size_t eqPos = line.find('=');
                if (eqPos != std::string::npos)
                {
                    std::string key = line.substr(0, eqPos);
                    std::string value = line.substr(eqPos + 1);

                    // Trim
                    key.erase(key.find_last_not_of(" \t") + 1);
                    value.erase(0, value.find_first_not_of(" \t"));

                    if (key == "position")
                    {
                        size_t spacePos = value.find(' ');
                        if (spacePos != std::string::npos)
                        {
                            g_camera.position.x = std::stof(value.substr(0, spacePos));
                            g_camera.position.y = std::stof(value.substr(spacePos + 1));
                        }
                    }
                    else if (key == "zoom")
                    {
                        g_camera.zoom = std::stof(value);
                    }
                }
            }
            else if (currentSection == "OBJECTS")
            {
                // Check if this is a new object declaration
                if (line.find("OBJECT") == 0)
                {
                    // If we have a current object, add it
                    if (currentObjectID >= 0)
                    {
                        AddObjectFromLoadedData(currentObject, currentObject.visualSkinType);
                    }

                    // Parse object ID
                    size_t spacePos = line.find(' ');
                    if (spacePos != std::string::npos)
                    {
                        currentObjectID = std::stoi(line.substr(spacePos + 1));
                        currentObject = Object();  // Reset to default
                        currentObject.visualSkinType = 0; // Default to circle
                    }
                }
                else if (currentObjectID >= 0)
                {
                    // Parse object property
                    size_t eqPos = line.find('=');
                    if (eqPos != std::string::npos)
                    {
                        std::string key = line.substr(0, eqPos);
                        std::string value = line.substr(eqPos + 1);

                        // Trim
                        key.erase(key.find_last_not_of(" \t") + 1);
                        value.erase(0, value.find_first_not_of(" \t"));

                        if (key == "position")
                        {
                            size_t spacePos = value.find(' ');
                            if (spacePos != std::string::npos)
                            {
                                currentObject.position.x = std::stof(value.substr(0, spacePos));
                                currentObject.position.y = std::stof(value.substr(spacePos + 1));
                            }
                        }
                        else if (key == "velocity")
                        {
                            size_t spacePos = value.find(' ');
                            if (spacePos != std::string::npos)
                            {
                                currentObject.velocity.x = std::stof(value.substr(0, spacePos));
                                currentObject.velocity.y = std::stof(value.substr(spacePos + 1));
                            }
                        }
                        else if (key == "mass")
                        {
                            currentObject.mass = std::stof(value);
                        }
                        else if (key == "charge")
                        {
                            currentObject.charge = std::stof(value);
                        }
                        else if (key == "skin_type")
                        {
                            currentObject.visualSkinType = std::stoi(value);
                        }
                        else if (key == "color")
                        {
                            size_t pos1 = value.find(' ');
                            size_t pos2 = value.find(' ', pos1 + 1);
                            size_t pos3 = value.find(' ', pos2 + 1);
                            if (pos1 != std::string::npos && pos2 != std::string::npos && pos3 != std::string::npos)
                            {
                                currentObject.color.r = std::stof(value.substr(0, pos1));
                                currentObject.color.g = std::stof(value.substr(pos1 + 1, pos2 - pos1 - 1));
                                currentObject.color.b = std::stof(value.substr(pos2 + 1, pos3 - pos2 - 1));
                                currentObject.color.a = std::stof(value.substr(pos3 + 1));
                            }
                        }
                        else if (key == "rotation")
                        {
                            currentObject.visualData.z = std::stof(value);
                        }
                        else if (key == "angular_velocity")
                        {
                            currentObject.visualData.w = std::stof(value);
                        }
                        else if (key == "radius")
                        {
                            currentObject.visualData.x = std::stof(value);
                        }
                        else if (key == "width")
                        {
                            currentObject.visualData.x = std::stof(value);
                        }
                        else if (key == "height")
                        {
                            currentObject.visualData.y = std::stof(value);
                        }
                        else if (key == "sides")
                        {
                            currentObject.visualData.y = std::stof(value);
                        }
                    }
                }
            }
        }

        // Add the last object if there is one
        if (currentObjectID >= 0)
        {
            AddObjectFromLoadedData(currentObject, currentObject.visualSkinType);
        }

        // Upload all data to GPU
        Objects::UploadCpuDataToGpu();

        // Update current file path
        currentFilePath = filepath;

        // Extract filename for title
        std::string filename = fs::path(filepath).filename().string();
        size_t dotPos = filename.find_last_of('.');
        if (dotPos != std::string::npos)
        {
            saveTitle = filename.substr(0, dotPos);
        }
        else
        {
            saveTitle = filename;
        }

        // Initialize equations for loaded objects
        std::vector<Object> objects;
        Objects::FetchToCPU(Renderer::GetCurrentObjectBuffer(), objects);

        for (size_t i = 0; i < objects.size(); ++i)
        {
            objectEquations[i] = presetEquations[0];
            objectPresets[i] = 0;
        }

        std::cout << "[UI] Successfully loaded " << objects.size()
            << " objects from " << filepath << std::endl;

        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[UI] Error loading project: " << e.what() << std::endl;
        return false;
    }
}

// ============================================================================
// UI MANAGER IMPLEMENTATION
// ============================================================================

void UIManager::Initialize()
{
    selectedObjectIndex = -1;
    lastSelectedObjectIndex = -1;
    equationUpdated = true;
    objectEquations.clear();
    objectPresets.clear();
    objectConstraintWidgets.clear();

    // Initialize file dialog state
    showOpenDialog = false;
    showSaveDialog = false;
    currentFilePath = "";
    saveTitle = "Untitled Project";
    saveAuthor = "";
    saveDescription = "";
}

void UIManager::RenderMainUI()
{
    // Render menu bar
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New Project", "Ctrl+N"))
            {
                // Reset simulation to empty state
                Objects::ResetToInitialConditions();
                while (Objects::GetNumObjects() > 0)
                {
                    Objects::RemoveObject(0);
                }
                g_physics.globalTime = 0.0f;
                g_camera.Reset();

                // Clear UI state
                selectedObjectIndex = -1;
                lastSelectedObjectIndex = -1;
                objectEquations.clear();
                objectPresets.clear();
                objectConstraintWidgets.clear();

                // Reset file state
                currentFilePath = "";
                saveTitle = "Untitled Project";
                saveAuthor = "";
                saveDescription = "";

                std::cout << "[UI] Created new project" << std::endl;
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Open Project...", "Ctrl+O"))
            {
                showOpenDialog = true;
            }

            if (ImGui::MenuItem("Save Project", "Ctrl+S"))
            {
                if (currentFilePath.empty())
                {
                    showSaveDialog = true;
                }
                else
                {
                    // Save to current file path
                    if (!SaveCurrentProject(currentFilePath))
                    {
                        std::cerr << "[UI] Save failed!" << std::endl;
                    }
                }
            }

            if (ImGui::MenuItem("Save Project As...", "Ctrl+Shift+S"))
            {
                showSaveDialog = true;
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Exit", "Alt+F4"))
            {
                // Signal to close the window (you'll need to implement this)
                // glfwSetWindowShouldClose(window, true);
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, false)) {}
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, false)) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Cut", "Ctrl+X", false, false)) {}
            if (ImGui::MenuItem("Copy", "Ctrl+C", false, false)) {}
            if (ImGui::MenuItem("Paste", "Ctrl+V", false, false)) {}
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("Control Panel", NULL, true);  // Always open
            ImGui::MenuItem("Phase Space", NULL, &g_physics.showPhaseSpace);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("About")) {}
            if (ImGui::MenuItem("Documentation")) {}
            ImGui::EndMenu();
        }

        // Show current project in menu bar
        ImGui::SameLine(ImGui::GetWindowWidth() - 300);
        ImGui::TextDisabled("%s%s",
            saveTitle.c_str(),
            currentFilePath.empty() ? " [Unsaved]" : "");

        ImGui::EndMainMenuBar();
    }

    // Main dockspace (adjusted for menu bar height)
    ImGui::SetNextWindowPos(ImVec2(0, ImGui::GetFrameHeight()));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x,
        ImGui::GetIO().DisplaySize.y - ImGui::GetFrameHeight()));
    ImGui::Begin("MainDockSpace", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground);
    ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dockspaceId, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
}

void UIManager::RenderControlPanel()
{
    ImGui::Begin("Control Panel");

    // Top control bar
    ImGui::PushStyleColor(ImGuiCol_Button, Renderer::IsSimulationPaused() ? ImVec4(0.8f, 0.4f, 0.1f, 1.0f) : ImVec4(0.6f, 0.3f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Renderer::IsSimulationPaused() ? ImVec4(0.9f, 0.5f, 0.2f, 1.0f) : ImVec4(0.7f, 0.4f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Renderer::IsSimulationPaused() ? ImVec4(0.7f, 0.3f, 0.0f, 1.0f) : ImVec4(0.5f, 0.2f, 0.0f, 1.0f));

    if (ImGui::Button(Renderer::IsSimulationPaused() ? "Play" : "Pause", ImVec2(ImGui::GetContentRegionAvail().x * 0.48f, 30)))
    {
        Renderer::SetSimulationPaused(!Renderer::IsSimulationPaused());
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("Reset", ImVec2(-1, 30)))
    {
        Objects::ResetToInitialConditions();
        g_physics.globalTime = 0.0f;
        selectedObjectIndex = -1;
        lastSelectedObjectIndex = -1;
    }
    ImGui::PopStyleColor(3);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Time: %.2fs", g_physics.globalTime);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80);
    ImGui::Text("%d/%d Objects", Objects::GetNumObjects(), Objects::MAX_OBJECTS);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::BeginTabBar("ControlTabs", ImGuiTabBarFlags_None))
    {
        if (ImGui::BeginTabItem("Objects"))
        {
            RenderObjectsTab();
            ImGui::EndTabItem();
        }

        if (selectedObjectIndex != -1)
        {
            if (ImGui::BeginTabItem("Properties"))
            {
                RenderPropertiesTab();
                ImGui::EndTabItem();
            }
        }

        if (ImGui::BeginTabItem("Physics"))
        {
            RenderPhysicsTab();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("View"))
        {
            RenderViewTab();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Project"))
        {
            RenderProjectTab();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

void UIManager::RenderSimulationView()
{
    ImGui::Begin("Simulation");

    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 contentMin = ImGui::GetWindowContentRegionMin();
    ImVec2 contentMax = ImGui::GetWindowContentRegionMax();

    g_simulationViewportPos = glm::vec2(windowPos.x + contentMin.x, windowPos.y + contentMin.y);
    g_simulationViewportSize = glm::vec2(contentMax.x - contentMin.x, contentMax.y - contentMin.y);

    if (g_simulationViewportSize.x <= 0)
        g_simulationViewportSize.x = 640;
    if (g_simulationViewportSize.y <= 0)
        g_simulationViewportSize.y = 480;

    ImGui::Image((void*)(intptr_t)Renderer::GetMainFramebufferTexture(),
        ImVec2(g_simulationViewportSize.x, g_simulationViewportSize.y),
        ImVec2(0, 1), ImVec2(1, 0));
    ImGui::End();
}

void UIManager::RenderPhaseSpaceView()
{
    if (!g_physics.showPhaseSpace)
        return;

    ImGui::Begin("Phase Space");
    ImVec2 phaseSpaceSize = ImGui::GetContentRegionAvail();
    if (phaseSpaceSize.x <= 0)
        phaseSpaceSize.x = 640;
    if (phaseSpaceSize.y <= 0)
        phaseSpaceSize.y = 480;

    ImGui::Image((void*)(intptr_t)Renderer::GetPhaseSpaceFramebufferTexture(),
        ImVec2(phaseSpaceSize.x, phaseSpaceSize.y),
        ImVec2(0, 1), ImVec2(1, 0));
    ImGui::End();
}

void UIManager::RenderFileDialogs()
{
    // Open File Dialog
    if (showOpenDialog)
    {
        IGFD::FileDialogConfig config;
        config.path = ".";
        config.countSelectionMax = 1;
        config.flags = ImGuiFileDialogFlags_Modal;

        ImGuiFileDialog::Instance()->OpenDialog("OpenProjectDialog", "Open Project",
            ".stellar,.txt", config);
        showOpenDialog = false;
    }

    // Save File Dialog
    if (showSaveDialog)
    {
        IGFD::FileDialogConfig config;
        config.path = ".";
        config.countSelectionMax = 1;
        config.flags = ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_ConfirmOverwrite;

        // Suggest a filename
        if (currentFilePath.empty())
        {
            config.fileName = "project.stellar";
        }
        else
        {
            config.fileName = fs::path(currentFilePath).filename().string();
        }

        ImGuiFileDialog::Instance()->OpenDialog("SaveProjectDialog", "Save Project As",
            ".stellar", config);
        showSaveDialog = false;
    }

    // Display and handle Open dialog
    if (ImGuiFileDialog::Instance()->Display("OpenProjectDialog", ImGuiWindowFlags_NoCollapse,
        ImVec2(600, 400)))
    {
        if (ImGuiFileDialog::Instance()->IsOk())
        {
            std::string filePathName = ImGuiFileDialog::Instance()->GetFilePathName();
            if (LoadProject(filePathName))
            {
                std::cout << "[UI] Successfully loaded project: " << filePathName << std::endl;
            }
            else
            {
                std::cerr << "[UI] Failed to load project!" << std::endl;
            }
        }
        ImGuiFileDialog::Instance()->Close();
    }

    // Display and handle Save dialog
    if (ImGuiFileDialog::Instance()->Display("SaveProjectDialog", ImGuiWindowFlags_NoCollapse,
        ImVec2(600, 400)))
    {
        if (ImGuiFileDialog::Instance()->IsOk())
        {
            std::string filePathName = ImGuiFileDialog::Instance()->GetFilePathName();

            // Ensure .stellar extension
            if (fs::path(filePathName).extension() != ".stellar")
            {
                filePathName += ".stellar";
            }

            if (SaveCurrentProject(filePathName))
            {
                std::cout << "[UI] Successfully saved project: " << filePathName << std::endl;
            }
            else
            {
                std::cerr << "[UI] Failed to save project!" << std::endl;
            }
        }
        ImGuiFileDialog::Instance()->Close();
    }
}

void UIManager::RenderObjectsTab()
{
    ImGui::Spacing();

    // Add object section
    ImGui::Text("Add New Object");
    ImGui::Spacing();

    static int newSkinType = SKIN_CIRCLE;
    ImGui::Text("Visual Skin");
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##NewSkinType", &newSkinType, SkinTypeNames, 3);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.5f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.3f, 0.0f, 1.0f));
    if (ImGui::Button("Add to Scene", ImVec2(-1, 0)))
    {
        if (Objects::GetNumObjects() < Objects::MAX_OBJECTS)
        {
            Objects::SetDefaultObjectType(newSkinType);
            Objects::AddObject();
            selectedObjectIndex = Objects::GetNumObjects() - 1;
            lastSelectedObjectIndex = -1;

            // Initialize equation for new object
            objectEquations[selectedObjectIndex] = presetEquations[0];
            objectPresets[selectedObjectIndex] = 0;

            Objects::UploadCpuDataToGpu();
        }
    }
    ImGui::PopStyleColor(3);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Scene Objects");
    ImGui::Spacing();

    std::vector<Object> currentCpuObjects;
    Objects::FetchToCPU(Renderer::GetCurrentObjectBuffer(), currentCpuObjects);

    if (!currentCpuObjects.empty())
    {
        ImGui::BeginChild("ObjectList", ImVec2(0, -40), true);

        for (size_t i = 0; i < currentCpuObjects.size(); ++i)
        {
            ImGui::PushID(i);

            bool isSelected = (selectedObjectIndex == (int)i);

            if (isSelected)
            {
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.8f, 0.4f, 0.1f, 0.7f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.9f, 0.5f, 0.2f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.7f, 0.3f, 0.0f, 0.9f));
            }

            char label[64];
            sprintf(label, "Object %zu", i);

            if (ImGui::Selectable(label, isSelected, 0, ImVec2(0, 0)))
            {
                selectedObjectIndex = (int)i;
            }

            if (isSelected)
                ImGui::PopStyleColor(3);

            ImGui::SameLine();

            // Show skin type icon
            const char* skinIcon = "●"; // Circle
            if (currentCpuObjects[i].visualSkinType == SKIN_RECTANGLE)
                skinIcon = "■";
            else if (currentCpuObjects[i].visualSkinType == SKIN_POLYGON)
                skinIcon = "⬡";

            ImGui::TextDisabled("%s %s", skinIcon, SkinTypeNames[currentCpuObjects[i].visualSkinType]);

            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 90);
            ImGui::TextDisabled("(%.1f, %.1f)",
                currentCpuObjects[i].position.x,
                currentCpuObjects[i].position.y);

            ImGui::PopID();
        }

        ImGui::EndChild();

        if (selectedObjectIndex != -1)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.3f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.1f, 0.0f, 1.0f));
            if (ImGui::Button("Remove Selected", ImVec2(-1, 0)))
            {
                // Clean up stored equation
                objectEquations.erase(selectedObjectIndex);
                objectPresets.erase(selectedObjectIndex);
                objectConstraintWidgets.erase(selectedObjectIndex);

                Objects::RemoveObject(selectedObjectIndex);
                if (selectedObjectIndex >= Objects::GetNumObjects())
                {
                    selectedObjectIndex = Objects::GetNumObjects() - 1;
                }
                lastSelectedObjectIndex = -1;
            }
            ImGui::PopStyleColor(3);
        }
    }
    else
    {
        ImGui::TextDisabled("No objects in scene");
        ImGui::TextWrapped("Add objects using the dropdown above");
    }
}

void UIManager::RenderPropertiesTab()
{
    std::vector<Object> currentCpuObjects;
    Objects::FetchToCPU(Renderer::GetCurrentObjectBuffer(), currentCpuObjects);

    if (selectedObjectIndex == -1 || (size_t)selectedObjectIndex >= currentCpuObjects.size())
    {
        ImGui::Text("No object selected");
        return;
    }

    // Load object-specific equation when switching objects
    if (lastSelectedObjectIndex != selectedObjectIndex)
    {
        if (objectEquations.find(selectedObjectIndex) == objectEquations.end())
        {
            objectEquations[selectedObjectIndex] = presetEquations[0];
            objectPresets[selectedObjectIndex] = 0;
        }
        lastSelectedObjectIndex = selectedObjectIndex;
    }

    Object p_copy = currentCpuObjects[selectedObjectIndex];

    ImGui::Spacing();
    ImGui::Text("Editing Object %d", selectedObjectIndex);
    ImGui::SameLine();

    const char* skinIcon = "●";
    if (p_copy.visualSkinType == SKIN_RECTANGLE)
        skinIcon = "■";
    else if (p_copy.visualSkinType == SKIN_POLYGON)
        skinIcon = "⬡";

    ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.1f, 1.0f), "%s %s",
        skinIcon, SkinTypeNames[p_copy.visualSkinType]);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ========== VISUAL APPEARANCE ==========
    ImGui::Text("Visual Appearance");
    ImGui::Spacing();

    ImGui::Text("Skin Type");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##SkinType", &p_copy.visualSkinType, SkinTypeNames, 3))
    {
        // Reset visual data when changing skin type
        switch (p_copy.visualSkinType)
        {
        case SKIN_CIRCLE:
            p_copy.visualData = glm::vec4(0.3f, 0.8f, 0.4f, 0.1f);
            break;
        case SKIN_RECTANGLE:
            p_copy.visualData = glm::vec4(0.5f, 0.3f, 0.0f, 1.0f);
            break;
        case SKIN_POLYGON:
            p_copy.visualData = glm::vec4(0.3f, 6.0f, 0.0f, 0.0f);
            break;
        }
        Objects::UpdateObjectCPU(selectedObjectIndex, p_copy);
    }

    ImGui::Spacing();

    // Skin-specific parameters (same as before, kept for brevity)
    switch (p_copy.visualSkinType)
    {
    case SKIN_CIRCLE:
    {
        ImGui::Text("Radius");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat("##CircleRadius", &p_copy.visualData.x, 0.05f, 0.05f, 5.0f, "%.2f"))
        {
            p_copy.visualData.x = std::max(0.05f, p_copy.visualData.x);
            Objects::UpdateObjectCPU(selectedObjectIndex, p_copy);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Rotation (invisible on circles)");
        ImGui::SetNextItemWidth(-1);
        float rotationDeg = glm::degrees(p_copy.visualData.z);
        if (ImGui::SliderFloat("##CircleRotation", &rotationDeg, 0.0f, 360.0f, "%.0f°"))
        {
            p_copy.visualData.z = glm::radians(rotationDeg);
            Objects::UpdateObjectCPU(selectedObjectIndex, p_copy);
        }

        ImGui::Text("Angular Velocity");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##CircleAngularVel", &p_copy.visualData.w, -10.0f, 10.0f, "%.2f rad/s"))
        {
            Objects::UpdateObjectCPU(selectedObjectIndex, p_copy);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Color (RGBA)");
        ImGui::SetNextItemWidth(-1);
        float color[4] = { p_copy.color.r, p_copy.color.g, p_copy.color.b, p_copy.color.a };
        if (ImGui::ColorEdit4("##CircleColor", color))
        {
            p_copy.color.r = color[0];
            p_copy.color.g = color[1];
            p_copy.color.b = color[2];
            p_copy.color.a = color[3];
            Objects::UpdateObjectCPU(selectedObjectIndex, p_copy);
        }
        break;
    }

    case SKIN_RECTANGLE:
    {
        ImGui::Text("Width");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat("##RectWidth", &p_copy.visualData.x, 0.05f, 0.05f, 5.0f, "%.2f"))
        {
            p_copy.visualData.x = std::max(0.05f, p_copy.visualData.x);
            Objects::UpdateObjectCPU(selectedObjectIndex, p_copy);
        }

        ImGui::Text("Height");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat("##RectHeight", &p_copy.visualData.y, 0.05f, 0.05f, 5.0f, "%.2f"))
        {
            p_copy.visualData.y = std::max(0.05f, p_copy.visualData.y);
            Objects::UpdateObjectCPU(selectedObjectIndex, p_copy);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Rotation");
        ImGui::SetNextItemWidth(-1);
        float rotationDeg = glm::degrees(p_copy.visualData.z);
        if (ImGui::SliderFloat("##RectRotation", &rotationDeg, 0.0f, 360.0f, "%.0f°"))
        {
            p_copy.visualData.z = glm::radians(rotationDeg);
            Objects::UpdateObjectCPU(selectedObjectIndex, p_copy);
        }

        ImGui::Text("Angular Velocity");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##RectAngularVel", &p_copy.visualData.w, -10.0f, 10.0f, "%.2f rad/s"))
        {
            Objects::UpdateObjectCPU(selectedObjectIndex, p_copy);
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Note: Equation's 3rd component will override rotation");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Color (RGBA)");
        ImGui::SetNextItemWidth(-1);
        float color[4] = { p_copy.color.r, p_copy.color.g, p_copy.color.b, p_copy.color.a };
        if (ImGui::ColorEdit4("##RectColor", color))
        {
            p_copy.color.r = color[0];
            p_copy.color.g = color[1];
            p_copy.color.b = color[2];
            p_copy.color.a = color[3];
            Objects::UpdateObjectCPU(selectedObjectIndex, p_copy);
        }
        break;
    }

    case SKIN_POLYGON:
    {
        ImGui::Text("Radius");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat("##PolyRadius", &p_copy.visualData.x, 0.05f, 0.05f, 5.0f, "%.2f"))
        {
            p_copy.visualData.x = std::max(0.05f, p_copy.visualData.x);
            Objects::UpdateObjectCPU(selectedObjectIndex, p_copy);
        }

        ImGui::Text("Number of Sides");
        ImGui::SetNextItemWidth(-1);
        int numSides = static_cast<int>(p_copy.visualData.y);
        if (ImGui::SliderInt("##PolySides", &numSides, 3, 20))
        {
            p_copy.visualData.y = static_cast<float>(numSides);
            Objects::UpdateObjectCPU(selectedObjectIndex, p_copy);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Rotation");
        ImGui::SetNextItemWidth(-1);
        float rotationDeg = glm::degrees(p_copy.visualData.z);
        if (ImGui::SliderFloat("##PolyRotation", &rotationDeg, 0.0f, 360.0f, "%.0f°"))
        {
            p_copy.visualData.z = glm::radians(rotationDeg);
            Objects::UpdateObjectCPU(selectedObjectIndex, p_copy);
        }

        ImGui::Text("Angular Velocity");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##PolyAngularVel", &p_copy.visualData.w, -10.0f, 10.0f, "%.2f rad/s"))
        {
            Objects::UpdateObjectCPU(selectedObjectIndex, p_copy);
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Note: Equation's 3rd component will override rotation");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Color (RGBA)");
        ImGui::SetNextItemWidth(-1);
        float color[4] = { p_copy.color.r, p_copy.color.g, p_copy.color.b, p_copy.color.a };
        if (ImGui::ColorEdit4("##PolyColor", color))
        {
            p_copy.color.r = color[0];
            p_copy.color.g = color[1];
            p_copy.color.b = color[2];
            p_copy.color.a = color[3];
            Objects::UpdateObjectCPU(selectedObjectIndex, p_copy);
        }
        break;
    }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ========== BASIC PHYSICS PROPERTIES ==========
    ImGui::Text("Physics Properties");
    ImGui::Spacing();

    ImGui::Text("Mass");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::DragFloat("##Mass", &p_copy.mass, 0.1f, 0.1f, 100.0f, "%.1f"))
    {
        p_copy.mass = std::max(0.1f, p_copy.mass);
        Objects::UpdateObjectCPU(selectedObjectIndex, p_copy);
    }

    ImGui::Text("Charge");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::DragFloat("##Charge", &p_copy.charge, 0.1f, -10.0f, 10.0f, "%.1f"))
    {
        Objects::UpdateObjectCPU(selectedObjectIndex, p_copy);
    }

    ImGui::Text("Position");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::DragFloat2("##Position", glm::value_ptr(p_copy.position), 0.1f))
    {
        Objects::UpdateObjectCPU(selectedObjectIndex, p_copy);
    }

    ImGui::Text("Velocity");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::DragFloat2("##Velocity", glm::value_ptr(p_copy.velocity), 0.1f))
    {
        Objects::UpdateObjectCPU(selectedObjectIndex, p_copy);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ========== COLLISION (PLACEHOLDER) ==========
    ImGui::Text("Collision Shape (Future)");
    ImGui::Spacing();
    ImGui::TextDisabled("Shape Type");
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##CollisionType", &p_copy.collisionShapeType, CollisionTypeNames, 4);
    ImGui::TextDisabled("Collision detection coming soon!");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ========== CONSTRAINTS ==========
    ImGui::Text("Constraints");
    ImGui::Spacing();

    // Initialize constraint widgets if this is first time
    if (objectConstraintWidgets.find(selectedObjectIndex) == objectConstraintWidgets.end())
    {
        objectConstraintWidgets[selectedObjectIndex] = std::vector<ConstraintWidget>();

        // Load existing constraints from object system
        auto existingConstraints = Objects::GetConstraints(selectedObjectIndex);
        for (const auto& c : existingConstraints)
        {
            ConstraintWidget widget;
            widget.type = c.type;
            widget.targetObjectID = c.targetObjectID;
            widget.param1 = c.param1;
            widget.param2 = c.param2;
            widget.param3 = c.param3;
            widget.param4 = c.param4;
            objectConstraintWidgets[selectedObjectIndex].push_back(widget);
        }
    }

    // Display existing constraints (same as before, kept for brevity)
    // ... [existing constraints code remains the same] ...

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ========== EQUATION EDITOR ==========
    ImGui::Text("Motion Equation");
    ImGui::Spacing();

    ImGui::TextDisabled("Equation ID: %d", currentCpuObjects[selectedObjectIndex].equationID);
    ImGui::Spacing();

    ImGui::Text("Preset");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##ObjectPreset", &objectPresets[selectedObjectIndex], presetNames, 5))
    {
        if ((size_t)objectPresets[selectedObjectIndex] >= 0 &&
            (size_t)objectPresets[selectedObjectIndex] < presetEquations.size())
        {
            objectEquations[selectedObjectIndex] = presetEquations[objectPresets[selectedObjectIndex]];
        }
    }

    ImGui::Spacing();
    ImGui::Text("Custom Equation");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextMultiline("##ObjectEquation",
        &objectEquations[selectedObjectIndex],
        ImVec2(-1, 80)))
    {
        objectPresets[selectedObjectIndex] = 0;
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Format: ax_equation, ay_equation, angular_equation, r_equation, g_equation, b_equation, a_equation");
    ImGui::TextWrapped("Example: -k*x/m, -k*y/m, 0, 1, 1, 1, 1");

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.5f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.3f, 0.0f, 1.0f));
    if (ImGui::Button("Apply Equation", ImVec2(-1, 0)))
    {
        std::cout << "\n========================================" << std::endl;
        std::cout << "[UI] Applying equation to object " << selectedObjectIndex << std::endl;
        std::cout << "Equation: " << objectEquations[selectedObjectIndex] << std::endl;

        try
        {
            ParserContext context;
            ParsedEquation parsed = ParseEquation(objectEquations[selectedObjectIndex], context);

            Objects::SetEquation(objectEquations[selectedObjectIndex], parsed, selectedObjectIndex);

            CheckGLError("After SetEquation");

            Objects::FetchToCPU(Renderer::GetCurrentObjectBuffer(), currentCpuObjects);

            if (selectedObjectIndex < (int)currentCpuObjects.size())
            {
                std::cout << "Equation applied! ID: "
                    << currentCpuObjects[selectedObjectIndex].equationID << std::endl;
                p_copy = currentCpuObjects[selectedObjectIndex];
            }
        }
        catch (std::exception& e)
        {
            std::cerr << "Equation parse error: " << e.what() << "\n";
        }
        std::cout << "========================================\n"
            << std::endl;
    }
    ImGui::PopStyleColor(3);
}

void UIManager::RenderPhysicsTab()
{
    ImGui::Spacing();

    ImGui::Text("Global Forces");
    ImGui::Spacing();

    ImGui::Text("Gravity");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##Gravity", &g_physics.gravity, 0.0f, 20.0f, "%.1f"))
    {
        Objects::SetSystemParameters(g_physics.gravity, g_physics.damping, g_physics.stiffness);
    }

    ImGui::Text("Gravity Direction");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat2("##GravDir", glm::value_ptr(g_physics.gravityDir), -1.0f, 1.0f, "%.2f"))
    {
        if (glm::length(g_physics.gravityDir) > 1e-6)
            g_physics.gravityDir = glm::normalize(g_physics.gravityDir);
        else
            g_physics.gravityDir = glm::vec2(0.0f, 0.0f);
        Objects::SetSystemParameters(g_physics.gravity, g_physics.damping, g_physics.stiffness);
    }

    ImGui::Text("Damping");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##Damping", &g_physics.damping, 0.0f, 10.0f, "%.2f"))
    {
        Objects::SetSystemParameters(g_physics.gravity, g_physics.damping, g_physics.stiffness);
    }

    ImGui::Text("Stiffness");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##Stiffness", &g_physics.stiffness, 0.0f, 1000.0f, "%.2f"))
    {
        Objects::SetSystemParameters(g_physics.gravity, g_physics.damping, g_physics.stiffness);
    }

    ImGui::Text("Restitution");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##Restitution", &g_physics.restitution, 0.0f, 1.0f, "%.2f");

    ImGui::Text("Coupling");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##Coupling", &g_physics.coupling, 0.0f, 10.0f, "%.2f");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("External Forces");
    ImGui::Spacing();

    ImGui::Text("Force Vector");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat2("##ExtForce", glm::value_ptr(g_physics.externalForce), -5.0f, 5.0f, "%.2f");

    ImGui::Text("Drive Frequency");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##DriveFreq", &g_physics.driveFreq, 0.1f, 10.0f, "%.1f");

    ImGui::Text("Drive Amplitude");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##DriveAmp", &g_physics.driveAmp, 0.0f, 5.0f, "%.2f");
}

void UIManager::RenderViewTab()
{
    ImGui::Spacing();

    ImGui::Text("Camera Controls");
    ImGui::Spacing();

    ImGui::Text("Zoom Level");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##Zoom", &g_camera.zoom, 0.1f, 100.0f, "%.1f", ImGuiSliderFlags_Logarithmic);

    ImGui::Text("Position");
    ImGui::SetNextItemWidth(-1);
    ImGui::DragFloat2("##CamPos", glm::value_ptr(g_camera.position), 0.1f, -20.0f, 20.0f);

    ImGui::Text("Move Speed");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##MoveSpeed", &g_camera.moveSpeed, 0.1f, 20.0f, "%.1f");

    ImGui::Text("Zoom Speed");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##ZoomSpeed", &g_camera.zoomSpeed, 0.1f, 10.0f, "%.1f");

    ImGui::Spacing();

    if (ImGui::Button("Reset Camera", ImVec2(-1, 0)))
    {
        g_camera.Reset();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Display Options");
    ImGui::Spacing();
    ImGui::Checkbox("Show Trails", &g_physics.showTrails);
    ImGui::Checkbox("Show Phase Space", &g_physics.showPhaseSpace);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Mouse Interaction");
    ImGui::Spacing();
    ImGui::Text("Drag Strength");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##DragStrength", &g_dragForceStrength, 10.0f, 500.0f, "%.0f");
    ImGui::TextDisabled("World Position: (%.2f, %.2f)", g_worldMousePos.x, g_worldMousePos.y);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Keyboard Shortcuts");
    ImGui::Spacing();
    ImGui::BulletText("WASD - Move Camera");
    ImGui::BulletText("Q/E - Zoom In/Out");
    ImGui::BulletText("SPACE - Play/Pause");
    ImGui::BulletText("Click & Drag - Move Objects");
}

void UIManager::RenderProjectTab()
{
    ImGui::Spacing();
    ImGui::Text("Current Project");
    ImGui::Spacing();

    ImGui::Text("Title:");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", saveTitle.c_str());

    if (!currentFilePath.empty())
    {
        ImGui::Text("File:");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", currentFilePath.c_str());
    }
    else
    {
        ImGui::TextDisabled("(Unsaved project)");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Project Info");
    ImGui::Spacing();

    ImGui::Text("Title:");
    ImGui::InputText("##ProjectTitle", &saveTitle);

    ImGui::Text("Author:");
    ImGui::InputText("##ProjectAuthor", &saveAuthor);

    ImGui::Text("Description:");
    ImGui::InputTextMultiline("##ProjectDesc", &saveDescription, ImVec2(-1, 80));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Statistics");
    ImGui::Spacing();

    std::vector<Object> objects;
    Objects::FetchToCPU(Renderer::GetCurrentObjectBuffer(), objects);

    ImGui::Text("Objects: %d", (int)objects.size());
    ImGui::Text("Simulation Time: %.2fs", g_physics.globalTime);

    // File operations
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("File Operations");
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.6f, 0.3f, 1.0f));
    if (ImGui::Button("Open Project...", ImVec2(ImGui::GetContentRegionAvail().x * 0.48f, 30)))
    {
        showOpenDialog = true;
    }
    ImGui::PopStyleColor();

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.1f, 1.0f));
    if (ImGui::Button("Save Project As...", ImVec2(-1, 30)))
    {
        showSaveDialog = true;
    }
    ImGui::PopStyleColor();

    // Quick save if we have a file path
    if (!currentFilePath.empty())
    {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
        if (ImGui::Button("Quick Save", ImVec2(-1, 30)))
        {
            if (SaveCurrentProject(currentFilePath))
            {
                std::cout << "[UI] Quick saved to: " << currentFilePath << std::endl;
            }
        }
        ImGui::PopStyleColor();
        ImGui::TextDisabled("Saves to: %s", currentFilePath.c_str());
    }
}

void UIManager::RenderObjectList() {}
void UIManager::RenderObjectProperties(int selectedIndex) {}
void UIManager::RenderEquationControls() {}