﻿#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "MpPlayer.h"

using namespace Jazz2::Multiplayer;

namespace Jazz2::Actors::Multiplayer
{
	/** @brief Player on the server in online session */
	class PlayerOnServer : public MpPlayer
	{
		DEATH_RUNTIME_OBJECT(MpPlayer);

		friend class Jazz2::Multiplayer::MpLevelHandler;

	public:
		PlayerOnServer();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;
		bool CanCauseDamage(ActorBase* collider) override;
		bool TakeDamage(std::int32_t amount, float pushForce = 0.0f, bool ignoreInvulnerable = false) override;
		bool AddLives(std::int32_t count) override;

	protected:
		std::shared_ptr<ActorBase> _lastAttacker;
		float _lastAttackerTimeout;
		bool _canTakeDamage;
		bool _justWarped;

		void OnUpdate(float timeMult) override;

		bool IsAttacking() const;
	};
}

#endif