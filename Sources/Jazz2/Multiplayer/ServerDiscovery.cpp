﻿#include "ServerDiscovery.h"

#if defined(WITH_MULTIPLAYER)

#include "NetworkManager.h"
#include "PacketTypes.h"
#include "../PreferencesCache.h"
#include "../../nCine/Application.h"
#include "../../nCine/Base/Algorithms.h"
#include "../../nCine/Base/FrameTimer.h"
#include "../../nCine/Threading/Thread.h"

#include <Containers/String.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringStlView.h>
#include <Containers/StringUtils.h>
#include <IO/MemoryStream.h>

#if defined(DEATH_TARGET_ANDROID)
#	include "Backends/ifaddrs-android.h"
#elif defined(DEATH_TARGET_SWITCH) && ENET_IPV6
// `ipv6_mreq` is not defined in Switch SDK, but it doesn't work well anyway
struct ipv6_mreq {
	struct in6_addr ipv6mr_multiaddr; /* IPv6 multicast address */
	unsigned int    ipv6mr_interface; /* Interface index */
};
#elif defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX)
#	include <ifaddrs.h>
#	include <net/if.h>
#elif defined(DEATH_TARGET_WINDOWS)
#	include <iphlpapi.h>
#	include <Utf8.h>
#endif

#include "../../jsoncpp/json.h"

using namespace Death::Containers::Literals;
using namespace Death::IO;
using namespace nCine;

using namespace std::string_view_literals;

/** @brief @ref Death::Containers::StringView from @ref NCINE_VERSION */
#define NCINE_VERSION_s DEATH_PASTE(NCINE_VERSION, _s)

namespace Jazz2::Multiplayer
{
#if ENET_IPV6
	static std::int32_t GetDefaultIPv6MulticastIfIndex()
	{
#	if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX)
		std::int32_t ifidx = 0;
		struct ifaddrs* ifaddr;
		struct ifaddrs* ifa;
		if (getifaddrs(&ifaddr) == 0) {
			for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
				// Prefer first adapter that is up, not loopback, supports multicast
				if (ifa->ifa_addr != nullptr && (ifa->ifa_addr->sa_family == AF_INET || ifa->ifa_addr->sa_family == AF_INET6) &&
					(ifa->ifa_flags & IFF_UP) && !(ifa->ifa_flags & IFF_LOOPBACK) && (ifa->ifa_flags & IFF_MULTICAST)) {
					ifidx = if_nametoindex(ifa->ifa_name);
					if (ifidx > 0) {
						LOGI("[MP] Using %s interface \"%s\" (%i) for local discovery", ifa->ifa_addr->sa_family == AF_INET6
							? "IPv6" : "IPv4", ifa->ifa_name, ifidx);
						break;
					}
				}
			}
			freeifaddrs(ifaddr);
		}
		if (ifidx == 0) {
			LOGI("[MP] No suitable interface found for local discovery");
			ifidx = if_nametoindex("wlan0");
		}
		return ifidx;
#	elif defined(DEATH_TARGET_WINDOWS)
		ULONG bufferSize = 0;
		::GetAdaptersAddresses(AF_INET6, GAA_FLAG_INCLUDE_PREFIX, NULL, NULL, &bufferSize);
		std::unique_ptr<std::uint8_t[]> buffer = std::make_unique<std::uint8_t[]>(bufferSize);
		auto* adapterAddresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.get());

		if (::GetAdaptersAddresses(AF_INET6, GAA_FLAG_INCLUDE_PREFIX, NULL, adapterAddresses, &bufferSize) == NO_ERROR) {
			for (auto* adapter = adapterAddresses; adapter != nullptr; adapter = adapter->Next) {
				// Prefer first adapter that is up, not loopback, supports multicast
				if (adapter->OperStatus == IfOperStatusUp && adapter->IfType != IF_TYPE_SOFTWARE_LOOPBACK &&
					!(adapter->Flags & IP_ADAPTER_NO_MULTICAST)) {
					LOGI("[MP] Using IPv6 interface \"%s\" (%s:%i) for local discovery", Utf8::FromUtf16(adapter->FriendlyName).data(),
						adapter->AdapterName, (std::int32_t)adapter->Ipv6IfIndex);
					return (std::int32_t)adapter->Ipv6IfIndex;
				}
			}
		}
		LOGI("[MP] No suitable interface found for local discovery");
		return 0;
#	else
		return 0;
#	endif
	}
#endif

	ServerDiscovery::ServerDiscovery(NetworkManager* server)
		: _server(server), _observer(nullptr), _onlineSuccess(false)
	{
		DEATH_DEBUG_ASSERT(server != nullptr, "server is null", );

		_thread = Thread(ServerDiscovery::OnServerThread, this);
	}

	ServerDiscovery::ServerDiscovery(IServerObserver* observer)
		: _server(nullptr), _observer(observer)
	{
		DEATH_DEBUG_ASSERT(observer != nullptr, "observer is null", );

		_thread = Thread(ServerDiscovery::OnClientThread, this);
	}

	ServerDiscovery::~ServerDiscovery()
	{
		_server = nullptr;
		_observer = nullptr;

		_thread.Join();

		NetworkManagerBase::ReleaseBackend();
	}

	void ServerDiscovery::SetStatusProvider(std::weak_ptr<IServerStatusProvider> statusProvider)
	{
		_statusProvider = std::move(statusProvider);
	}

	ENetSocket ServerDiscovery::TryCreateLocalSocket(const char* multicastAddress, ENetAddress& parsedAddress)
	{
#if ENET_IPV6
		std::int32_t ifidx = GetDefaultIPv6MulticastIfIndex();

		ENetSocket socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
		if (socket == ENET_SOCKET_NULL) {
#	if defined(DEATH_TARGET_WINDOWS)
			std::int32_t error = ::WSAGetLastError();
#	else
			std::int32_t error = errno;
#	endif
			LOGE("[MP] Failed to create socket for local server discovery (error: %i)", error);
			return ENET_SOCKET_NULL;
		}

		std::int32_t on = 1, hops = 3;
		if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on)) != 0 ||
			setsockopt(socket, IPPROTO_IPV6, IPV6_MULTICAST_IF, (const char*)&ifidx, sizeof(ifidx)) != 0 ||
			setsockopt(socket, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, (const char*)&hops, sizeof(hops)) != 0 ||
			setsockopt(socket, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, (const char*)&on, sizeof(on)) != 0) {
#	if defined(DEATH_TARGET_WINDOWS)
			std::int32_t error = ::WSAGetLastError();
#	else
			std::int32_t error = errno;
#	endif
			LOGE("[MP] Failed to enable multicast on socket for local server discovery (error: %i)", error);
			enet_socket_destroy(socket);
			return ENET_SOCKET_NULL;
		}

		struct sockaddr_in6 saddr;
		std::memset(&saddr, 0, sizeof(saddr));
		saddr.sin6_family = AF_INET6;
		saddr.sin6_port = htons(DiscoveryPort);
		saddr.sin6_addr = in6addr_any;

		if (bind(socket, (struct sockaddr*)&saddr, sizeof(saddr))) {
#	if defined(DEATH_TARGET_WINDOWS)
			std::int32_t error = ::WSAGetLastError();
#	else
			std::int32_t error = errno;
#	endif
			LOGE("[MP] Failed to bind socket for local server discovery (error: %i)", error);
			enet_socket_destroy(socket);
			return ENET_SOCKET_NULL;
		}

		std::int32_t result = inet_pton(AF_INET6, multicastAddress, &parsedAddress.host);
		if (result != 1) {
#	if defined(DEATH_TARGET_WINDOWS)
			std::int32_t error = ::WSAGetLastError();
#	else
			std::int32_t error = errno;
#	endif
			LOGE("[MP] Failed to parse multicast address for local server discovery (result: %i, error: %i)", result, error);
			enet_socket_destroy(socket);
			return ENET_SOCKET_NULL;
		}

		parsedAddress.sin6_scope_id = ifidx;
		parsedAddress.port = DiscoveryPort;

		struct ipv6_mreq mreq;
		std::memcpy(&mreq.ipv6mr_multiaddr, &parsedAddress.host, sizeof(parsedAddress.host));
		mreq.ipv6mr_interface = ifidx;

		if (setsockopt(socket, IPPROTO_IPV6, IPV6_JOIN_GROUP, (char*)&mreq, sizeof(mreq))) {
#	if defined(DEATH_TARGET_WINDOWS)
			std::int32_t error = ::WSAGetLastError();
#	else
			std::int32_t error = errno;
#	endif
			LOGE("[MP] Failed to join multicast group on socket for local server discovery (error: %i)", error);
			enet_socket_destroy(socket);
			return ENET_SOCKET_NULL;
		}
#else
		// TODO: Use broadcast on IPv4
		LOGW("[MP] Local server discovery is not supported on IPv4");
		ENetSocket socket = ENET_SOCKET_NULL;
#endif

		return socket;
	}

	void ServerDiscovery::SendLocalDiscoveryRequest(ENetSocket socket, const ENetAddress& address)
	{
		if (socket == ENET_SOCKET_NULL) {
			return;
		}

		MemoryStream packet(9);
		packet.WriteValue<std::uint64_t>(PacketSignature);
		packet.WriteValue<std::uint8_t>((std::uint8_t)BroadcastPacketType::DiscoveryRequest);

		ENetBuffer sendbuf;
		sendbuf.data = (void*)packet.GetBuffer();
		sendbuf.dataLength = packet.GetSize();
		std::int32_t result = enet_socket_send(socket, &address, &sendbuf, 1);
		if (result != (std::int32_t)sendbuf.dataLength) {
#if defined(DEATH_TARGET_WINDOWS)
			std::int32_t error = ::WSAGetLastError();
#else
			std::int32_t error = errno;
#endif
			LOGE("[MP] Failed to send local discovery request (result: %i, error: %i)", result, error);
		}
	}

	void ServerDiscovery::DownloadPublicServerList(IServerObserver* observer)
	{
		LOGD("[MP] Downloading public server list…");

		String url = "https://deat.tk/jazz2/servers?fetch&v=2&d="_s + PreferencesCache::GetDeviceID();
		auto request = WebSession::GetDefault().CreateRequest(url);
		auto result = request.Execute();
		if (result) {
			auto s = request.GetResponse().GetStream();
			auto size = s->GetSize();
			auto buffer = std::make_unique<char[]>(size);
			s->Read(buffer.get(), size);

			Json::CharReaderBuilder builder;
			auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
			Json::Value doc; std::string errors;
			if (reader->parse(buffer.get(), buffer.get() + size, &doc, &errors)) {
				LOGI("[MP] Downloaded public server list with %u entries (%u bytes)", (std::uint32_t)doc["s"].size(), (std::uint32_t)size);

				for (auto serverItem : doc["s"]) {
					std::string_view serverName, serverUuid, serverEndpoints;
					if (serverItem["n"].get(serverName) == Json::SUCCESS && !serverName.empty() &&
						serverItem["u"].get(serverUuid) == Json::SUCCESS && !serverUuid.empty() &&
						serverItem["e"].get(serverEndpoints) == Json::SUCCESS && !serverEndpoints.empty()) {

						std::int64_t currentPlayers = 0, maxPlayers = 0;
						serverItem["c"].get(currentPlayers);
						serverItem["m"].get(maxPlayers);

						std::string_view version;
						serverItem["v"].get(version);

						ServerDescription discoveredServer{};
						discoveredServer.Version = version;
						discoveredServer.Name = serverName;
						discoveredServer.EndpointString = serverEndpoints;
						discoveredServer.Name = serverName;
						discoveredServer.CurrentPlayerCount = (std::uint32_t)currentPlayers;
						discoveredServer.MaxPlayerCount = (std::uint32_t)maxPlayers;

						LOGD("[MP] -\tFound server \"%s\" at %s", discoveredServer.Name.data(), discoveredServer.EndpointString.data());
						observer->OnServerFound(std::move(discoveredServer));
					}
				}
			} else {
				LOGE("[MP] Failed to parse public server list: %s", errors.c_str());
			}
		} else {
			LOGE("[MP] Failed to download public server list: %s", result.error.data());
		}
	}

	bool ServerDiscovery::ProcessLocalDiscoveryResponses(ENetSocket socket, ServerDescription& discoveredServer, std::int32_t timeoutMs)
	{
		if (socket == ENET_SOCKET_NULL) {
			return false;
		}

		ENetSocketSet set;
		ENET_SOCKETSET_EMPTY(set);
		ENET_SOCKETSET_ADD(set, socket);
		if (enet_socketset_select(socket, &set, NULL, timeoutMs) <= 0) {
			return false;
		}

		ENetAddress endpoint;
		std::uint8_t buffer[512];
		ENetBuffer recvbuf;
		recvbuf.data = buffer;
		recvbuf.dataLength = sizeof(buffer);
		const std::int32_t bytesRead = enet_socket_receive(socket, &endpoint, &recvbuf, 1);
		if (bytesRead <= 0) {
			return false;
		}

		MemoryStream packet(buffer, bytesRead);
		std::uint64_t signature = packet.ReadValue<std::uint64_t>();
		BroadcastPacketType packetType = (BroadcastPacketType)packet.ReadValue<std::uint8_t>();
		if (signature != PacketSignature || packetType != BroadcastPacketType::DiscoveryResponse) {
			return false;
		}

		// Override the port, because it points to the discovery service, not the actual server
		endpoint.port = packet.ReadValue<std::uint16_t>();

		discoveredServer.EndpointString = NetworkManagerBase::AddressToString(endpoint, true);
		if (discoveredServer.EndpointString.empty()) {
			return false;
		}

		packet.Read(discoveredServer.UniqueServerID, sizeof(discoveredServer.UniqueServerID));

		std::uint8_t versionLength = packet.ReadValue<std::uint8_t>();
		discoveredServer.Version = String(NoInit, versionLength);
		packet.Read(discoveredServer.Version.data(), versionLength);

		std::uint8_t nameLength = packet.ReadValue<std::uint8_t>();
		discoveredServer.Name = String(NoInit, nameLength);
		packet.Read(discoveredServer.Name.data(), nameLength);

		discoveredServer.Flags = packet.ReadVariableUint32() | 0x80000000u /*Local*/;
		discoveredServer.GameMode = (MpGameMode)packet.ReadValue<std::uint8_t>();
		discoveredServer.CurrentPlayerCount = packet.ReadVariableUint32();
		discoveredServer.MaxPlayerCount = packet.ReadVariableUint32();

		nameLength = packet.ReadValue<std::uint8_t>();
		discoveredServer.LevelName = String(NoInit, nameLength);
		packet.Read(discoveredServer.LevelName.data(), nameLength);

		LOGD("[MP] Found local server \"%s\" at %s", discoveredServer.Name.data(), discoveredServer.EndpointString.data());
		return true;
	}

	bool ServerDiscovery::ProcessLocalDiscoveryRequests(ENetSocket socket, std::int32_t timeoutMs)
	{
		if (socket == ENET_SOCKET_NULL) {
			return false;
		}

		ENetSocketSet set;
		ENET_SOCKETSET_EMPTY(set);
		ENET_SOCKETSET_ADD(set, socket);
		if (enet_socketset_select(socket, &set, NULL, timeoutMs) <= 0) {
			return false;
		}

		ENetAddress endpoint;
		std::uint8_t buffer[512];
		ENetBuffer recvbuf;
		recvbuf.data = buffer;
		recvbuf.dataLength = sizeof(buffer);
		const std::int32_t bytesRead = enet_socket_receive(socket, &endpoint, &recvbuf, 1);
		if (bytesRead <= 0) {
			return false;
		}

		MemoryStream packet(buffer, bytesRead);
		std::uint64_t signature = packet.ReadValue<std::uint64_t>();
		BroadcastPacketType packetType = (BroadcastPacketType)packet.ReadValue<std::uint8_t>();
		if (signature != PacketSignature || packetType != BroadcastPacketType::DiscoveryRequest) {
			return false;
		}

		return true;
	}

	void ServerDiscovery::SendLocalDiscoveryResponse(ENetSocket socket, NetworkManager* server)
	{
		if (socket == ENET_SOCKET_NULL) {
			return;
		}

		// If server name is empty, it's private and shouldn't respond to discovery messages
		auto& serverConfig = server->GetServerConfiguration();
		if (!serverConfig.ServerName.empty()) {
			const auto& id = serverConfig.UniqueServerID;

			MemoryStream packet(512);
			packet.WriteValue<std::uint64_t>(PacketSignature);
			packet.WriteValue<std::uint8_t>((std::uint8_t)BroadcastPacketType::DiscoveryResponse);
			packet.WriteValue<std::uint16_t>(server->GetServerPort());
			packet.Write(id.data(), id.size());

			StringView serverVersion = NCINE_VERSION_s;
			serverVersion = serverVersion.prefix(serverVersion.findOr('-', serverVersion.end()).begin());
			packet.WriteValue<std::uint8_t>((std::uint8_t)serverVersion.size());
			packet.Write(serverVersion.data(), (std::uint8_t)serverVersion.size());

			packet.WriteValue<std::uint8_t>((std::uint8_t)serverConfig.ServerName.size());
			packet.Write(serverConfig.ServerName.data(), (std::uint8_t)serverConfig.ServerName.size());

			std::uint32_t flags = 0;
			if (!serverConfig.ServerPassword.empty()) {
				flags |= 0x01;
			}
			if (!serverConfig.WhitelistedUniquePlayerIDs.empty()) {
				flags |= 0x02;
			}
			packet.WriteVariableUint32(flags);
			packet.WriteValue<std::uint8_t>((std::uint8_t)serverConfig.GameMode);

			packet.WriteVariableUint32(server->GetPeerCount());
			packet.WriteVariableUint32(serverConfig.MaxPlayerCount);

			if (auto statusProvider = _statusProvider.lock()) {
				auto levelDisplayName = statusProvider->GetLevelDisplayName().trimmed();
				packet.WriteValue<std::uint8_t>((std::uint8_t)levelDisplayName.size());
				packet.Write(levelDisplayName.data(), (std::uint8_t)levelDisplayName.size());
			} else {
				packet.WriteValue<std::uint8_t>(0);
			}

			ENetBuffer sendbuf;
			sendbuf.data = (void*)packet.GetBuffer();
			sendbuf.dataLength = packet.GetSize();
			std::int32_t result = enet_socket_send(socket, &_localMulticastAddress, &sendbuf, 1);
			if (result != (std::int32_t)sendbuf.dataLength) {
#if defined(DEATH_TARGET_WINDOWS)
				std::int32_t error = ::WSAGetLastError();
#else
				std::int32_t error = errno;
#endif
				LOGE("[MP] Failed to send local discovery response (result: %i, error: %i)", result, error);
			}
		}
	}

	void ServerDiscovery::PublishToPublicServerList(NetworkManager* server)
	{
		_onlineSuccess = false;

		auto& serverConfig = server->GetServerConfiguration();
		if (serverConfig.ServerName.empty()) {
			return;
		}

		String serverName = StringUtils::replaceAll(StringUtils::replaceAll(StringUtils::replaceAll(serverConfig.ServerName,
			"\\"_s, "\\\\"_s), "\""_s, "\\\""_s), "\f"_s, "\\f"_s);

		char input[2048];
		std::int32_t length = formatString(input, sizeof(input), "{\"n\":\"%s\",\"u\":\"", serverName.data());

		const auto& id = serverConfig.UniqueServerID;
		length += formatString(input + length, sizeof(input) - length,
			"%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X",
			id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7], id[8], id[9], id[10], id[11], id[12], id[13], id[14], id[15]);

		length += formatString(input + length, sizeof(input) - length, "\",\"e\":\"");

		StringView address; std::uint16_t port;
		if (NetworkManagerBase::TrySplitAddressAndPort(serverConfig.ServerAddressOverride, address, port)) {
			String addressEscaped = StringUtils::replaceAll(StringUtils::replaceAll(address,
				"\\"_s, "\\\\"_s), "\""_s, "\\\""_s);
			if (port == 0) {
				port = server->_host->address.port;
			}
			length += formatString(input + length, sizeof(input) - length, "%s:%u", addressEscaped.data(), port);
		} else {
			bool isFirst = true;
			auto endpoints = server->GetServerEndpoints();
			for (auto& endpoint : endpoints) {
				if (length > 1228) { // It's usually enough for all the endpoints
					break;
				}
				if (isFirst) {
					isFirst = false;
				} else {
					length += formatString(input + length, sizeof(input) - length, "|");
				}

				length += formatString(input + length, sizeof(input) - length, "%s", endpoint.data());
			}
		}

		std::int32_t serverLoad = (std::int32_t)(theApplication().GetFrameTimer().GetLastFrameDuration() * 1000.0f);
		if (serverLoad > 400) {
			serverLoad = -1;
		}

		String levelDisplayName;
		if (auto statusProvider = _statusProvider.lock()) {
			levelDisplayName = StringUtils::replaceAll(StringUtils::replaceAll(StringUtils::replaceAll(statusProvider->GetLevelDisplayName().trimmed(),
				"\\"_s, "\\\\"_s), "\""_s, "\\\""_s), "\f"_s, "\\f"_s);
		}

		length += formatString(input + length, sizeof(input) - length, "\",\"v\":\"%s\",\"d\":\"%s\",\"p\":%u,\"m\":%u,\"s\":%llu,\"l\":%i,\"g\":%u,\"f\":\"%s\"}",
			NCINE_VERSION, PreferencesCache::GetDeviceID().data(), server->GetPeerCount(), serverConfig.MaxPlayerCount,
			serverConfig.StartUnixTimestamp, serverLoad, std::uint32_t(serverConfig.GameMode), levelDisplayName.data());

		auto request = WebSession::GetDefault().CreateRequest("https://deat.tk/jazz2/servers"_s);
		request.SetMethod("POST"_s);
		request.SetData(StringView(input, length), "application/json"_s);
		if (auto result = request.Execute()) {
			auto s = request.GetResponse().GetStream();
			auto size = s->GetSize();
			auto buffer = std::make_unique<char[]>(size);
			s->Read(buffer.get(), size);

			Json::CharReaderBuilder builder;
			auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
			Json::Value doc; std::string errors;
			if (reader->parse(buffer.get(), buffer.get() + size, &doc, &errors)) {
				bool success; std::string_view endpoints;
				if (doc["r"].get(success) == Json::SUCCESS && success &&
					doc["e"].get(endpoints) == Json::SUCCESS && !endpoints.empty()) {
					_onlineSuccess = true;
					LOGD("[MP] Server published with following endpoints: %s", String(endpoints).data());
				} else {
					LOGE("[MP] Failed to publish the server: Request rejected");
				}
			} else {
				LOGE("[MP] Failed to publish the server: Response cannot be parsed: %s", errors.c_str());
			}
		} else {
			LOGE("[MP] Failed to publish the server: %s", result.error.data());
		}
	}

	void ServerDiscovery::DelistFromPublicServerList(NetworkManager* server)
	{
		if (!_onlineSuccess) {
			return;
		}

		_onlineSuccess = false;

		auto& serverConfig = server->GetServerConfiguration();
		if (serverConfig.ServerName.empty()) {
			return;
		}

		String serverName = StringUtils::replaceAll(StringUtils::replaceAll(StringUtils::replaceAll(serverConfig.ServerName,
			"\\"_s, "\\\\"_s), "\""_s, "\\\""_s), "\f"_s, "\\f"_s);

		char input[2048];
		std::int32_t length = formatString(input, sizeof(input), "{\"n\":\"%s\",\"u\":\"", serverName.data());

		const auto& id = serverConfig.UniqueServerID;
		length += formatString(input + length, sizeof(input) - length,
			"%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X",
			id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7], id[8], id[9], id[10], id[11], id[12], id[13], id[14], id[15]);

		length += formatString(input + length, sizeof(input) - length, "\",\"e\":null,\"v\":\"%s\",\"d\":\"%s\"}",
			NCINE_VERSION, PreferencesCache::GetDeviceID().data());

		auto request = WebSession::GetDefault().CreateRequest("https://deat.tk/jazz2/servers"_s);
		request.SetMethod("POST"_s);
		request.SetData(StringView(input, length), "application/json"_s);
		if (auto result = request.Execute()) {
			auto s = request.GetResponse().GetStream();
			auto size = s->GetSize();
			auto buffer = std::make_unique<char[]>(size);
			s->Read(buffer.get(), size);

			Json::CharReaderBuilder builder;
			auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
			Json::Value doc; std::string errors;
			if (reader->parse(buffer.get(), buffer.get() + size, &doc, &errors)) {
				bool success; std::string_view endpoints;
				if (doc["r"].get(success) == Json::SUCCESS && success) {
					LOGD("[MP] Server delisted successfully");
				} else {
					LOGE("[MP] Failed to delist the server: Request rejected");
				}
			} else {
				LOGE("[MP] Failed to delist the server: Response cannot be parsed: %s", errors.c_str());
			}
		} else {
			LOGE("[MP] Failed to delist the server: %s", result.error.data());
		}
	}

	void ServerDiscovery::OnClientThread(void* param)
	{
		ServerDiscovery* _this = static_cast<ServerDiscovery*>(param);
		IServerObserver* observer = _this->_observer;

		NetworkManagerBase::InitializeBackend();

		ENetSocket socket = TryCreateLocalSocket("ff02::1", _this->_localMulticastAddress);
		_this->_socket = socket;

		while (_this->_observer != nullptr) {
			if (_this->_lastLocalRequestTime.secondsSince() > 10) {
				_this->_lastLocalRequestTime = TimeStamp::now();
				_this->SendLocalDiscoveryRequest(socket, _this->_localMulticastAddress);
			}

			if (_this->_lastOnlineRequestTime.secondsSince() > 60) {
				_this->_lastOnlineRequestTime = TimeStamp::now();
				_this->DownloadPublicServerList(observer);
			}

			ServerDescription discoveredServer;
			if (_this->ProcessLocalDiscoveryResponses(socket, discoveredServer, 0)) {
				observer->OnServerFound(std::move(discoveredServer));
			} else {
				// No responses, sleep for a while
				Thread::Sleep(500);
			}
		}

		if (_this->_socket != ENET_SOCKET_NULL) {
			enet_socket_destroy(_this->_socket);
			_this->_socket = ENET_SOCKET_NULL;
		}

		LOGD("[MP] Server discovery thread exited");
	}

	void ServerDiscovery::OnServerThread(void* param)
	{
		ServerDiscovery* _this = static_cast<ServerDiscovery*>(param);
		NetworkManager* server = _this->_server;
		std::int32_t delayCount = 30;	// Delay for 15 seconds before starting to send discovery responses

		NetworkManagerBase::InitializeBackend();

		ENetSocket socket = TryCreateLocalSocket("ff02::1", _this->_localMulticastAddress);
		_this->_socket = socket;

		while (_this->_server != nullptr) {
			delayCount--;
			if (delayCount <= 0) {
				delayCount = 10;

				while (_this->_localMulticastAddress.port != 0 && _this->ProcessLocalDiscoveryRequests(socket, 0)) {
					if (_this->_lastLocalRequestTime.secondsSince() > 15) {
						_this->_lastLocalRequestTime = TimeStamp::now();
						_this->SendLocalDiscoveryResponse(socket, server);
					}
				}

				if (_this->_lastOnlineRequestTime.secondsSince() > 300) {
					_this->_lastOnlineRequestTime = TimeStamp::now();
					_this->PublishToPublicServerList(server);
				}
			}

			Thread::Sleep(500);
		}

		_this->DelistFromPublicServerList(server);

		if (_this->_socket != ENET_SOCKET_NULL) {
			enet_socket_destroy(_this->_socket);
			_this->_socket = ENET_SOCKET_NULL;
		}

		LOGD("[MP] Server discovery thread exited");
	}
}

#endif