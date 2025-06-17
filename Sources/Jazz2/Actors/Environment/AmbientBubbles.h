﻿#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	/** @brief Ambient bubbles */
	class AmbientBubbles : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		AmbientBubbles();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;

	private:
		static constexpr float BaseTime = 20.0f;

		uint8_t _speed;
		float _cooldown;
		std::int32_t _bubblesLeft;
		float _delay;

		void SpawnBubbles(std::int32_t count);
	};
}