﻿#include "RemoteActor.h"

#if defined(WITH_MULTIPLAYER)

#include "../../../nCine/Base/Clock.h"

namespace Jazz2::Actors::Multiplayer
{
	RemoteActor::RemoteActor()
		: _stateBufferPos(0), _lastAnim(AnimState::Idle), _isAttachedLocally(false)
	{
	}

	Task<bool> RemoteActor::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::PreserveOnRollback, true);
		SetState(ActorState::CanBeFrozen | ActorState::CollideWithTileset | ActorState::ApplyGravitation, false);

		Clock& c = nCine::clock();
		std::uint64_t now = c.now() * 1000 / c.frequency();
		for (std::int32_t i = 0; i < std::int32_t(arraySize(_stateBuffer)); i++) {
			_stateBuffer[i].Time = now - arraySize(_stateBuffer) + i;
			_stateBuffer[i].Pos = Vector2f(details.Pos.X, details.Pos.Y);
		}

		async_return true;
	}

	void RemoteActor::OnUpdate(float timeMult)
	{
		if (!_isAttachedLocally) {
			Clock& c = nCine::clock();
			std::int64_t now = c.now() * 1000 / c.frequency();
			std::int64_t renderTime = now - ServerDelay;

			std::int32_t nextIdx = _stateBufferPos - 1;
			if (nextIdx < 0) {
				nextIdx += std::int32_t(arraySize(_stateBuffer));
			}

			if (renderTime <= _stateBuffer[nextIdx].Time) {
				std::int32_t prevIdx;
				while (true) {
					prevIdx = nextIdx - 1;
					if (prevIdx < 0) {
						prevIdx += std::int32_t(arraySize(_stateBuffer));
					}

					if (prevIdx == _stateBufferPos || _stateBuffer[prevIdx].Time <= renderTime) {
						break;
					}

					nextIdx = prevIdx;
				}

				Vector2f pos;
				std::int64_t timeRange = (_stateBuffer[nextIdx].Time - _stateBuffer[prevIdx].Time);
				if (timeRange > 0) {
					float lerp = (float)(renderTime - _stateBuffer[prevIdx].Time) / timeRange;
					pos = _stateBuffer[prevIdx].Pos + (_stateBuffer[nextIdx].Pos - _stateBuffer[prevIdx].Pos) * lerp;
				} else {
					pos = _stateBuffer[nextIdx].Pos;
				}

				MoveInstantly(pos, MoveType::Absolute | MoveType::Force);
			}
		}

		ActorBase::OnUpdate(timeMult);
	}

	void RemoteActor::OnAttach(ActorBase* parent)
	{
		_isAttachedLocally = true;
	}

	void RemoteActor::OnDetach(ActorBase* parent)
	{
		_isAttachedLocally = false;
	}

	void RemoteActor::AssignMetadata(std::uint8_t flags, ActorState state, StringView path, AnimState anim, float rotation, float scaleX, float scaleY, ActorRendererType rendererType)
	{
		constexpr ActorState RemotedFlags = ActorState::Illuminated | ActorState::IsInvulnerable |
			ActorState::CollideWithOtherActors | ActorState::CollideWithSolidObjects | ActorState::IsSolidObject |
			ActorState::CollideWithTilesetReduced | ActorState::CollideWithSolidObjectsBelow | ActorState::ExcludeSimilar;

		RequestMetadata(path);
		SetAnimation(anim);
		SetState((GetState() & ~RemotedFlags) | (state & RemotedFlags));

		_renderer.Initialize(rendererType);
		_renderer.setRotation(rotation);
		
		SyncMiscWithServer(flags);
	}

	void RemoteActor::SyncPositionWithServer(Vector2f pos)
	{
		Clock& c = nCine::clock();
		std::int64_t now = c.now() * 1000 / c.frequency();

		if (_renderer.isDrawEnabled()) {
			// Actor is still visible, enable interpolation
			_stateBuffer[_stateBufferPos].Time = now;
			_stateBuffer[_stateBufferPos].Pos = pos;
		} else {
			// Actor was hidden before, reset state buffer to disable interpolation
			std::int32_t stateBufferPrevPos = _stateBufferPos - 1;
			if (stateBufferPrevPos < 0) {
				stateBufferPrevPos += std::int32_t(arraySize(_stateBuffer));
			}

			std::int64_t renderTime = now - ServerDelay;

			_stateBuffer[stateBufferPrevPos].Time = renderTime;
			_stateBuffer[stateBufferPrevPos].Pos = pos;
			_stateBuffer[_stateBufferPos].Time = renderTime;
			_stateBuffer[_stateBufferPos].Pos = pos;
		}

		_stateBufferPos++;
		if (_stateBufferPos >= std::int32_t(arraySize(_stateBuffer))) {
			_stateBufferPos = 0;
		}
	}

	void RemoteActor::SyncAnimationWithServer(AnimState anim, float rotation, float scaleX, float scaleY, Actors::ActorRendererType rendererType)
	{
		if (_lastAnim != anim) {
			_lastAnim = anim;
			SetAnimation(anim);
		}

		_renderer.setRotation(rotation);
		_renderer.setScale(scaleX, scaleY);
		_renderer.Initialize(rendererType);
	}

	void RemoteActor::SyncMiscWithServer(std::uint8_t flags)
	{
		_renderer.setDrawEnabled((flags & 0x04) != 0);
		_renderer.AnimPaused = (flags & 0x08) != 0;
		SetFacingLeft((flags & 0x10) != 0);
		_renderer.setFlippedY((flags & 0x20) != 0);

		bool justWarped = (flags & 0x40) != 0;
		if (justWarped) {
			Clock& c = nCine::clock();
			std::int64_t now = c.now() * 1000 / c.frequency();

			std::int32_t stateBufferPrevPos = _stateBufferPos - 1;
			if (stateBufferPrevPos < 0) {
				stateBufferPrevPos += std::int32_t(arraySize(_stateBuffer));
			}

			Vector2f pos = _stateBuffer[stateBufferPrevPos].Pos;

			for (std::size_t i = 0; i < arraySize(_stateBuffer); i++) {
				_stateBuffer[i].Time = now;
				_stateBuffer[i].Pos = pos;
			}
		}
	}
}

#endif