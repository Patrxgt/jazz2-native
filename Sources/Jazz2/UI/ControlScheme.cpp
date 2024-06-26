#include "ControlScheme.h"
#include "../PreferencesCache.h"
#include "../../nCine/Input/IInputManager.h"

using namespace nCine;

namespace Jazz2::UI
{
	ControlSchemeMapping ControlScheme::_mappings[MaxSupportedPlayers * (std::int32_t)PlayerActions::Count] { };

	void ControlScheme::Reset()
	{
		// Set default mappings
		_mappings[(std::int32_t)PlayerActions::Left].Targets.emplace_back(CreateTarget(KeySym::LEFT));
		_mappings[(std::int32_t)PlayerActions::Left].Targets.emplace_back(CreateTarget(0, ButtonName::Left));
		_mappings[(std::int32_t)PlayerActions::Left].Targets.emplace_back(CreateTarget(0, AxisName::LeftX, true));
		_mappings[(std::int32_t)PlayerActions::Right].Targets.emplace_back(CreateTarget(KeySym::RIGHT));
		_mappings[(std::int32_t)PlayerActions::Right].Targets.emplace_back(CreateTarget(0, ButtonName::Right));
		_mappings[(std::int32_t)PlayerActions::Right].Targets.emplace_back(CreateTarget(0, AxisName::LeftX));
		_mappings[(std::int32_t)PlayerActions::Up].Targets.emplace_back(CreateTarget(KeySym::UP));
		_mappings[(std::int32_t)PlayerActions::Up].Targets.emplace_back(CreateTarget(0, ButtonName::Up));
		_mappings[(std::int32_t)PlayerActions::Up].Targets.emplace_back(CreateTarget(0, AxisName::LeftY, true));
		_mappings[(std::int32_t)PlayerActions::Down].Targets.emplace_back(CreateTarget(KeySym::DOWN));
		_mappings[(std::int32_t)PlayerActions::Down].Targets.emplace_back(CreateTarget(0, ButtonName::Down));
		_mappings[(std::int32_t)PlayerActions::Down].Targets.emplace_back(CreateTarget(0, AxisName::LeftY));

		_mappings[(std::int32_t)PlayerActions::Fire].Targets.emplace_back(CreateTarget(KeySym::SPACE));
		_mappings[(std::int32_t)PlayerActions::Fire].Targets.emplace_back(CreateTarget(0, ButtonName::X));
		_mappings[(std::int32_t)PlayerActions::Jump].Targets.emplace_back(CreateTarget(KeySym::V));
		_mappings[(std::int32_t)PlayerActions::Jump].Targets.emplace_back(CreateTarget(0, ButtonName::A));
		_mappings[(std::int32_t)PlayerActions::Run].Targets.emplace_back(CreateTarget(KeySym::C));
		_mappings[(std::int32_t)PlayerActions::Run].Targets.emplace_back(CreateTarget(0, ButtonName::B));
		_mappings[(std::int32_t)PlayerActions::ChangeWeapon].Targets.emplace_back(CreateTarget(KeySym::X));
		_mappings[(std::int32_t)PlayerActions::ChangeWeapon].Targets.emplace_back(CreateTarget(0, ButtonName::Y));
		_mappings[(std::int32_t)PlayerActions::Menu].Targets.emplace_back(CreateTarget(KeySym::ESCAPE));
		_mappings[(std::int32_t)PlayerActions::Menu].Targets.emplace_back(CreateTarget(0, ButtonName::Start));

		_mappings[(std::int32_t)PlayerActions::SwitchToBlaster].Targets.emplace_back(CreateTarget(KeySym::N1));
		_mappings[(std::int32_t)PlayerActions::SwitchToBouncer].Targets.emplace_back(CreateTarget(KeySym::N2));
		_mappings[(std::int32_t)PlayerActions::SwitchToFreezer].Targets.emplace_back(CreateTarget(KeySym::N3));
		_mappings[(std::int32_t)PlayerActions::SwitchToSeeker].Targets.emplace_back(CreateTarget(KeySym::N4));
		_mappings[(std::int32_t)PlayerActions::SwitchToRF].Targets.emplace_back(CreateTarget(KeySym::N5));
		_mappings[(std::int32_t)PlayerActions::SwitchToToaster].Targets.emplace_back(CreateTarget(KeySym::N6));
		_mappings[(std::int32_t)PlayerActions::SwitchToTNT].Targets.emplace_back(CreateTarget(KeySym::N7));
		_mappings[(std::int32_t)PlayerActions::SwitchToPepper].Targets.emplace_back(CreateTarget(KeySym::N8));
		_mappings[(std::int32_t)PlayerActions::SwitchToElectro].Targets.emplace_back(CreateTarget(KeySym::N9));
		_mappings[(std::int32_t)PlayerActions::SwitchToThunderbolt].Targets.emplace_back(CreateTarget(KeySym::N0));
	}

	ProcessedInput ControlScheme::FetchProcessedInput(std::int32_t playerIndex, const BitArray& pressedKeys, const ArrayView<const JoyMappedState*> joyStates, bool analogAsButtons)
	{
		ProcessedInput result = { };
		std::size_t joyStateCount = joyStates.size();

		const auto* mappings = &_mappings[playerIndex * (std::int32_t)PlayerActions::Count];
		for (std::size_t i = 0; i < (std::size_t)PlayerActions::Count; i++) {
			for (const auto& target : mappings[i].Targets) {
				if (target.Data & GamepadMask) {
					// Gamepad
					std::uint32_t joyIdx = (target.Data & GamepadIndexMask) >> 16;
					if (joyIdx < joyStateCount) {
						if (target.Data & GamepadAnalogMask) {
							// Analog axis
							float axisValue = joyStates[joyIdx]->axisValue((AxisName)(target.Data & ButtonMask));
							if (target.Data & GamepadNegativeMask) {
								axisValue = -axisValue;
							}
							if (analogAsButtons && axisValue >= IInputManager::AnalogButtonDeadZone) {
								result.PressedActions |= (1ull << (std::uint32_t)i) | (1ull << (32 + (std::uint32_t)i));
							}
							if (i < 4 && axisValue > GamepadDeadZone) {
								switch ((PlayerActions)i) {
									case PlayerActions::Left: {
										float axisValueAdjusted = -axisValue;
										if (result.Movement.X < GamepadDeadZone && axisValueAdjusted < result.Movement.X) {
											result.Movement.X = axisValueAdjusted;
										}
										break;
									}
									case PlayerActions::Right: {
										if (result.Movement.X > -GamepadDeadZone && axisValue > result.Movement.X) {
											result.Movement.X = axisValue;
										}
										break;
									}
									case PlayerActions::Up: {
										float axisValueAdjusted = -axisValue;
										if (result.Movement.Y < GamepadDeadZone && axisValueAdjusted < result.Movement.Y) {
											result.Movement.Y = axisValueAdjusted;
										}
										break;
									}
									case PlayerActions::Down: {
										if (result.Movement.Y > -GamepadDeadZone && axisValue > result.Movement.Y) {
											result.Movement.Y = axisValue;
										}
										break;
									}
								}
							}
						} else {
							// Button
							if (joyStates[joyIdx]->isButtonPressed((ButtonName)(target.Data & ButtonMask))) {
								result.PressedActions |= (1ull << (std::uint32_t)i) | (1ull << (32 + (std::uint32_t)i));
								if (analogAsButtons) {
									switch ((PlayerActions)i) {
										case PlayerActions::Left: result.Movement.X = -1.0f; break;
										case PlayerActions::Right: result.Movement.X = 1.0f; break;
										case PlayerActions::Up: result.Movement.Y = -1.0f; break;
										case PlayerActions::Down: result.Movement.Y = 1.0f; break;
									}
								}
								break;
							}
						}
					}
				} else {
					// Keyboard
					if (pressedKeys[target.Data]) {
						result.PressedActions |= (1ull << (std::uint32_t)i);
						if (analogAsButtons) {
							switch ((PlayerActions)i) {
								case PlayerActions::Left: result.Movement.X = -1.0f; break;
								case PlayerActions::Right: result.Movement.X = 1.0f; break;
								case PlayerActions::Up: result.Movement.Y = -1.0f; break;
								case PlayerActions::Down: result.Movement.Y = 1.0f; break;
							}
						}
						break;
					}
				}
			}
		}

		// Normalize both axes
		float movementLengthX = std::abs(result.Movement.X);
		float normalizedLength = (movementLengthX - GamepadDeadZone) / (1.0f - GamepadDeadZone * 2.0f);
		normalizedLength = std::clamp(normalizedLength, 0.0f, 1.0f);
		result.Movement.X = std::copysign(normalizedLength, result.Movement.X);

		float movementLengthY = std::abs(result.Movement.Y);
		normalizedLength = (movementLengthY - GamepadDeadZone) / (1.0f - GamepadDeadZone * 2.0f);
		normalizedLength = std::clamp(normalizedLength, 0.0f, 1.0f);
		result.Movement.Y = std::copysign(normalizedLength, result.Movement.Y);

#if defined(DEATH_TARGET_ANDROID)
		// Allow native Android back button as menu key
		if (PreferencesCache::UseNativeBackButton && pressedKeys[(uint32_t)KeySym::BACK]) {
			result.PressedActions |= (1 << (int32_t)PlayerActions::Menu);
		}
#endif
		return result;
	}

	std::uint32_t ControlScheme::FetchNativation(std::int32_t playerIndex, const BitArray& pressedKeys, const ArrayView<const JoyMappedState*> joyStates, bool allowGamepads)
	{
		std::uint32_t pressedActions = 0;
		std::size_t joyStateCount = joyStates.size();

		const auto* mappings = &_mappings[0 * (std::int32_t)PlayerActions::Count];
		for (std::size_t i = 0; i < (std::size_t)PlayerActions::CountInMenu; i++) {
			for (const auto& target : mappings[i].Targets) {
				if (target.Data & GamepadMask) {
					// Gamepad
					std::uint32_t joyIdx = (target.Data & GamepadIndexMask) >> 16;
					if (allowGamepads && joyIdx < joyStateCount) {
						if (target.Data & GamepadAnalogMask) {
							// Analog axis
							float axisValue = joyStates[joyIdx]->axisValue((AxisName)(target.Data & ButtonMask));
							if (target.Data & GamepadNegativeMask) {
								axisValue = -axisValue;
							}
							if (axisValue >= IInputManager::AnalogButtonDeadZone) {
								pressedActions |= (1 << (std::uint32_t)i);
							}
						} else {
							// Button
							bool isPressed = false;
							switch ((PlayerActions)i) {
								case PlayerActions::Up:
								case PlayerActions::Down:
								case PlayerActions::Left:
								case PlayerActions::Right:
									isPressed = (joyStates[joyIdx]->isButtonPressed((ButtonName)(target.Data & ButtonMask)));
									break;
								case PlayerActions::Fire:
									isPressed = (joyStates[joyIdx]->isButtonPressed(ButtonName::A) || joyStates[joyIdx]->isButtonPressed(ButtonName::X));
									break;
								case PlayerActions::Menu:
									isPressed = (joyStates[joyIdx]->isButtonPressed(ButtonName::B) || joyStates[joyIdx]->isButtonPressed(ButtonName::Start));
									break;
								case PlayerActions::ChangeWeapon:
									isPressed = (joyStates[joyIdx]->isButtonPressed(ButtonName::Y));
									break;
							}
							if (isPressed) {
								pressedActions |= (1 << (std::uint32_t)i);
								break;
							}
						}
					}
				} else {
					// Keyboard
					if (pressedKeys[target.Data]) {
						pressedActions |= (1 << (std::uint32_t)i);
						break;
					}
				}
			}
		}

		// Allow Jump action as confirm key
		if (pressedActions & (1 << (std::uint32_t)PlayerActions::Jump)) {
			pressedActions |= (1 << (std::uint32_t)PlayerActions::Fire);
		}

		// Also allow Return (Enter) as confirm key
		if (pressedKeys[(uint32_t)KeySym::RETURN]) {
			pressedActions |= (1 << (int32_t)PlayerActions::Fire);
		}
#if defined(DEATH_TARGET_ANDROID)
		// Allow native Android back button as menu key
		if (PreferencesCache::UseNativeBackButton && pressedKeys[(uint32_t)KeySym::BACK]) {
			pressedActions |= (1 << (int32_t)PlayerActions::Menu);
		}
#endif
		// Use ChangeWeapon action as delete key
		if (pressedKeys[(uint32_t)KeySym::Delete]) {
			pressedActions |= (1 << (int32_t)PlayerActions::ChangeWeapon);
		}

		return pressedActions;
	}

	ArrayView<ControlSchemeMapping> ControlScheme::GetMappings()
	{
		return _mappings;
	}

	MappingTarget ControlScheme::CreateTarget(KeySym key)
	{
		return { (std::uint32_t)key };
	}

	MappingTarget ControlScheme::CreateTarget(std::uint32_t gamepadIndex, ButtonName button)
	{
		return { GamepadMask | ((gamepadIndex << 16) & GamepadIndexMask) | (std::uint32_t)button };
	}

	MappingTarget ControlScheme::CreateTarget(std::uint32_t gamepadIndex, AxisName axis, bool negative)
	{
		std::uint32_t result = GamepadMask | GamepadAnalogMask | ((gamepadIndex << 16) & GamepadIndexMask) | (std::uint32_t)axis;
		if (negative) {
			result |= GamepadNegativeMask;
		}
		return { result };
	}
}