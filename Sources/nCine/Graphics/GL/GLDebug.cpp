#include "GLDebug.h"
#include "../IGfxCapabilities.h"
#include "../../Application.h"

#if !defined(DEATH_TARGET_ANDROID) && defined(WITH_OPENGLES) && defined(__linux__)
#	include <GLES3/gl32.h>
#endif

#if defined(DEATH_DEBUG) &&																				\
	((defined(DEATH_TARGET_ANDROID) && __ANDROID_API__ >= 21) || !defined(DEATH_TARGET_ANDROID)) &&		\
	!defined(DEATH_TARGET_APPLE) && !defined(DEATH_TARGET_EMSCRIPTEN) &&								\
	!defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_WINDOWS_RT)
#	define GL_DEBUG_SUPPORTED
#endif

#if defined(GL_DEBUG_SUPPORTED) && defined(WITH_OPENGLES) && GL_ES_VERSION_3_0 && !GL_ES_VERSION_3_2
#	define glPushDebugGroup glPushDebugGroupKHR
#	define glPopDebugGroup glPopDebugGroupKHR
#	define glDebugMessageInsert glDebugMessageInsertKHR
#	define glObjectLabel glObjectLabelKHR
#	define glGetObjectLabel glGetObjectLabelKHR
#	define GL_MAX_LABEL_LENGTH GL_MAX_LABEL_LENGTH_KHR

#	define glDebugMessageCallback glDebugMessageCallbackKHR
#	define GLDEBUGPROC GLDEBUGPROCKHR
#	define GL_DEBUG_OUTPUT_SYNCHRONOUS GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR

#	define GL_DEBUG_SOURCE_API GL_DEBUG_SOURCE_API_KHR
#	define GL_DEBUG_SOURCE_WINDOW_SYSTEM GL_DEBUG_SOURCE_WINDOW_SYSTEM_KHR
#	define GL_DEBUG_SOURCE_SHADER_COMPILER GL_DEBUG_SOURCE_SHADER_COMPILER_KHR
#	define GL_DEBUG_SOURCE_THIRD_PARTY GL_DEBUG_SOURCE_THIRD_PARTY_KHR
#	define GL_DEBUG_SOURCE_APPLICATION GL_DEBUG_SOURCE_APPLICATION_KHR
#	define GL_DEBUG_SOURCE_OTHER GL_DEBUG_SOURCE_OTHER_KHR

#	define GL_DEBUG_TYPE_ERROR GL_DEBUG_TYPE_ERROR_KHR
#	define GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_KHR
#	define GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_KHR
#	define GL_DEBUG_TYPE_PORTABILITY GL_DEBUG_TYPE_PORTABILITY_KHR
#	define GL_DEBUG_TYPE_PERFORMANCE GL_DEBUG_TYPE_PERFORMANCE_KHR
#	define GL_DEBUG_TYPE_MARKER GL_DEBUG_TYPE_MARKER_KHR
#	define GL_DEBUG_TYPE_PUSH_GROUP GL_DEBUG_TYPE_PUSH_GROUP_KHR
#	define GL_DEBUG_TYPE_POP_GROUP GL_DEBUG_TYPE_POP_GROUP_KHR
#	define GL_DEBUG_TYPE_OTHER GL_DEBUG_TYPE_OTHER_KHR

#	define GL_DEBUG_SEVERITY_NOTIFICATION GL_DEBUG_SEVERITY_NOTIFICATION_KHR
#	define GL_DEBUG_SEVERITY_LOW GL_DEBUG_SEVERITY_LOW_KHR
#	define GL_DEBUG_SEVERITY_MEDIUM GL_DEBUG_SEVERITY_MEDIUM_KHR
#	define GL_DEBUG_SEVERITY_HIGH GL_DEBUG_SEVERITY_HIGH_KHR
#endif

namespace nCine
{
#if defined(GL_DEBUG_SUPPORTED)
	namespace
	{
		static const char emptyString[1] = { '\0' };
	}
#endif

	bool GLDebug::debugAvailable_ = false;
	GLuint GLDebug::debugGroupId_ = 0;
	std::int32_t GLDebug::maxLabelLength_ = 0;

	void GLDebug::Init(const IGfxCapabilities& gfxCaps)
	{
#if defined(GL_DEBUG_SUPPORTED)
		debugAvailable_ = gfxCaps.HasExtension(IGfxCapabilities::GLExtensions::KHR_DEBUG) &&
			theApplication().GetGfxDevice().glContextInfo().debugContext;

		glGetIntegerv(GL_MAX_LABEL_LENGTH, &maxLabelLength_);

		if (debugAvailable_) {
			EnableDebugOutput();
		}
#endif
	}

	void GLDebug::PushGroup(StringView message)
	{
#if defined(GL_DEBUG_SUPPORTED)
		if (debugAvailable_) {
			glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, debugGroupId_++, GLsizei(message.size()), message.data());
		}
#endif
	}

	void GLDebug::PopGroup()
	{
#if defined(GL_DEBUG_SUPPORTED)
		if (debugAvailable_) {
			glPopDebugGroup();
		}
#endif
	}

	void GLDebug::MessageInsert(StringView message)
	{
#if defined(GL_DEBUG_SUPPORTED)
		if (debugAvailable_) {
			glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER, debugGroupId_++, GL_DEBUG_SEVERITY_NOTIFICATION, GLsizei(message.size()), message.data());
		}
#endif
	}

	void GLDebug::SetObjectLabel(LabelTypes identifier, GLuint name, StringView label)
	{
#if defined(GL_DEBUG_SUPPORTED)
		if (debugAvailable_) {
			glObjectLabel(static_cast<GLenum>(identifier), name, GLsizei(label.size()), label.data());
		}
#endif
	}

	void GLDebug::GetObjectLabel(LabelTypes identifier, GLuint name, GLsizei bufSize, GLsizei* length, char* label)
	{
#if defined(GL_DEBUG_SUPPORTED)
		if (debugAvailable_) {
			glGetObjectLabel(static_cast<GLenum>(identifier), name, bufSize, length, label);
		}
#endif
	}

#if defined(GL_DEBUG_SUPPORTED)
	/// Callback for `glDebugMessageCallback()`
	void
#	if defined(DEATH_TARGET_WINDOWS)
		__stdcall
#	endif
		DebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const char* message, const void* userParam)
	{
		const char* sourceString;
		switch (source) {
			case GL_DEBUG_SOURCE_API: sourceString = "API"; break;
			case GL_DEBUG_SOURCE_WINDOW_SYSTEM: sourceString = "Window System"; break;
			case GL_DEBUG_SOURCE_SHADER_COMPILER: sourceString = "Shader Compiler"; break;
			case GL_DEBUG_SOURCE_THIRD_PARTY: sourceString = "3rd Party"; break;
			case GL_DEBUG_SOURCE_APPLICATION: sourceString = "Application"; break;
			case GL_DEBUG_SOURCE_OTHER: sourceString = "Other"; break;
			default: sourceString = "Unknown"; break;
		}

		const char* typeString;
		switch (type) {
			case GL_DEBUG_TYPE_ERROR: typeString = "Error"; break;
			case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: typeString = "Deprecated Behavior"; break;
			case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: typeString = "Undefined Behavior"; break;
			case GL_DEBUG_TYPE_PORTABILITY: typeString = "Portability"; break;
			case GL_DEBUG_TYPE_PERFORMANCE: typeString = "Performance"; break;
			case GL_DEBUG_TYPE_MARKER: typeString = "Marker"; break;
			case GL_DEBUG_TYPE_PUSH_GROUP: typeString = "Push Group"; break;
			case GL_DEBUG_TYPE_POP_GROUP: typeString = "Pop Group"; break;
			case GL_DEBUG_TYPE_OTHER: typeString = "Other"; break;
			default: typeString = "Unknown"; break;
		}

		const char* severityString;
		switch (severity) {
			case GL_DEBUG_SEVERITY_NOTIFICATION: severityString = "notification"; break;
			case GL_DEBUG_SEVERITY_LOW: severityString = "low"; break;
			case GL_DEBUG_SEVERITY_MEDIUM: severityString = "medium"; break;
			case GL_DEBUG_SEVERITY_HIGH: severityString = "high"; break;
			default: severityString = "Unknown"; break;
		}

		LOGD("OpenGL message {} of type \"{}\" from source \"{}\" with {} severity: \"{}\"",
			id, typeString, sourceString, severityString, message);
	}
#endif

	void GLDebug::EnableDebugOutput()
	{
#if defined(GL_DEBUG_SUPPORTED)
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		glDebugMessageCallback(static_cast<GLDEBUGPROC>(DebugCallback), nullptr);
		LOGI("OpenGL debug callback initialized");
#endif
	}
}
