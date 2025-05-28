﻿#pragma once

#include "../../Main.h"
#include "JJ2Block.h"
#include "JJ2Version.h"

#include <memory>

#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::Compatibility
{
	/** @brief Parses original `.j2t` tile set files */
	class JJ2Tileset
	{
	public:
		/** @{ @name Constants */

		/** @brief Size of a tile */
		static constexpr std::int32_t BlockSize = 32;

		/** @} */

		JJ2Tileset() : _version(JJ2Version::Unknown), _tileCount(0) { }

		bool Open(StringView path, bool strictParser);

		void Convert(StringView targetPath) const;

		/** @brief Returns maximum number of supported tiles */
		std::int32_t GetMaxSupportedTiles() const {
			return (_version == JJ2Version::BaseGame ? 1024 : 4096);
		}

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct TilesetTileSection {
			bool Opaque;
			std::uint32_t ImageDataOffset;
			std::uint32_t AlphaDataOffset;
			std::uint32_t MaskDataOffset;

			std::uint8_t Image[BlockSize * BlockSize * 4];
			std::uint8_t Mask[BlockSize * BlockSize / 8];
		};
#endif

		static constexpr std::int32_t PaletteSize = 256;
		static constexpr std::uint32_t Is32bitTile = 0x80000000u;

		String _name;
		JJ2Version _version;
		std::uint32_t _palette[PaletteSize];
		std::unique_ptr<TilesetTileSection[]> _tiles;
		std::int32_t _tileCount;

		void LoadMetadata(JJ2Block& block);
		void LoadImageData(JJ2Block& imageBlock, JJ2Block& alphaBlock);
		void LoadMaskData(JJ2Block& block);
	};
}