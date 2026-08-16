// script_manager.h
#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <glad/glad.h>

struct ScriptEntry {
    GLuint program = 0;
    std::string sourceHash;
};

class ScriptManager {
public:
    ScriptManager();
    ~ScriptManager();

    int  registerScript(const std::string& source);
    GLuint getProgram(int scriptId) const;

    // ---- Agent tracking ----
    void markAsAgent(int id);
    bool isAgent(int id) const;
    std::vector<int> getAgentIDs() const;

    void setAsGlobal();

private:
    std::unordered_map<int, ScriptEntry> scripts;
    int nextId = 0;

    // ---- Store agent script IDs ----
    std::unordered_set<int> agentIDs;

    // Cache helpers
    std::string getCachePath(const std::string& source) const;
    bool tryLoadCached(const std::string& source, GLuint& outProgram);
    void saveToCache(const std::string& source, GLuint program);
    bool compileShader(const std::string& source, GLuint& outProgram);
};