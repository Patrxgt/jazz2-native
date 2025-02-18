﻿#include "MultiLevelHandler.h"

#if defined(WITH_MULTIPLAYER)

#include "PacketTypes.h"
#include "../PreferencesCache.h"
#include "../UI/ControlScheme.h"
#include "../UI/HUD.h"
#include "../UI/InGameConsole.h"
#include "../../Main.h"

#if defined(WITH_ANGELSCRIPT)
#	include "../Scripting/LevelScriptLoader.h"
#endif

#include "../../nCine/MainApplication.h"
#include "../../nCine/IAppEventHandler.h"
#include "../../nCine/ServiceLocator.h"
#include "../../nCine/Input/IInputEventHandler.h"
#include "../../nCine/Graphics/Camera.h"
#include "../../nCine/Graphics/Sprite.h"
#include "../../nCine/Graphics/Texture.h"
#include "../../nCine/Graphics/Viewport.h"
#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/Audio/AudioReaderMpt.h"
#include "../../nCine/Base/Random.h"

#include "../Actors/Player.h"
#include "../Actors/Multiplayer/LocalPlayerOnServer.h"
#include "../Actors/Multiplayer/RemotablePlayer.h"
#include "../Actors/Multiplayer/RemotePlayerOnServer.h"
#include "../Actors/Multiplayer/RemoteActor.h"
#include "../Actors/SolidObjectBase.h"
#include "../Actors/Enemies/Bosses/BossBase.h"

#include "../Actors/Environment/AirboardGenerator.h"
#include "../Actors/Environment/SteamNote.h"
#include "../Actors/Environment/SwingingVine.h"
#include "../Actors/Solid/Bridge.h"
#include "../Actors/Solid/MovingPlatform.h"
#include "../Actors/Solid/PinballBumper.h"
#include "../Actors/Solid/PinballPaddle.h"
#include "../Actors/Solid/SpikeBall.h"

#include <float.h>

#include <Utf8.h>
#include <Containers/StaticArray.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/MemoryStream.h>
#include <IO/Compression/DeflateStream.h>

using namespace Death::IO::Compression;
using namespace nCine;

namespace Jazz2::Multiplayer
{
	MultiLevelHandler::MultiLevelHandler(IRootController* root, NetworkManager* networkManager)
		: LevelHandler(root), _gameMode(MultiplayerGameMode::Unknown), _networkManager(networkManager), _updateTimeLeft(1.0f),
			_initialUpdateSent(false), _enableSpawning(true), _lastSpawnedActorId(-1), _seqNum(0), _seqNumWarped(0), _suppressRemoting(false),
			_ignorePackets(false)
#if defined(DEATH_DEBUG) && defined(WITH_IMGUI)
			, _plotIndex(0), _actorsMaxCount(0.0f), _actorsCount{}, _remoteActorsCount{}, _remotingActorsCount{},
			_mirroredActorsCount{}, _updatePacketMaxSize(0.0f), _updatePacketSize{}, _compressedUpdatePacketSize{}
#endif
	{
		_isServer = (networkManager->GetState() == NetworkState::Listening);
	}

	MultiLevelHandler::~MultiLevelHandler()
	{
	}

	bool MultiLevelHandler::Initialize(const LevelInitialization& levelInit)
	{
		DEATH_DEBUG_ASSERT(!levelInit.IsLocalSession);

		_suppressRemoting = true;
		bool initialized = LevelHandler::Initialize(levelInit);
		_suppressRemoting = false;
		if (!initialized) {
			return false;
		}

		if (_isServer) {
			// Reserve first 255 indices for players
			_lastSpawnedActorId = UINT8_MAX;

			std::uint8_t flags = 0;
			if (PreferencesCache::EnableReforgedGameplay) {
				flags |= 0x01;
			}
			MemoryStream packet(10 + _episodeName.size() + _levelFileName.size());
			packet.WriteValue<std::uint8_t>(flags);
			packet.WriteValue<std::uint8_t>((std::uint8_t)_gameMode);
			packet.WriteVariableUint32(_episodeName.size());
			packet.Write(_episodeName.data(), _episodeName.size());
			packet.WriteVariableUint32(_levelFileName.size());
			packet.Write(_levelFileName.data(), _levelFileName.size());
			// TODO: Send it to only authenticated peers
			_networkManager->SendTo(AllPeers, NetworkChannel::Main, (std::uint8_t)ServerPacketType::LoadLevel, packet);
		}

		auto& resolver = ContentResolver::Get();
		resolver.PreloadMetadataAsync("Interactive/PlayerJazz"_s);
		resolver.PreloadMetadataAsync("Interactive/PlayerSpaz"_s);
		resolver.PreloadMetadataAsync("Interactive/PlayerLori"_s);
		return true;
	}

	bool MultiLevelHandler::IsLocalSession() const
	{
		return false;
	}

	bool MultiLevelHandler::IsPausable() const
	{
		return false;
	}

	float MultiLevelHandler::GetDefaultAmbientLight() const
	{
		// TODO: Remove this override
		return LevelHandler::GetDefaultAmbientLight();
	}

	void MultiLevelHandler::SetAmbientLight(Actors::Player* player, float value)
	{
		if (_isServer && player != nullptr) {
			auto it = _playerStates.find(player->_playerIndex);
			if (it != _playerStates.end()) {
				// TODO: Send it to remote peer
				return;
			}
		}

		LevelHandler::SetAmbientLight(player, value);
	}

	void MultiLevelHandler::OnBeginFrame()
	{
		LevelHandler::OnBeginFrame();

		if (!_isServer) {
			auto& input = _playerInputs[0];
			if (input.PressedActions != input.PressedActionsLast) {
				MemoryStream packet(12);
				packet.WriteVariableUint32(_lastSpawnedActorId);
				packet.WriteVariableUint64(_console->IsVisible() ? 0 : input.PressedActions);
				_networkManager->SendTo(AllPeers, NetworkChannel::UnreliableUpdates, (std::uint8_t)ClientPacketType::PlayerKeyPress, packet);
			}
		}
	}

	void MultiLevelHandler::OnEndFrame()
	{
		LevelHandler::OnEndFrame();

		float timeMult = theApplication().GetTimeMult();
		std::uint32_t frameCount = theApplication().GetFrameCount();

		// Update last pressed keys only if it wasn't done this frame yet (because of PlayerKeyPress packet)
		for (auto& [playerIndex, playerState] : _playerStates) {
			if (playerState.UpdatedFrame != frameCount) {
				playerState.UpdatedFrame = frameCount;
				playerState.PressedKeysLast = playerState.PressedKeys;
			}
		}

		_updateTimeLeft -= timeMult;
		if (_updateTimeLeft < 0.0f) {
			_updateTimeLeft = FrameTimer::FramesPerSecond / UpdatesPerSecond;

			if (!_initialUpdateSent) {
				_initialUpdateSent = true;

				LOGD("[MP] Level \"%s/%s\" is ready", _episodeName.data(), _levelFileName.data());

				if (!_isServer) {
					_networkManager->SendTo(AllPeers, NetworkChannel::Main, (std::uint8_t)ClientPacketType::LevelReady, {});
				}
			}

#if defined(DEATH_DEBUG) && defined(WITH_IMGUI)
			_plotIndex = (_plotIndex + 1) % PlotValueCount;

			_actorsCount[_plotIndex] = _actors.size();
			_actorsMaxCount = std::max(_actorsMaxCount, _actorsCount[_plotIndex]);
			_remoteActorsCount[_plotIndex] = 0;
			_mirroredActorsCount[_plotIndex] = 0;
			for (auto& actor : _remoteActors) {
				if (actor.second == nullptr) {
					continue;
				}

				bool isMirrored = (_isServer
					? ActorShouldBeMirrored(actor.second.get())
					: !runtime_cast<Actors::Multiplayer::RemoteActor*>(actor.second));

				if (isMirrored) {
					_mirroredActorsCount[_plotIndex]++;
				} else {
					_remoteActorsCount[_plotIndex]++;
				}
			}
			_remotingActorsCount[_plotIndex] = _remotingActors.size();
#endif

			if (_isServer) {
				std::uint32_t actorCount = (std::uint32_t)(_players.size() + _remotingActors.size());

				MemoryStream packet(5 + actorCount * 19);
				packet.WriteVariableUint32(actorCount);

				for (Actors::Player* player : _players) {
					/*Vector2f pos;
					auto it = _playerStates.find(player->_playerIndex);
					if (it != _playerStates.end()) {
						// Remote players
						pos = player->_pos;

						// TODO: This should be WarpUpdatesLeft
						if (it->second.WarpTimeLeft > 0.0f) {
							it->second.WarpTimeLeft -= timeMult;
							if (it->second.WarpTimeLeft <= 0.0f) {
								it->second.WarpTimeLeft = 0.0f;
								LOGW("Player warped without permission (possible cheating attempt)");
							}
						} else if (it->second.WarpTimeLeft < 0.0f) {
							it->second.WarpTimeLeft += timeMult;
							if (it->second.WarpTimeLeft >= 0.0f) {
								it->second.WarpTimeLeft = 0.0f;
								LOGW("Player failed to warp in time (possible cheating attempt)");
							}
						}
					} else {
						// Local players
						pos = player->_pos;
					}*/
					Vector2f pos = player->_pos;

					packet.WriteVariableUint32(player->_playerIndex);
					packet.WriteValue<std::int32_t>((std::int32_t)(pos.X * 512.0f));
					packet.WriteValue<std::int32_t>((std::int32_t)(pos.Y * 512.0f));
					packet.WriteVariableUint32((std::uint32_t)(player->_currentTransition != nullptr ? player->_currentTransition->State : player->_currentAnimation->State));

					float rotation = player->_renderer.rotation();
					if (rotation < 0.0f) rotation += fRadAngle360;
					packet.WriteValue<std::uint8_t>((std::uint8_t)(rotation * 255.0f / fRadAngle360));

					std::uint8_t flags = 0;
					if (player->IsFacingLeft()) {
						flags |= 0x01;
					}
					if (player->_renderer.isDrawEnabled()) {
						flags |= 0x02;
					}
					if (player->_renderer.AnimPaused) {
						flags |= 0x04;
					}
					packet.WriteValue<std::uint8_t>(flags);

					Actors::ActorRendererType rendererType = player->_renderer.GetRendererType();
					packet.WriteValue<std::uint8_t>((std::uint8_t)rendererType);
				}

				for (const auto& [remotingActor, remotingActorId] : _remotingActors) {
					packet.WriteVariableUint32(remotingActorId);
					packet.WriteValue<std::int32_t>((std::int32_t)(remotingActor->_pos.X * 512.0f));
					packet.WriteValue<std::int32_t>((std::int32_t)(remotingActor->_pos.Y * 512.0f));
					packet.WriteVariableUint32((std::uint32_t)(remotingActor->_currentTransition != nullptr ? remotingActor->_currentTransition->State : (remotingActor->_currentAnimation != nullptr ? remotingActor->_currentAnimation->State : AnimState::Idle)));
					
					float rotation = remotingActor->_renderer.rotation();
					if (rotation < 0.0f) rotation += fRadAngle360;
					packet.WriteValue<std::uint8_t>((std::uint8_t)(rotation * 255.0f / fRadAngle360));

					std::uint8_t flags = 0;
					if (remotingActor->IsFacingLeft()) {
						flags |= 0x01;
					}
					if (remotingActor->_renderer.isDrawEnabled()) {
						flags |= 0x02;
					}
					if (remotingActor->_renderer.AnimPaused) {
						flags |= 0x04;
					}
					packet.WriteValue<std::uint8_t>(flags);

					Actors::ActorRendererType rendererType = remotingActor->_renderer.GetRendererType();
					packet.WriteValue<std::uint8_t>((std::uint8_t)rendererType);
				}

				MemoryStream packetCompressed(1024);
				{
					DeflateWriter dw(packetCompressed);
					dw.Write(packet.GetBuffer(), packet.GetSize());
				}

#if defined(DEATH_DEBUG) && defined(WITH_IMGUI)
				_updatePacketSize[_plotIndex] = packet.GetSize();
				_updatePacketMaxSize = std::max(_updatePacketMaxSize, _updatePacketSize[_plotIndex]);
				_compressedUpdatePacketSize[_plotIndex] = packetCompressed.GetSize();
#endif

				_networkManager->SendTo([this](const Peer& peer) {
					auto it = _peerDesc.find(peer);
					return (it != _peerDesc.end() && it->second.State == PeerState::LevelSynchronized);
				}, NetworkChannel::UnreliableUpdates, (std::uint8_t)ServerPacketType::UpdateAllActors, packetCompressed);

				SynchronizePeers();
			} else {
				if (!_players.empty()) {
					_seqNum++;

					Clock& c = nCine::clock();
					std::uint64_t now = c.now() * 1000 / c.frequency();
					auto player = _players[0];

					PlayerFlags flags = (PlayerFlags)player->_currentSpecialMove;
					if (player->IsFacingLeft()) {
						flags |= PlayerFlags::IsFacingLeft;
					}
					if (player->_renderer.isDrawEnabled()) {
						flags |= PlayerFlags::IsVisible;
					}
					if (player->_isActivelyPushing) {
						flags |= PlayerFlags::IsActivelyPushing;
					}
					if (_seqNumWarped != 0) {
						flags |= PlayerFlags::JustWarped;
					}

					MemoryStream packet(20);
					packet.WriteVariableUint32(_lastSpawnedActorId);
					packet.WriteVariableUint64(now);
					packet.WriteValue<std::int32_t>((std::int32_t)(player->_pos.X * 512.0f));
					packet.WriteValue<std::int32_t>((std::int32_t)(player->_pos.Y * 512.0f));
					packet.WriteValue<std::int16_t>((std::int16_t)(player->_speed.X * 512.0f));
					packet.WriteValue<std::int16_t>((std::int16_t)(player->_speed.Y * 512.0f));
					packet.WriteVariableUint32((std::uint32_t)flags);

					if (_seqNumWarped != 0) {
						packet.WriteVariableUint64(_seqNumWarped);
					}

#if defined(DEATH_DEBUG) && defined(WITH_IMGUI)
					_updatePacketSize[_plotIndex] = packet.GetSize();
					_compressedUpdatePacketSize[_plotIndex] = _updatePacketSize[_plotIndex];
					_updatePacketMaxSize = std::max(_updatePacketMaxSize, _updatePacketSize[_plotIndex]);
#endif

					_networkManager->SendTo(AllPeers, NetworkChannel::UnreliableUpdates, (std::uint8_t)ClientPacketType::PlayerUpdate, packet);
				}
			}
		}

#if defined(DEATH_DEBUG) && defined(WITH_IMGUI)
		ShowDebugWindow();

		if (PreferencesCache::ShowPerformanceMetrics) {
			ImDrawList* drawList = ImGui::GetBackgroundDrawList();

			/*if (_isServer) {
				for (Actors::Player* player : _players) {
					char actorIdString[32];

					Vector2f pos;
					auto it = _playerStates.find(player->_playerIndex);
					if (it != _playerStates.end()) {
						for (std::size_t i = 0; i < arraySize(it->second.StateBuffer); i++) {
							auto posFrom = WorldPosToScreenSpace(it->second.StateBuffer[i].Pos);
							auto posTo = WorldPosToScreenSpace(it->second.StateBuffer[i].Pos + it->second.StateBuffer[i].Speed);

							drawList->AddLine(posFrom, posTo, ImColor(255, 200, 120, 220), 2.0f);
						}

						std::int32_t prevIdx = it->second.StateBufferPos - 1;
						while (prevIdx < 0) {
							prevIdx += std::int32_t(arraySize(it->second.StateBuffer));
						}

						auto posPrev = WorldPosToScreenSpace(it->second.StateBuffer[prevIdx].Pos);
						auto posLocal = WorldPosToScreenSpace(player->_pos);
						drawList->AddLine(posPrev, posLocal, ImColor(255, 60, 60, 220), 2.0f);

						formatString(actorIdString, arraySize(actorIdString), "%i [%0.1f] %04x", player->_playerIndex, it->second.DeviationTime, it->second.Flags);
					} else {
						formatString(actorIdString, arraySize(actorIdString), "%i", player->_playerIndex);
					}

					auto aabbMin = WorldPosToScreenSpace({ player->AABB.L, player->AABB.T });
					aabbMin.x += 4.0f;
					aabbMin.y += 3.0f;
					drawList->AddText(aabbMin, ImColor(255, 255, 255), actorIdString);
				}

				for (const auto& [actor, actorId] : _remotingActors) {
					char actorIdString[16];
					formatString(actorIdString, arraySize(actorIdString), "%u", actorId);

					auto aabbMin = WorldPosToScreenSpace({ actor->AABB.L, actor->AABB.T });
					aabbMin.x += 4.0f;
					aabbMin.y += 3.0f;
					drawList->AddText(aabbMin, ImColor(255, 255, 255), actorIdString);
				}
			} else {
				for (Actors::Player* player : _players) {
					char actorIdString[16];
					formatString(actorIdString, arraySize(actorIdString), "%i", player->_playerIndex);

					auto aabbMin = WorldPosToScreenSpace({ player->AABB.L, player->AABB.T });
					aabbMin.x += 4.0f;
					aabbMin.y += 3.0f;
					drawList->AddText(aabbMin, ImColor(255, 255, 255), actorIdString);
				}

				for (const auto& [actorId, actor] : _remoteActors) {
					char actorIdString[16];
					formatString(actorIdString, arraySize(actorIdString), "%u", actorId);

					auto aabbMin = WorldPosToScreenSpace({ actor->AABBInner.L, actor->AABBInner.T });
					aabbMin.x += 4.0f;
					aabbMin.y += 3.0f;
					drawList->AddText(aabbMin, ImColor(255, 255, 255), actorIdString);
				}
			}*/
		}
#endif
	}

	void MultiLevelHandler::OnInitializeViewport(std::int32_t width, std::int32_t height)
	{
		LevelHandler::OnInitializeViewport(width, height);
	}

	bool MultiLevelHandler::OnConsoleCommand(StringView line)
	{
		if (LevelHandler::OnConsoleCommand(line)) {
			return true;
		}

		if (_isServer) {
			if (line.hasPrefix('/')) {
				if (line.hasPrefix("/ban "_s)) {
					// TODO: Implement /ban
				} else if (line == "/info"_s) {
					char infoBuffer[128];
					formatString(infoBuffer, sizeof(infoBuffer), "Current level: %s/%s (\f[w:80]\f[c:#707070]%s\f[/c]\f[/w])", _episodeName.data(), _levelFileName.data(), GameModeToString(_gameMode).data());
					_console->WriteLine(UI::MessageLevel::Info, infoBuffer);
					formatString(infoBuffer, sizeof(infoBuffer), "Players: \f[w:80]\f[c:#707070]%zu\f[/c]\f[/w]/%zu", _peerDesc.size() + 1, NetworkManager::MaxPeerCount);
					_console->WriteLine(UI::MessageLevel::Info, infoBuffer);
					formatString(infoBuffer, sizeof(infoBuffer), "Server load: %i ms", (std::int32_t)(theApplication().GetFrameTimer().GetLastFrameDuration() * 1000.0f));
					_console->WriteLine(UI::MessageLevel::Info, infoBuffer);
					return true;
				} else if (line.hasPrefix("/kick "_s)) {
					// TODO: Implement /kick
				} else if (line.hasPrefix("/set "_s)) {
					auto [variableName, _, value] = line.exceptPrefix("/set "_s).trimmedPrefix().partition(' ');
					if (variableName == "mode"_s) {
						auto gameModeString = StringUtils::lowercase(value.trimmed());

						MultiplayerGameMode gameMode;
						if (gameModeString == "battle"_s || gameModeString == "b"_s) {
							gameMode = MultiplayerGameMode::Battle;
						} else if (gameModeString == "teambattle"_s || gameModeString == "tb"_s) {
							gameMode = MultiplayerGameMode::TeamBattle;
						} else if (gameModeString == "capturetheflag"_s || gameModeString == "ctf"_s) {
							gameMode = MultiplayerGameMode::CaptureTheFlag;
						} else if (gameModeString == "race"_s || gameModeString == "r"_s) {
							gameMode = MultiplayerGameMode::Race;
						} else if (gameModeString == "teamrace"_s || gameModeString == "tr"_s) {
							gameMode = MultiplayerGameMode::TeamRace;
						} else if (gameModeString == "treasurehunt"_s || gameModeString == "th"_s) {
							gameMode = MultiplayerGameMode::TreasureHunt;
						} else if (gameModeString == "cooperation"_s || gameModeString == "coop"_s || gameModeString == "c"_s) {
							gameMode = MultiplayerGameMode::Cooperation;
						} else {
							return false;
						}

						if (SetGameMode(gameMode)) {
							char infoBuffer[128];
							formatString(infoBuffer, sizeof(infoBuffer), "Game mode set to \f[w:80]\f[c:#707070]%s\f[/c]\f[/w]", GameModeToString(_gameMode).data());
							_console->WriteLine(UI::MessageLevel::Info, infoBuffer);
							return true;
						}
					} else if (variableName == "level"_s) {
						// TODO: Implement /set level
					} else if (variableName == "name"_s) {
						auto name = value.trimmed();
						_root->SetServerName(name);

						name = _root->GetServerName();
						char infoBuffer[128];
						if (!name.empty()) {
							formatString(infoBuffer, sizeof(infoBuffer), "Server name set to \f[w:80]\f[c:#707070]%s\f[/c]\f[/w]", String::nullTerminatedView(name).data());
						} else {
							formatString(infoBuffer, sizeof(infoBuffer), "Server visibility to \f[w:80]\f[c:#707070]hidden\f[/c]\f[/w]");
						}
						_console->WriteLine(UI::MessageLevel::Info, infoBuffer);
						return true;
					} else if (variableName == "spawning"_s) {
						auto boolValue = StringUtils::lowercase(value.trimmed());
						if (boolValue == "false"_s || boolValue == "off"_s || boolValue == "0"_s) {
							_enableSpawning = false;
						} else if (boolValue == "true"_s || boolValue == "on"_s || boolValue == "1"_s) {
							_enableSpawning = true;
						} else {
							return false;
						}

						char infoBuffer[128];
						formatString(infoBuffer, sizeof(infoBuffer), "Spawning set to \f[w:80]\f[c:#707070]%s\f[/c]\f[/w]", _enableSpawning ? "Enabled" : "Disabled");
						_console->WriteLine(UI::MessageLevel::Info, infoBuffer);
						return true;
					}
				} else if (line.hasPrefix("/alert "_s)) {
					StringView message = line.exceptPrefix("/alert "_s).trimmed();
					if (!message.empty()) {
						MemoryStream packet(4 + message.size());
						packet.WriteVariableUint32((std::uint32_t)message.size());
						packet.Write(message.data(), (std::uint32_t)message.size());

						_networkManager->SendTo([this](const Peer& peer) {
							auto it = _peerDesc.find(peer);
							return (it != _peerDesc.end() && it->second.State != PeerState::Unknown);
						}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::ShowAlert, packet);
					}
				}

				return false;
			}

			// Chat message
			MemoryStream packet(9 + line.size());
			packet.WriteVariableUint32(0); // TODO: Player index
			packet.WriteValue<std::uint8_t>(0); // Reserved
			packet.WriteVariableUint32((std::uint32_t)line.size());
			packet.Write(line.data(), (std::uint32_t)line.size());

			_networkManager->SendTo([this](const Peer& peer) {
				auto it = _peerDesc.find(peer);
				return (it != _peerDesc.end() && it->second.State != PeerState::Unknown);
			}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::ChatMessage, packet);
		} else {
			if (line.hasPrefix('/')) {
				// Command are allowed only on server
				return false;
			}

			// Chat message
			MemoryStream packet(9 + line.size());
			packet.WriteVariableUint32(_lastSpawnedActorId);
			packet.WriteValue<std::uint8_t>(0); // Reserved
			packet.WriteVariableUint32((std::uint32_t)line.size());
			packet.Write(line.data(), (std::uint32_t)line.size());
			_networkManager->SendTo(AllPeers, NetworkChannel::Main, (std::uint8_t)ClientPacketType::ChatMessage, packet);
		}

		return true;
	}

	void MultiLevelHandler::OnKeyPressed(const KeyboardEvent& event)
	{
		LevelHandler::OnKeyPressed(event);
	}

	void MultiLevelHandler::OnKeyReleased(const KeyboardEvent& event)
	{
		LevelHandler::OnKeyReleased(event);
	}

	void MultiLevelHandler::OnTouchEvent(const TouchEvent& event)
	{
		LevelHandler::OnTouchEvent(event);
	}

	void MultiLevelHandler::AddActor(std::shared_ptr<Actors::ActorBase> actor)
	{
		LevelHandler::AddActor(actor);

		if (!_suppressRemoting && _isServer) {
			std::uint32_t actorId = FindFreeActorId();
			Actors::ActorBase* actorPtr = actor.get();
			_remotingActors[actorPtr] = actorId;

			// Store only used IDs on server-side
			_remoteActors[actorId] = nullptr;

			if (ActorShouldBeMirrored(actorPtr)) {
				Vector2i originTile = actorPtr->_originTile;
				const auto& eventTile = _eventMap->GetEventTile(originTile.X, originTile.Y);
				if (eventTile.Event != EventType::Empty) {
					MemoryStream packet(24 + Events::EventSpawner::SpawnParamsSize);
					packet.WriteVariableUint32(actorId);
					packet.WriteVariableUint32((std::uint32_t)eventTile.Event);
					packet.Write(eventTile.EventParams, Events::EventSpawner::SpawnParamsSize);
					packet.WriteVariableUint32((std::uint32_t)eventTile.EventFlags);
					packet.WriteVariableInt32((std::int32_t)originTile.X);
					packet.WriteVariableInt32((std::int32_t)originTile.Y);
					packet.WriteVariableInt32((std::int32_t)actorPtr->_renderer.layer());

					_networkManager->SendTo([this](const Peer& peer) {
						auto it = _peerDesc.find(peer);
						return (it != _peerDesc.end() && it->second.State == PeerState::LevelSynchronized);
					}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::CreateMirroredActor, packet);
				}
			} else {
				const auto& metadataPath = actorPtr->_metadata->Path;

				MemoryStream packet(28 + metadataPath.size());
				packet.WriteVariableUint32(actorId);
				packet.WriteVariableInt32((std::int32_t)actorPtr->_pos.X);
				packet.WriteVariableInt32((std::int32_t)actorPtr->_pos.Y);
				packet.WriteVariableInt32((std::int32_t)actorPtr->_renderer.layer());
				packet.WriteVariableUint32((std::uint32_t)actorPtr->_state);
				packet.WriteVariableUint32((std::uint32_t)metadataPath.size());
				packet.Write(metadataPath.data(), (std::uint32_t)metadataPath.size());
				packet.WriteVariableUint32((std::uint32_t)(actorPtr->_currentTransition != nullptr ? actorPtr->_currentTransition->State : actorPtr->_currentAnimation->State));
				
				_networkManager->SendTo([this](const Peer& peer) {
					auto it = _peerDesc.find(peer);
					return (it != _peerDesc.end() && it->second.State == PeerState::LevelSynchronized);
				}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::CreateRemoteActor, packet);
			}
		}
	}

	std::shared_ptr<AudioBufferPlayer> MultiLevelHandler::PlaySfx(Actors::ActorBase* self, StringView identifier, AudioBuffer* buffer, const Vector3f& pos, bool sourceRelative, float gain, float pitch)
	{
		if (_isServer) {
			std::uint32_t actorId;
			auto it = _remotingActors.find(self);
			if (auto* player = runtime_cast<Actors::Player*>(self)) {
				actorId = player->_playerIndex;
			} else if (it != _remotingActors.end()) {
				actorId = it->second;
			} else {
				actorId = UINT32_MAX;
			}

			if (actorId != UINT32_MAX) {
				MemoryStream packet(12 + identifier.size());
				packet.WriteVariableUint32(actorId);
				// TODO: sourceRelative
				// TODO: looping
				packet.WriteValue<std::uint16_t>(floatToHalf(gain));
				packet.WriteValue<std::uint16_t>(floatToHalf(pitch));
				packet.WriteVariableUint32((std::uint32_t)identifier.size());
				packet.Write(identifier.data(), (std::uint32_t)identifier.size());

				_networkManager->SendTo([this, self](const Peer& peer) {
					auto it = _peerDesc.find(peer);
					return (it != _peerDesc.end() && it->second.State == PeerState::LevelSynchronized && it->second.Player != self);
				}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlaySfx, packet);
			}
		}

		return LevelHandler::PlaySfx(self, identifier, buffer, pos, sourceRelative, gain, pitch);
	}

	std::shared_ptr<AudioBufferPlayer> MultiLevelHandler::PlayCommonSfx(StringView identifier, const Vector3f& pos, float gain, float pitch)
	{
		if (_isServer) {
			MemoryStream packet(16 + identifier.size());
			packet.WriteVariableInt32((std::int32_t)pos.X);
			packet.WriteVariableInt32((std::int32_t)pos.Y);
			// TODO: looping
			packet.WriteValue<std::uint16_t>(floatToHalf(gain));
			packet.WriteValue<std::uint16_t>(floatToHalf(pitch));
			packet.WriteVariableUint32((std::uint32_t)identifier.size());
			packet.Write(identifier.data(), (std::uint32_t)identifier.size());

			_networkManager->SendTo([this](const Peer& peer) {
				auto it = _peerDesc.find(peer);
				return (it != _peerDesc.end() && it->second.State == PeerState::LevelSynchronized);
			}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayCommonSfx, packet);
		}

		return LevelHandler::PlayCommonSfx(identifier, pos, gain, pitch);
	}

	void MultiLevelHandler::WarpCameraToTarget(Actors::ActorBase* actor, bool fast)
	{
		LevelHandler::WarpCameraToTarget(actor, fast);
	}

	bool MultiLevelHandler::IsPositionEmpty(Actors::ActorBase* self, const AABBf& aabb, TileCollisionParams& params, Actors::ActorBase** collider)
	{
		return LevelHandler::IsPositionEmpty(self, aabb, params, collider);
	}

	void MultiLevelHandler::FindCollisionActorsByAABB(const Actors::ActorBase* self, const AABBf& aabb, Function<bool(Actors::ActorBase*)>&& callback)
	{
		LevelHandler::FindCollisionActorsByAABB(self, aabb, std::move(callback));
	}

	void MultiLevelHandler::FindCollisionActorsByRadius(float x, float y, float radius, Function<bool(Actors::ActorBase*)>&& callback)
	{
		LevelHandler::FindCollisionActorsByRadius(x, y, radius, std::move(callback));
	}

	void MultiLevelHandler::GetCollidingPlayers(const AABBf& aabb, Function<bool(Actors::ActorBase*)>&& callback)
	{
		LevelHandler::GetCollidingPlayers(aabb, std::move(callback));
	}

	void MultiLevelHandler::BroadcastTriggeredEvent(Actors::ActorBase* initiator, EventType eventType, uint8_t* eventParams)
	{
		LevelHandler::BroadcastTriggeredEvent(initiator, eventType, eventParams);
	}

	void MultiLevelHandler::BeginLevelChange(Actors::ActorBase* initiator, ExitType exitType, StringView nextLevel)
	{
		if (!_isServer) {
			// Level can be changed only by server
			return;
		}

		LOGD("[MP] Changing level to \"%s\" (0x%02x)", nextLevel.data(), exitType);

		LevelHandler::BeginLevelChange(initiator, exitType, nextLevel);
	}

	void MultiLevelHandler::HandleGameOver(Actors::Player* player)
	{
		// TODO
		//LevelHandler::HandleGameOver(player);
	}

	bool MultiLevelHandler::HandlePlayerDied(Actors::Player* player, Actors::ActorBase* collider)
	{
#if defined(WITH_ANGELSCRIPT)
		if (_scripts != nullptr) {
			_scripts->OnPlayerDied(player, collider);
		}
#endif

		// TODO
		//return LevelHandler::HandlePlayerDied(player, collider);

		if (_isServer && _enableSpawning) {
			for (const auto& [peer, peerDesc] : _peerDesc) {
				if (peerDesc.Player == player) {
					MemoryStream packet(12);
					packet.WriteVariableUint32(player->_playerIndex);
					packet.WriteValue<std::int32_t>((std::int32_t)(player->_checkpointPos.X * 512.0f));
					packet.WriteValue<std::int32_t>((std::int32_t)(player->_checkpointPos.Y * 512.0f));
					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerRespawn, packet);
					break;
				}
			}
			return true;
		} else {
			return false;
		}
	}

	void MultiLevelHandler::HandlePlayerLevelChanging(Actors::Player* player, ExitType exitType)
	{
		// TODO: Only called by RemotePlayerOnServer
		if (_isServer) {
			for (const auto& [peer, peerDesc] : _peerDesc) {
				if (peerDesc.Player == player) {
					MemoryStream packet(5);
					packet.WriteVariableUint32(player->_playerIndex);
					packet.WriteValue<std::uint8_t>((std::uint8_t)exitType);
					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerWarpIn, packet);
					break;
				}
			}
		}
	}

	bool MultiLevelHandler::HandlePlayerSpring(Actors::Player* player, Vector2f pos, Vector2f force, bool keepSpeedX, bool keepSpeedY)
	{
		// TODO: Only called by RemotePlayerOnServer
		if (_isServer) {
			for (const auto& [peer, peerDesc] : _peerDesc) {
				if (peerDesc.Player == player) {
					std::uint8_t flags = 0;
					if (keepSpeedX) {
						flags |= 0x01;
					}
					if (keepSpeedY) {
						flags |= 0x02;
					}

					MemoryStream packet(17);
					packet.WriteVariableUint32(player->_playerIndex);
					packet.WriteValue<std::int32_t>((std::int32_t)(pos.X * 512.0f));
					packet.WriteValue<std::int32_t>((std::int32_t)(pos.Y * 512.0f));
					packet.WriteValue<std::int16_t>((std::int16_t)(force.X * 512.0f));
					packet.WriteValue<std::int16_t>((std::int16_t)(force.Y * 512.0f));
					packet.WriteValue<std::uint8_t>(flags);
					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerActivateSpring, packet);
					break;
				}
			}
		}

		return true;
	}

	void MultiLevelHandler::HandlePlayerBeforeWarp(Actors::Player* player, Vector2f pos, WarpFlags flags)
	{
		if (!_isServer) {
			return;
		}

		if (_gameMode == MultiplayerGameMode::Race && (flags & WarpFlags::IncrementLaps) == WarpFlags::IncrementLaps) {
			// TODO: Increment laps
		}

		if ((flags & WarpFlags::Fast) == WarpFlags::Fast) {
			// Nothing to do, sending PlayerMoveInstantly packet is enough
			return;
		}

		for (const auto& [peer, peerDesc] : _peerDesc) {
			if (peerDesc.Player == player) {
				MemoryStream packet(5);
				packet.WriteVariableUint32(player->_playerIndex);
				packet.WriteValue<std::uint8_t>(0xFF);	// Only temporary, no level changing
				_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerWarpIn, packet);
				break;
			}
		}
	}

	void MultiLevelHandler::HandlePlayerTakeDamage(Actors::Player* player, std::int32_t amount, float pushForce)
	{
		// TODO: Only called by RemotePlayerOnServer
		if (_isServer) {
			for (const auto& [peer, peerDesc] : _peerDesc) {
				if (peerDesc.Player == player) {
					MemoryStream packet(10);
					packet.WriteVariableUint32(player->_playerIndex);
					packet.WriteVariableInt32(player->_health);
					packet.WriteValue<std::int16_t>((std::int16_t)(pushForce * 512.0f));
					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerTakeDamage, packet);
					break;
				}
			}
		}
	}

	void MultiLevelHandler::HandlePlayerRefreshAmmo(Actors::Player* player, WeaponType weaponType)
	{
		// TODO: Only called by RemotePlayerOnServer
		if (_isServer) {
			for (const auto& [peer, peerDesc] : _peerDesc) {
				if (peerDesc.Player == player) {
					MemoryStream packet(7);
					packet.WriteVariableUint32(player->_playerIndex);
					packet.WriteValue<std::uint8_t>((std::uint8_t)player->_currentWeapon);
					packet.WriteValue<std::uint16_t>((std::uint16_t)player->_weaponAmmo[(std::uint8_t)player->_currentWeapon]);
					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerRefreshAmmo, packet);
					break;
				}
			}
		}
	}

	void MultiLevelHandler::HandlePlayerRefreshWeaponUpgrades(Actors::Player* player, WeaponType weaponType)
	{
		// TODO: Only called by RemotePlayerOnServer
		if (_isServer) {
			for (const auto& [peer, peerDesc] : _peerDesc) {
				if (peerDesc.Player == player) {
					MemoryStream packet(6);
					packet.WriteVariableUint32(player->_playerIndex);
					packet.WriteValue<std::uint8_t>((std::uint8_t)player->_currentWeapon);
					packet.WriteValue<std::uint8_t>((std::uint8_t)player->_weaponUpgrades[(std::uint8_t)player->_currentWeapon]);
					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerRefreshWeaponUpgrades, packet);
					break;
				}
			}
		}
	}

	void MultiLevelHandler::HandlePlayerEmitWeaponFlare(Actors::Player* player)
	{
		// TODO: Only called by RemotePlayerOnServer
		if (_isServer) {
			for (const auto& [peer, peerDesc] : _peerDesc) {
				if (peerDesc.Player == player) {
					MemoryStream packet(4);
					packet.WriteVariableUint32(player->_playerIndex);
					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerEmitWeaponFlare, packet);
					break;
				}
			}
		}
	}

	void MultiLevelHandler::HandlePlayerWeaponChanged(Actors::Player* player)
	{
		// TODO: Only called by RemotePlayerOnServer
		if (_isServer) {
			for (const auto & [peer, peerDesc] : _peerDesc) {
				if (peerDesc.Player == player) {
					MemoryStream packet(5);
					packet.WriteVariableUint32(player->_playerIndex);
					packet.WriteValue<std::uint8_t>((std::uint8_t)player->_currentWeapon);
					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerChangeWeapon, packet);
					break;
				}
			}
		}
	}

	void MultiLevelHandler::HandlePlayerWarped(Actors::Player* player, Vector2f prevPos, WarpFlags flags)
	{
		LevelHandler::HandlePlayerWarped(player, prevPos, flags);

		/*if (_isServer) {
			auto it = _playerStates.find(player->_playerIndex);
			if (it != _playerStates.end()) {
				it->second.Flags |= PlayerFlags::JustWarped;
			}
		} else {
			_seqNumWarped = _seqNum;
		}*/

		if (_isServer) {
			for (const auto& [peer, peerDesc] : _peerDesc) {
				if (peerDesc.Player == player) {
					MemoryStream packet(16);
					packet.WriteVariableUint32(player->_playerIndex);
					packet.WriteValue<std::int32_t>((std::int32_t)(player->_pos.X * 512.0f));
					packet.WriteValue<std::int32_t>((std::int32_t)(player->_pos.Y * 512.0f));
					packet.WriteValue<std::int16_t>((std::int16_t)(player->_speed.X * 512.0f));
					packet.WriteValue<std::int16_t>((std::int16_t)(player->_speed.Y * 512.0f));
					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerMoveInstantly, packet);
					break;
				}
			}
		}
	}

	void MultiLevelHandler::HandlePlayerCoins(Actors::Player* player, std::int32_t prevCount, std::int32_t newCount)
	{
		LevelHandler::HandlePlayerCoins(player, prevCount, newCount);

		if (_isServer) {
			for (const auto& [peer, peerDesc] : _peerDesc) {
				if (peerDesc.Player == player) {
					MemoryStream packet(8);
					packet.WriteVariableUint32(player->_playerIndex);
					packet.WriteVariableInt32(newCount);
					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerRefreshCoins, packet);
					break;
				}
			}
		}
	}

	void MultiLevelHandler::HandlePlayerGems(Actors::Player* player, std::uint8_t gemType, std::int32_t prevCount, std::int32_t newCount)
	{
		LevelHandler::HandlePlayerGems(player, gemType, prevCount, newCount);

		if (_isServer) {
			for (const auto& [peer, peerDesc] : _peerDesc) {
				if (peerDesc.Player == player) {
					MemoryStream packet(9);
					packet.WriteVariableUint32(player->_playerIndex);
					packet.WriteValue<std::uint8_t>(gemType);
					packet.WriteVariableInt32(newCount);
					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerRefreshGems, packet);
					break;
				}
			}
		}
	}

	void MultiLevelHandler::SetCheckpoint(Actors::Player* player, Vector2f pos)
	{
		LevelHandler::SetCheckpoint(player, pos);
	}

	void MultiLevelHandler::RollbackToCheckpoint(Actors::Player* player)
	{
		LevelHandler::RollbackToCheckpoint(player);
	}

	void MultiLevelHandler::HandleActivateSugarRush(Actors::Player* player)
	{
		LevelHandler::HandleActivateSugarRush(player);
	}

	void MultiLevelHandler::ShowLevelText(StringView text, Actors::ActorBase* initiator)
	{
		if (initiator == nullptr || IsLocalPlayer(initiator)) {
			// Pass through only messages for local players
			LevelHandler::ShowLevelText(text, initiator);
		}
	}

	StringView MultiLevelHandler::GetLevelText(std::uint32_t textId, std::int32_t index, std::uint32_t delimiter)
	{
		return LevelHandler::GetLevelText(textId, index, delimiter);
	}

	void MultiLevelHandler::OverrideLevelText(std::uint32_t textId, StringView value)
	{
		LevelHandler::OverrideLevelText(textId, value);

		if (_isServer) {
			std::uint32_t textLength = (std::uint32_t)value.size();

			MemoryStream packet(8 + textLength);
			packet.WriteVariableUint32(textId);
			packet.WriteVariableUint32(textLength);
			packet.Write(value.data(), textLength);

			_networkManager->SendTo([this](const Peer& peer) {
				auto it = _peerDesc.find(peer);
				return (it != _peerDesc.end() && it->second.State >= PeerState::LevelLoaded);
			}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::OverrideLevelText, packet);
		}
	}

	bool MultiLevelHandler::PlayerActionPressed(std::int32_t index, PlayerActions action, bool includeGamepads)
	{
		// TODO: Remove this override
		return LevelHandler::PlayerActionPressed(index, action, includeGamepads);
	}

	bool MultiLevelHandler::PlayerActionPressed(std::int32_t index, PlayerActions action, bool includeGamepads, bool& isGamepad)
	{
		if (index > 0) {
			auto it = _playerStates.find(index);
			if (it != _playerStates.end()) {
				if ((it->second.PressedKeys & (1ull << (std::int32_t)action)) != 0) {
					isGamepad = (it->second.PressedKeys & (1ull << (32 + (std::int32_t)action))) != 0;
					return true;
				}

				isGamepad = false;
				return false;
			}
		}

		return LevelHandler::PlayerActionPressed(index, action, includeGamepads, isGamepad);
	}

	bool MultiLevelHandler::PlayerActionHit(std::int32_t index, PlayerActions action, bool includeGamepads)
	{
		// TODO: Remove this override
		return LevelHandler::PlayerActionHit(index, action, includeGamepads);
	}

	bool MultiLevelHandler::PlayerActionHit(std::int32_t index, PlayerActions action, bool includeGamepads, bool& isGamepad)
	{
		if (index > 0) {
			auto it = _playerStates.find(index);
			if (it != _playerStates.end()) {
				if ((it->second.PressedKeys & (1ull << (std::int32_t)action)) != 0 && (it->second.PressedKeysLast & (1ull << (std::int32_t)action)) == 0) {
					isGamepad = (it->second.PressedKeys & (1ull << (32 + (std::int32_t)action))) != 0;
					return true;
				}

				isGamepad = false;
				return false;
			}
		}

		return LevelHandler::PlayerActionHit(index, action, includeGamepads, isGamepad);
	}

	float MultiLevelHandler::PlayerHorizontalMovement(std::int32_t index)
	{
		if (index > 0) {
			auto it = _playerStates.find(index);
			if (it != _playerStates.end()) {
				if ((it->second.PressedKeys & (1ull << (std::int32_t)PlayerActions::Left)) != 0) {
					return -1.0f;
				} else if ((it->second.PressedKeys & (1ull << (std::int32_t)PlayerActions::Right)) != 0) {
					return 1.0f;
				} else {
					return 0.0f;
				}
			}
		}

		return LevelHandler::PlayerHorizontalMovement(index);
	}

	float MultiLevelHandler::PlayerVerticalMovement(std::int32_t index)
	{
		if (index > 0) {
			auto it = _playerStates.find(index);
			if (it != _playerStates.end()) {
				if ((it->second.PressedKeys & (1ull << (std::int32_t)PlayerActions::Up)) != 0) {
					return -1.0f;
				} else if ((it->second.PressedKeys & (1ull << (std::int32_t)PlayerActions::Down)) != 0) {
					return 1.0f;
				} else {
					return 0.0f;
				}
			}
		}

		return LevelHandler::PlayerVerticalMovement(index);
	}

	bool MultiLevelHandler::SerializeResumableToStream(Stream& dest)
	{
		// Online multiplayer sessions cannot be resumed
		return false;
	}

	void MultiLevelHandler::OnAdvanceDestructibleTileAnimation(std::int32_t tx, std::int32_t ty, std::int32_t amount)
	{
		if (_isServer) {
			MemoryStream packet(12);
			packet.WriteVariableInt32(tx);
			packet.WriteVariableInt32(ty);
			packet.WriteVariableInt32(amount);

			_networkManager->SendTo([this](const Peer& peer) {
				auto it = _peerDesc.find(peer);
				return (it != _peerDesc.end() && it->second.State == PeerState::LevelSynchronized);
			}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::AdvanceTileAnimation, packet);
		}
	}

	void MultiLevelHandler::AttachComponents(LevelDescriptor&& descriptor)
	{
		LevelHandler::AttachComponents(std::move(descriptor));

		if (_isServer) {
			// Cache all possible multiplayer spawn points (if it's not coop level)
			_multiplayerSpawnPoints.clear();
			_eventMap->ForEachEvent(EventType::LevelStartMultiplayer, [this](const Events::EventMap::EventTile& event, std::int32_t x, std::int32_t y) {
				_multiplayerSpawnPoints.emplace_back(Vector2f(x * Tiles::TileSet::DefaultTileSize, y * Tiles::TileSet::DefaultTileSize - 8), event.EventParams[0]);
				return true;
			});
		} else {
			Vector2i size = _eventMap->GetSize();
			for (std::int32_t y = 0; y < size.Y; y++) {
				for (std::int32_t x = 0; x < size.X; x++) {
					std::uint8_t* eventParams;
					switch (_eventMap->GetEventByPosition(x, y, &eventParams)) {
						case EventType::WarpOrigin:
						case EventType::ModifierHurt:
						case EventType::ModifierDeath:
						case EventType::ModifierLimitCameraView:
						case EventType::AreaEndOfLevel:
						case EventType::AreaCallback:
						case EventType::AreaActivateBoss:
						case EventType::AreaFlyOff:
						case EventType::AreaRevertMorph:
						case EventType::AreaMorphToFrog:
						case EventType::AreaNoFire:
						case EventType::TriggerZone:
						case EventType::RollingRockTrigger:
							// These events are handled on server-side only
							_eventMap->StoreTileEvent(x, y, EventType::Empty);
							break;
					}
				}
			}	
		}
	}

	void MultiLevelHandler::SpawnPlayers(const LevelInitialization& levelInit)
	{
		if (!_isServer) {
			// Player spawning is delayed/controlled by server
			return;
		}

		for (std::int32_t i = 0; i < std::int32_t(arraySize(levelInit.PlayerCarryOvers)); i++) {
			if (levelInit.PlayerCarryOvers[i].Type == PlayerType::None) {
				continue;
			}

			Vector2f spawnPosition;
			if (!_multiplayerSpawnPoints.empty()) {
				// TODO: Select spawn according to team
				spawnPosition = _multiplayerSpawnPoints[Random().Next(0, _multiplayerSpawnPoints.size())].Pos;
			} else {
				spawnPosition = _eventMap->GetSpawnPosition(levelInit.PlayerCarryOvers[i].Type);
				if (spawnPosition.X < 0.0f && spawnPosition.Y < 0.0f) {
					spawnPosition = _eventMap->GetSpawnPosition(PlayerType::Jazz);
					if (spawnPosition.X < 0.0f && spawnPosition.Y < 0.0f) {
						continue;
					}
				}
			}

			std::shared_ptr<Actors::Multiplayer::LocalPlayerOnServer> player = std::make_shared<Actors::Multiplayer::LocalPlayerOnServer>();
			std::uint8_t playerParams[2] = { (std::uint8_t)levelInit.PlayerCarryOvers[i].Type, (std::uint8_t)i };
			player->OnActivated(Actors::ActorActivationDetails(
				this,
				Vector3i((std::int32_t)spawnPosition.X + (i * 30), (std::int32_t)spawnPosition.Y - (i * 30), PlayerZ - i),
				playerParams
			));

			Actors::Multiplayer::LocalPlayerOnServer* ptr = player.get();
			_players.push_back(ptr);
			AddActor(player);
			AssignViewport(ptr);

			ptr->ReceiveLevelCarryOver(levelInit.LastExitType, levelInit.PlayerCarryOvers[i]);
		}

		ApplyGameModeToAllPlayers(_gameMode);
	}

	MultiplayerGameMode MultiLevelHandler::GetGameMode() const
	{
		return _gameMode;
	}

	bool MultiLevelHandler::SetGameMode(MultiplayerGameMode value)
	{
		if (!_isServer) {
			return false;
		}

		_gameMode = value;

		ApplyGameModeToAllPlayers(_gameMode);

		// TODO: Send new teamId to each player
		// TODO: Reset level and broadcast it to players
		std::uint8_t packet[1] = { (std::uint8_t)_gameMode };
		_networkManager->SendTo([this](const Peer& peer) {
			auto it = _peerDesc.find(peer);
			return (it != _peerDesc.end() && it->second.State == PeerState::LevelSynchronized);
		}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::ChangeGameMode, packet);

		return true;
	}

	bool MultiLevelHandler::OnPeerDisconnected(const Peer& peer)
	{
		if (_isServer) {
			auto it = _peerDesc.find(peer);
			if (it != _peerDesc.end()) {
				Actors::Player* player = it->second.Player;
				std::int32_t playerIndex = player->_playerIndex;

				for (std::size_t i = 0; i < _players.size(); i++) {
					if (_players[i] == player) {
						_players.eraseUnordered(i);
						break;
					}
				}

				it->second.Player->SetState(Actors::ActorState::IsDestroyed, true);
				_peerDesc.erase(it);

				_playerStates.erase(playerIndex);

				MemoryStream packet(4);
				packet.WriteVariableUint32(playerIndex);

				_networkManager->SendTo([this, otherPeer = peer](const Peer& peer) {
					return (peer != otherPeer);
				}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::DestroyRemoteActor, packet);
			}
			return true;
		}

		return false;
	}

	bool MultiLevelHandler::OnPacketReceived(const Peer& peer, std::uint8_t channelId, std::uint8_t packetType, ArrayView<const std::uint8_t> data)
	{
		if (_ignorePackets) {
			// IRootController is probably going to load a new level in a moment, so ignore all packets now
			return false;
		}

		if (_isServer) {
			switch ((ClientPacketType)packetType) {
				case ClientPacketType::Auth: {
					std::uint8_t flags = 0;
					if (PreferencesCache::EnableReforgedGameplay) {
						flags |= 0x01;
					}

					MemoryStream packet(10 + _episodeName.size() + _levelFileName.size());
					packet.WriteValue<std::uint8_t>(flags);
					packet.WriteValue<std::uint8_t>((std::uint8_t)_gameMode);
					packet.WriteVariableUint32(_episodeName.size());
					packet.Write(_episodeName.data(), _episodeName.size());
					packet.WriteVariableUint32(_levelFileName.size());
					packet.Write(_levelFileName.data(), _levelFileName.size());

					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::LoadLevel, packet);
					return true;
				}
				case ClientPacketType::LevelReady: {
					LOGD("[MP] ClientPacketType::LevelReady - peer: 0x%p", peer._enet);

					_peerDesc[peer] = PeerDesc(nullptr, PeerState::LevelLoaded);
					return true;
				}
				case ClientPacketType::ChatMessage: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();

					auto it = _peerDesc.find(peer);
					auto it2 = _playerStates.find(playerIndex);
					if (it == _peerDesc.end() || it2 == _playerStates.end()) {
						LOGD("[MP] ClientPacketType::ChatMessage - invalid playerIndex (%u)", playerIndex);
						return true;
					}

					auto player = it->second.Player;
					if (playerIndex != player->_playerIndex) {
						LOGD("[MP] ClientPacketType::ChatMessage - received playerIndex %u instead of %i", playerIndex, player->_playerIndex);
						return true;
					}

					std::uint8_t reserved = packet.ReadValue<std::uint8_t>();

					std::uint32_t messageLength = packet.ReadVariableUint32();
					if (messageLength == 0 && messageLength > 1024) {
						LOGD("[MP] ClientPacketType::ChatMessage - length out of bounds (%u)", messageLength);
						return true;
					}

					String message{NoInit, messageLength};
					packet.Read(message.data(), messageLength);

					MemoryStream packetOut(9 + message.size());
					packetOut.WriteVariableUint32(playerIndex);
					packetOut.WriteValue<std::uint8_t>(0); // Reserved
					packetOut.WriteVariableUint32((std::uint32_t)message.size());
					packetOut.Write(message.data(), (std::uint32_t)message.size());

					_networkManager->SendTo([this, otherPeer = peer](const Peer& peer) {
						auto it = _peerDesc.find(peer);
						return (it != _peerDesc.end() && it->second.State != PeerState::Unknown && peer != otherPeer);
					}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::ChatMessage, packetOut);

					_root->InvokeAsync([this, peer, message = std::move(message)]() {
						_console->WriteLine(UI::MessageLevel::Info, message);
					}, NCINE_CURRENT_FUNCTION);
					return true;
				}
				case ClientPacketType::PlayerUpdate: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();

					auto it = _peerDesc.find(peer);
					auto it2 = _playerStates.find(playerIndex);
					if (it == _peerDesc.end() || it2 == _playerStates.end()) {
						LOGD("[MP] ClientPacketType::PlayerUpdate - invalid playerIndex (%u)", playerIndex);
						return true;
					}

					auto player = it->second.Player;
					if (playerIndex != player->_playerIndex) {
						LOGD("[MP] ClientPacketType::PlayerUpdate - received playerIndex %u instead of %i", playerIndex, player->_playerIndex);
						return true;
					}

					std::uint64_t now = packet.ReadVariableUint64();
					if (it->second.LastUpdated >= now) {
						return true;
					}

					float posX = packet.ReadValue<std::int32_t>() / 512.0f;
					float posY = packet.ReadValue<std::int32_t>() / 512.0f;
					float speedX = packet.ReadValue<std::int16_t>() / 512.0f;
					float speedY = packet.ReadValue<std::int16_t>() / 512.0f;
					PlayerFlags flags = (PlayerFlags)packet.ReadVariableUint32();

					/*bool justWarped = (flags & PlayerFlags::JustWarped) == PlayerFlags::JustWarped;
					if (justWarped) {
						std::uint64_t seqNumWarped = packet.ReadVariableUint64();
						if (seqNumWarped == it2->second.WarpSeqNum) {
							justWarped = false;
						} else {
							LOGD("Acknowledged player %i warp for sequence #%i", playerIndex, seqNumWarped);
							it2->second.WarpSeqNum = seqNumWarped;

							MemoryStream packet2(13);
							packet2.WriteValue<std::uint8_t>((std::uint8_t)ServerPacketType::PlayerAckWarped);
							packet2.WriteVariableUint32(playerIndex);
							packet2.WriteVariableUint64(seqNumWarped);

							_networkManager->SendTo(peer, NetworkChannel::Main, packet2);
						}
					}
		
					if ((it2->second.Flags & PlayerFlags::JustWarped) == PlayerFlags::JustWarped) {
						// Player already warped locally, mark the request as handled
						if (it2->second.WarpTimeLeft > 0.0f) {
							LOGD("Granted permission to player %i to warp asynchronously", playerIndex);
							it2->second.WarpTimeLeft = 0.0f;
						} else if (!justWarped) {
							LOGD("Granted permission to player %i to warp before client", playerIndex);
							it2->second.WarpTimeLeft = -90.0f;
						}
					} else if (justWarped) {
						// Player warped remotely but not locally, keep some time to resync with local state
						if (it2->second.WarpTimeLeft < 0.0f) {
							// Server is faster than client (probably due to higher latency)
							LOGD("Player already warped locally");
							it2->second.WarpTimeLeft = 0.0f;
						} else {
							// Client is faster than server
							LOGD("Player warped remotely (local permission pending)");
							it2->second.WarpTimeLeft = 90.0f;
						}
					} else if (it2->second.WarpTimeLeft <= 0.0f) {
						constexpr float MaxDeviation = 256.0f;

						float posDiffSqr = (Vector2f(posX, posY) - player->_pos).SqrLength();
						float speedSqr = std::max(player->_speed.SqrLength(), Vector2f(speedX, speedY).SqrLength());
						if (posDiffSqr > speedSqr + (MaxDeviation * MaxDeviation)) {
							LOGW("Player %i position mismatch by %i pixels (speed: %0.2f)", playerIndex, (std::int32_t)sqrt(posDiffSqr), sqrt(speedSqr));

							posX = player->_pos.X;
							posY = player->_pos.Y;

							MemoryStream packet2(13);
							packet2.WriteValue<std::uint8_t>((std::uint8_t)ServerPacketType::PlayerMoveInstantly);
							packet2.WriteVariableUint32(player->_playerIndex);
							packet2.WriteValue<std::int32_t>((std::int32_t)(posX * 512.0f));
							packet2.WriteValue<std::int32_t>((std::int32_t)(posY * 512.0f));
							packet2.WriteValue<std::int16_t>((std::int16_t)(player->_speed.X * 512.0f));
							packet2.WriteValue<std::int16_t>((std::int16_t)(player->_speed.Y * 512.0f));
							_networkManager->SendTo(peer, NetworkChannel::Main, packet2);
						}
					}*/

					// TODO: Special move

					it->second.LastUpdated = now;

					player->SyncWithServer(Vector2f(posX, posY), Vector2f(speedX, speedY),
						(flags & PlayerFlags::IsVisible) != PlayerFlags::None,
						(flags & PlayerFlags::IsFacingLeft) != PlayerFlags::None, 
						(flags & PlayerFlags::IsActivelyPushing) != PlayerFlags::None);
					return true;
				}
				case ClientPacketType::PlayerKeyPress: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();

					auto it = _playerStates.find(playerIndex);
					if (it == _playerStates.end()) {
						return true;
					}
					
					std::uint32_t frameCount = theApplication().GetFrameCount();
					if (it->second.UpdatedFrame != frameCount) {
						it->second.UpdatedFrame = frameCount;
						it->second.PressedKeysLast = it->second.PressedKeys;
					}
					it->second.PressedKeys = packet.ReadVariableUint64();

					//LOGD("Player %i pressed 0x%08x, last state was 0x%08x", playerIndex, it->second.PressedKeys & 0xffffffffu, prevState);
					return true;
				}
			}
		} else {
			switch ((ServerPacketType)packetType) {
				case ServerPacketType::LoadLevel: {
					// Start to ignore all incoming packets, because they no longer belong to this handler
					_ignorePackets = true;

					LOGD("[MP] ServerPacketType::LoadLevel");
					break;
				}
				case ServerPacketType::ChangeGameMode: {
					MemoryStream packet(data);
					MultiplayerGameMode gameMode = (MultiplayerGameMode)packet.ReadValue<std::uint8_t>();

					LOGD("[MP] ServerPacketType::ChangeGameMode - mode: %u", gameMode);

					_gameMode = gameMode;
					break;
				}
				case ServerPacketType::PlaySfx: {
					MemoryStream packet(data);
					std::uint32_t actorId = packet.ReadVariableUint32();
					float gain = halfToFloat(packet.ReadValue<std::uint16_t>());
					float pitch = halfToFloat(packet.ReadValue<std::uint16_t>());
					std::uint32_t identifierLength = packet.ReadVariableUint32();
					String identifier = String(NoInit, identifierLength);
					packet.Read(identifier.data(), identifierLength);

					// TODO: Use only lock here
					_root->InvokeAsync([this, actorId, gain, pitch, identifier = std::move(identifier)]() {
						auto it = _remoteActors.find(actorId);
						if (it != _remoteActors.end()) {
							// TODO: gain, pitch, ...
							it->second->PlaySfx(identifier, gain, pitch);
						}
					}, NCINE_CURRENT_FUNCTION);
					break;
				}
				case ServerPacketType::PlayCommonSfx: {
					MemoryStream packet(data);
					std::int32_t posX = packet.ReadVariableInt32();
					std::int32_t posY = packet.ReadVariableInt32();
					float gain = halfToFloat(packet.ReadValue<std::uint16_t>());
					float pitch = halfToFloat(packet.ReadValue<std::uint16_t>());
					std::uint32_t identifierLength = packet.ReadVariableUint32();
					String identifier(NoInit, identifierLength);
					packet.Read(identifier.data(), identifierLength);

					// TODO: Use only lock here
					_root->InvokeAsync([this, posX, posY, gain, pitch, identifier = std::move(identifier)]() {
						PlayCommonSfx(identifier, Vector3f((float)posX, (float)posY, 0.0f), gain, pitch);
					}, NCINE_CURRENT_FUNCTION);
					break;
				}
				case ServerPacketType::ShowAlert: {
					MemoryStream packet(data);
					std::uint32_t messageLength = packet.ReadVariableUint32();
					String message = String(NoInit, messageLength);
					packet.Read(message.data(), messageLength);

					LOGD("[MP] ServerPacketType::ShowAlert - text: \"%s\"", message.data());

					_hud->ShowLevelText(message);
					break;
				}
				case ServerPacketType::ChatMessage: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					std::uint8_t reserved = packet.ReadValue<std::uint8_t>();

					std::uint32_t messageLength = packet.ReadVariableUint32();
					if (messageLength == 0 && messageLength > 1024) {
						LOGD("[MP] ServerPacketType::ChatMessage - length out of bounds (%u)", messageLength);
						return true;
					}

					String message{NoInit, messageLength};
					packet.Read(message.data(), messageLength);

					_root->InvokeAsync([this, message = std::move(message)]() mutable {
						_console->WriteLine(UI::MessageLevel::Info, std::move(message));
					}, NCINE_CURRENT_FUNCTION);
					break;
				}
				case ServerPacketType::CreateControllablePlayer: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					PlayerType playerType = (PlayerType)packet.ReadValue<std::uint8_t>();
					std::uint8_t health = packet.ReadValue<std::uint8_t>();
					std::uint8_t flags = packet.ReadValue<std::uint8_t>();
					std::uint8_t teamId = packet.ReadValue<std::uint8_t>();
					std::int32_t posX = packet.ReadVariableInt32();
					std::int32_t posY = packet.ReadVariableInt32();

					LOGD("[MP] ServerPacketType::CreateControllablePlayer - playerIndex: %u, playerType: %u, health: %u, flags: %u, team: %u, x: %i, y: %i",
						playerIndex, playerType, health, flags, teamId, posX, posY);

					_lastSpawnedActorId = playerIndex;

					_root->InvokeAsync([this, playerType, health, teamId, posX, posY]() {
						std::shared_ptr<Actors::Multiplayer::RemotablePlayer> player = std::make_shared<Actors::Multiplayer::RemotablePlayer>();
						std::uint8_t playerParams[2] = { (std::uint8_t)playerType, 0 };
						player->OnActivated(Actors::ActorActivationDetails(
							this,
							Vector3i(posX, posY, PlayerZ),
							playerParams
						));
						player->SetTeamId(teamId);
						player->SetHealth(health);

						Actors::Multiplayer::RemotablePlayer* ptr = player.get();
						_players.push_back(ptr);
						AddActor(player);
						AssignViewport(ptr);
						// TODO: Needed to initialize newly assigned viewport, because it called asynchronously, not from handler initialization
						Vector2i res = theApplication().GetResolution();
						OnInitializeViewport(res.X, res.Y);
					}, NCINE_CURRENT_FUNCTION);
					return true;
				}
				case ServerPacketType::CreateRemoteActor: {
					MemoryStream packet(data);
					std::uint32_t actorId = packet.ReadVariableUint32();
					std::int32_t posX = packet.ReadVariableInt32();
					std::int32_t posY = packet.ReadVariableInt32();
					std::int32_t posZ = packet.ReadVariableInt32();
					Actors::ActorState state = (Actors::ActorState)packet.ReadVariableUint32();
					std::uint32_t metadataLength = packet.ReadVariableUint32();
					String metadataPath = String(NoInit, metadataLength);
					packet.Read(metadataPath.data(), metadataLength);
					std::uint32_t anim = packet.ReadVariableUint32();

					//LOGD("Remote actor %u created on [%i;%i] with metadata \"%s\"", actorId, posX, posY, metadataPath.data());
					LOGD("[MP] ServerPacketType::CreateRemoteActor - actorId: %u, metadata: \"%s\", x: %i, y: %i", actorId, metadataPath.data(), posX, posY);

					_root->InvokeAsync([this, actorId, posX, posY, posZ, state, metadataPath = std::move(metadataPath), anim]() {
						std::shared_ptr<Actors::Multiplayer::RemoteActor> remoteActor = std::make_shared<Actors::Multiplayer::RemoteActor>();
						remoteActor->OnActivated(Actors::ActorActivationDetails(this, Vector3i(posX, posY, posZ)));
						remoteActor->AssignMetadata(metadataPath, (AnimState)anim, state);

						_remoteActors[actorId] = remoteActor;
						AddActor(std::static_pointer_cast<Actors::ActorBase>(remoteActor));
					}, NCINE_CURRENT_FUNCTION);
					return true;
				}
				case ServerPacketType::CreateMirroredActor: {
					MemoryStream packet(data);
					std::uint32_t actorId = packet.ReadVariableUint32();
					EventType eventType = (EventType)packet.ReadVariableUint32();
					StaticArray<Events::EventSpawner::SpawnParamsSize, std::uint8_t> eventParams(NoInit);
					packet.Read(eventParams, Events::EventSpawner::SpawnParamsSize);
					Actors::ActorState actorFlags = (Actors::ActorState)packet.ReadVariableUint32();
					std::int32_t tileX = packet.ReadVariableInt32();
					std::int32_t tileY = packet.ReadVariableInt32();
					std::int32_t posZ = packet.ReadVariableInt32();

					//LOGD("Mirrored actor %u created on [%i;%i] with event %u", actorId, tileX * 32 + 16, tileY * 32 + 16, (std::uint32_t)eventType);
					LOGD("[MP] ServerPacketType::CreateMirroredActor - actorId: %u, event: %u, x: %i, y: %i", actorId, (std::uint32_t)eventType, tileX * 32 + 16, tileY * 32 + 16);

					_root->InvokeAsync([this, actorId, eventType, eventParams = std::move(eventParams), actorFlags, tileX, tileY, posZ]() {
						// TODO: Remove const_cast
						std::shared_ptr<Actors::ActorBase> actor =_eventSpawner.SpawnEvent(eventType, const_cast<std::uint8_t*>(eventParams.data()), actorFlags, tileX, tileY, ILevelHandler::SpritePlaneZ);
						if (actor != nullptr) {
							_remoteActors[actorId] = actor;
							AddActor(actor);
						} else {
							LOGD("[MP] ServerPacketType::CreateMirroredActor - CANNOT CREATE - actorId: %u", actorId);
						}
					}, NCINE_CURRENT_FUNCTION);
					return true;
				}
				case ServerPacketType::DestroyRemoteActor: {
					MemoryStream packet(data);
					std::uint32_t actorId = packet.ReadVariableUint32();

					//LOGD("Remote actor %u destroyed", actorId);
					LOGD("[MP] ServerPacketType::DestroyRemoteActor - actorId: %u", actorId);

					_root->InvokeAsync([this, actorId]() {
						auto it = _remoteActors.find(actorId);
						if (it != _remoteActors.end()) {
							it->second->SetState(Actors::ActorState::IsDestroyed, true);
							_remoteActors.erase(it);
						} else {
							LOGD("[MP] ServerPacketType::DestroyRemoteActor - NOT FOUND - actorId: %u", actorId);
						}
					}, NCINE_CURRENT_FUNCTION);
					return true;
				}
				case ServerPacketType::UpdateAllActors: {
					MemoryStream packetCompressed(data);
					DeflateStream packet(packetCompressed);
					std::uint32_t actorCount = packet.ReadVariableUint32();
					for (std::uint32_t i = 0; i < actorCount; i++) {
						std::uint32_t index = packet.ReadVariableUint32();
						float posX = packet.ReadValue<std::int32_t>() / 512.0f;
						float posY = packet.ReadValue<std::int32_t>() / 512.0f;
						std::uint32_t anim = packet.ReadVariableUint32();
						float rotation = packet.ReadValue<std::uint8_t>() * fRadAngle360 / 255.0f;
						std::uint8_t flags = packet.ReadValue<std::uint8_t>();
						Actors::ActorRendererType rendererType = (Actors::ActorRendererType)packet.ReadValue<std::uint8_t>();

						auto it = _remoteActors.find(index);
						if (it != _remoteActors.end()) {
							if (auto* remoteActor = runtime_cast<Actors::Multiplayer::RemoteActor*>(it->second)) {
								remoteActor->SyncWithServer(Vector2f(posX, posY), (AnimState)anim, rotation,
									(flags & 0x02) != 0, (flags & 0x01) != 0, (flags & 0x04) != 0, rendererType);
							}
						}
					}
					return true;
				}
				case ServerPacketType::SyncTileMap: {
					MemoryStream packet(data);

					LOGD("[MP] ServerPacketType::SyncTileMap");

					// TODO: No lock here ???
					TileMap()->InitializeFromStream(packet);
					return true;
				}
				case ServerPacketType::SetTrigger: {
					MemoryStream packet(data);
					std::uint8_t triggerId = packet.ReadValue<std::uint8_t>();
					bool newState = (bool)packet.ReadValue<std::uint8_t>();

					LOGD("[MP] ServerPacketType::SetTrigger - id: %u, state: %u", triggerId, newState);

					_root->InvokeAsync([this, triggerId, newState]() {
						TileMap()->SetTrigger(triggerId, newState);
					}, NCINE_CURRENT_FUNCTION);
					return true;
				}
				case ServerPacketType::AdvanceTileAnimation: {
					MemoryStream packet(data);
					std::int32_t tx = packet.ReadVariableInt32();
					std::int32_t ty = packet.ReadVariableInt32();
					std::int32_t amount = packet.ReadVariableInt32();

					LOGD("[MP] ServerPacketType::AdvanceTileAnimation - tx: %i, ty: %i, amount: %i", tx, ty, amount);

					_root->InvokeAsync([this, tx, ty, amount]() {
						TileMap()->AdvanceDestructibleTileAnimation(tx, ty, amount);
					}, NCINE_CURRENT_FUNCTION);
					return true;
				}
				case ServerPacketType::PlayerRespawn: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerRespawn - received playerIndex %u instead of %u", playerIndex, _lastSpawnedActorId);
						return true;
					}

					LOGD("[MP] ServerPacketType::PlayerRespawn - playerIndex: %u", playerIndex);

					float posX = packet.ReadValue<std::int32_t>() / 512.0f;
					float posY = packet.ReadValue<std::int32_t>() / 512.0f;
					_players[0]->Respawn(Vector2f(posX, posY));
					return true;
				}
				case ServerPacketType::PlayerMoveInstantly: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						return true;
					}

					float posX = packet.ReadValue<std::int32_t>() / 512.0f;
					float posY = packet.ReadValue<std::int32_t>() / 512.0f;
					float speedX = packet.ReadValue<std::int16_t>() / 512.0f;
					float speedY = packet.ReadValue<std::int16_t>() / 512.0f;

					LOGD("[MP] ServerPacketType::PlayerMoveInstantly - playerIndex: %u, x: %f, y: %f, sx: %f, sy: %f",
						playerIndex, posX, posY, speedX, speedY);

					_root->InvokeAsync([this, posX, posY, speedX, speedY]() {
						static_cast<Actors::Multiplayer::RemotablePlayer*>(_players[0])->MoveRemotely(Vector2f(posX, posY), Vector2f(speedX, speedY));
					}, NCINE_CURRENT_FUNCTION);
					return true;
				}
				case ServerPacketType::PlayerAckWarped: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					std::uint64_t seqNum = packet.ReadVariableUint64();

					LOGD("[MP] ServerPacketType::PlayerAckWarped - playerIndex: %u, seqNum: %llu", playerIndex, seqNum);

					if (_lastSpawnedActorId == playerIndex && _seqNumWarped == seqNum) {
						_seqNumWarped = 0;
					}
					return true;
				}
				case ServerPacketType::PlayerEmitWeaponFlare: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerEmitWeaponFlare - received playerIndex %u instead of %u", playerIndex, _lastSpawnedActorId);
						return true;
					}

					LOGD("[MP] ServerPacketType::PlayerEmitWeaponFlare - playerIndex: %u", playerIndex);

					_players[0]->EmitWeaponFlare();
					return true;
				}
				case ServerPacketType::PlayerChangeWeapon: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerChangeWeapon - received playerIndex %u instead of %u", playerIndex, _lastSpawnedActorId);
						return true;
					}

					std::uint8_t weaponType = packet.ReadValue<std::uint8_t>();

					LOGD("[MP] ServerPacketType::PlayerChangeWeapon - playerIndex: %u, weaponType: %u", playerIndex, weaponType);

					_players[0]->SetCurrentWeapon((WeaponType)weaponType);
					return true;
				}
				case ServerPacketType::PlayerRefreshAmmo: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerRefreshAmmo - received playerIndex %u instead of %u", playerIndex, _lastSpawnedActorId);
						return true;
					}

					std::uint8_t weaponType = packet.ReadValue<std::uint8_t>();
					std::uint16_t weaponAmmo = packet.ReadValue<std::uint16_t>();

					LOGD("[MP] ServerPacketType::PlayerRefreshAmmo - playerIndex: %u, weaponType: %u, weaponAmmo: %u", playerIndex, weaponType, weaponAmmo);

					_players[0]->_weaponAmmo[weaponType] = weaponAmmo;
					return true;
				}
				case ServerPacketType::PlayerRefreshWeaponUpgrades: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerRefreshWeaponUpgrades - received playerIndex %u instead of %u", playerIndex, _lastSpawnedActorId);
						return true;
					}

					std::uint8_t weaponType = packet.ReadValue<std::uint8_t>();
					std::uint8_t weaponUpgrades = packet.ReadValue<std::uint8_t>();

					LOGD("[MP] ServerPacketType::PlayerRefreshWeaponUpgrades - playerIndex: %u, weaponType: %u, weaponUpgrades: %u", playerIndex, weaponType, weaponUpgrades);

					_players[0]->_weaponUpgrades[weaponType] = weaponUpgrades;
					return true;
				}
				case ServerPacketType::PlayerRefreshCoins: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerRefreshCoins - received playerIndex %u instead of %u", playerIndex, _lastSpawnedActorId);
						return true;
					}

					std::int32_t newCount = packet.ReadVariableInt32();

					LOGD("[MP] ServerPacketType::PlayerRefreshCoins - playerIndex: %u, newCount: %i", playerIndex, newCount);

					_players[0]->_coins = newCount;
					_hud->ShowCoins(newCount);
					return true;
				}
				case ServerPacketType::PlayerRefreshGems: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerRefreshGems - received playerIndex %u instead of %u", playerIndex, _lastSpawnedActorId);
						return true;
					}

					std::uint8_t gemType = packet.ReadValue<std::uint8_t>();
					std::int32_t newCount = packet.ReadVariableInt32();

					LOGD("[MP] ServerPacketType::PlayerRefreshGems - playerIndex: %u, gemType: %u, newCount: %i", playerIndex, gemType, newCount);

					if (gemType < arraySize(_players[0]->_gems)) {
						_players[0]->_gems[gemType] = newCount;
						_hud->ShowGems(gemType, newCount);
					}
					return true;
				}
				case ServerPacketType::PlayerTakeDamage: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerTakeDamage - received playerIndex %u instead of %u", playerIndex, _lastSpawnedActorId);
						return true;
					}

					std::int32_t health = packet.ReadVariableInt32();
					float pushForce = packet.ReadValue<std::int16_t>() / 512.0f;

					LOGD("[MP] ServerPacketType::PlayerTakeDamage - playerIndex: %u, health: %i, pushForce: %f", playerIndex, health, pushForce);

					_root->InvokeAsync([this, health, pushForce]() {
						_players[0]->TakeDamage(_players[0]->_health - health, pushForce);
					}, NCINE_CURRENT_FUNCTION);
					return true;
				}
				case ServerPacketType::PlayerActivateSpring: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerActivateSpring - received playerIndex %u instead of %u", playerIndex, _lastSpawnedActorId);
						return true;
					}

					float posX = packet.ReadValue<std::int32_t>() / 512.0f;
					float posY = packet.ReadValue<std::int32_t>() / 512.0f;
					float forceX = packet.ReadValue<std::int16_t>() / 512.0f;
					float forceY = packet.ReadValue<std::int16_t>() / 512.0f;
					std::uint8_t flags = packet.ReadValue<std::uint8_t>();
					_root->InvokeAsync([this, posX, posY, forceX, forceY, flags]() {
						bool removeSpecialMove = false;
						_players[0]->OnHitSpring(Vector2f(posX, posY), Vector2f(forceX, forceY), (flags & 0x01) == 0x01, (flags & 0x02) == 0x02, removeSpecialMove);
						if (removeSpecialMove) {
							_players[0]->_controllable = true;
							_players[0]->EndDamagingMove();
						}
					}, NCINE_CURRENT_FUNCTION);
					return true;
				}
				case ServerPacketType::PlayerWarpIn: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerWarpIn - received playerIndex %u instead of %u", playerIndex, _lastSpawnedActorId);
						return true;
					}

					ExitType exitType = (ExitType)packet.ReadValue<std::uint8_t>();
					LOGD("[MP] ServerPacketType::PlayerWarpIn - playerIndex: %u, exitType: 0x%02x", playerIndex, exitType);

					_root->InvokeAsync([this, exitType]() {
						static_cast<Actors::Multiplayer::RemotablePlayer*>(_players[0])->WarpIn(exitType);
					}, NCINE_CURRENT_FUNCTION);
					return true;
				}
			}
		}

		return false;
	}

	void MultiLevelHandler::LimitCameraView(Actors::Player* player, std::int32_t left, std::int32_t width)
	{
		// TODO: This should probably be client local
		LevelHandler::LimitCameraView(player, left, width);
	}

	void MultiLevelHandler::ShakeCameraView(Actors::Player* player, float duration)
	{
		// TODO: This should probably be client local
		LevelHandler::ShakeCameraView(player, duration);
	}
	
	void MultiLevelHandler::ShakeCameraViewNear(Vector2f pos, float duration)
	{
		// TODO: This should probably be client local
		LevelHandler::ShakeCameraViewNear(pos, duration);
	}

	void MultiLevelHandler::SetTrigger(std::uint8_t triggerId, bool newState)
	{
		LevelHandler::SetTrigger(triggerId, newState);

		if (_isServer) {
			MemoryStream packet(2);
			packet.WriteValue<std::uint8_t>(triggerId);
			packet.WriteValue<std::uint8_t>(newState);

			_networkManager->SendTo([this](const Peer& peer) {
				auto it = _peerDesc.find(peer);
				return (it != _peerDesc.end() && it->second.State == PeerState::LevelSynchronized);
			}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::SetTrigger, packet);
		}
	}

	void MultiLevelHandler::SetWeather(WeatherType type, std::uint8_t intensity)
	{
		// TODO: This should probably be client local
		LevelHandler::SetWeather(type, intensity);
	}

	bool MultiLevelHandler::BeginPlayMusic(StringView path, bool setDefault, bool forceReload)
	{
		// TODO: This should probably be client local
		return LevelHandler::BeginPlayMusic(path, setDefault, forceReload);
	}

	void MultiLevelHandler::BeforeActorDestroyed(Actors::ActorBase* actor)
	{
		if (!_isServer) {
			return;
		}

		auto it = _remotingActors.find(actor);
		if (it == _remotingActors.end()) {
			return;
		}

		std::uint32_t actorId = it->second;

		MemoryStream packet(4);
		packet.WriteVariableUint32(actorId);

		_networkManager->SendTo([this](const Peer& peer) {
			auto it = _peerDesc.find(peer);
			return (it != _peerDesc.end() && it->second.State == PeerState::LevelSynchronized);
		}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::DestroyRemoteActor, packet);

		_remotingActors.erase(it);
		_remoteActors.erase(actorId);
	}

	void MultiLevelHandler::ProcessEvents(float timeMult)
	{
		// Process events only by server
		if (_isServer) {
			LevelHandler::ProcessEvents(timeMult);
		}
	}

	void MultiLevelHandler::PrepareNextLevelInitialization(LevelInitialization& levelInit)
	{
		LevelHandler::PrepareNextLevelInitialization(levelInit);

		// Initialize only local players
		for (std::int32_t i = 1; i < std::int32_t(arraySize(levelInit.PlayerCarryOvers)); i++) {
			levelInit.PlayerCarryOvers[i].Type = PlayerType::None;
		}
	}

	void MultiLevelHandler::SynchronizePeers()
	{
		for (auto& [peer, peerDesc] : _peerDesc) {
			if (peerDesc.State != PeerState::LevelLoaded) {
				continue;
			}

			Vector2f spawnPosition;
			if (!_multiplayerSpawnPoints.empty()) {
				// TODO: Select spawn according to team
				spawnPosition = _multiplayerSpawnPoints[Random().Next(0, _multiplayerSpawnPoints.size())].Pos;
			} else {
				// TODO: Spawn on the last checkpoint
				spawnPosition = EventMap()->GetSpawnPosition(PlayerType::Jazz);
				if (spawnPosition.X < 0.0f && spawnPosition.Y < 0.0f) {
					continue;
				}
			}

			std::uint8_t playerIndex = FindFreePlayerId();
			LOGD("[MP] Syncing player %u", playerIndex);

			std::shared_ptr<Actors::Multiplayer::RemotePlayerOnServer> player = std::make_shared<Actors::Multiplayer::RemotePlayerOnServer>();
			std::uint8_t playerParams[2] = { (std::uint8_t)PlayerType::Spaz, (std::uint8_t)playerIndex };
			player->OnActivated(Actors::ActorActivationDetails(
				this,
				Vector3i((std::int32_t)spawnPosition.X, (std::int32_t)spawnPosition.Y, PlayerZ - playerIndex),
				playerParams
			));

			Actors::Multiplayer::RemotePlayerOnServer* ptr = player.get();
			_players.push_back(ptr);

			peerDesc.Player = ptr;
			peerDesc.State = PeerState::LevelSynchronized;

			_playerStates[playerIndex] = PlayerState(player->_pos, player->_speed);

			_suppressRemoting = true;
			AddActor(player);
			_suppressRemoting = false;

			ApplyGameModeToPlayer(_gameMode, ptr);

			// Synchronize tilemap
			{
				// TODO: Use deflate compression here?
				MemoryStream packet(20 * 1024);
				_tileMap->SerializeResumableToStream(packet);
				_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::SyncTileMap, packet);
			}

			// Spawn the player also on the remote side
			{
				std::uint8_t flags = 0;
				if (player->_controllable) {
					flags |= 0x01;
				}

				MemoryStream packet(16);
				packet.WriteVariableUint32(playerIndex);
				packet.WriteValue<std::uint8_t>((std::uint8_t)player->_playerType);
				packet.WriteValue<std::uint8_t>((std::uint8_t)player->_health);
				packet.WriteValue<std::uint8_t>(flags);
				packet.WriteValue<std::uint8_t>(player->GetTeamId());
				packet.WriteVariableInt32((std::int32_t)player->_pos.X);
				packet.WriteVariableInt32((std::int32_t)player->_pos.Y);

				_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::CreateControllablePlayer, packet);
			}

			for (Actors::Player* otherPlayer : _players) {
				if (otherPlayer == ptr) {
					continue;
				}

				const auto& metadataPath = otherPlayer->_metadata->Path;

				MemoryStream packet(28 + metadataPath.size());
				packet.WriteVariableUint32(otherPlayer->_playerIndex);
				packet.WriteVariableInt32((std::int32_t)otherPlayer->_pos.X);
				packet.WriteVariableInt32((std::int32_t)otherPlayer->_pos.Y);
				packet.WriteVariableInt32((std::int32_t)otherPlayer->_renderer.layer());
				packet.WriteVariableUint32((std::uint32_t)otherPlayer->_state);
				packet.WriteVariableUint32((std::uint32_t)metadataPath.size());
				packet.Write(metadataPath.data(), (std::uint32_t)metadataPath.size());
				packet.WriteVariableUint32((std::uint32_t)(otherPlayer->_currentTransition != nullptr ? otherPlayer->_currentTransition->State : otherPlayer->_currentAnimation->State));

				_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::CreateRemoteActor, packet);
			}

			for (const auto& [remotingActor, remotingActorId] : _remotingActors) {
				if (ActorShouldBeMirrored(remotingActor)) {
					Vector2i originTile = remotingActor->_originTile;
					const auto& eventTile = _eventMap->GetEventTile(originTile.X, originTile.Y);
					if (eventTile.Event != EventType::Empty) {
						for (const auto& [peer, peerDesc] : _peerDesc) {
							MemoryStream packet(24 + Events::EventSpawner::SpawnParamsSize);
							packet.WriteVariableUint32(remotingActorId);
							packet.WriteVariableUint32((std::uint32_t)eventTile.Event);
							packet.Write(eventTile.EventParams, Events::EventSpawner::SpawnParamsSize);
							packet.WriteVariableUint32((std::uint32_t)eventTile.EventFlags);
							packet.WriteVariableInt32((std::int32_t)originTile.X);
							packet.WriteVariableInt32((std::int32_t)originTile.Y);
							packet.WriteVariableInt32((std::int32_t)remotingActor->_renderer.layer());

							_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::CreateMirroredActor, packet);
						}
					}
				} else {
					const auto& metadataPath = remotingActor->_metadata->Path;

					MemoryStream packet(28 + metadataPath.size());
					packet.WriteVariableUint32(remotingActorId);
					packet.WriteVariableInt32((std::int32_t)remotingActor->_pos.X);
					packet.WriteVariableInt32((std::int32_t)remotingActor->_pos.Y);
					packet.WriteVariableInt32((std::int32_t)remotingActor->_renderer.layer());
					packet.WriteVariableUint32((std::uint32_t)remotingActor->_state);
					packet.WriteVariableUint32((std::uint32_t)metadataPath.size());
					packet.Write(metadataPath.data(), (std::uint32_t)metadataPath.size());
					packet.WriteVariableUint32((std::uint32_t)(remotingActor->_currentTransition != nullptr ? remotingActor->_currentTransition->State : remotingActor->_currentAnimation->State));

					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::CreateRemoteActor, packet);
				}
			}

			for (const auto& [otherPeer, otherPeerDesc] : _peerDesc) {
				if (otherPeer == peer || otherPeerDesc.State != PeerState::LevelSynchronized) {
					continue;
				}

				const auto& metadataPath = player->_metadata->Path;

				MemoryStream packet(28 + metadataPath.size());
				packet.WriteVariableUint32(playerIndex);
				packet.WriteVariableInt32((std::int32_t)player->_pos.X);
				packet.WriteVariableInt32((std::int32_t)player->_pos.Y);
				packet.WriteVariableInt32((std::int32_t)player->_renderer.layer());
				packet.WriteVariableUint32((std::uint32_t)player->_state);
				packet.WriteVariableUint32((std::uint32_t)metadataPath.size());
				packet.Write(metadataPath.data(), (std::uint32_t)metadataPath.size());
				packet.WriteVariableUint32((std::uint32_t)(player->_currentTransition != nullptr ? player->_currentTransition->State : player->_currentAnimation->State));

				_networkManager->SendTo(otherPeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::CreateRemoteActor, packet);
			}
		}
	}

	std::uint32_t MultiLevelHandler::FindFreeActorId()
	{
		for (std::uint32_t i = UINT8_MAX + 1; i < UINT32_MAX - 1; i++) {
			if (!_remoteActors.contains(i)) {
				return i;
			}
		}

		return UINT32_MAX;
	}

	std::uint8_t MultiLevelHandler::FindFreePlayerId()
	{
		std::size_t count = _players.size();
		for (std::uint8_t i = 0; i < UINT8_MAX - 1; i++) {
			bool found = false;
			for (std::size_t j = 0; j < count; j++) {
				if (_players[j]->_playerIndex == i) {
					found = true;
					break;
				}
			}

			if (!found) {
				return i;
			}
		}

		return UINT8_MAX;
	}

	bool MultiLevelHandler::IsLocalPlayer(Actors::ActorBase* actor)
	{
		return (runtime_cast<Actors::Multiplayer::LocalPlayerOnServer*>(actor) ||
				runtime_cast<Actors::Multiplayer::RemotablePlayer*>(actor));
	}

	void MultiLevelHandler::ApplyGameModeToAllPlayers(MultiplayerGameMode gameMode)
	{
		if (gameMode == MultiplayerGameMode::Cooperation) {
			// Everyone in single team
			for (auto* player : _players) {
				static_cast<Actors::Multiplayer::PlayerOnServer*>(player)->SetTeamId(0);
			}
		} else if (gameMode == MultiplayerGameMode::TeamBattle ||
				   gameMode == MultiplayerGameMode::CaptureTheFlag ||
				   gameMode == MultiplayerGameMode::TeamRace) {
			// Create two teams
			std::int32_t playerCount = (std::int32_t)_players.size();
			std::int32_t splitIdx = (playerCount + 1) / 2;
			SmallVector<std::int32_t, 0> teamIds(playerCount);
			for (std::int32_t i = 0; i < playerCount; i++) {
				teamIds.push_back(i);
			}
			Random().Shuffle(arrayView(teamIds));

			for (std::int32_t i = 0; i < playerCount; i++) {
				std::uint8_t teamId = (teamIds[i] < splitIdx ? 0 : 1);
				static_cast<Actors::Multiplayer::PlayerOnServer*>(_players[i])->SetTeamId(teamId);
			}
		} else {
			// Each player is in their own team
			std::int32_t playerCount = (std::int32_t)_players.size();
			for (std::int32_t i = 0; i < playerCount; i++) {
				std::int32_t playerIdx = _players[i]->_playerIndex;
				DEATH_DEBUG_ASSERT(0 <= playerIdx && playerIdx < UINT8_MAX);
				static_cast<Actors::Multiplayer::PlayerOnServer*>(_players[i])->SetTeamId((std::uint8_t)playerIdx);
			}
		}
	}

	void MultiLevelHandler::ApplyGameModeToPlayer(MultiplayerGameMode gameMode, Actors::Player* player)
	{
		if (gameMode == MultiplayerGameMode::Cooperation) {
			// Everyone in single team
			static_cast<Actors::Multiplayer::PlayerOnServer*>(player)->SetTeamId(0);
		} else if (gameMode == MultiplayerGameMode::TeamBattle ||
				   gameMode == MultiplayerGameMode::CaptureTheFlag ||
				   gameMode == MultiplayerGameMode::TeamRace) {
			// Create two teams
			std::int32_t playerCountsInTeam[2] = {};
			std::int32_t playerCount = (std::int32_t)_players.size();
			for (std::int32_t i = 0; i < playerCount; i++) {
				if (_players[i] != player) {
					std::uint8_t teamId = static_cast<Actors::Multiplayer::PlayerOnServer*>(_players[i])->GetTeamId();
					if (teamId < arraySize(playerCountsInTeam)) {
						playerCountsInTeam[teamId]++;
					}
				}
			}
			std::uint8_t teamId = (playerCountsInTeam[0] < playerCountsInTeam[1] ? 0
				: (playerCountsInTeam[0] > playerCountsInTeam[1] ? 1
					: Random().Next(0, 2)));

			static_cast<Actors::Multiplayer::PlayerOnServer*>(player)->SetTeamId(teamId);
		} else {
			// Each player is in their own team
			std::int32_t playerIdx = player->_playerIndex;
			DEATH_DEBUG_ASSERT(0 <= playerIdx && playerIdx < UINT8_MAX);
			static_cast<Actors::Multiplayer::PlayerOnServer*>(player)->SetTeamId((std::uint8_t)playerIdx);
		}
	}

	bool MultiLevelHandler::ActorShouldBeMirrored(Actors::ActorBase* actor)
	{
		// If actor has no animation, it's probably some special object (usually lights and ambient sounds)
		if (actor->_currentAnimation == nullptr && actor->_currentTransition == nullptr) {
			return true;
		}

		// List of objects that needs to be recreated on client-side instead of remoting
		return (runtime_cast<Actors::Environment::AirboardGenerator*>(actor) || runtime_cast<Actors::Environment::SteamNote*>(actor) ||
				runtime_cast<Actors::Environment::SwingingVine*>(actor) || runtime_cast<Actors::Solid::Bridge*>(actor) ||
				runtime_cast<Actors::Solid::MovingPlatform*>(actor) || runtime_cast<Actors::Solid::PinballBumper*>(actor) ||
				runtime_cast<Actors::Solid::PinballPaddle*>(actor) || runtime_cast<Actors::Solid::SpikeBall*>(actor));
	}

	StringView MultiLevelHandler::GameModeToString(MultiplayerGameMode mode)
	{
		switch (mode) {
			case MultiplayerGameMode::Battle: return _("Battle");
			case MultiplayerGameMode::TeamBattle: return _("Team Battle");
			case MultiplayerGameMode::CaptureTheFlag: return _("Capture The Flag");
			case MultiplayerGameMode::Race: return _("Race");
			case MultiplayerGameMode::TeamRace: return _("Team Race");
			case MultiplayerGameMode::TreasureHunt: return _("Treasure Hunt");
			case MultiplayerGameMode::Cooperation: return _("Cooperation");
			default: return _("Unknown");
		}
	}

	/*void MultiLevelHandler::UpdatePlayerLocalPos(Actors::Player* player, PlayerState& playerState, float timeMult)
	{
		if (playerState.WarpTimeLeft > 0.0f || !player->_controllable || !player->GetState(Actors::ActorState::CollideWithTileset)) {
			// Don't interpolate if warping is in progress or if collisions with tileset are disabled (when climbing or in tube)
			return;
		}

		Clock& c = nCine::clock();
		std::int64_t now = c.now() * 1000 / c.frequency();
		std::int64_t renderTime = now - ServerDelay;

		std::int32_t nextIdx = playerState.StateBufferPos - 1;
		if (nextIdx < 0) {
			nextIdx += arraySize(playerState.StateBuffer);
		}

		if (renderTime <= playerState.StateBuffer[nextIdx].Time) {
			std::int32_t prevIdx;
			while (true) {
				prevIdx = nextIdx - 1;
				if (prevIdx < 0) {
					prevIdx += arraySize(playerState.StateBuffer);
				}

				if (prevIdx == playerState.StateBufferPos || playerState.StateBuffer[prevIdx].Time <= renderTime) {
					break;
				}

				nextIdx = prevIdx;
			}

			Vector2f pos;
			Vector2f speed;
			std::int64_t timeRange = (playerState.StateBuffer[nextIdx].Time - playerState.StateBuffer[prevIdx].Time);
			if (timeRange > 0) {
				float lerp = (float)(renderTime - playerState.StateBuffer[prevIdx].Time) / timeRange;
				pos = playerState.StateBuffer[prevIdx].Pos + (playerState.StateBuffer[nextIdx].Pos - playerState.StateBuffer[prevIdx].Pos) * lerp;
				speed = playerState.StateBuffer[prevIdx].Speed + (playerState.StateBuffer[nextIdx].Speed - playerState.StateBuffer[prevIdx].Speed) * lerp;
			} else {
				pos = playerState.StateBuffer[nextIdx].Pos;
				speed = playerState.StateBuffer[nextIdx].Speed;
			}

			constexpr float BaseDeviation = 2.0f;
			constexpr float DeviationTimeMax = 90.0f;
			constexpr float DeviationTimeCutoff = 40.0f;

			float devSqr = (player->_pos - pos).SqrLength();
			float speedSqr = std::max(player->_speed.SqrLength(), speed.SqrLength());
			if (devSqr > speedSqr + (BaseDeviation * BaseDeviation)) {
				playerState.DeviationTime += timeMult;
				if (playerState.DeviationTime > DeviationTimeMax) {
					LOGW("Deviation of player %i was high for too long (deviation: %0.2fpx, speed: %0.2f)", player->_playerIndex, sqrt(devSqr), sqrt(speedSqr));
					playerState.DeviationTime = 0.0f;
					player->MoveInstantly(pos, Actors::MoveType::Absolute);
					return;
				}
			} else {
				playerState.DeviationTime = 0.0f;
			}

			float alpha = std::clamp(playerState.DeviationTime - DeviationTimeCutoff, 0.0f, DeviationTimeMax - DeviationTimeCutoff) / (DeviationTimeMax - DeviationTimeCutoff);
			if (alpha > 0.01f) {
				player->MoveInstantly(Vector2f(lerp(player->_pos.X, pos.X, alpha), lerp(player->_pos.Y, pos.Y, alpha)), Actors::MoveType::Absolute);
				player->_speed = Vector2f(lerp(player->_speed.X, speed.X, alpha), lerp(player->_speed.Y, speed.Y, alpha));
				LOGW("Deviation of player %i is high (alpha: %0.1f, deviation: %0.2fpx, speed: %0.2f)", player->_playerIndex, alpha, sqrt(devSqr), sqrt(speedSqr));
			}
		}
	}*/

	/*void MultiLevelHandler::OnRemotePlayerPosReceived(PlayerState& playerState, Vector2f pos, const Vector2f speed, PlayerFlags flags)
	{
		Clock& c = nCine::clock();
		std::int64_t now = c.now() * 1000 / c.frequency();

		if ((flags & PlayerFlags::JustWarped) != PlayerFlags::JustWarped) {
			// Player is still visible, enable interpolation
			playerState.StateBuffer[playerState.StateBufferPos].Time = now;
			playerState.StateBuffer[playerState.StateBufferPos].Pos = pos;
			playerState.StateBuffer[playerState.StateBufferPos].Speed = speed;
		} else {
			// Player just warped, reset state buffer to disable interpolation
			std::int32_t stateBufferPrevPos = playerState.StateBufferPos - 1;
			if (stateBufferPrevPos < 0) {
				stateBufferPrevPos += arraySize(playerState.StateBuffer);
			}

			std::int64_t renderTime = now - ServerDelay;

			playerState.StateBuffer[stateBufferPrevPos].Time = renderTime;
			playerState.StateBuffer[stateBufferPrevPos].Pos = pos;
			playerState.StateBuffer[stateBufferPrevPos].Speed = speed;
			playerState.StateBuffer[playerState.StateBufferPos].Time = renderTime;
			playerState.StateBuffer[playerState.StateBufferPos].Pos = pos;
			playerState.StateBuffer[playerState.StateBufferPos].Speed = speed;
		}

		playerState.StateBufferPos++;
		if (playerState.StateBufferPos >= std::int32_t(arraySize(playerState.StateBuffer))) {
			playerState.StateBufferPos = 0;
		}

		playerState.Flags = (flags & ~PlayerFlags::JustWarped);
	}*/

#if defined(DEATH_DEBUG) && defined(WITH_IMGUI)
	void MultiLevelHandler::ShowDebugWindow()
	{
		const float appWidth = theApplication().GetWidth();

		//const ImVec2 windowPos = ImVec2(appWidth * 0.5f, ImGui::GetIO().DisplaySize.y - Margin);
		//const ImVec2 windowPosPivot = ImVec2(0.5f, 1.0f);
		//ImGui::SetNextWindowPos(windowPos, ImGuiCond_FirstUseEver, windowPosPivot);
		ImGui::Begin("Multiplayer Debug", nullptr);

		ImGui::PlotLines("Update Packet Size", _updatePacketSize, PlotValueCount, _plotIndex, nullptr, 0.0f, _updatePacketMaxSize, ImVec2(appWidth * 0.2f, 40.0f));
		ImGui::SameLine(360.0f);
		ImGui::Text("%.0f bytes", _updatePacketSize[_plotIndex]);

		ImGui::PlotLines("Update Packet Size Compressed", _compressedUpdatePacketSize, PlotValueCount, _plotIndex, nullptr, 0.0f, _updatePacketMaxSize, ImVec2(appWidth * 0.2f, 40.0f));
		ImGui::SameLine(360.0f);
		ImGui::Text("%.0f bytes", _compressedUpdatePacketSize[_plotIndex]);

		ImGui::Separator();

		ImGui::PlotLines("Actors", _actorsCount, PlotValueCount, _plotIndex, nullptr, 0.0f, _actorsMaxCount, ImVec2(appWidth * 0.2f, 40.0f));
		ImGui::SameLine(360.0f);
		ImGui::Text("%.0f", _actorsCount[_plotIndex]);

		ImGui::PlotLines("Remote Actors", _remoteActorsCount, PlotValueCount, _plotIndex, nullptr, 0.0f, _actorsMaxCount, ImVec2(appWidth * 0.2f, 40.0f));
		ImGui::SameLine(360.0f);
		ImGui::Text("%.0f", _remoteActorsCount[_plotIndex]);

		ImGui::PlotLines("Mirrored Actors", _mirroredActorsCount, PlotValueCount, _plotIndex, nullptr, 0.0f, _actorsMaxCount, ImVec2(appWidth * 0.2f, 40.0f));
		ImGui::SameLine(360.0f);
		ImGui::Text("%.0f", _mirroredActorsCount[_plotIndex]);

		ImGui::PlotLines("Remoting Actors", _remotingActorsCount, PlotValueCount, _plotIndex, nullptr, 0.0f, _actorsMaxCount, ImVec2(appWidth * 0.2f, 40.0f));
		ImGui::SameLine(360.0f);
		ImGui::Text("%.0f", _remotingActorsCount[_plotIndex]);

		ImGui::Text("Last spawned ID: %u", _lastSpawnedActorId);

		ImGui::SeparatorText("Peers");

		ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInner | ImGuiTableFlags_NoPadOuterX;
		if (ImGui::BeginTable("peers", 6, flags, ImVec2(0.0f, 0.0f))) {
			ImGui::TableSetupColumn("Peer");
			ImGui::TableSetupColumn("Player Index");
			ImGui::TableSetupColumn("State");
			ImGui::TableSetupColumn("Last Updated");
			ImGui::TableSetupColumn("X");
			ImGui::TableSetupColumn("Y");
			ImGui::TableHeadersRow();
			
			for (auto& [peer, desc] : _peerDesc) {
				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				ImGui::Text("0x%p", peer._enet);

				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%u", desc.Player != nullptr ? desc.Player->GetPlayerIndex() : -1);

				ImGui::TableSetColumnIndex(2);
				ImGui::Text("0x%x", desc.State);

				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%u", desc.LastUpdated);

				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%.2f", desc.Player != nullptr ? desc.Player->GetPos().X : -1.0f);

				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%.2f", desc.Player != nullptr ? desc.Player->GetPos().Y : -1.0f);
			}
			ImGui::EndTable();
		}

		ImGui::SeparatorText("Player States");

		if (ImGui::BeginTable("playerStates", 4, flags, ImVec2(0.0f, 0.0f))) {
			ImGui::TableSetupColumn("Player Index");
			ImGui::TableSetupColumn("Flags");
			ImGui::TableSetupColumn("Pressed");
			ImGui::TableSetupColumn("Pressed (Last)");
			ImGui::TableHeadersRow();

			for (auto& [playerIdx, state] : _playerStates) {
				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%u", playerIdx);

				ImGui::TableSetColumnIndex(1);
				ImGui::Text("0x%08x", state.Flags);

				ImGui::TableSetColumnIndex(2);
				ImGui::Text("0x%08x", state.PressedKeys);

				ImGui::TableSetColumnIndex(3);
				ImGui::Text("0x%08x", state.PressedKeysLast);
			}
			ImGui::EndTable();
		}

		ImGui::End();
	}
#endif

	MultiLevelHandler::PlayerState::PlayerState()
	{
	}

	MultiLevelHandler::PlayerState::PlayerState(Vector2f pos, Vector2f speed)
		: Flags(PlayerFlags::None), PressedKeys(0), PressedKeysLast(0), UpdatedFrame(0)/*, WarpSeqNum(0), WarpTimeLeft(0.0f)*/
	{
	}
}

#endif