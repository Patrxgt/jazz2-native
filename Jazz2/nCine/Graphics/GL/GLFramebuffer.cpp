#include "GLFramebuffer.h"
#include "GLRenderbuffer.h"
#include "GLTexture.h"
#include "GLDebug.h"

namespace nCine {

	///////////////////////////////////////////////////////////
	// STATIC DEFINITIONS
	///////////////////////////////////////////////////////////

	unsigned int GLFramebuffer::readBoundBuffer_ = 0;
	unsigned int GLFramebuffer::drawBoundBuffer_ = 0;

	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	GLFramebuffer::GLFramebuffer()
		: glHandle_(0)
	{
		attachedRenderbuffers_.reserve(4);

		glGenFramebuffers(1, &glHandle_);
	}

	GLFramebuffer::~GLFramebuffer()
	{
		if (readBoundBuffer_ == glHandle_)
			unbind(GL_READ_FRAMEBUFFER);
		if (drawBoundBuffer_ == glHandle_)
			unbind(GL_DRAW_FRAMEBUFFER);

		glDeleteFramebuffers(1, &glHandle_);
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	bool GLFramebuffer::bind() const
	{
		return bind(GL_FRAMEBUFFER);
	}

	bool GLFramebuffer::unbind()
	{
		return unbind(GL_FRAMEBUFFER);
	}

	bool GLFramebuffer::bind(GLenum target) const
	{
		return bindHandle(target, glHandle_);
	}

	bool GLFramebuffer::unbind(GLenum target)
	{
		return bindHandle(target, 0);
	}

	bool GLFramebuffer::drawBuffers(unsigned int numDrawBuffers)
	{
		static const GLenum drawBuffers[8] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3,
											   GL_COLOR_ATTACHMENT4, GL_COLOR_ATTACHMENT5, GL_COLOR_ATTACHMENT6, GL_COLOR_ATTACHMENT7 };
		if (numDrawBuffers_ != numDrawBuffers) {
			glDrawBuffers(numDrawBuffers, drawBuffers);
			numDrawBuffers_ = numDrawBuffers;
			return true;
		}
		return false;
	}

	void GLFramebuffer::attachRenderbuffer(const char* label, GLenum internalFormat, GLsizei width, GLsizei height, GLenum attachment)
	{
		for (unsigned int i = 0; i < attachedRenderbuffers_.size(); i++) {
			if (attachedRenderbuffers_[i]->attachment() == attachment)
				return;
		}

		std::unique_ptr<GLRenderbuffer>& buffer = attachedRenderbuffers_.emplace_back(std::make_unique<GLRenderbuffer>(internalFormat, width, height));
		buffer->setObjectLabel(label);
		buffer->setAttachment(attachment);

		bind(GL_FRAMEBUFFER);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, buffer->glHandle_);
	}

	void GLFramebuffer::attachRenderbuffer(GLenum internalFormat, GLsizei width, GLsizei height, GLenum attachment)
	{
		attachRenderbuffer(nullptr, internalFormat, width, height, attachment);
	}

	void GLFramebuffer::detachRenderbuffer(GLenum attachment)
	{
		for (unsigned int i = 0; i < attachedRenderbuffers_.size(); i++) {
			if (attachedRenderbuffers_[i]->attachment() == attachment) {
				bind(GL_FRAMEBUFFER);
				glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, 0);
				attachedRenderbuffers_.erase(attachedRenderbuffers_.begin() + i);
				break;
			}
		}
	}

	void GLFramebuffer::attachTexture(GLTexture& texture, GLenum attachment)
	{
		bind(GL_FRAMEBUFFER);
		glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, texture.target_, texture.glHandle_, 0);
	}

	void GLFramebuffer::detachTexture(GLenum attachment)
	{
		bind(GL_FRAMEBUFFER);
		glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, 0, 0);
	}

	void GLFramebuffer::invalidate(GLsizei numAttachments, const GLenum* attachments)
	{
		bind(GL_FRAMEBUFFER);
		glInvalidateFramebuffer(GL_FRAMEBUFFER, numAttachments, attachments);
	}

	bool GLFramebuffer::isStatusComplete()
	{
		bind(GL_FRAMEBUFFER);
		const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		unbind(GL_FRAMEBUFFER);

		return (status == GL_FRAMEBUFFER_COMPLETE);
	}

	void GLFramebuffer::setObjectLabel(const char* label)
	{
		GLDebug::objectLabel(GLDebug::LabelTypes::FRAMEBUFFER, glHandle_, label);
	}

	///////////////////////////////////////////////////////////
	// PRIVATE FUNCTIONS
	///////////////////////////////////////////////////////////

	bool GLFramebuffer::bindHandle(GLenum target, GLuint glHandle)
	{
		//FATAL_ASSERT(target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER);

		if (target == GL_FRAMEBUFFER &&
			(readBoundBuffer_ != glHandle || drawBoundBuffer_ != glHandle)) {
			glBindFramebuffer(target, glHandle);
			readBoundBuffer_ = glHandle;
			drawBoundBuffer_ = glHandle;
			return true;
		} else if (target == GL_READ_FRAMEBUFFER && readBoundBuffer_ != glHandle) {
			glBindFramebuffer(target, glHandle);
			readBoundBuffer_ = glHandle;
			return true;
		} else if (target == GL_DRAW_FRAMEBUFFER && drawBoundBuffer_ != glHandle) {
			glBindFramebuffer(target, glHandle);
			drawBoundBuffer_ = glHandle;
			return true;
		}
		return false;
	}

}