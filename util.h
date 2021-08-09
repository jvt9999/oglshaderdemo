#pragma once
#include <string>
#include <vector>
#include "GL/glew.h"

namespace Util
{
    std::string combinePath(const char* partA, const char* partB);
    bool loadFileToBuffer(const char* filename, std::vector<char>& buffer, bool fixedBufferSize = false, bool nullTerminate = false);
    void readToBuffer(FILE* f, std::vector<char>& buf, bool fixedBufferSize = false, bool nullTerminate = false);
    void split(const char* str, char delim, std::vector<std::string>& retVal);
    bool compileShader(GLuint shader, std::string* errString);
    bool linkProgram(GLuint program, std::string* errString);
    GLuint createShaderProgram(const char* vertexShaderFile, const char* pixelShaderFile, std::string* errString);
}
