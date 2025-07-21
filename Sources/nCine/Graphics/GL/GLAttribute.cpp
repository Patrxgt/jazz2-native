#include "GLAttribute.h"
#include "GLDebug.h"
#include "../../../Main.h"

namespace nCine
{
	GLAttribute::GLAttribute()
		: location_(-1), size_(0), type_(GL_FLOAT)
	{
		name_[0] = '\0';
	}

	GLAttribute::GLAttribute(GLuint program, GLuint index)
		: location_(-1), size_(0), type_(GL_FLOAT)
	{
		GLsizei length;
		glGetActiveAttrib(program, index, MaxNameLength, &length, &size_, &type_, name_);
		DEATH_ASSERT(length <= MaxNameLength);

		if (!HasReservedPrefix()) {
			location_ = glGetAttribLocation(program, name_);
			if (location_ == -1) {
				LOGW("Attribute location not found for attribute \"{}\" ({}) in shader program {}", name_, index, program);
			}
		}
		GL_LOG_ERRORS();
	}

	GLenum GLAttribute::GetBasicType() const
	{
		switch (type_) {
			case GL_FLOAT:
			case GL_FLOAT_VEC2:
			case GL_FLOAT_VEC3:
			case GL_FLOAT_VEC4:
				return GL_FLOAT;
			case GL_INT:
			case GL_INT_VEC2:
			case GL_INT_VEC3:
			case GL_INT_VEC4:
				return GL_INT;
			case GL_BOOL:
			case GL_BOOL_VEC2:
			case GL_BOOL_VEC3:
			case GL_BOOL_VEC4:
				return GL_BOOL;
			case GL_UNSIGNED_INT:
			case GL_UNSIGNED_INT_VEC2:
			case GL_UNSIGNED_INT_VEC3:
			case GL_UNSIGNED_INT_VEC4:
				return GL_UNSIGNED_INT;
			default:
				LOGW("No available case to handle type: {}", type_);
				return type_;
		}
	}

	std::int32_t GLAttribute::GetComponentCount() const
	{
		switch (type_) {
			case GL_BYTE:
			case GL_UNSIGNED_BYTE:
			case GL_SHORT:
			case GL_UNSIGNED_SHORT:
				return 1;
			case GL_FLOAT:
			case GL_INT:
			case GL_BOOL:
			case GL_UNSIGNED_INT:
				return 1;
			case GL_FLOAT_VEC2:
			case GL_INT_VEC2:
			case GL_BOOL_VEC2:
			case GL_UNSIGNED_INT_VEC2:
				return 2;
			case GL_FLOAT_VEC3:
			case GL_INT_VEC3:
			case GL_BOOL_VEC3:
			case GL_UNSIGNED_INT_VEC3:
				return 3;
			case GL_FLOAT_VEC4:
			case GL_INT_VEC4:
			case GL_BOOL_VEC4:
			case GL_UNSIGNED_INT_VEC4:
				return 4;
			default:
				LOGW("No available case to handle type: {}", type_);
				return 0;
		}
	}

	bool GLAttribute::HasReservedPrefix() const
	{
		return (MaxNameLength >= 3 && name_[0] == 'g' && name_[1] == 'l' && name_[2] == '_');
	}
}
