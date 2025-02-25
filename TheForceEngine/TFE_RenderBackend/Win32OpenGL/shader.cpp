#include "glslParser.h"
#include <TFE_RenderBackend/shader.h>
#include <TFE_RenderBackend/vertexBuffer.h>
#include <TFE_System/system.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <GL/glew.h>
#include <vector>
#include <string>

namespace ShaderGL
{
	static const char* c_shaderAttrName[]=
	{
		"vtx_pos",   // ATTR_POS
		"vtx_nrm",   // ATTR_NRM
		"vtx_uv",    // ATTR_UV
		"vtx_uv1",   // ATTR_UV1
		"vtx_uv2",   // ATTR_UV2
		"vtx_uv3",   // ATTR_UV3
		"vtx_color", // ATTR_COLOR
	};

	static const s32 c_glslVersion[] = { 130, 330, 450 };
	static const GLchar* c_glslVersionString[] = { "#version 130\n", "#version 330\n", "#version 450\n" };
	static std::vector<char> s_buffers[2];
	static std::string s_defineString;

	// If you get an error please report on github. You may try different GL context version or GLSL version. See GL<>GLSL version table at the top of this file.
	bool CheckShader(GLuint handle, const char* desc)
	{
		GLint status = 0, log_length = 0;
		glGetShaderiv(handle, GL_COMPILE_STATUS, &status);
		glGetShaderiv(handle, GL_INFO_LOG_LENGTH, &log_length);
		if ((GLboolean)status == GL_FALSE)
		{
			TFE_System::logWrite(LOG_ERROR, "Shader", "Failed to compile %s!\n", desc);
		}

		if (log_length > 1)
		{
			std::vector<char> buf;
			buf.resize(size_t(log_length + 1));
			glGetShaderInfoLog(handle, log_length, NULL, (GLchar*)buf.data());
			TFE_System::logWrite(LOG_ERROR, "Shader", "Error: %s\n", buf.data());
		}
		return (GLboolean)status == GL_TRUE;
	}

	// If you get an error please report on GitHub. You may try different GL context version or GLSL version.
	bool CheckProgram(GLuint handle, const char* desc, ShaderVersion version)
	{
		GLint status = 0, log_length = 0;
		glGetProgramiv(handle, GL_LINK_STATUS, &status);
		glGetProgramiv(handle, GL_INFO_LOG_LENGTH, &log_length);
		if ((GLboolean)status == GL_FALSE)
		{
			TFE_System::logWrite(LOG_ERROR, "Shader", "Failed to link '%s' - with GLSL %s", desc, c_glslVersionString[version]);
		}

		if (log_length > 1)
		{
			std::vector<char> buf;
			buf.resize(size_t(log_length + 1));
			glGetShaderInfoLog(handle, log_length, NULL, (GLchar*)buf.data());
			TFE_System::logWrite(LOG_ERROR, "Shader", "Error: %s\n", buf.data());
		}

		return (GLboolean)status == GL_TRUE;
	}
}

bool Shader::create(const char* vertexShaderGLSL, const char* fragmentShaderGLSL, const char* defineString/* = nullptr*/, ShaderVersion version/* = SHADER_VER_COMPTABILE*/)
{
	// Create shaders
	m_shaderVersion = version;
	const GLchar* vertex_shader_with_version[3] = { ShaderGL::c_glslVersionString[m_shaderVersion], defineString ? defineString : "", vertexShaderGLSL };
	u32 vertHandle = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertHandle, 3, vertex_shader_with_version, NULL);
	glCompileShader(vertHandle);
	if (!ShaderGL::CheckShader(vertHandle, "vertex shader")) { return false; }

	const GLchar* fragment_shader_with_version[3] = { ShaderGL::c_glslVersionString[m_shaderVersion], defineString ? defineString : "", fragmentShaderGLSL };
	u32 fragHandle = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragHandle, 3, fragment_shader_with_version, NULL);
	glCompileShader(fragHandle);
	if (!ShaderGL::CheckShader(fragHandle, "fragment shader")) { return false; }

	m_gpuHandle = glCreateProgram();
	glAttachShader(m_gpuHandle, vertHandle);
	glAttachShader(m_gpuHandle, fragHandle);
	// Bind vertex attribute names to slots.
	for (u32 i = 0; i < ATTR_COUNT; i++)
	{
		glBindAttribLocation(m_gpuHandle, i, ShaderGL::c_shaderAttrName[i]);
	}
	glLinkProgram(m_gpuHandle);
	if (!ShaderGL::CheckProgram(m_gpuHandle, "shader program", m_shaderVersion)) { return false; }

	return m_gpuHandle != 0;
}

bool Shader::load(const char* vertexShaderFile, const char* fragmentShaderFile, u32 defineCount/* = 0*/, ShaderDefine* defines/* = nullptr*/, ShaderVersion version/* = SHADER_VER_COMPTABILE*/)
{
	m_shaderVersion = version;
	ShaderGL::s_buffers[0].clear();
	ShaderGL::s_buffers[1].clear();

	GLSLParser::parseFile(vertexShaderFile, ShaderGL::s_buffers[0]);
	GLSLParser::parseFile(fragmentShaderFile, ShaderGL::s_buffers[1]);

	ShaderGL::s_buffers[0].push_back(0);
	ShaderGL::s_buffers[1].push_back(0);

	// Build a string of defines.
	ShaderGL::s_defineString.clear();
	if (defineCount)
	{
		ShaderGL::s_defineString += "\r\n";
		for (u32 i = 0; i < defineCount; i++)
		{
			ShaderGL::s_defineString += "#define ";
			ShaderGL::s_defineString += defines[i].name;
			ShaderGL::s_defineString += " ";
			ShaderGL::s_defineString += defines[i].value;
			ShaderGL::s_defineString += "\r\n";
		}
		ShaderGL::s_defineString += "\r\n";
	}

	return create(ShaderGL::s_buffers[0].data(), ShaderGL::s_buffers[1].data(), ShaderGL::s_defineString.c_str(), m_shaderVersion);
}

void Shader::destroy()
{
	glDeleteProgram(m_gpuHandle);
	m_gpuHandle = 0;
}

void Shader::bind()
{
	glUseProgram(m_gpuHandle);
}

void Shader::unbind()
{
	glUseProgram(0);
}

s32 Shader::getVariableId(const char* name)
{
	return glGetUniformLocation(m_gpuHandle, name);
}

// For debugging.
s32 Shader::getVariables()
{
	s32 length;
	s32 size;
	GLenum type;
	char name[256];

	s32 count;
	glGetProgramiv(m_gpuHandle, GL_ACTIVE_UNIFORMS, &count);
	printf("Active Uniforms: %d\n", count);

	for (s32 i = 0; i < count; i++)
	{
		glGetActiveUniform(m_gpuHandle, (GLuint)i, 256, &length, &size, &type, name);
		printf("Uniform #%d Type: %u Name: %s\n", i, type, name);
	}

	s32 attribCount;
	glGetProgramiv(m_gpuHandle, GL_ACTIVE_ATTRIBUTES, &attribCount);
	printf("Active Attributes: %d\n", attribCount);

	for (s32 i = 0; i < attribCount; i++)
	{
		glGetActiveAttrib(m_gpuHandle, (GLuint)i, 256, &length, &size, &type, name);
		printf("Attribute #%d Type: %u Name: %s\n", i, type, name);
	}

	return count;
}

void Shader::bindTextureNameToSlot(const char* texName, s32 slot)
{
	const s32 curSlot = glGetUniformLocation(m_gpuHandle, texName);
	if (curSlot < 0 || slot < 0) { return; }

	bind();
	glUniform1i(curSlot, slot);
	unbind();
}

void Shader::setVariable(s32 id, ShaderVariableType type, const f32* data)
{
	if (id < 0) { return; }

	switch (type)
	{
	case SVT_SCALAR:
		glUniform1f(id, data[0]);
		break;
	case SVT_VEC2:
		glUniform2fv(id, 1, data);
		break;
	case SVT_VEC3:
		glUniform3fv(id, 1, data);
		break;
	case SVT_VEC4:
		glUniform4fv(id, 1, data);
		break;
	case SVT_ISCALAR:
		glUniform1i(id, *((s32*)&data[0]));
		break;
	case SVT_IVEC2:
		glUniform2iv(id, 1, (s32*)data);
		break;
	case SVT_IVEC3:
		glUniform3iv(id, 1, (s32*)data);
		break;
	case SVT_IVEC4:
		glUniform4iv(id, 1, (s32*)data);
		break;
	case SVT_MAT3x3:
		glUniformMatrix3fv(id, 1, false, data);
		break;
	case SVT_MAT4x3:
		glUniformMatrix4x3fv(id, 1, false, data);
		break;
	case SVT_MAT4x4:
		glUniformMatrix4fv(id, 1, false, data);
		break;
	}
}
