﻿#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/** @brief Dragonfly */
	class Dragonfly : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		Dragonfly();
		~Dragonfly();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnHitWall(float timeMult) override;
		void OnHitFloor(float timeMult) override;
		void OnHitCeiling(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr std::int32_t StateIdle = 0;
		static constexpr std::int32_t StateAttacking = 1;
		static constexpr std::int32_t StateBraking = 2;

		std::int32_t _state;
		float _idleTime;
		float _attackCooldown;
		Vector2f _direction;
		std::shared_ptr<AudioBufferPlayer> _noise;
	};
}