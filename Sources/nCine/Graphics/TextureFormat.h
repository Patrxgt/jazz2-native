#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../CommonHeaders.h"
#endif

#include "../../Main.h"

namespace nCine
{
	/// Texture format
	class TextureFormat
	{
	public:
		TextureFormat() : internalFormat_(-1), format_(-1), type_(-1), isCompressed_(false) {}
		explicit TextureFormat(GLenum internalFormat);
		TextureFormat(GLenum internalFormat, GLenum type);

		/// Returns the specified internal format
		inline GLenum internalFormat() const {
			return internalFormat_;
		}
		/// Returns the corresponding format
		inline GLenum format() const {
			return format_;
		}
		/// Returns the corresponding pixel data type
		inline GLenum type() const {
			return type_;
		}
		/// Returns `true` if the format holds compressed data
		inline bool isCompressed() const {
			return isCompressed_;
		}
		/// Returns the number of color channels
		std::uint32_t numChannels() const;

		/// Converts the external format to the corresponding BGR one
		void bgrFormat();

		/// Calculates the pixel data size for each MIP map level
		static std::uint32_t calculateMipSizes(GLenum internalFormat, std::int32_t width, std::int32_t height, std::int32_t mipMapCount, std::uint32_t* mipDataOffsets, std::uint32_t* mipDataSizes);

	private:
		GLenum internalFormat_;
		GLenum format_;
		GLenum type_;
		bool isCompressed_;

		/// Searches a match between an integer internal format and an external one
		bool integerFormat();
		/// Searches a match between a non integer internal format and an external one
		bool nonIntegerFormat();
		/// Searches a match between a floating point internal format and an external one
		bool floatFormat();
		/// Searches a match between a compressed internal format and an external one
		bool compressedFormat();
#if defined(WITH_OPENGLES)
		/// Searches a match between an OpenGL ES compressed internal format and an external one
		bool oesCompressedFormat();
#endif

		/// Attempts to find a match between an external format and the corresponding internal one
		void findExternalFormat();
		/// Checks if the internal format is supported by the GPU
		void checkFormatSupport() const;
	};

}

