#include "ControlScheme.h"
#include "../PreferencesCache.h"
#include "../../nCine/Input/IInputManager.h"

using namespace nCine;

namespace Jazz2::UI
{
	ControlSchemeMapping ControlScheme::_mappings[MaxSupportedPlayers * (std::int32_t)PlayerActions::Count] = {};

	void ControlScheme::Reset()
	{
		// Clear all mappings
		for (auto& mapping : _mappings) {
			mapping.Targets.clear();
		}

		// Set default mappings for 1st player
		auto first = GetMappings(0);
		first[(std::int32_t)PlayerActions::Left].Targets.push_back(CreateTarget(Keys::Left));
		first[(std::int32_t)PlayerActions::Left].Targets.push_back(CreateTarget(0, ButtonName::Left));
		first[(std::int32_t)PlayerActions::Left].Targets.push_back(CreateTarget(0, AxisName::LeftX, true));
		first[(std::int32_t)PlayerActions::Right].Targets.push_back(CreateTarget(Keys::Right));
		first[(std::int32_t)PlayerActions::Right].Targets.push_back(CreateTarget(0, ButtonName::Right));
		first[(std::int32_t)PlayerActions::Right].Targets.push_back(CreateTarget(0, AxisName::LeftX));
		first[(std::int32_t)PlayerActions::Up].Targets.push_back(CreateTarget(Keys::Up));
		first[(std::int32_t)PlayerActions::Up].Targets.push_back(CreateTarget(0, ButtonName::Up));
		first[(std::int32_t)PlayerActions::Up].Targets.push_back(CreateTarget(0, AxisName::LeftY, true));
		first[(std::int32_t)PlayerActions::Down].Targets.push_back(CreateTarget(Keys::Down));
		first[(std::int32_t)PlayerActions::Down].Targets.push_back(CreateTarget(0, ButtonName::Down));
		first[(std::int32_t)PlayerActions::Down].Targets.push_back(CreateTarget(0, AxisName::LeftY));
		first[(std::int32_t)PlayerActions::Buttstomp].Targets.push_back(CreateTarget(Keys::Down));
		first[(std::int32_t)PlayerActions::Buttstomp].Targets.push_back(CreateTarget(0, ButtonName::Down));
		first[(std::int32_t)PlayerActions::Buttstomp].Targets.push_back(CreateTarget(0, AxisName::LeftY));

		first[(std::int32_t)PlayerActions::Fire].Targets.push_back(CreateTarget(Keys::Space));
		first[(std::int32_t)PlayerActions::Fire].Targets.push_back(CreateTarget(0, ButtonName::X));
		first[(std::int32_t)PlayerActions::Fire].Targets.push_back(CreateTarget(0, AxisName::RightTrigger));
		first[(std::int32_t)PlayerActions::Jump].Targets.push_back(CreateTarget(Keys::V));
		first[(std::int32_t)PlayerActions::Jump].Targets.push_back(CreateTarget(0, ButtonName::A));
		first[(std::int32_t)PlayerActions::Run].Targets.push_back(CreateTarget(Keys::C));
		first[(std::int32_t)PlayerActions::Run].Targets.push_back(CreateTarget(0, ButtonName::B));
		first[(std::int32_t)PlayerActions::Run].Targets.push_back(CreateTarget(0, AxisName::LeftTrigger));
		first[(std::int32_t)PlayerActions::ChangeWeapon].Targets.push_back(CreateTarget(Keys::X));
		first[(std::int32_t)PlayerActions::ChangeWeapon].Targets.push_back(CreateTarget(0, ButtonName::Y));
		first[(std::int32_t)PlayerActions::Menu].Targets.push_back(CreateTarget(Keys::Escape));
		first[(std::int32_t)PlayerActions::Menu].Targets.push_back(CreateTarget(0, ButtonName::Start));
		first[(std::int32_t)PlayerActions::Console].Targets.push_back(CreateTarget(Keys::Backquote));

		first[(std::int32_t)PlayerActions::SwitchToBlaster].Targets.push_back(CreateTarget(Keys::D1));
		first[(std::int32_t)PlayerActions::SwitchToBouncer].Targets.push_back(CreateTarget(Keys::D2));
		first[(std::int32_t)PlayerActions::SwitchToFreezer].Targets.push_back(CreateTarget(Keys::D3));
		first[(std::int32_t)PlayerActions::SwitchToSeeker].Targets.push_back(CreateTarget(Keys::D4));
		first[(std::int32_t)PlayerActions::SwitchToRF].Targets.push_back(CreateTarget(Keys::D5));
		first[(std::int32_t)PlayerActions::SwitchToToaster].Targets.push_back(CreateTarget(Keys::D6));
		first[(std::int32_t)PlayerActions::SwitchToTNT].Targets.push_back(CreateTarget(Keys::D7));
		first[(std::int32_t)PlayerActions::SwitchToPepper].Targets.push_back(CreateTarget(Keys::D8));
		first[(std::int32_t)PlayerActions::SwitchToElectro].Targets.push_back(CreateTarget(Keys::D9));
		first[(std::int32_t)PlayerActions::SwitchToThunderbolt].Targets.push_back(CreateTarget(Keys::D0));

		// Set default mappings for 2nd player
		if (MaxSupportedPlayers >= 2) {
			auto second = GetMappings(1);
			second[(std::int32_t)PlayerActions::Left].Targets.push_back(CreateTarget(Keys::A));
			second[(std::int32_t)PlayerActions::Left].Targets.push_back(CreateTarget(1, ButtonName::Left));
			second[(std::int32_t)PlayerActions::Left].Targets.push_back(CreateTarget(1, AxisName::LeftX, true));
			second[(std::int32_t)PlayerActions::Right].Targets.push_back(CreateTarget(Keys::D));
			second[(std::int32_t)PlayerActions::Right].Targets.push_back(CreateTarget(1, ButtonName::Right));
			second[(std::int32_t)PlayerActions::Right].Targets.push_back(CreateTarget(1, AxisName::LeftX));
			second[(std::int32_t)PlayerActions::Up].Targets.push_back(CreateTarget(Keys::W));
			second[(std::int32_t)PlayerActions::Up].Targets.push_back(CreateTarget(1, ButtonName::Up));
			second[(std::int32_t)PlayerActions::Up].Targets.push_back(CreateTarget(1, AxisName::LeftY, true));
			second[(std::int32_t)PlayerActions::Down].Targets.push_back(CreateTarget(Keys::S));
			second[(std::int32_t)PlayerActions::Down].Targets.push_back(CreateTarget(1, ButtonName::Down));
			second[(std::int32_t)PlayerActions::Down].Targets.push_back(CreateTarget(1, AxisName::LeftY));
			second[(std::int32_t)PlayerActions::Buttstomp].Targets.push_back(CreateTarget(Keys::S));
			second[(std::int32_t)PlayerActions::Buttstomp].Targets.push_back(CreateTarget(1, ButtonName::Down));
			second[(std::int32_t)PlayerActions::Buttstomp].Targets.push_back(CreateTarget(1, AxisName::LeftY));

			second[(std::int32_t)PlayerActions::Fire].Targets.push_back(CreateTarget(Keys::R));
			second[(std::int32_t)PlayerActions::Fire].Targets.push_back(CreateTarget(1, ButtonName::X));
			second[(std::int32_t)PlayerActions::Fire].Targets.push_back(CreateTarget(1, AxisName::RightTrigger));
			second[(std::int32_t)PlayerActions::Jump].Targets.push_back(CreateTarget(Keys::F));
			second[(std::int32_t)PlayerActions::Jump].Targets.push_back(CreateTarget(1, ButtonName::A));
			second[(std::int32_t)PlayerActions::Run].Targets.push_back(CreateTarget(Keys::G));
			second[(std::int32_t)PlayerActions::Run].Targets.push_back(CreateTarget(1, ButtonName::B));
			second[(std::int32_t)PlayerActions::Run].Targets.push_back(CreateTarget(1, AxisName::LeftTrigger));
			second[(std::int32_t)PlayerActions::ChangeWeapon].Targets.push_back(CreateTarget(Keys::T));
			second[(std::int32_t)PlayerActions::ChangeWeapon].Targets.push_back(CreateTarget(1, ButtonName::Y));
			second[(std::int32_t)PlayerActions::Menu].Targets.push_back(CreateTarget(1, ButtonName::Start));
		}
	}

	ProcessedInput ControlScheme::FetchProcessedInput(std::int32_t playerIndex, const BitArray& pressedKeys, const ArrayView<const JoyMappedState*> joyStates, bool analogAsButtons)
	{
		ProcessedInput result = { };
		std::size_t joyStateCount = joyStates.size();

		auto mappings = GetMappings(playerIndex);
		for (std::size_t i = 0; i < (std::size_t)PlayerActions::Count; i++) {
			for (const auto& target : mappings[i].Targets) {
				if (target.Data & GamepadMask) {
					// Gamepad
					std::uint32_t joyIdx = (target.Data & GamepadIndexMask) >> 16;
					if (joyIdx < joyStateCount) {
						if (target.Data & GamepadAnalogMask) {
							// Analog axis
							AxisName axisName = (AxisName)(target.Data & ButtonMask);
							float axisValue = joyStates[joyIdx]->axisValue(axisName);
							if (target.Data & GamepadNegativeMask) {
								axisValue = -axisValue;
							}
							if (analogAsButtons && axisValue >= (axisName >= AxisName::LeftTrigger ? IInputManager::TriggerButtonDeadZone : IInputManager::AnalogButtonDeadZone)) {
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
		if (PreferencesCache::UseNativeBackButton && pressedKeys[(uint32_t)Keys::Back]) {
			result.PressedActions |= (1 << (int32_t)PlayerActions::Menu);
		}
#endif
		return result;
	}

	std::uint32_t ControlScheme::FetchNavigation(const BitArray& pressedKeys, const ArrayView<const JoyMappedState*> joyStates, NavigationFlags flags)
	{
		std::uint32_t pressedActions = 0;
		std::size_t joyStateCount = joyStates.size();
		bool allowGamepads = (flags & NavigationFlags::AllowGamepads) == NavigationFlags::AllowGamepads;
		bool allowKeyboard = (flags & NavigationFlags::AllowKeyboard) == NavigationFlags::AllowKeyboard;

		for (std::int32_t j = 0; j < MaxSupportedPlayers; j++) {
			const auto* mappings = &_mappings[j * (std::int32_t)PlayerActions::Count];
			for (std::size_t i = 0; i < (std::size_t)PlayerActions::CountInMenu; i++) {
				for (const auto& target : mappings[i].Targets) {
					if (target.Data & GamepadMask) {
						// Gamepad
						std::uint32_t joyIdx = (target.Data & GamepadIndexMask) >> 16;
						if (allowGamepads && joyIdx < joyStateCount) {
							if (target.Data & GamepadAnalogMask) {
								// Analog axis
								AxisName axisName = (AxisName)(target.Data & ButtonMask);
								float axisValue = joyStates[joyIdx]->axisValue(axisName);
								if (target.Data & GamepadNegativeMask) {
									axisValue = -axisValue;
								}
								if (axisValue >= (axisName >= AxisName::LeftTrigger ? IInputManager::TriggerButtonDeadZone : IInputManager::AnalogButtonDeadZone)) {
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
					} else if (allowKeyboard) {
						// Keyboard
						if (pressedKeys[target.Data]) {
							pressedActions |= (1 << (std::uint32_t)i);
							break;
						}
					}
				}
			}
		}

		// Allow Jump action as confirm key
		if (pressedActions & (1 << (std::uint32_t)PlayerActions::Jump)) {
			pressedActions |= (1 << (std::uint32_t)PlayerActions::Fire);
		}

		if (allowKeyboard) {
			// Also allow Return (Enter) as confirm key
			if (pressedKeys[(std::uint32_t)Keys::Return] || pressedKeys[(std::uint32_t)Keys::NumPadEnter]) {
				pressedActions |= (1 << (std::int32_t)PlayerActions::Fire);
			}
			// Use ChangeWeapon action as delete key
			if (pressedKeys[(std::uint32_t)Keys::Delete]) {
				pressedActions |= (1 << (std::int32_t)PlayerActions::ChangeWeapon);
			}
		}

#if defined(DEATH_TARGET_ANDROID)
		// Allow native Android back button as menu key
		if (PreferencesCache::UseNativeBackButton && pressedKeys[(std::uint32_t)Keys::Back]) {
			pressedActions |= (1 << (std::int32_t)PlayerActions::Menu);
		}
#endif
		return pressedActions;
	}

	ArrayView<ControlSchemeMapping> ControlScheme::GetAllMappings()
	{
		return _mappings;
	}

	ArrayView<ControlSchemeMapping> ControlScheme::GetMappings(std::int32_t playerIdx)
	{
		return ArrayView(&_mappings[playerIdx * (std::int32_t)PlayerActions::Count], (std::int32_t)PlayerActions::Count);
	}

	std::int32_t ControlScheme::GetGamepadForPlayer(std::int32_t playerIdx)
	{
		const auto* mappings = &_mappings[playerIdx * (std::int32_t)PlayerActions::Count];
		for (std::size_t i = 0; i < (std::size_t)PlayerActions::CountInMenu; i++) {
			for (const auto& target : mappings[i].Targets) {
				if (target.Data & GamepadMask) {
					std::uint32_t joyIdx = (target.Data & GamepadIndexMask) >> 16;
					return joyIdx;
				}
			}
		}
		return -1;
	}

	MappingTarget ControlScheme::CreateTarget(Keys key)
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