﻿#pragma once
#if COMPILE_OPEN_GL

#include "Window/GLFWWindowWrapper.h"

class GLWindowWrapper : public GLFWWindowWrapper
{
public:
	GLWindowWrapper(std::string title, glm::vec2i size, glm::vec2i pos, GameContext& gameContext);
	virtual ~GLWindowWrapper();

	virtual void SetSize(int width, int height) override;

private:

	GLWindowWrapper(const GLWindowWrapper&) = delete;
	GLWindowWrapper& operator=(const GLWindowWrapper&) = delete;
};

void WINAPI glDebugOutput(GLenum source, GLenum type, GLuint id, GLenum severity,
	GLsizei length, const GLchar *message, const void *userParam);

#endif // COMPILE_OPEN_GL