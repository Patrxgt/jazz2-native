#include "Random.h"
#include "../../Main.h"

#include <chrono>
#include <cmath>	// for ldexp()
#include <cstring>
#include <tuple>

#if defined(DEATH_TARGET_WINDOWS)
#	include <objbase.h>
#endif

namespace nCine
{
	namespace
	{
		const std::uint64_t DefaultInitState = 0x853c49e6748fea9bULL;
		const std::uint64_t DefaultInitSequence = 0xda3e39cb94b95bdbULL;

		std::uint32_t random(std::uint64_t& state, std::uint64_t& increment) noexcept
		{
			const std::uint64_t oldState = state;
			state = oldState * 6364136223846793005ULL + increment;
			const std::uint32_t xorShifted = static_cast<std::uint32_t>(((oldState >> 18u) ^ oldState) >> 27u);
			const std::uint32_t rotation = static_cast<std::uint32_t>(oldState >> 59u);
			return (xorShifted >> rotation) | (xorShifted << ((std::uint32_t)(-(std::int32_t)rotation) & 31));
		}

		std::uint32_t boundRandom(std::uint64_t& state, std::uint64_t& increment, std::uint32_t bound) noexcept
		{
			const std::uint32_t threshold = (std::uint32_t)(-(std::int32_t)bound) % bound;
			while (true) {
				const std::uint32_t r = random(state, increment);
				if (r >= threshold) {
					return r % bound;
				}
			}
		}
	}

	RandomGenerator& Random() noexcept
	{
		static RandomGenerator instance;
		return instance;
	}

	RandomGenerator::RandomGenerator() noexcept
		: _state(0ULL), _increment(0ULL)
	{
		std::uint64_t now = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::system_clock::now()).time_since_epoch().count();
		Init(DefaultInitState ^ now, DefaultInitSequence);
	}

	RandomGenerator::RandomGenerator(std::uint64_t initState, std::uint64_t initSequence) noexcept
		: _state(0ULL), _increment(0ULL)
	{
		Init(initState, initSequence);
	}

	void RandomGenerator::Init(std::uint64_t initState, std::uint64_t initSequence) noexcept
	{
		_state = 0U;
		_increment = (initSequence << 1u) | 1u;
		random(_state, _increment);
		_state += initState;
		random(_state, _increment);
	}

	std::uint32_t RandomGenerator::Next() noexcept
	{
		return random(_state, _increment);
	}

	std::uint32_t RandomGenerator::Next(std::uint32_t min, std::uint32_t max) noexcept
	{
		if (min == max) {
			return min;
		} else {
			return min + boundRandom(_state, _increment, max - min);
		}
	}

	float RandomGenerator::NextFloat() noexcept
	{
		return static_cast<float>(ldexp(random(_state, _increment), -32));
	}

	float RandomGenerator::NextFloat(float min, float max) noexcept
	{
		return min + static_cast<float>(ldexp(random(_state, _increment), -32)) * (max - min);
	}

	bool RandomGenerator::NextBool() noexcept
	{
		return (boundRandom(_state, _increment, 2) != 0);
	}

	std::uint32_t RandomGenerator::Fast(std::uint32_t min, std::uint32_t max) noexcept
	{
		return (min == max ? min : min + random(_state, _increment) % (max - min));
	}

	float RandomGenerator::FastFloat() noexcept
	{
		return static_cast<float>(random(_state, _increment) / static_cast<float>(UINT32_MAX));
	}

	float RandomGenerator::FastFloat(float min, float max) noexcept
	{
		return min + (static_cast<float>(random(_state, _increment)) / static_cast<float>(UINT32_MAX)) * (max - min);
	}

	void RandomGenerator::Uuid(Containers::StaticArrayView<16, std::uint8_t> result)
	{
#if defined(DEATH_TARGET_WINDOWS)
		GUID guid;
		std::ignore = ::CoCreateGuid(&guid);
		static_assert(sizeof(guid) == 16);
		std::memcpy(result.data(), &guid, sizeof(GUID));
#else
		std::uint64_t now = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::system_clock::now()).time_since_epoch().count();
		std::uint64_t timeMs = now / 1000;
		std::uint64_t timeUs = now % 1000;
		std::uint64_t timestamp = (timeMs << 16) | 0x7000 | timeUs;

		std::uint32_t i1 = Next();
		std::uint32_t i2 = Next();

		std::memcpy(&result[0], &timestamp, 8);
		std::memcpy(&result[8], &i1, 4);
		std::memcpy(&result[12], &i2, 4);

		// Variant
		result[8] = (result[8] & 0x3F) | 0x80;
#endif
	}
}
