﻿#pragma once

#include "../SolidObjectBase.h"
#include "../../ShieldType.h"

namespace Jazz2::Actors::Solid
{
	/** @brief Power-up shield monitor */
	class PowerUpShieldMonitor : public SolidObjectBase
	{
		DEATH_RUNTIME_OBJECT(SolidObjectBase);

	public:
		PowerUpShieldMonitor();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;
		bool CanCauseDamage(ActorBase* collider) override;

		static void Preload(const ActorActivationDetails& details);

		void DestroyAndApplyToPlayer(Player* player);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;

	private:
		ShieldType _shieldType;
	};
}