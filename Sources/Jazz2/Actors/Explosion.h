﻿#pragma once

#include "ActorBase.h"

namespace Jazz2::Actors
{
	/** @brief Explosion effects */
	class Explosion : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/** @brief Explosion effect type */
		enum class Type {
			Tiny,
			TinyBlue,
			TinyDark,
			Small,
			SmallDark,
			Large,

			SmokeBrown,
			SmokeGray,
			SmokeWhite,
			SmokePoof,

			WaterSplash,

			Pepper,
			RF,
			RFUpgraded,

			Generator,

			IceShrapnel,
		};

		Explosion();

		static void Create(ILevelHandler* levelHandler, const Vector3i& pos, Type type, float scale = 1.0f);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;
		void OnAnimationFinished() override;

	private:
		Type _type;
		float _lightBrightness;
		float _lightIntensity;
		float _lightRadiusNear;
		float _lightRadiusFar;
		float _scale;
		float _time;
	};
}