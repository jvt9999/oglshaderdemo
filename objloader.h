#pragma once

#include <inttypes.h>
#include <functional>
#include <vector>
#include <map>
#include <string>
#include <memory>

#include "glm/glm.hpp"
#include "GL/glew.h"

namespace ObjLoader
{
    struct Material
    {
        std::string m_name;
        std::string m_diffuseMap;
        std::string m_ambientMap;
        std::string m_specularColorMap;
        std::string m_specularMap;
        std::string m_alphaMap;
        std::string m_displacementMap;
        std::string m_bumpMap;
        glm::vec3 m_ambientColor = glm::vec3(0.0f);
        glm::vec3 m_diffuseColor = glm::vec3(0.0f);
        glm::vec3 m_specularColor = glm::vec3(0.0f);
        float m_shininess = 0.0f;
        float m_alpha = 0.0f;
        uint32_t m_illuminationType = 0;

        // Rendering Data
        GLuint m_diffuseTexId = 0;
        GLuint m_ambientTexId = 0;
        GLuint m_specularColorTexId = 0;
        GLuint m_specularMapTexId = 0;
        GLuint m_alphaTexId = 0;
        GLuint m_displacementTexId = 0;
        GLuint m_bumpTexId = 0;

        Material() {}
        Material(const char* name) : m_name(name) {}
    };
    
    struct SubMesh
    {
        SubMesh(const char* name) : m_name(name), m_material(nullptr) {}
        std::string m_name;
        Material* m_material;
        std::vector<unsigned int> m_indices;
        GLuint m_indexBuffer = 0;
    };
    struct MeshVertex
    {
        glm::vec4 m_position;
        glm::vec3 m_normal;
        glm::vec2 m_texCoord;
        MeshVertex() : m_position(0), m_normal(0), m_texCoord(0) {}

    };
    struct Mesh
    {
        Mesh(const char* name) : m_name(name) {}
        std::string m_name;
        std::vector<glm::vec4> m_positions;
        std::vector<glm::vec3> m_normals;
        std::vector<glm::vec2> m_texCoords;
        std::vector<MeshVertex> m_vertices;
        std::vector<std::unique_ptr<SubMesh>> m_subMeshes;
        GLuint m_vertexBuffer = 0;
        GLuint m_vao = 0;
    };
    
    class ObjectFile
    {
    public:
        typedef std::function<void(int, const char*)> fnErrFunc;

        ObjectFile(const char* dataPath);
        void setErrorCallback(fnErrFunc func);
        bool loadFile(const char* filename);
        bool initGraphics();
        bool destroyGraphics();
        void setVertexDescriptor();
        const std::vector<std::unique_ptr<Mesh>>& meshes() const { return m_meshes; }
    private:
        bool loadTextureFile(const char* filename, GLuint& outId);
        bool loadMaterialLibrary(const char* filename);
        fnErrFunc m_errorCallback;
        std::string m_dataPath;
        std::map<std::string, std::unique_ptr<Material>> m_materialLibrary;
        std::vector<std::unique_ptr<Mesh>> m_meshes;
    };
}
