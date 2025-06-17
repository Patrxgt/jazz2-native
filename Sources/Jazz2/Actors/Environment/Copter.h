﻿#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	/** @brief Copter */
	class Copter : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		Copter();
		~Copter();

		static void Preload(const ActorActivationDetails& details);

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

		/** @brief Unmounts from the assigned actor */
		void Unmount(float timeLeft);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnDetach(ActorBase* parent) override;

	private:
		enum class State {
			Free,
			Unmounted,
			Mounted
		};

		Vector2f _originPos;
		float _phase;
		State _state;
#if defined(WITH_AUDIO)
		std::shared_ptr<AudioBufferPlayer> _noise;
#endif
		float _noiseDec;
	};
}