﻿#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	/** @brief Eva */
	class Eva : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		Eva();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;

	private:
		float _animationTime;
	};
}