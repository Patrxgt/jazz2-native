﻿#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "MpGameMode.h"
#include "../PreferencesCache.h"

#include "../../nCine/Base/TimeStamp.h"
#include "../../nCine/Threading/Thread.h"

// <mmeapi.h> included by "enet.h" still uses `far` macro
#define far

#define ENET_FEATURE_ADDRESS_MAPPING
#if defined(DEATH_DEBUG)
#	define ENET_DEBUG
#endif
#include "Backends/enet.h"

// Undefine it again after include
#undef far

#include <Base/TypeInfo.h>
#include <Containers/SmallVector.h>
#include <Containers/StaticArray.h>
#include <Containers/String.h>
#include <Containers/StringView.h>
#include <IO/WebRequest.h>

using namespace Death::Containers;
using namespace Death::IO;
using namespace nCine;

namespace Jazz2::Multiplayer
{
	class NetworkManager;

	/** @brief Server description */
	struct ServerDescription
	{
		/** @brief Server endpoint in text format */
		String EndpointString;
		/** @brief Server unique identifier */
		Uuid UniqueServerID;
		/** @brief Server version */
		String Version;
		/** @brief Server name */
		String Name;
		/** @brief Multiplayer game mode */
		MpGameMode GameMode;
		/** @brief Server flags */
		std::uint32_t Flags;
		/** @brief Current number of players */
		std::uint32_t CurrentPlayerCount;
		/** @brief Maximum number of players */
		std::uint32_t MaxPlayerCount;
		/** @brief Current level name */
		String LevelName;
		/** @brief Whether the server is compatible with the local client */
		bool IsCompatible;

		// TODO: LastPingTime
		//bool IsLost;
	};

	/**
		@brief Interface to observe publicly-listed running servers

		@experimental
	*/
	class IServerObserver
	{
	public:
		/** @brief Called when a server is discovered */
		virtual void OnServerFound(ServerDescription&& desc) = 0;
	};

	/**
		@brief Interface to provide current status of the server

		@experimental
	*/
	class IServerStatusProvider
	{
		DEATH_RUNTIME_OBJECT();

	public:
		/** @brief Returns display name of current level */
		virtual StringView GetLevelDisplayName() const = 0;
	};

	/**
		@brief Allows to monitor publicly-listed running servers for server listing

		@experimental
	*/
	class ServerDiscovery
	{
	public:
		/** @{ @name Constants */

		/** @brief UDP port for server discovery broadcast */
		static constexpr std::uint16_t DiscoveryPort = 7439;
		/** @brief Length of server unique identifier */
		static constexpr std::int32_t UniqueIdentifierLength = 16;

		/** @} */

		/** @brief Creates an instance to advertise a running local server */
		ServerDiscovery(NetworkManager* server);
		/** @brief Creates an instance to observe remote servers */
		ServerDiscovery(IServerObserver* observer);
		~ServerDiscovery();

		/** @brief Sets status provider */
		void SetStatusProvider(std::weak_ptr<IServerStatusProvider> statusProvider);

	private:
		ServerDiscovery(const ServerDiscovery&) = delete;
		ServerDiscovery& operator=(const ServerDiscovery&) = delete;

		static constexpr std::uint64_t PacketSignature = 0x2095A59FF0BFBBEF;

		NetworkManager* _server;
		IServerObserver* _observer;
		std::weak_ptr<IServerStatusProvider> _statusProvider;
		ENetSocket _socket;
		Thread _thread;
		TimeStamp _lastLocalRequestTime;
		TimeStamp _lastOnlineRequestTime;
		ENetAddress _localMulticastAddress;
		bool _onlineSuccess;

		static ENetSocket TryCreateLocalSocket(const char* multicastAddress, ENetAddress& parsedAddress);

		void SendLocalDiscoveryRequest(ENetSocket socket, const ENetAddress& address);
		void DownloadPublicServerList(IServerObserver* observer);
		bool ProcessLocalDiscoveryResponses(ENetSocket socket, ServerDescription& discoveredServer, std::int32_t timeoutMs = 0);
		bool ProcessLocalDiscoveryRequests(ENetSocket socket, std::int32_t timeoutMs = 0);
		void SendLocalDiscoveryResponse(ENetSocket socket, NetworkManager* server);
		void PublishToPublicServerList(NetworkManager* server);
		void DelistFromPublicServerList(NetworkManager* server);

		static void OnClientThread(void* param);
		static void OnServerThread(void* param);
	};
}

#endif