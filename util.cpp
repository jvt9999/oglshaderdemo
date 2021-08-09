#include "util.h"
#include <sstream>
#include <stdio.h>

std::string Util::combinePath(const char* partA, const char* partB)
{
    if (!partA || !partB)
        return std::string();

    std::stringstream ss;
    ss << partA;

    size_t slen = strlen(partA);
    if (slen > 0 && partA[slen] != '\\' && partA[slen] != '/')
    {
        ss << '/';
    }
    ss << partB;

    return ss.str();
}

bool Util::loadFileToBuffer(const char* filename, std::vector<char>& buffer, bool fixedBufferSize, bool nullTerminate)
{
    FILE* f = fopen(filename, "rb");
    if (f)
    {
        readToBuffer(f, buffer, fixedBufferSize, nullTerminate);
        fclose(f);
        return true;
    }
    return false;
}

void Util::readToBuffer(FILE* f, std::vector<char>& buf, bool fixedBufferSize, bool nullTerminate)
{
    // Get the filesize
    fseek(f, 0, SEEK_END);
    size_t bufSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    size_t readSize = bufSize;
    if (!fixedBufferSize)
    {
        buf.resize(nullTerminate ? bufSize + 1 : bufSize);
    }
    else
    {
        readSize = std::min(buf.size(), bufSize);
    }
    fread(&buf[0], 1, readSize, f);

    if (nullTerminate)
    {
        size_t nullPos = std::min(readSize, buf.size() - 1);
        buf[nullPos] = 0;
    }
}

void Util::split(const char* str, char delim, std::vector<std::string>& retVal)
{
    size_t sz = strlen(str);

    std::stringstream ss;
    for (size_t i = 0; i < sz; i++)
    {
        char c = str[i];
        if (c == delim || i == sz - 1)
        {
            if (i == sz - 1)
                ss << c;
            std::string str = ss.str();
            if (str.length() > 0)
                retVal.push_back(ss.str());
            ss.str("");
            ss.clear();
        }
        else
            ss << c;
    };
}

bool Util::compileShader(GLuint shader, std::string* errString)
{
    GLint compiled = 0;

    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled)
    {
        if (errString)
        {
            GLsizei len;

            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
            if (len > 1)
            {
                std::vector<char> infoBuffer;
                infoBuffer.resize(len);

                GLsizei infoSize;
                glGetShaderInfoLog(shader, len, &infoSize, &infoBuffer[0]);
                *errString = &infoBuffer[0];
            }
        }
        return false;
    }
    return true;
}

bool Util::linkProgram(GLuint program, std::string* errString)
{
    GLint linked = 0;

    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        if (errString)
        {
            GLsizei len;

            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
            if (len > 1)
            {
                std::vector<char> infoBuffer;
                infoBuffer.resize(len);

                GLsizei infoSize;
                glGetProgramInfoLog(program, len, &infoSize, &infoBuffer[0]);

                *errString = &infoBuffer[0];
            }
        }
        return false;
    }
    return true;
}

GLuint Util::createShaderProgram(const char* vsCode, const char* psCode, std::string* errString)
{
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vsCode, 0);
    if (!compileShader(vertexShader, errString))
    {
        glDeleteShader(vertexShader);
        return 0;
    }

    GLuint pixelShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(pixelShader, 1, &psCode, 0);
    if (!compileShader(pixelShader, errString))
    {
        glDeleteShader(pixelShader);
        return 0;
    }

    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, pixelShader);

    if (!linkProgram(shaderProgram, errString))
    {
        glDeleteShader(vertexShader);
        glDeleteShader(pixelShader);
        glDeleteShader(shaderProgram);
        return 0;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(pixelShader);
    return shaderProgram;
}
