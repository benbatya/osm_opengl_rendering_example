#pragma once

#include <GL/glew.h>

#include <optional>
#include <sstream>
#include <string>

struct ShaderProgram {

    void Build() {
        lastBuildLog_ = {};

        unsigned int vertexShader = CompileShader(GL_VERTEX_SHADER, vertexShaderSource_.c_str());
        unsigned int fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentShaderSource_.c_str());

        shaderProgram_ = glCreateProgram();
        glAttachShader(shaderProgram_.value(), vertexShader);
        glAttachShader(shaderProgram_.value(), fragmentShader);

        unsigned int geometryShader = 0;
        if (geometryShaderSource_.size() > 0) {
            geometryShader = CompileShader(GL_GEOMETRY_SHADER, geometryShaderSource_.c_str());
            glAttachShader(shaderProgram_.value(), geometryShader);
        }

        glLinkProgram(shaderProgram_.value());

        // check linking errors
        int success;

        glGetProgramiv(shaderProgram_.value(), GL_LINK_STATUS, &success);

        if (!success) {
            char infoLog[512];
            glGetProgramInfoLog(shaderProgram_.value(), 512, nullptr, infoLog);
            lastBuildLog_ << "Shader program linking failed: " << infoLog;
        }

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        if (geometryShaderSource_.size() > 0) {
            glDeleteShader(geometryShader);
        }
    }

    unsigned int CompileShader(unsigned int shaderType, const char *shaderSource) {
        unsigned int shader = glCreateShader(shaderType);
        glShaderSource(shader, 1, &shaderSource, nullptr);
        glCompileShader(shader);

        int success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

        if (!success) {
            char infoLog[512];
            glGetShaderInfoLog(shader, 512, nullptr, infoLog);
            lastBuildLog_ << "Shader compilation failed: " << infoLog;
        }

        return shader;
    }

    std::optional<unsigned int> shaderProgram_ = std::nullopt;

    std::string vertexShaderSource_{};
    std::string geometryShaderSource_{};
    std::string fragmentShaderSource_{};

    std::stringstream lastBuildLog_;
};
