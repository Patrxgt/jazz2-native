﻿#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Solid
{
	/** @brief Pole */
	class Pole : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		enum class FallDirection {
			None,
			Right,
			Left,
			Fallen
		};

		Pole();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;
		bool CanCauseDamage(ActorBase* collider) override;

		FallDirection GetFallDirection() const {
			return _fall;
		}

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnPacketReceived(MemoryStream& packet) override;

	private:
		static constexpr std::int32_t BouncesMax = 3;

		FallDirection _fall;
		float _angleVel;
		float _angleVelLast;
		float _fallTime;
		std::int32_t _bouncesLeft = BouncesMax;

		void Fall(FallDirection dir);
		bool IsPositionBlocked();
	};
}