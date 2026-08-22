#include "script_manager.h"
#include "objects.h"   
#include <glad/glad.h>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <sstream>

static std::string computeHash(const std::string& s) {
    std::hash<std::string> h;
    return std::to_string(h(s));
}

ScriptManager::ScriptManager() {}
ScriptManager::~ScriptManager() {
    for (auto& pair : scripts) {
        glDeleteProgram(pair.second.program);
    }
}

std::string ScriptManager::getCachePath(const std::string& source) const {
    std::string hash = computeHash(source);
    const char* vendor = (const char*)glGetString(GL_VENDOR);
    const char* version = (const char*)glGetString(GL_VERSION);
    std::string key = hash + "_" + (vendor ? vendor : "") + "_" + (version ? version : "");
    std::hash<std::string> h2;
    std::string finalKey = std::to_string(h2(key));
    std::string cacheDir = "./shader_cache/";
    std::filesystem::create_directories(cacheDir);
    return cacheDir + "jit_" + finalKey + ".bin";
}

bool ScriptManager::tryLoadCached(const std::string& source, GLuint& outProgram) {
    std::string path = getCachePath(source);
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    GLsizei binaryLength;
    GLenum binaryFormat;
    in.read(reinterpret_cast<char*>(&binaryLength), sizeof(binaryLength));
    in.read(reinterpret_cast<char*>(&binaryFormat), sizeof(binaryFormat));
    std::vector<unsigned char> binaryData(binaryLength);
    in.read(reinterpret_cast<char*>(binaryData.data()), binaryLength);
    in.close();

    GLuint prog = glCreateProgram();
    glProgramBinary(prog, binaryFormat, binaryData.data(), binaryLength);
    GLint status;
    glGetProgramiv(prog, GL_LINK_STATUS, &status);
    if (status == GL_TRUE) {
        outProgram = prog;
        std::cout << "[ScriptManager] Loaded cached JIT shader: " << path << std::endl;
        return true;
    }
    glDeleteProgram(prog);
    return false;
}

void ScriptManager::saveToCache(const std::string& source, GLuint program) {
    GLint binaryLength;
    glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &binaryLength);
    if (binaryLength <= 0) return;

    GLenum binaryFormat;
    std::vector<unsigned char> binaryData(binaryLength);
    glGetProgramBinary(program, binaryLength, &binaryLength, &binaryFormat, binaryData.data());

    std::string path = getCachePath(source);
    std::ofstream out(path, std::ios::binary);
    if (out) {
        out.write(reinterpret_cast<char*>(&binaryLength), sizeof(binaryLength));
        out.write(reinterpret_cast<char*>(&binaryFormat), sizeof(binaryFormat));
        out.write(reinterpret_cast<char*>(binaryData.data()), binaryLength);
        std::cout << "[ScriptManager] Saved JIT shader to cache: " << path << std::endl;
    }
}

bool ScriptManager::compileShader(const std::string& source, GLuint& outProgram) {
    // 1) Try cache
    if (tryLoadCached(source, outProgram)) {
        return true;
    }

    // 2) Compile fresh
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetShaderInfoLog(shader, 1024, nullptr, log);
        std::cerr << "[JIT] Compile error:\n" << log << std::endl;
        glDeleteShader(shader);
        return false;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetProgramInfoLog(program, 1024, nullptr, log);
        std::cerr << "[JIT] Link error:\n" << log << std::endl;
        glDeleteProgram(program);
        glDeleteShader(shader);
        return false;
    }
    glDeleteShader(shader);

    outProgram = program;
    saveToCache(source, program);
    return true;
}

int ScriptManager::registerScript(const std::string& source) {
    GLuint prog;
    if (!compileShader(source, prog)) {
        return -1;
    }
    int id = nextId++;
    scripts[id].program = prog;
    scripts[id].sourceHash = computeHash(source);
    return id;
}

GLuint ScriptManager::getProgram(int scriptId) const {
    auto it = scripts.find(scriptId);
    return (it != scripts.end()) ? it->second.program : 0;
}

void ScriptManager::setAsGlobal() {
    Objects::SetScriptManager(this);
}

void ScriptManager::unregisterScript(int script_id)
{
    auto it = scripts.find(script_id);
    if (it != scripts.end()) {
        glDeleteProgram(it->second.program);
        scripts.erase(it);
        agentIDs.erase(script_id);
    }
    agentIDs.erase(script_id);
}

// Agent tracking

void ScriptManager::markAsAgent(int id) {
    agentIDs.insert(id);
}

bool ScriptManager::isAgent(int id) const {
    return agentIDs.find(id) != agentIDs.end();
}

std::vector<int> ScriptManager::getAgentIDs() const {
    return std::vector<int>(agentIDs.begin(), agentIDs.end());
}