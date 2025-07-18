﻿#include "Spring.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Weapons/ShieldFireShot.h"
#include "../Weapons/ToasterShot.h"
#include "../Weapons/Thunderbolt.h"

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Environment
{
	Spring::Spring()
		: _cooldown(0.0f)
	{
	}

	void Spring::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Object/Spring"_s);
	}

	Vector2f Spring::Activate()
	{
		if (_state == State::Frozen || _cooldown > 0.0f || _frozenTimeLeft > 0.0f) {
			return Vector2f::Zero;
		}

		_cooldown = (_delay > 0 ? _delay : 6.0f);

		SetTransition(_currentAnimation->State | (AnimState)0x200, false);
		switch (_orientation) {
			case Orientation::Bottom:
				PlaySfx("Vertical"_s);
				return Vector2f(0, -_strength);
			case Orientation::Top:
				PlaySfx("VerticalReversed"_s);
				return Vector2f(0, _strength);
			case Orientation::Right:
			case Orientation::Left:
				PlaySfx("Horizontal"_s);
				return Vector2f(_strength * (_orientation == Orientation::Right ? 1 : -1), 0);
			default:
				return Vector2f::Zero;
		}
	}

	Task<bool> Spring::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_type = details.Params[0];
		_orientation = (Orientation)details.Params[1];
		KeepSpeedX = ((details.Params[2] & 0x04) != 0);
		KeepSpeedY = ((details.Params[2] & 0x08) != 0);
		bool applyGravitation = (details.Params[2] & 0x01) != 0; // Jazz² Resurrection only
		_state = ((details.Params[2] & 0x02) != 0 ? State::Frozen : State::Default);
		_delay = details.Params[3];

		SetState(ActorState::SkipPerPixelCollisions, true);

		async_await RequestMetadataAsync("Object/Spring"_s);

		Vector2f tileCorner = Vector2f((std::int32_t)(_pos.X / Tiles::TileSet::DefaultTileSize) * Tiles::TileSet::DefaultTileSize,
			(std::int32_t)(_pos.Y / Tiles::TileSet::DefaultTileSize) * Tiles::TileSet::DefaultTileSize);
		if (_orientation > Orientation::Left) {
			// JJ2 horizontal springs held no data about which way they were facing.
			// For compatibility, correct orientation is evaluated during runtime.
			AABBf aabb = AABBf(_pos.X + 6.0f, _pos.Y - 2.0f, _pos.X + 26.0f, _pos.Y);
			TileCollisionParams params = { TileDestructType::None, true };
			_orientation = (_levelHandler->TileMap()->IsTileEmpty(aabb, params) != (_orientation == (Orientation)5) ? Orientation::Right : Orientation::Left);
		}

		std::int32_t orientationBit = 0;
		switch (_orientation) {
			case Orientation::Bottom:
				MoveInstantly(Vector2f(tileCorner.X + 16, tileCorner.Y + 8), MoveType::Absolute | MoveType::Force);
				break;
			case Orientation::Right:
				MoveInstantly(Vector2f(tileCorner.X + 16, tileCorner.Y + 16), MoveType::Absolute | MoveType::Force);
				orientationBit = 1;
				break;
			case Orientation::Top:
				MoveInstantly(Vector2f(tileCorner.X + 16, tileCorner.Y + 8), MoveType::Absolute | MoveType::Force);
				orientationBit = 2;
				break;
			case Orientation::Left:
				MoveInstantly(Vector2f(tileCorner.X + 16, tileCorner.Y + 16), MoveType::Absolute | MoveType::Force);
				orientationBit = 1;
				SetFacingLeft(true);
				break;
		}

		SetState(ActorState::ApplyGravitation, applyGravitation);

		// Red starts at 1 in "Object/Spring"
		SetAnimation((AnimState)(((_type + 1) << 10) | (orientationBit << 12)));
		_renderer.setLayer(_renderer.layer() - 8);

		if (_orientation == Orientation::Right || _orientation == Orientation::Left) {
			// Horizontal springs all seem to have the same strength.
			// This constant strength gives about the correct amount of horizontal push.
			_strength = 9.5f;
		} else {
			// Vertical springs should work as follows:
			// Red spring lifts the player 9 tiles, green 14, and blue 19.
			// Vertical strength currently works differently from horizontal, that explains
			// the otherwise inexplicable difference of scale between the two types.
			switch (_type) {
				case 0: // Red
					_strength = 1.25f;
					break;
				case 1: // Green
					_strength = (_levelHandler->IsReforged() ? 1.50f : 1.52f);
					break;
				case 2: // Blue
					_strength = (_levelHandler->IsReforged() ? 1.68f : 1.72f);
					break;
			}
		}

		if (GetState(ActorState::ApplyGravitation)) {
			OnUpdateHitbox();

			// Apply instant gravitation
			std::int32_t i = 10;
			while (i-- > 0 && MoveInstantly(Vector2f(0.0f, 4.0f), MoveType::Relative)) {
				// Nothing to do...
			}
			while (i-- > 0 && MoveInstantly(Vector2f(0.0f, 1.0f), MoveType::Relative)) {
				// Nothing to do...
			}
		}

		if (_state == State::Frozen) {
			_renderer.Initialize(ActorRendererType::FrozenMask);
			SetState(ActorState::CanBeFrozen, false);
		}

		async_return true;
	}

	void Spring::OnUpdate(float timeMult)
	{
		ActorBase::OnUpdate(timeMult);

		if (_cooldown > 0.0f) {
			_cooldown -= timeMult;
		}
		if (_state == State::Heated) {
			// Cannot be directly in `Spring::OnHandleCollision()` due to bug in `BaseSprite::updateRenderCommand()`,
			// it would be called before `BaseSprite::updateRenderCommand()` but after `SceneNode::transform()`
			_renderer.Initialize(ActorRendererType::Default);
			_state = State::Default;
		}
	}

	void Spring::OnUpdateHitbox()
	{
		switch (_orientation) {
			case Orientation::Right:
				AABBInner = AABBf(_pos.X - 8, _pos.Y - 10, _pos.X, _pos.Y + 10);
				break;
			case Orientation::Left:
				AABBInner = AABBf(_pos.X, _pos.Y - 10, _pos.X + 8, _pos.Y + 10);
				break;
			default:
			case Orientation::Bottom:
			case Orientation::Top:
				AABBInner = AABBf(_pos.X - 10, _pos.Y, _pos.X + 10, _pos.Y + 8);
				break;
		}
	}

	bool Spring::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (_state == State::Frozen) {
			ActorBase* actorBase = other.get();
			if (runtime_cast<Weapons::ToasterShot>(actorBase) || runtime_cast<Weapons::Thunderbolt>(actorBase) ||
				runtime_cast<Weapons::ShieldFireShot>(actorBase)) {
				_state = State::Heated;
				SetState(ActorState::CanBeFrozen, true);
			}
		}

		return ActorBase::OnHandleCollision(std::move(other));
	}
}