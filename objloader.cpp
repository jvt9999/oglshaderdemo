#include "objloader.h"
#include <stdio.h>
#include <sstream>
#include <string.h>
#include <stdarg.h>
#include <unordered_map>
#include "memorystream.h"
#include "util.h"
#include "png.h"
using namespace ObjLoader;
using namespace Util;

static void error(ObjectFile::fnErrFunc errFunc, int id, const char* msg, ...)
{
    if (errFunc)
    {
        char buffer[512];
        va_list vl;
        va_start(vl, msg);
        vsprintf(buffer, msg, vl);
        errFunc(id, buffer);
        va_end(vl);
    }
}

// Only for png files!
GLuint loadTexture(const char* file, GLint wrapS = GL_REPEAT, GLint wrapT = GL_REPEAT)
{
    FILE* f = fopen(file, "rb");
    if (f)
    {
        png_byte header[8];
        fread(header, 1, 8, f);
        if (png_sig_cmp(header, 0, 8))
            return 0;
        png_struct* ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
        if (!ptr)
            return 0;
        png_info* info = png_create_info_struct(ptr);
        if (!info)
            return 0;

        if (setjmp(png_jmpbuf(ptr)))
            return 0;
        png_init_io(ptr, f);
        png_set_sig_bytes(ptr, 8);
        png_read_info(ptr, info);
        png_uint_32 width = png_get_image_width(ptr, info);
        png_uint_32 height = png_get_image_height(ptr, info);
        png_uint_32 colorType = png_get_color_type(ptr, info);
        png_uint_32 bitDepth = png_get_bit_depth(ptr, info);

        if (bitDepth == 16)
            png_set_strip_16(ptr);

        if (colorType == PNG_COLOR_TYPE_PALETTE)
            png_set_palette_to_rgb(ptr);
        if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8)
            png_set_expand_gray_1_2_4_to_8(ptr);
        if (colorType == PNG_COLOR_TYPE_RGB ||
            colorType == PNG_COLOR_TYPE_GRAY ||
            colorType == PNG_COLOR_TYPE_PALETTE)
            png_set_filler(ptr, 0xFF, PNG_FILLER_AFTER);
        
        if (colorType == PNG_COLOR_TYPE_GRAY ||
            colorType == PNG_COLOR_TYPE_GRAY_ALPHA)
            png_set_gray_to_rgb(ptr);

        if (png_get_valid(ptr, info, PNG_INFO_tRNS))
            png_set_tRNS_to_alpha(ptr);

        png_read_update_info(ptr, info);

        
        size_t rowSize = png_get_rowbytes(ptr, info);
        std::vector<png_byte> buffer;
        buffer.resize(rowSize * height);
        for (png_uint_32 y = 0; y < height; y++)
            png_read_row(ptr, &buffer[0] + rowSize * y, nullptr);
        //png_destroy_info_struct(ptr, &info);
        png_destroy_read_struct(&ptr, &info, nullptr);
        GLuint texId;
        glGenTextures(1, &texId);
        glBindTexture(GL_TEXTURE_2D, texId);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, &buffer[0]);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 16.0f);
        return texId;
    }
    return 0;
}


#define CANNOT_OPEN(file) { error(m_errorCallback, 1, "Cannot open file: '%s'", (file)); return false; }
#define CHECK_MESHGROUP_WITHOUT_MESH() if (currentMesh == nullptr) { error(m_errorCallback, 2, "Trying to create a meshgroup without an active mesh"); return false; }
#define CHECK_VERTS_WITHOUT_MESH() if (currentMesh == nullptr) { error(m_errorCallback, 3, "Trying to define vertices without an active mesh"); return false; }
#define CHECK_MAT_WITHOUT_MESHGROUP() if (currentMesh == nullptr) { error(m_errorCallback, 4, "Trying to use material outside of mesh group"); return false; }
#define CHECK_FACES_WITHOUT_MESHGROUP() if (currentMesh == nullptr) { error(m_errorCallback, 5, "Trying to define face outside of mesh group"); return false; }
#define UNKNOWN_FACE() { error(m_errorCallback, 10, "Unknown face format"); return false; }
#define UNKNOWN_MATERIAL(matName) { error(m_errorCallback, 11, "Unknown material: '%s'", (matName)); return false; }
#define MAT_EXISTS(matName) { error(m_errorCallback, 12, "Duplicate Material: '%s'", (matName)); return false; }
#define CHECK_MATDEF_WITHOUT_MAT() if (currentMaterial == nullptr) { error(m_errorCallback, 20, "Trying define material without an active material"); return false; }

struct VertexID
{
    int pidx;
    int tidx;
    int nidx;
    VertexID() : pidx(-1), tidx(-1), nidx(-1) {}
    VertexID(int posIdx, int texIdx = -1, int normalIdx = -1) : pidx(posIdx), tidx(texIdx), nidx(normalIdx) {}

    bool operator==(const VertexID& other) const
    {
        return pidx == other.pidx && tidx == other.tidx && nidx == other.tidx;
    }
};

namespace std
{
    template<>
    struct hash<VertexID>
    {
        size_t operator()(const VertexID& k) const
        {
            return ((size_t)k.pidx << 31 | (size_t)k.tidx) ^ ((size_t)k.nidx << 16);
        }
    };
}

ObjectFile::ObjectFile(const char* dataPath) : m_dataPath(dataPath)
{

}

void ObjectFile::setErrorCallback(fnErrFunc func)
{
    m_errorCallback = func;
}

bool ObjectFile::loadTextureFile(const char* filename, GLuint& outId, GLint wrapS, GLint wrapT)
{
    if (strlen(filename) > 0)
    {
        std::string texFile = combinePath(m_dataPath.c_str(), filename);
        outId = loadTexture(texFile.c_str(), wrapS, wrapT);
        return true;
    }
    return false;
}

bool ObjectFile::initGraphics()
{
    // Load material textures
    for(auto& iter : m_materialLibrary)
    {
        Material& material = *iter.second;
        loadTextureFile(material.m_diffuseMap.c_str(), material.m_diffuseTexId);
        loadTextureFile(material.m_specularColorMap.c_str(), material.m_specularColorTexId);
        loadTextureFile(material.m_specularMap.c_str(), material.m_specularMapTexId);
        loadTextureFile(material.m_ambientMap.c_str(), material.m_ambientTexId);
        loadTextureFile(material.m_displacementMap.c_str(), material.m_displacementTexId);
        loadTextureFile(material.m_bumpMap.c_str(), material.m_bumpTexId);
    }

    // Initialize Vertex and Index Buffers
    for(std::unique_ptr<Mesh>& mesh : m_meshes)
    {
        glGenVertexArrays(1, &mesh->m_vao);
        glBindVertexArray(mesh->m_vao);
        glGenBuffers(1, &mesh->m_vertexBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, mesh->m_vertexBuffer);
        glBufferData(GL_ARRAY_BUFFER, mesh->m_vertices.size() * sizeof(MeshVertex), &mesh->m_vertices[0], GL_STATIC_DRAW);
        setVertexDescriptor();
        glBindVertexArray(0);
        for (std::unique_ptr<SubMesh>& subMesh : mesh->m_subMeshes)
        {
            glGenBuffers(1, &subMesh->m_indexBuffer);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, subMesh->m_indexBuffer);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, subMesh->m_indices.size() * sizeof(unsigned int), &subMesh->m_indices[0], GL_STATIC_DRAW);
        }
    }
    return true;
}

bool ObjectFile::destroyGraphics()
{
    for (std::unique_ptr<Mesh>& mesh : m_meshes)
    {
        glDeleteBuffers(1, &mesh->m_vertexBuffer);
        glDeleteVertexArrays(1, &mesh->m_vao);
        for (std::unique_ptr<SubMesh>& subMesh : mesh->m_subMeshes)
        {
            glDeleteBuffers(1, &subMesh->m_indexBuffer);
        }
    }

    for (auto& iter : m_materialLibrary)
    {
        Material& material = *iter.second;
        glDeleteTextures(1, &material.m_diffuseTexId);
        glDeleteTextures(1, &material.m_specularColorTexId);
        glDeleteTextures(1, &material.m_specularMapTexId);
        glDeleteTextures(1, &material.m_ambientTexId);
        glDeleteTextures(1, &material.m_displacementTexId);
        glDeleteTextures(1, &material.m_bumpTexId);
    }
    return true;
}


void ObjectFile::setVertexDescriptor()
{
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)offsetof(MeshVertex, MeshVertex::m_position));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)offsetof(MeshVertex, MeshVertex::m_normal));
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)offsetof(MeshVertex, MeshVertex::m_tangent));
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)offsetof(MeshVertex, MeshVertex::m_texCoord));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
}

bool ObjectFile::loadFile(const char* filename)
{
    std::string filePath = combinePath(m_dataPath.c_str(), filename);
    FILE* f = fopen(filePath.c_str(), "rb");
    if (f)
    {
        std::vector<char> fileBuffer;
        readToBuffer(f, fileBuffer);

        MemoryStream ms(&fileBuffer[0], fileBuffer.size());
        TextReader<MemoryStream> reader(ms);

        Mesh* currentMesh = nullptr;
        SubMesh* currentSubMesh = nullptr;
        std::vector<std::string> parts;
        std::vector<std::string> sparts;
        std::unordered_map<VertexID, size_t> vertexMap;

        while (const char* line = reader.readLine())
        {
            if (line[0] == '#')
                continue;
            parts.clear();
            split(line, ' ', parts);
            if (parts.size() == 2)
            {
                if (parts[0] == "mtllib")
                {
                    if (!loadMaterialLibrary(parts[1].c_str()))
                        return false;
                }
                else if (parts[0] == "o")
                {
                    m_meshes.push_back(std::make_unique<Mesh>(parts[1].c_str()));
                    if (currentMesh)
                    {
                        currentMesh->m_positions.resize(0);
                        currentMesh->m_texCoords.resize(0);
                        currentMesh->m_normals.resize(0);
                    }
                    currentMesh = m_meshes.back().get();
                    currentSubMesh = nullptr;
                    vertexMap.clear();
                }
                else if (parts[0] == "g")
                {
                    CHECK_MESHGROUP_WITHOUT_MESH();
                    currentMesh->m_subMeshes.push_back(std::make_unique<SubMesh>(parts[1].c_str()));
                    currentSubMesh = currentMesh->m_subMeshes.back().get();
                }
                else if (parts[0] == "usemtl")
                {
                    CHECK_MAT_WITHOUT_MESHGROUP();
                    auto mtlIter = m_materialLibrary.find(parts[1]);
                    if (mtlIter == m_materialLibrary.end())
                    {
                        UNKNOWN_MATERIAL(parts[1].c_str());
                    }

                    currentSubMesh->m_material = mtlIter->second.get();
                }
            }
            else if (parts.size() == 3)
            {
                if (parts[0] == "vt")
                {
                    CHECK_VERTS_WITHOUT_MESH();
                    glm::vec2 uv;
                    uv[0] = (float)atof(parts[1].c_str());
                    uv[1] = 1.0f - (float)atof(parts[2].c_str());
                    currentMesh->m_texCoords.push_back(uv);
                }
            }
            else if (parts.size() == 4)
            {
                if (parts[0] == "v")
                {
                    CHECK_VERTS_WITHOUT_MESH();
                    glm::vec4 vec;
                    vec[0] = (float)atof(parts[1].c_str());
                    vec[1] = (float)atof(parts[2].c_str());
                    vec[2] = (float)atof(parts[3].c_str());
                    vec[3] = 1.0f;
                    currentMesh->m_positions.push_back(vec);
                }
                else if (parts[0] == "vt")
                {
                    CHECK_VERTS_WITHOUT_MESH();
                    glm::vec2 uv;
                    uv[0] = (float)atof(parts[1].c_str());
                    uv[1] = 1.0f - (float)atof(parts[2].c_str());
                    currentMesh->m_texCoords.push_back(uv);
                }
                else if (parts[0] == "vn")
                {
                    CHECK_VERTS_WITHOUT_MESH();
                    glm::vec3 vec;
                    vec[0] = (float)atof(parts[1].c_str());
                    vec[1] = (float)atof(parts[2].c_str());
                    vec[2] = (float)atof(parts[3].c_str());
                    currentMesh->m_normals.push_back(vec);
                }
                else if (parts[0] == "f")
                {
                    CHECK_FACES_WITHOUT_MESHGROUP();
                    for (int curIdx = 1; curIdx < 4; curIdx++)
                    {
                        sparts.clear();
                        split(parts[curIdx].c_str(), '/', sparts);
                        unsigned int pidx = -1;
                        unsigned int tidx = -1;
                        unsigned int nidx = -1;
                        pidx = atoi(sparts[0].c_str());
                        if (sparts.size() >= 1) // p/uv
                        {
                            tidx = atoi(sparts[1].c_str());
                        }
                        if (sparts.size() >= 2) // p/uv/n
                        {
                            nidx = atoi(sparts[2].c_str());
                        }
                        if (sparts.size() > 3)
                        {
                            UNKNOWN_FACE();
                        }
                        VertexID id(pidx, tidx, nidx);
                        auto viter = vertexMap.find(id);
                        unsigned int vertIdx = 0;
                        if (viter == vertexMap.end())
                        {
                            MeshVertex vert;

                            vert.m_position = currentMesh->m_positions[pidx-1];
                            if (currentMesh->m_texCoords.size() > 0 && tidx != -1)
                                vert.m_texCoord = currentMesh->m_texCoords[tidx-1];
                            if (currentMesh->m_normals.size() > 0 && nidx != -1)
                                vert.m_normal = currentMesh->m_normals[nidx-1];

                            vertIdx = (unsigned int)currentMesh->m_vertices.size();
                            currentMesh->m_vertices.push_back(vert);
                            vertexMap[id] = vertIdx;
                        }
                        else
                        {
                            vertIdx = (unsigned int)viter->second;
                        }

                        currentSubMesh->m_indices.push_back(vertIdx);
                    }
                }
            }
            else if (parts.size() == 5)
            {
                if (parts[0] == "v")
                {
                    CHECK_VERTS_WITHOUT_MESH();
                    glm::vec4 vec;
                    vec[0] = (float)atof(parts[1].c_str());
                    vec[1] = (float)atof(parts[2].c_str());
                    vec[2] = (float)atof(parts[3].c_str());
                    vec[3] = (float)atof(parts[4].c_str());
                    currentMesh->m_positions.push_back(vec);
                }
                else if (parts[0] == "f")
                {
                    CHECK_FACES_WITHOUT_MESHGROUP();
                    unsigned int indices[4];
                    unsigned int curIndex = 0;
                    for (int curIdx = 1; curIdx < 5; curIdx++)
                    {
                        sparts.clear();
                        split(parts[curIdx].c_str(), '/', sparts);
                        unsigned int pidx = -1;
                        unsigned int tidx = -1;
                        unsigned int nidx = -1;
                        pidx = atoi(sparts[0].c_str());
                        if (sparts.size() >= 1) // p/uv
                        {
                            tidx = atoi(sparts[1].c_str());
                        }
                        if (sparts.size() >= 2) // p/uv/n
                        {
                            nidx = atoi(sparts[2].c_str());
                        }
                        if (sparts.size() > 3)
                        {
                            UNKNOWN_FACE();
                        }
                        VertexID id(pidx, tidx, nidx);
                        auto viter = vertexMap.find(id);
                        unsigned int vertIdx = 0;
                        if (viter == vertexMap.end())
                        {
                            MeshVertex vert;

                            vert.m_position = currentMesh->m_positions[pidx - 1];
                            if (currentMesh->m_texCoords.size() > 0 && tidx != -1)
                                vert.m_texCoord = currentMesh->m_texCoords[tidx - 1];
                            if (currentMesh->m_normals.size() > 0 && nidx != -1)
                                vert.m_normal = currentMesh->m_normals[nidx - 1];

                            vertIdx = (unsigned int)currentMesh->m_vertices.size();
                            currentMesh->m_vertices.push_back(vert);
                            vertexMap[id] = vertIdx;
                        }
                        else
                        {
                            vertIdx = (unsigned int)viter->second;
                        }
                        indices[curIndex++] = vertIdx;
                    }
                    currentSubMesh->m_indices.push_back(indices[0]);
                    currentSubMesh->m_indices.push_back(indices[1]);
                    currentSubMesh->m_indices.push_back(indices[2]);
                    currentSubMesh->m_indices.push_back(indices[0]);
                    currentSubMesh->m_indices.push_back(indices[2]);
                    currentSubMesh->m_indices.push_back(indices[3]);

                }

            }
        };
        fclose(f);
        generateTangents();
    }
    else
    {
        CANNOT_OPEN(filename);
    }
    return true;
}

void ObjectFile::generateTangents()
{
    for (std::unique_ptr<Mesh>& mesh : m_meshes)
    {
        std::vector<MeshVertex>& vertices = mesh->m_vertices;
        std::vector<glm::vec3> binormals;
        binormals.resize(mesh->m_vertices.size());
        memset(&binormals[0], 0, sizeof(glm::vec3) * binormals.size());

        for (std::unique_ptr<SubMesh>& subMesh : mesh->m_subMeshes)
        {
            std::vector<unsigned int>& indices = subMesh->m_indices;
            size_t numFaces = indices.size() / 3;
            for (size_t face = 0; face < numFaces; face++)
            {
                MeshVertex& v1 = vertices[indices[face * 3 + 0]];
                MeshVertex& v2 = vertices[indices[face * 3 + 1]];
                MeshVertex& v3 = vertices[indices[face * 3 + 2]];

                glm::vec3 dp1 = v2.m_position - v1.m_position;
                glm::vec3 dp2 = v3.m_position - v1.m_position;
                glm::vec2 duv1 = v2.m_texCoord - v1.m_texCoord;
                glm::vec2 duv2 = v3.m_texCoord - v1.m_texCoord;

                float r = 1.0f / (duv1.x * duv2.y - duv1.y * duv2.x);
                glm::vec3 tangentVec = r * (dp1 * duv2.y - dp2 * duv1.y);
                glm::vec3 binormVec = r * (duv1.x * dp2 - duv2.x * dp1);
                v1.m_tangent += tangentVec;
                v2.m_tangent += tangentVec;
                v3.m_tangent += tangentVec;
                binormals[indices[face * 3 + 0]] += binormVec;
                binormals[indices[face * 3 + 1]] += binormVec;
                binormals[indices[face * 3 + 2]] += binormVec;
            }
        }

        for (size_t i = 0; i < vertices.size(); i++)
        {
            MeshVertex& vert = vertices[i];
            glm::vec3 normal = vert.m_normal;
            glm::vec3 tangent = vert.m_tangent;
            glm::vec3 binormal = binormals[i];

            vert.m_tangent = glm::normalize(tangent - normal * glm::dot(normal, tangent));

            if (glm::dot(glm::cross(tangent, normal), binormal) < 0.0f)
            {
                vert.m_tangent *= -1.0f;
            }
        }
    }
}

bool ObjectFile::loadMaterialLibrary(const char* filename)
{
    std::string filePath = combinePath(m_dataPath.c_str(), filename);
    FILE* f = fopen(filePath.c_str(), "rb");
    if (f)
    {
        std::vector<char> fileBuffer;
        readToBuffer(f, fileBuffer);

        MemoryStream ms(&fileBuffer[0], fileBuffer.size());
        TextReader<MemoryStream> reader(ms);

        std::vector<std::string> parts;

        Material* currentMaterial = nullptr;
        while (const char* line = reader.readLine())
        {
            if (line[0] == '#')
                continue;
            if (line[0] == '\t')
                line++;
            parts.clear();
            split(line, ' ', parts);
            if (parts.size() == 2)
            {
                if (parts[0] == "newmtl")
                {
                    auto miter = m_materialLibrary.find(parts[1]);
                    if (miter == m_materialLibrary.end())
                    {
                        auto insertResult = m_materialLibrary.insert(std::make_pair(parts[1], std::make_unique<Material>(parts[1].c_str())));
                        currentMaterial = insertResult.first->second.get();
                    }
                    else
                    {
                        MAT_EXISTS(parts[1]);
                    }
                }
                else if (parts[0] == "illum")
                {
                    CHECK_MATDEF_WITHOUT_MAT();
                    currentMaterial->m_illuminationType = atoi(parts[1].c_str());
                }
                else if (parts[0] == "Ns")
                {
                    CHECK_MATDEF_WITHOUT_MAT();
                    currentMaterial->m_shininess = (float)atof(parts[1].c_str());
                }
                else if (parts[0] == "d")
                {
                    CHECK_MATDEF_WITHOUT_MAT();
                    currentMaterial->m_alpha = (float)atof(parts[1].c_str());
                }
                else if (parts[0] == "Tr")
                {
                    CHECK_MATDEF_WITHOUT_MAT();
                    currentMaterial->m_alpha = 1.0f - (float)atof(parts[1].c_str());
                }
                else if (parts[0] == "map_Kd")
                {
                    CHECK_MATDEF_WITHOUT_MAT();
                    currentMaterial->m_diffuseMap = parts[1];
                }
                else if (parts[0] == "map_Ks")
                {
                    CHECK_MATDEF_WITHOUT_MAT();
                    currentMaterial->m_specularColorMap = parts[1];
                }
                else if (parts[0] == "map_Ns")
                {
                    CHECK_MATDEF_WITHOUT_MAT();
                    currentMaterial->m_specularMap = parts[1];
                }
                else if (parts[0] == "map_Disp" || parts[0] == "disp")
                {
                    CHECK_MATDEF_WITHOUT_MAT();
                    currentMaterial->m_displacementMap = parts[1];
                }
                else if (parts[0] == "map_bump" || parts[0] == "bump")
                {
                    CHECK_MATDEF_WITHOUT_MAT();
                    currentMaterial->m_bumpMap = parts[1];
                }
                else if (parts[0] == "map_Ka")
                {
                    CHECK_MATDEF_WITHOUT_MAT();
                    currentMaterial->m_ambientMap = parts[1];
                }
                else if (parts[0] == "map_d")
                {
                    CHECK_MATDEF_WITHOUT_MAT();
                    currentMaterial->m_alphaMap = parts[1];
                }
            }
            else if (parts.size() == 4)
            {
                if (parts[0] == "Ka")
                {
                    CHECK_MATDEF_WITHOUT_MAT();
                    glm::vec3 vec;
                    vec[0] = (float)atof(parts[1].c_str());
                    vec[1] = (float)atof(parts[2].c_str());
                    vec[2] = (float)atof(parts[3].c_str());
                    currentMaterial->m_ambientColor = vec;
                }
                else if (parts[0] == "Kd")
                {
                    CHECK_MATDEF_WITHOUT_MAT();
                    glm::vec3 vec;
                    vec[0] = (float)atof(parts[1].c_str());
                    vec[1] = (float)atof(parts[2].c_str());
                    vec[2] = (float)atof(parts[3].c_str());
                    currentMaterial->m_diffuseColor = vec;
                }
                else if (parts[0] == "Ks")
                {
                    CHECK_MATDEF_WITHOUT_MAT();
                    glm::vec3 vec;
                    vec[0] = (float)atof(parts[1].c_str());
                    vec[1] = (float)atof(parts[2].c_str());
                    vec[2] = (float)atof(parts[3].c_str());
                    currentMaterial->m_specularColor = vec;
                }
            }
        }
        fclose(f);
    }
    else
    {
        CANNOT_OPEN(filename);
    }
    return true;
}
