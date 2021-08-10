#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#include <chrono>

#include "GL/glew.h"
#include "GLFW/glfw3.h"
#include "glm/gtc/matrix_transform.hpp"
#include "objloader.h"
#include "util.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_win32.h"

const float degToRad = 3.1416f / 180.0f;
ObjLoader::ObjectFile g_sponza("../data");

GLuint g_blackTexture;
GLuint g_whiteTexture;
GLuint g_flatNormalTexture;

ImFont* g_uiFont = nullptr;
ImFont* g_codeFont = nullptr;

void update(GLFWwindow* window); 
void render(GLFWwindow* window);
void renderUI();

void errorHandler(int errCode, const char* errMessage)
{
    MessageBoxA(0, errMessage, "Error", MB_OK);
}

const unsigned int windowWidth = 1920;
const unsigned int windowHeight = 1080;

struct InputState
{
    glm::dvec2 m_lastMousePosition = { 0, 0 };
};
InputState g_inputState;

enum class LightType : int
{
    Unlit,
    Ambient,
    Directional,
    Spot,
    Point,
    NumLightTypes // Always at the last position
};

enum class ShaderType : int
{
    Ambient,
    Directional,
    Spot,
    Point,
    NumShaderTypes // Always at the last position
};

struct DirectionalLight
{
    glm::vec3 m_lightDirection;
    glm::vec3 m_lightColor;
};

struct SpotLight
{
    glm::vec3 m_lightPosition;
    glm::vec3 m_lightDirection;
    glm::vec3 m_lightColor;
    float m_innerCone;
    float m_outerCone;
};

struct PointLight
{
    glm::vec3 m_lightPosition;
    glm::vec3 m_lightColor;
    float m_outerRadius;
};

const size_t MaxShaderLength = 8192;
struct ShaderState
{
    std::string m_shaderFile;
    std::vector<char> m_shaderCode;
    GLuint m_shaderId = 0;
    ShaderState()
    {
        m_shaderCode.resize(MaxShaderLength);
    }
};

struct DemoState
{
    glm::vec3 m_cameraPosition = glm::vec3(-900.0f, 200.0f, 0);
    glm::vec3 m_cameraDirection = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 m_cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 m_ambientColor = glm::vec3(0.25f, 0.25f, 0.25f);
    DirectionalLight m_directionalLight;
    SpotLight m_spotLight;
    PointLight m_pointLight;
    LightType m_lightType = LightType::Unlit;
    float m_specularMultiplier = 0.0f;
    float m_specPowerMultiplier = 32.0f;
    float m_camFov = 60.0f;
    double m_appTime = 0.0f;
    double m_dt = 0.0f;
    bool m_lightFollowsCamera = false;

    // Player Movement
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    float m_moveSpeed = 400.0f;
    float m_sensitivity = 0.4f;

    // Shader editing
    ShaderState m_pixelShaders[(int)ShaderType::NumShaderTypes];
    ShaderState m_vertexShader;
    bool m_recompileShaders = false;
    bool m_reloadShaders = false;
    bool m_isEditing = false;
    std::string m_shaderErrors;

    DemoState()
    {
        m_directionalLight.m_lightColor = glm::vec3(1.0f, 1.0f, 1.0f);
        m_directionalLight.m_lightDirection = glm::vec3(0.826f, -0.311f, -0.471f);

        m_spotLight.m_lightColor = glm::vec3(1.0f, 1.0f, 1.0f);
        m_spotLight.m_lightPosition = glm::vec3(-775.0f, 800.0f, -50.0f);
        m_spotLight.m_lightDirection = glm::vec3(0.91f, -0.415f, 0.016f);
        m_spotLight.m_innerCone = 9.0f;
        m_spotLight.m_outerCone = 10.0f;

        m_pointLight.m_lightColor = glm::vec3(1.0f, 1.0f, 1.0f);
        m_pointLight.m_lightPosition = glm::vec3(535.0f, 162.0f, -13.0f);
        m_pointLight.m_outerRadius = 865.0f;
    }
};
DemoState g_demoState;

bool readShaderFromFile(const char* filename, ShaderState& output)
{
    output.m_shaderFile = filename;
    return Util::loadFileToBuffer(filename, output.m_shaderCode, true, true);
}

bool reloadShaders(bool reopen, std::string* errorString)
{
    if (!readShaderFromFile("../shaders/vertex.glsl", g_demoState.m_vertexShader))
    {
        if (errorString)
            *errorString = "Cannot read vertex shader from file!";
        return false;
    }

    const char* shaderFiles[] = {
        "../shaders/ambient.glsl",
        "../shaders/directional.glsl",
        "../shaders/spot.glsl",
        "../shaders/point.glsl"
    };

    for (size_t i = 0; i < (size_t)ShaderType::NumShaderTypes; i++)
    {
        if (reopen)
        {
            if (!readShaderFromFile(shaderFiles[i], g_demoState.m_pixelShaders[i]))
            {
                if (errorString)
                    *errorString = "Cannot read ambient shader from file!";
                return false;
            }
        }
        const char* vsCode = &g_demoState.m_vertexShader.m_shaderCode[0];
        const char* psCode = &g_demoState.m_pixelShaders[i].m_shaderCode[0];
        GLuint shader = Util::createShaderProgram(vsCode, psCode, errorString);
        if (!shader)
        {
            return false;
        }
        if (g_demoState.m_pixelShaders[i].m_shaderId)
            glDeleteProgram(g_demoState.m_pixelShaders[i].m_shaderId);
        g_demoState.m_pixelShaders[i].m_shaderId = shader;
    }
    return true;
}

int __stdcall WinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR lpCmdLine,
    int nCmdShow
)
{
    glfwSetErrorCallback(errorHandler);
    glfwInit();
    glfwWindowHint(GLFW_SAMPLES, 8);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* mainWindow = glfwCreateWindow(windowWidth, windowHeight, "Shaders", nullptr, nullptr);
    if (mainWindow == nullptr)
    {
        return -1;
    }

    glfwMakeContextCurrent(mainWindow);
    glewExperimental = true;
    if (glewInit() != GLEW_OK)
    {
        return -1;
    }

    // Init ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(mainWindow, true);
    ImGui_ImplOpenGL3_Init("#version 150");
    
    g_uiFont = io.Fonts->AddFontFromFileTTF("../fonts/Roboto-Regular.ttf", 18);
    g_codeFont = io.Fonts->AddFontFromFileTTF("../fonts/RobotoMono-Regular.ttf", 18);
    
    ImGuiStyle& guiStyle = ImGui::GetStyle();
    guiStyle.FrameRounding = 8;
    guiStyle.WindowRounding = 8;
    
    // Load our shaders
    std::string errString;
    if (!reloadShaders(true, &errString))
    {
        MessageBoxA(nullptr, errString.c_str(), "Error", MB_OK);
        return -1;
    }
    
    // Initialize black and white textures
    {
        uint32_t black = 0xFF000000;
        uint32_t white = 0xFFFFFFFF;
        uint32_t flatNormal = 0xFFFF8080;

        glGenTextures(1, &g_blackTexture);
        glBindTexture(GL_TEXTURE_2D, g_blackTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &black);
        glGenerateMipmap(g_blackTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glGenTextures(1, &g_whiteTexture);
        glBindTexture(GL_TEXTURE_2D, g_whiteTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
        glGenerateMipmap(g_whiteTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glGenTextures(1, &g_flatNormalTexture);
        glBindTexture(GL_TEXTURE_2D, g_flatNormalTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &flatNormal);
        glGenerateMipmap(g_flatNormalTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
  
    // Load our 3d Model
    g_sponza.setErrorCallback(errorHandler);
    g_sponza.loadFile("sponza.obj");
    g_sponza.initGraphics();

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    std::chrono::high_resolution_clock::time_point prevTime = std::chrono::high_resolution_clock::now();
    
    glfwGetCursorPos(mainWindow, &g_inputState.m_lastMousePosition.x, &g_inputState.m_lastMousePosition.y);
    while(!glfwWindowShouldClose(mainWindow))
    {
        glfwPollEvents();

        if (g_demoState.m_reloadShaders)
        {
            g_demoState.m_shaderErrors = "";
            reloadShaders(true, &g_demoState.m_shaderErrors);
            g_demoState.m_reloadShaders = false;
        }
        if (g_demoState.m_recompileShaders)
        {
            g_demoState.m_shaderErrors = "";
            reloadShaders(false, &g_demoState.m_shaderErrors);
            g_demoState.m_recompileShaders = false;
        }
        update(mainWindow);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
        
        render(mainWindow);
        renderUI();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(mainWindow, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(mainWindow);
        std::chrono::high_resolution_clock::time_point currentTime = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> frameTime = std::chrono::duration_cast<std::chrono::duration<double>>(currentTime - prevTime);
        prevTime = currentTime;
        g_demoState.m_dt = frameTime.count();
        g_demoState.m_appTime += frameTime.count();
    }

    // Cleanup
    g_sponza.destroyGraphics();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(mainWindow);
    glfwTerminate();
    return 0;
}

void render(GLFWwindow* window)
{
    glm::vec3 camPosition = g_demoState.m_cameraPosition;
    glm::vec3 camDir = glm::normalize(g_demoState.m_cameraDirection);
    glm::vec3 camUp = glm::normalize(g_demoState.m_cameraUp);


    GLuint shaderProgram = 0;

    // Setup light parameters
    if (g_demoState.m_lightType == LightType::Unlit)
    {
        shaderProgram = g_demoState.m_pixelShaders[(int)ShaderType::Ambient].m_shaderId;
        glm::vec3 ambient(1.0f);
        glUniform3fv(glGetUniformLocation(shaderProgram, "ambientColor"), 1, &ambient[0]);
        
    }
    else if (g_demoState.m_lightType == LightType::Ambient)
    {
        shaderProgram = g_demoState.m_pixelShaders[(int)ShaderType::Ambient].m_shaderId;
        glUniform3fv(glGetUniformLocation(shaderProgram, "ambientColor"), 1, &g_demoState.m_ambientColor[0]);
    }
    else if (g_demoState.m_lightType == LightType::Directional)
    {
        shaderProgram = g_demoState.m_pixelShaders[(int)ShaderType::Directional].m_shaderId;
        glm::vec3 lightDir = glm::normalize(g_demoState.m_directionalLight.m_lightDirection);
        glUniform3fv(glGetUniformLocation(shaderProgram, "lightDir"), 1, &lightDir[0]);
        glUniform3fv(glGetUniformLocation(shaderProgram, "lightColor"), 1, &g_demoState.m_directionalLight.m_lightColor[0]);
        glUniform3fv(glGetUniformLocation(shaderProgram, "ambientColor"), 1, &g_demoState.m_ambientColor[0]);
        glUniform1f(glGetUniformLocation(shaderProgram, "globalSpecMultiplier"), g_demoState.m_specularMultiplier);
        glUniform3fv(glGetUniformLocation(shaderProgram, "cameraPos"), 1, &g_demoState.m_cameraPosition[0]);
    }
    else if (g_demoState.m_lightType == LightType::Spot)
    {
        shaderProgram = g_demoState.m_pixelShaders[(int)ShaderType::Spot].m_shaderId;
        glm::vec3 lightDir = glm::normalize(g_demoState.m_spotLight.m_lightDirection);
        glUniform3fv(glGetUniformLocation(shaderProgram, "lightDir"), 1, &lightDir[0]);
        glUniform3fv(glGetUniformLocation(shaderProgram, "lightPos"), 1, &g_demoState.m_spotLight.m_lightPosition[0]);
        glUniform3fv(glGetUniformLocation(shaderProgram, "lightColor"), 1, &g_demoState.m_spotLight.m_lightColor[0]);
        glUniform1f(glGetUniformLocation(shaderProgram, "lightInnerCone"), g_demoState.m_spotLight.m_innerCone * degToRad);
        glUniform1f(glGetUniformLocation(shaderProgram, "lightOuterCone"), g_demoState.m_spotLight.m_outerCone * degToRad);
        glUniform3fv(glGetUniformLocation(shaderProgram, "ambientColor"), 1, &g_demoState.m_ambientColor[0]);
        glUniform1f(glGetUniformLocation(shaderProgram, "globalSpecMultiplier"), g_demoState.m_specularMultiplier);
        glUniform3fv(glGetUniformLocation(shaderProgram, "cameraPos"), 1, &g_demoState.m_cameraPosition[0]);
    }
    else if (g_demoState.m_lightType == LightType::Point)
    {
        shaderProgram = g_demoState.m_pixelShaders[(int)ShaderType::Point].m_shaderId;
        glUniform3fv(glGetUniformLocation(shaderProgram, "lightPos"), 1, &g_demoState.m_pointLight.m_lightPosition[0]);
        glUniform3fv(glGetUniformLocation(shaderProgram, "lightColor"), 1, &g_demoState.m_pointLight.m_lightColor[0]);
        glUniform1f(glGetUniformLocation(shaderProgram, "lightOuterRadius"), g_demoState.m_pointLight.m_outerRadius);
        glUniform3fv(glGetUniformLocation(shaderProgram, "ambientColor"), 1, &g_demoState.m_ambientColor[0]);
        glUniform1f(glGetUniformLocation(shaderProgram, "globalSpecMultiplier"), g_demoState.m_specularMultiplier);
        glUniform3fv(glGetUniformLocation(shaderProgram, "cameraPos"), 1, &g_demoState.m_cameraPosition[0]);
    }

    // Setup matrices
    int vpWidth, vpHeight;
    glfwGetFramebufferSize(window, &vpWidth, &vpHeight);

    glm::mat4x4 world(glm::vec4(1, 0, 0, 0),
                        glm::vec4(0, 1, 0, 0),
                        glm::vec4(0, 0, 1, 0),
                        glm::vec4(0, 0, 0, 1));

    glm::mat4x4 view = glm::lookAt(camPosition, camPosition + camDir, camUp);
    glm::mat4x4 projection = glm::perspectiveFov(g_demoState.m_camFov * degToRad, (float)vpWidth, (float)vpHeight, 1.0f, 5000.0f);
    glm::mat4x4 wvp = projection * view * world;

    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "worldViewProjection"), 1, GL_FALSE, &wvp[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "world"), 1, GL_FALSE, &world[0][0]);

    glUseProgram(shaderProgram);
    
    for(const std::unique_ptr<ObjLoader::Mesh>& mesh : g_sponza.meshes())
    {
        glBindVertexArray(mesh->m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, mesh->m_vertexBuffer);
        for (const std::unique_ptr<ObjLoader::SubMesh>& subMesh : mesh->m_subMeshes)
        {
            // Set material
            ObjLoader::Material* mat = subMesh->m_material;
            if (mat)
            {
                GLuint diffusetTex = mat->m_diffuseTexId ? mat->m_diffuseTexId : g_blackTexture;
                GLuint displacementTex = mat->m_displacementTexId ? mat->m_displacementTexId : g_flatNormalTexture;
                GLuint specColorTex = mat->m_specularColorTexId ? mat->m_specularColorTexId : g_whiteTexture;
                GLuint specPowerTex = mat->m_specularMapTexId ? mat->m_specularMapTexId : g_whiteTexture;
                GLfloat shiny = std::min(1.0f, mat->m_shininess);
                glActiveTexture(GL_TEXTURE0 + 0);
                glBindTexture(GL_TEXTURE_2D, diffusetTex);
                glUniform1i(glGetUniformLocation(shaderProgram, "diffuseTex"), 0);
                glActiveTexture(GL_TEXTURE0 + 1);
                glBindTexture(GL_TEXTURE_2D, displacementTex);
                glUniform1i(glGetUniformLocation(shaderProgram, "normalTex"), 1);
                glActiveTexture(GL_TEXTURE0 + 2);
                glBindTexture(GL_TEXTURE_2D, specColorTex);
                glUniform1i(glGetUniformLocation(shaderProgram, "specularColorTex"), 2);
                glActiveTexture(GL_TEXTURE0 + 3);
                glBindTexture(GL_TEXTURE_2D, specPowerTex);
                glUniform1i(glGetUniformLocation(shaderProgram, "specularPowerTex"), 3);

                glUniform1f(glGetUniformLocation(shaderProgram, "shininess"), g_demoState.m_specPowerMultiplier);
            }
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, subMesh->m_indexBuffer);
            glDrawElements(GL_TRIANGLES, (GLsizei)subMesh->m_indices.size(), GL_UNSIGNED_INT, 0);
        }
        glBindVertexArray(0);
    }
}

void update(GLFWwindow* window)
{
    glm::vec3& camPos = g_demoState.m_cameraPosition;
    glm::vec3& camDir = g_demoState.m_cameraDirection;
    glm::vec3& camUp = g_demoState.m_cameraUp;

    // Update camera direction
    float theta = (90.0f - g_demoState.m_pitch) * degToRad;
    float phi = g_demoState.m_yaw * degToRad;
    float cosPhi = cosf(phi);
    float cosTheta = cosf(theta);
    float sinPhi = sinf(phi);
    float sinTheta = sinf(theta);

    glm::vec3 tempDir(cosPhi * sinTheta, cosTheta, sinPhi * sinTheta);
    camDir = glm::normalize(tempDir);

    glm::vec3 camSide = glm::cross(camDir, glm::vec3(0.0f, 1.0f, 0.0f));
    camUp = glm::cross(camSide, camDir);
    camSide = glm::cross(camDir, camUp);

    // Update camera pos
    float realMoveSpeed = g_demoState.m_moveSpeed * (float)g_demoState.m_dt;

    // Update input
    if (!g_demoState.m_isEditing)
    {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        {
            camPos += camDir * realMoveSpeed;
        }
        else if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        {
            camPos -= camDir * realMoveSpeed;
        }

        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        {
            camPos -= camSide * realMoveSpeed;
        }
        else if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        {
            camPos += camSide * realMoveSpeed;
        }
    }

    camDir = glm::normalize(camDir);

    glm::dvec2 curMouse;
    glfwGetCursorPos(window, &curMouse.x, &curMouse.y);
    glm::dvec2 deltaMouse = curMouse - g_inputState.m_lastMousePosition;
    g_inputState.m_lastMousePosition = curMouse;

    if (glfwGetMouseButton(window, 1) == GLFW_PRESS)
    {
        g_demoState.m_pitch -= (float)deltaMouse.y * g_demoState.m_sensitivity;
        g_demoState.m_yaw += (float)deltaMouse.x * g_demoState.m_sensitivity;
        g_demoState.m_pitch = glm::clamp(g_demoState.m_pitch, -89.0f, 89.0f);
    }

    // Update lights
    if (g_demoState.m_lightFollowsCamera)
    {
        g_demoState.m_spotLight.m_lightPosition = camPos;
        g_demoState.m_pointLight.m_lightPosition = camPos;

        g_demoState.m_directionalLight.m_lightDirection = camDir;
        g_demoState.m_spotLight.m_lightDirection = camDir;
    }

}

void renderUI()
{
    ImGui::Begin("Shader Demo");
    // FPS
    //int fps = (int)(1.0f / g_demoState.m_dt);
    //ImGui::Text("%d frames/second", fps);
    // Camera Params
    ImGui::Text("Controls");
    ImGui::SliderFloat("FOV##fov", &g_demoState.m_camFov, 20.0f, 90.0f);
    ImGui::SliderFloat("Move Speed##movespeed", &g_demoState.m_moveSpeed, 100.0f, 1000.0f);
    ImGui::SliderFloat("Sensitivity##sensitivity", &g_demoState.m_sensitivity, 0.1f, 1.0f);

    if (ImGui::CollapsingHeader("Lights"))
    {
        const char* lightTypes[] = { "Unlit", "Ambient", "Directional", "Spot Light", "Point Light" };
        ImGui::Combo("Light Type##lighttype", (int*)&g_demoState.m_lightType, lightTypes, IM_ARRAYSIZE(lightTypes));
        ImGui::ColorEdit3("Ambient##ambientColor", &g_demoState.m_ambientColor[0], 0);
        ImGui::SliderFloat("Specular Multiplier##lightSpecMult", &g_demoState.m_specularMultiplier, 0.0f, 2.0f);
        ImGui::SliderFloat("Specular Power Multiplier##lightSpecPowMult", &g_demoState.m_specPowerMultiplier, 1.0f, 256.0f);
        ImGui::Checkbox("Follow Camera", &g_demoState.m_lightFollowsCamera);
        if (g_demoState.m_lightType == LightType::Directional)
        {
            ImGui::ColorEdit3("Color##spotcolor", &g_demoState.m_directionalLight.m_lightColor[0], 0);
            ImGui::SliderFloat3("Direction##d1", &g_demoState.m_directionalLight.m_lightDirection[0], -1.0f, 1.0f);
        }
        else if (g_demoState.m_lightType == LightType::Spot)
        {
            ImGui::ColorEdit3("Color##spotcolor", &g_demoState.m_spotLight.m_lightColor[0], 0);
            ImGui::SliderFloat("Inner Cone##innercone", &g_demoState.m_spotLight.m_innerCone, 0.0f, 20.0f);
            ImGui::SliderFloat("Outer Cone##outercone", &g_demoState.m_spotLight.m_outerCone, 0.0f, 20.0f);

            ImGui::SliderFloat3("Direction##d2", &g_demoState.m_spotLight.m_lightDirection[0], -1.0f, 1.0f);
            ImGui::SliderFloat3("Position##p2", &g_demoState.m_spotLight.m_lightPosition[0], -1000.0f, 1000.0f);

            
            if (g_demoState.m_spotLight.m_innerCone > g_demoState.m_spotLight.m_outerCone - 0.01f)
                g_demoState.m_spotLight.m_innerCone = g_demoState.m_spotLight.m_outerCone - 0.01f;
        }
        else if (g_demoState.m_lightType == LightType::Point)
        {
            ImGui::ColorEdit3("Color##spotcolor", &g_demoState.m_pointLight.m_lightColor[0], 0);
            ImGui::SliderFloat("Radius##outerradius", &g_demoState.m_pointLight.m_outerRadius, 20.0f, 4000.0f);
            ImGui::SliderFloat3("Position##p3", &g_demoState.m_pointLight.m_lightPosition[0], -1000.0f, 1000.0f);
        }

        // Shaders
        {
            ShaderType currentShader;
            switch (g_demoState.m_lightType)
            {
            case LightType::Unlit:
            case LightType::Ambient:
                currentShader = ShaderType::Ambient;
                break;
            case LightType::Directional:
                currentShader = ShaderType::Directional;
                break;
            case LightType::Spot:
                currentShader = ShaderType::Spot;
                break;
            case LightType::Point:
                currentShader = ShaderType::Point;
                break;
            default:
                currentShader = ShaderType::Ambient;
            }
            ShaderState& curShader = g_demoState.m_pixelShaders[(int)currentShader];

            ImGui::Text("Shader Code:");
            if (g_codeFont)
                ImGui::PushFont(g_codeFont);
            ImGui::InputTextMultiline("Code##shadercode", &curShader.m_shaderCode[0], curShader.m_shaderCode.size(), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 24), ImGuiInputTextFlags_AllowTabInput);
            if (g_codeFont)
                ImGui::PopFont();

            g_demoState.m_isEditing = ImGui::IsItemFocused();
            static bool enableSave = false;
            ImGui::Checkbox("Enable Save##enablesave", &enableSave);
            if (!enableSave)
            {
                ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            }
            if (ImGui::Button("Save to disk##saveshader"))
            {
                for (size_t i = 0; i < (size_t)ShaderType::NumShaderTypes; i++)
                {
                    ShaderState& shaderToSave = g_demoState.m_pixelShaders[i];
                    if (shaderToSave.m_shaderFile.length())
                    {
                        FILE* f = fopen(shaderToSave.m_shaderFile.c_str(), "wb");
                        if (f)
                        {
                            int size = (int)strlen((const char*)&shaderToSave.m_shaderCode[0]);
                            fwrite(&shaderToSave.m_shaderCode[0], 1, size, f);
                            fclose(f);
                        }
                    }
                }
            }
            if (!enableSave)
            {
                ImGui::PopItemFlag();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload from disk##saveshader"))
            {
                g_demoState.m_reloadShaders = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Compile Shaders##compile"))
            {
                g_demoState.m_recompileShaders = true;
            }
            ImGui::TextWrapped(g_demoState.m_shaderErrors.c_str());
        }
    }

    ImGui::End();
}
