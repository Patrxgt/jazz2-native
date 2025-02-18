#pragma once

#include "GL/GLBufferObject.h"
#include "RenderBuffersManager.h"

#include <memory>

namespace nCine
{
	/// Contains geometric data for a drawable node
	class Geometry
	{
		friend class RenderCommand;

	public:
		/// Default constructor
		Geometry();
		~Geometry();

		Geometry(const Geometry&) = delete;
		Geometry& operator=(const Geometry&) = delete;

		/// Returns the primitive type (`GL_TRIANGLES`, `GL_TRIANGLE_STRIP`, ...)
		inline GLenum primitiveType() const {
			return primitiveType_;
		}
		/// Returns the index of the first vertex to draw
		inline GLint firstVertex() const {
			return firstVertex_;
		}
		/// Returns the number of vertices
		inline GLsizei numVertices() const {
			return numVertices_;
		}
		/// Returns the number of float elements that composes the vertex format
		inline std::uint32_t numElementsPerVertex() const {
			return numElementsPerVertex_;
		}

		/// Sets all three drawing parameters
		void setDrawParameters(GLenum primitiveType, GLint firstVertex, GLsizei numVertices);
		/// Sets the primitive type (`GL_TRIANGLES`, `GL_TRIANGLE_STRIP`, ...)
		inline void setPrimitiveType(GLenum primitiveType) {
			primitiveType_ = primitiveType;
		}
		/// Sets the index number of the first vertex to draw
		inline void setFirstVertex(GLint firstVertex) {
			firstVertex_ = firstVertex;
		}
		/// Sets the number of vertices
		inline void setNumVertices(GLsizei numVertices) {
			numVertices_ = numVertices;
		}
		/// Sets the number of float elements that composes the vertex format
		inline void setNumElementsPerVertex(std::uint32_t numElements) {
			numElementsPerVertex_ = numElements;
		}
		/// Creates a custom VBO that is unique to this `Geometry` object
		void createCustomVbo(std::uint32_t numFloats, GLenum usage);
		/// Retrieves a pointer that can be used to write vertex data from a custom VBO owned by this object
		/*! This overloaded version allows a custom alignment specification */
		GLfloat* acquireVertexPointer(std::uint32_t numFloats, std::uint32_t numFloatsAlignment);
		/// Retrieves a pointer that can be used to write vertex data from a custom VBO owned by this object
		inline GLfloat* acquireVertexPointer(std::uint32_t numFloats) {
			return acquireVertexPointer(numFloats, 1);
		}
		/// Retrieves a pointer that can be used to write vertex data from a VBO owned by the buffers manager
		GLfloat* acquireVertexPointer();
		/// Releases the pointer used to write vertex data
		void releaseVertexPointer();

		/// Returns a pointer into host memory containing vertex data to be copied into a VBO
		inline const float* hostVertexPointer() const {
			return hostVertexPointer_;
		}
		/// Sets a pointer into host memory containing vertex data to be copied into a VBO
		void setHostVertexPointer(const float* vertexPointer);

		/// Shares the VBO of another `Geometry` object
		void shareVbo(const Geometry* geometry);

		/// Returns the number of indices used to render the geometry
		inline std::uint32_t numIndices() const {
			return numIndices_;
		}
		/// Sets the index number of the first index to draw
		inline void setFirstIndex(GLushort firstIndex) {
			firstIndex_ = firstIndex;
		}
		/// Sets the number of indices used to render the geometry
		inline void setNumIndices(std::uint32_t numIndices) {
			numIndices_ = numIndices;
		}
		/// Creates a custom IBO that is unique to this `Geometry` object
		void createCustomIbo(std::uint32_t numIndices, GLenum usage);
		/// Retrieves a pointer that can be used to write index data from a custom IBO owned by this object
		GLushort* acquireIndexPointer(std::uint32_t numIndices);
		/// Retrieves a pointer that can be used to write index data from a IBO owned by the buffers manager
		GLushort* acquireIndexPointer();
		/// Releases the pointer used to write index data
		void releaseIndexPointer();

		/// Returns a pointer into host memory containing index data to be copied into a IBO
		inline const GLushort* hostIndexPointer() const {
			return hostIndexPointer_;
		}
		/// Sets a pointer into host memory containing index data to be copied into a IBO
		void setHostIndexPointer(const GLushort* indexPointer);

		/// Shares the IBO of another `Geometry` object
		void shareIbo(const Geometry* geometry);

	private:
		GLenum primitiveType_;
		GLint firstVertex_;
		GLsizei numVertices_;
		std::uint32_t numElementsPerVertex_;
		GLushort firstIndex_;
		std::uint32_t numIndices_;
		const float* hostVertexPointer_;
		const GLushort* hostIndexPointer_;

		std::unique_ptr<GLBufferObject> vbo_;
		GLenum vboUsageFlags_;
		RenderBuffersManager::Parameters vboParams_;
		const RenderBuffersManager::Parameters* sharedVboParams_;

		std::unique_ptr<GLBufferObject> ibo_;
		GLenum iboUsageFlags_;
		RenderBuffersManager::Parameters iboParams_;
		const RenderBuffersManager::Parameters* sharedIboParams_;

		bool hasDirtyVertices_;
		bool hasDirtyIndices_;

		void bind();
		void draw(GLsizei numInstances);
		void commitVertices();
		void commitIndices();

		inline const RenderBuffersManager::Parameters& vboParams() const {
			return sharedVboParams_ ? *sharedVboParams_ : vboParams_;
		}
		inline const RenderBuffersManager::Parameters& iboParams() const {
			return sharedIboParams_ ? *sharedIboParams_ : iboParams_;
		}
	};

}
