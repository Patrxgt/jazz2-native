﻿#define ENET_IMPLEMENTATION
#include "NetworkManagerBase.h"

#if defined(WITH_MULTIPLAYER)

#include "INetworkHandler.h"
#include "../../nCine/Base/Algorithms.h"
#include "../../nCine/Threading/Thread.h"

#include <atomic>
#include <mutex>

#include <Containers/GrowableArray.h>
#include <Containers/String.h>

#if defined(DEATH_TARGET_ANDROID)
#	include "Backends/ifaddrs-android.h"
#elif defined(DEATH_TARGET_SWITCH)
#	include <net/if.h>
#elif defined(DEATH_TARGET_WINDOWS)
#	include <iphlpapi.h>
#else
#	include <ifaddrs.h>
#endif

using namespace Death;
using namespace Death::Containers::Literals;

namespace Jazz2::Multiplayer
{
	static std::atomic_int32_t _initializeCount{0};

	NetworkManagerBase::NetworkManagerBase()
		: _host(nullptr), _state(NetworkState::None), _handler(nullptr)
	{
		InitializeBackend();
	}

	NetworkManagerBase::~NetworkManagerBase()
	{
		Dispose();
		ReleaseBackend();
	}

	void NetworkManagerBase::CreateClient(INetworkHandler* handler, StringView endpoints, std::uint16_t defaultPort, std::uint32_t clientData)
	{
		if (_handler != nullptr) {
			LOGE("[MP] Client already created");
			return;
		}

		_state = NetworkState::Connecting;
		_clientData = clientData;
		_desiredEndpoints.clear();

#if defined(DEATH_TARGET_ANDROID)
		std::int32_t ifidx = 0;
		struct ifaddrs* ifaddr;
		struct ifaddrs* ifa;
		if (getifaddrs(&ifaddr) == 0) {
			for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
				if (ifa->ifa_addr != nullptr && (ifa->ifa_addr->sa_family == AF_INET || ifa->ifa_addr->sa_family == AF_INET6) &&
					(ifa->ifa_flags & IFF_UP)) {
					ifidx = if_nametoindex(ifa->ifa_name);
					if (ifidx > 0) {
						LOGI("[MP] Using %s interface \"%s\" (%i)", ifa->ifa_addr->sa_family == AF_INET6
							? "IPv6" : "IPv4", ifa->ifa_name, ifidx);
						break;
					}
				}
			}
			freeifaddrs(ifaddr);
		}
		if (ifidx == 0) {
			LOGI("[MP] No suitable interface found");
			ifidx = if_nametoindex("wlan0");
		}
#else
		std::int32_t ifidx = 0;
#endif

		while (endpoints) {
			auto p = endpoints.partition('|');
			if (p[0]) {
				StringView address; std::uint16_t port;
				if (TrySplitAddressAndPort(p[0], address, port)) {
					ENetAddress addr = {};
					String nullTerminatedAddress = String::nullTerminatedView(address);
					std::int32_t r = enet_address_set_host(&addr, nullTerminatedAddress.data());
					//std::int32_t r = enet_address_set_host_ip(&addr, nullTerminatedAddress.data());
					if (r == 0) {
#if ENET_IPV6
						if (addr.sin6_scope_id == 0) {
							addr.sin6_scope_id = (std::uint16_t)ifidx;
						}
#endif
						addr.port = (port != 0 ? port : defaultPort);
						_desiredEndpoints.push_back(std::move(addr));
					} else {
#if defined(DEATH_TARGET_WINDOWS)
						std::int32_t error = ::WSAGetLastError();
#else
						std::int32_t error = errno;
#endif
						LOGW("[MP] Failed to parse specified address \"%s\" with error %i", nullTerminatedAddress.data(), error);
					}
				} else {
					LOGW("[MP] Failed to parse specified endpoint \"%s\"", String::nullTerminatedView(p[0]).data());
				}
			}

			endpoints = p[2];
		}

		_handler = handler;
		_thread = Thread(NetworkManagerBase::OnClientThread, this);
	}

	bool NetworkManagerBase::CreateServer(INetworkHandler* handler, std::uint16_t port)
	{
		if (_handler != nullptr) {
			return false;
		}

		ENetAddress addr = {};
		addr.host = ENET_HOST_ANY;
		addr.port = port;

		_host = enet_host_create(&addr, MaxPeerCount, (std::size_t)NetworkChannel::Count, 0, 0);
		RETURNF_ASSERT_MSG(_host != nullptr, "Failed to create a server");

		_handler = handler;
		_state = NetworkState::Listening;
		_thread = Thread(NetworkManagerBase::OnServerThread, this);
		return true;
	}

	void NetworkManagerBase::Dispose()
	{
		if (_host == nullptr) {
			return;
		}

		_state = NetworkState::None;
		_thread.Join();

		_host = nullptr;
		_handler = nullptr;
	}

	NetworkState NetworkManagerBase::GetState() const
	{
		return _state;
	}

	std::uint32_t NetworkManagerBase::GetRoundTripTimeMs() const
	{
		return (_state == NetworkState::Connected && !_peers.empty() ? _peers[0]->roundTripTime : 0);
	}

	Array<String> NetworkManagerBase::GetServerEndpoints() const
	{
		Array<String> result;

		if (_state == NetworkState::Listening) {
#if defined(DEATH_TARGET_SWITCH)
			struct ifconf ifc;
			struct ifreq ifr[8];
			ifc.ifc_len = sizeof(ifr);
			ifc.ifc_req = ifr;

			if (ioctl(_host->socket, SIOCGIFCONF, &ifc) >= 0) {
				std::int32_t count = ifc.ifc_len / sizeof(struct ifreq);
				LOGI("[MP] Found %d interfaces:", count);
				for (std::int32_t i = 0; i < count; i++) {
					if (ifr[i].ifr_addr.sa_family == AF_INET) { // IPv4
						auto* addrPtr = &((struct sockaddr_in*)&ifr[i].ifr_addr)->sin_addr;
						String addressString = AddressToString(*addrPtr, _host->address.port);
						LOGI("[MP] -\t%s: %s", ifr[i].ifr_name, addressString.data());
						if (!addressString.empty() && !addressString.hasPrefix("127.0.0.1:"_s)) {
							arrayAppend(result, std::move(addressString));
						}
					}
#	if ENET_IPV6
					else if (ifr[i].ifr_addr.sa_family == AF_INET6) { // IPv6
						auto* addrPtr = &((struct sockaddr_in6*)&ifr[i].ifr_addr)->sin6_addr;
						//auto scopeId = ((struct sockaddr_in6*)&ifr[i].ifr_addr)->sin6_scope_id;
						String addressString = AddressToString(*addrPtr, /*scopeId*/0, _host->address.port);
						LOGI("[MP] -\t%s: %s", ifr[i].ifr_name, addressString.data());
						if (!addressString.empty() && !addressString.hasPrefix("[::1]:"_s)) {
							arrayAppend(result, std::move(addressString));
						}
					}
#	endif
				}
			} else {
				LOGW("[MP] Failed to get server endpoints");
			}
#elif defined(DEATH_TARGET_WINDOWS)
			ULONG bufferSize = 15000;
			std::unique_ptr<std::uint8_t[]> buffer = std::make_unique<std::uint8_t[]>(bufferSize);
			PIP_ADAPTER_ADDRESSES adapterAddresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.get());

			if (::GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, adapterAddresses, &bufferSize) == NO_ERROR) {
				for (PIP_ADAPTER_ADDRESSES adapter = adapterAddresses; adapter != NULL; adapter = adapter->Next) {
					for (PIP_ADAPTER_UNICAST_ADDRESS address = adapter->FirstUnicastAddress; address != NULL; address = address->Next) {
						String addressString;
						if (address->Address.lpSockaddr->sa_family == AF_INET) { // IPv4
							auto* addrPtr = &((struct sockaddr_in*)address->Address.lpSockaddr)->sin_addr;
							String addressString = AddressToString(*addrPtr, _host->address.port);
							if (!addressString.empty() && !addressString.hasPrefix("127.0.0.1:"_s)) {
								arrayAppend(result, std::move(addressString));
							}
						}
#	if ENET_IPV6
						else if (address->Address.lpSockaddr->sa_family == AF_INET6) { // IPv6
							auto* addrPtr = &((struct sockaddr_in6*)address->Address.lpSockaddr)->sin6_addr;
							//auto scopeId = ((struct sockaddr_in6*)address->Address.lpSockaddr)->sin6_scope_id;
							String addressString = AddressToString(*addrPtr, /*scopeId*/0, _host->address.port);
							if (!addressString.empty() && !addressString.hasPrefix("[::1]:"_s)) {
								arrayAppend(result, std::move(addressString));
							}
						}
#	endif
						else {
							// Unsupported address family
						}
					}
				}
			} else {
				LOGW("[MP] Failed to get server endpoints");
			}
#else
			struct ifaddrs* ifAddrStruct = nullptr;
			if (getifaddrs(&ifAddrStruct) == 0) {
				for (struct ifaddrs* ifa = ifAddrStruct; ifa != nullptr; ifa = ifa->ifa_next) {
					if (ifa->ifa_addr == nullptr) {
						continue;
					}

					if (ifa->ifa_addr->sa_family == AF_INET) { // IPv4
						auto* addrPtr = &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr;
						String addressString = AddressToString(*addrPtr, _host->address.port);
						if (!addressString.empty() && !addressString.hasPrefix("127.0.0.1:"_s)) {
							arrayAppend(result, std::move(addressString));
						}
					}
#	if ENET_IPV6
					else if (ifa->ifa_addr->sa_family == AF_INET6) { // IPv6
						auto* addrPtr = &((struct sockaddr_in6*)ifa->ifa_addr)->sin6_addr;
						//auto scopeId = ((struct sockaddr_in6*)ifa->ifa_addr)->sin6_scope_id;
						String addressString = AddressToString(*addrPtr, /*scopeId*/0, _host->address.port);
						if (!addressString.empty() && !addressString.hasPrefix("[::1]:"_s)) {
							arrayAppend(result, std::move(addressString));
						}
					}
#	endif
				}
				freeifaddrs(ifAddrStruct);
			} else {
				LOGW("[MP] Failed to get server endpoints");
			}
#endif
		} else {
			if (!_peers.empty()) {
				String addressString = AddressToString(_peers[0]->address, true);
				if (!addressString.empty() && !addressString.hasPrefix("[::1]:"_s)) {
					arrayAppend(result, std::move(addressString));
				}
			}
		}

		return result;
	}

	std::uint16_t NetworkManagerBase::GetServerPort() const
	{
		if (_state == NetworkState::Listening) {
			return _host->address.port;
		} else if (!_peers.empty()) {
			return _peers[0]->address.port;
		} else {
			return 0;
		}
	}

	void NetworkManagerBase::SendTo(const Peer& peer, NetworkChannel channel, std::uint8_t packetType, ArrayView<const std::uint8_t> data)
	{
		ENetPeer* target;
		if (peer == nullptr) {
			if (_state != NetworkState::Connected || _peers.empty()) {
				return;
			}
			target = _peers[0];
		} else {
			target = peer._enet;
		}

		enet_uint32 flags;
		if (channel == NetworkChannel::Main) {
			flags = ENET_PACKET_FLAG_RELIABLE;
		} else {
			flags = ENET_PACKET_FLAG_UNSEQUENCED;
		}

		ENetPacket* packet = enet_packet_create(packetType, data.data(), data.size(), flags);

		bool success;
		{
			std::unique_lock lock(_lock);
			success = enet_peer_send(target, std::uint8_t(channel), packet) >= 0;
			if (success && channel == NetworkChannel::UnreliableUpdates) {
				enet_host_flush(_host);
			}
		}

		if (!success) {
			enet_packet_destroy(packet);
		}
	}

	void NetworkManagerBase::SendTo(Function<bool(const Peer&)>&& predicate, NetworkChannel channel, std::uint8_t packetType, ArrayView<const std::uint8_t> data)
	{
		if (_peers.empty()) {
			return;
		}

		enet_uint32 flags;
		if (channel == NetworkChannel::Main) {
			flags = ENET_PACKET_FLAG_RELIABLE;
		} else {
			flags = ENET_PACKET_FLAG_UNSEQUENCED;
		}

		ENetPacket* packet = enet_packet_create(packetType, data.data(), data.size(), flags);

		bool success = false;
		{
			std::unique_lock lock(_lock);
			for (ENetPeer* peer : _peers) {
				if (predicate(Peer(peer))) {
					if (enet_peer_send(peer, std::uint8_t(channel), packet) >= 0) {
						success = true;
					}
				}
			}
			if (success && channel == NetworkChannel::UnreliableUpdates) {
				enet_host_flush(_host);
			}
		}

		if (!success) {
			enet_packet_destroy(packet);
		}
	}

	void NetworkManagerBase::SendTo(AllPeersT, NetworkChannel channel, std::uint8_t packetType, ArrayView<const std::uint8_t> data)
	{
		if (_peers.empty()) {
			return;
		}

		enet_uint32 flags;
		if (channel == NetworkChannel::Main) {
			flags = ENET_PACKET_FLAG_RELIABLE;
		} else {
			flags = ENET_PACKET_FLAG_UNSEQUENCED;
		}

		ENetPacket* packet = enet_packet_create(packetType, data.data(), data.size(), flags);

		bool success = false;
		{
			std::unique_lock lock(_lock);
			for (ENetPeer* peer : _peers) {
				if (enet_peer_send(peer, std::uint8_t(channel), packet) >= 0) {
					success = true;
				}
			}
			if (success && channel == NetworkChannel::UnreliableUpdates) {
				enet_host_flush(_host);
			}
		}

		if (!success) {
			enet_packet_destroy(packet);
		}
	}

	void NetworkManagerBase::Kick(const Peer& peer, Reason reason)
	{
		if (peer != nullptr) {
			std::unique_lock lock(_lock);
			enet_peer_disconnect(peer._enet, std::uint32_t(reason));
		}
	}

	String NetworkManagerBase::AddressToString(const struct in_addr& address, std::uint16_t port)
	{
		char addressString[64];

		if (inet_ntop(AF_INET, &address, addressString, sizeof(addressString) - 1) == NULL) {
			return {};
		}

		std::size_t addressLength = strnlen(addressString, sizeof(addressString));
		if (port != 0) {
			addressLength = addressLength + formatString(&addressString[addressLength], sizeof(addressString) - addressLength, ":%u", port);
		}
		return String(addressString, addressLength);
	}

#if ENET_IPV6
	String NetworkManagerBase::AddressToString(const struct in6_addr& address, std::uint16_t scopeId, std::uint16_t port)
	{
		char addressString[92];
		std::size_t addressLength = 0;

		if (IN6_IS_ADDR_V4MAPPED(&address)) {
			struct in_addr buf;
			enet_inaddr_map6to4(&address, &buf);

			if (inet_ntop(AF_INET, &buf, addressString, sizeof(addressString) - 1) == NULL) {
				return {};
			}

			addressLength = strnlen(addressString, sizeof(addressString));
		} else {
			if (inet_ntop(AF_INET6, (void*)&address, &addressString[1], sizeof(addressString) - 3) == NULL) {
				return {};
			}

			addressString[0] = '[';
			addressLength = strnlen(addressString, sizeof(addressString));

			if (scopeId != 0) {
				addressLength += formatString(&addressString[addressLength], sizeof(addressString) - addressLength, "%%%u", scopeId);
			}

			addressString[addressLength] = ']';
			addressLength++;
		}

		if (port != 0) {
			addressLength += formatString(&addressString[addressLength], sizeof(addressString) - addressLength, ":%u", port);
		}
		return String(addressString, addressLength);
	}
#endif

	String NetworkManagerBase::AddressToString(const ENetAddress& address, bool includePort)
	{
#if ENET_IPV6
		return AddressToString(address.host, address.sin6_scope_id, includePort ? address.port : 0);
#else
		return AddressToString(*(const struct in_addr*)&address.host, includePort ? address.port : 0);
#endif
	}

	String NetworkManagerBase::AddressToString(const Peer& peer)
	{
		if (peer._enet != nullptr) {
			return AddressToString(peer._enet->address);
		}

		return {};
	}

	bool NetworkManagerBase::IsAddressValid(StringView address)
	{
		auto nullTerminatedAddress = String::nullTerminatedView(address);
#if ENET_IPV6
		struct sockaddr_in sa;
		struct sockaddr_in6 sa6;
		return (inet_pton(AF_INET6, nullTerminatedAddress.data(), &(sa6.sin6_addr)) == 1)
			|| (inet_pton(AF_INET, nullTerminatedAddress.data(), &(sa.sin_addr)) == 1);
#else
		struct sockaddr_in sa;
		return (inet_pton(AF_INET, nullTerminatedAddress.data(), &(sa.sin_addr)) == 1);
#endif
	}

	bool NetworkManagerBase::IsDomainValid(StringView domain)
	{
		if (domain.empty() || domain.size() > 253) {
			return false;
		}

		while (!domain.empty()) {
			StringView end = domain.findOr('.', domain.end());
			StringView part = domain.prefix(end.begin());
			if (part.size() < 1 || part.size() > 63) {
				return false;
			}

			for (char c : part) {
				if (!isalnum(c) && c != '-') {
					return false;
				}
			}

			// Part can't start or end with hyphen
			if (part[0] == '-' || part[part.size() - 1] == '-') {
				return false;
			}

			if (end.begin() == domain.end()) {
				break;
			}
			domain = domain.suffix(end.begin() + 1);
		}

		return true;
	}

	bool NetworkManagerBase::TrySplitAddressAndPort(StringView input, StringView& address, std::uint16_t& port)
	{
		if (auto portSep = input.findLast(':')) {
			auto portString = input.suffix(portSep.begin() + 1);
			if (portString.contains(']')) {
				// Probably only IPv6 address (or some garbage)
				address = input;
				port = 0;
				return true;
			} else {
				// Address (or hostname) and port
				address = input.prefix(portSep.begin()).trimmed();
				if (address.hasPrefix('[') && address.hasSuffix(']')) {
					address = address.slice(1, address.size() - 1);
				}
				if (address.empty()) {
					return false;
				}

				auto portString = input.suffix(portSep.begin() + 1);
				port = std::uint16_t(stou32(portString.data(), portString.size()));
				return true;
			}
		} else {
			// Address (or hostname) only
			address = input.trimmed();
			if (address.hasPrefix('[') && address.hasSuffix(']')) {
				address = address.slice(1, address.size() - 1);
			}
			if (address.empty()) {
				return false;
			}

			port = 0;
			return true;
		}
	}

	const char* NetworkManagerBase::ReasonToString(Reason reason)
	{
		switch (reason) {
			case Reason::Disconnected: return "Client disconnected by user"; break;
			case Reason::InvalidParameter: return "Invalid parameter specified"; break;
			case Reason::IncompatibleVersion: return "Incompatible client version"; break;
			case Reason::AuthFailed: return "Authentication failed"; break;
			case Reason::InvalidPassword: return "Invalid password specified"; break;
			case Reason::InvalidPlayerName: return "Invalid player name specified"; break;
			case Reason::NotInWhitelist: return "Client is not in server whitelist"; break;
			case Reason::Requires3rdPartyAuthProvider: return "Server requires 3rd party authentication provider"; break;
			case Reason::ServerIsFull: return "Server is full or busy"; break;
			case Reason::ServerNotReady: return "Server is not ready yet"; break;
			case Reason::ServerStopped: return "Server is stopped for unknown reason"; break;
			case Reason::ServerStoppedForMaintenance: return "Server is stopped for maintenance"; break;
			case Reason::ServerStoppedForReconfiguration: return "Server is stopped for reconfiguration"; break;
			case Reason::ServerStoppedForUpdate: return "Server is stopped for update"; break;
			case Reason::ConnectionLost: return "Connection lost"; break;
			case Reason::ConnectionTimedOut: return "Connection timed out"; break;
			case Reason::Kicked: return "Kicked by server"; break;
			case Reason::Banned: return "Banned by server"; break;
			case Reason::CheatingDetected: return "Cheating detected"; break;
			case Reason::AssetStreamingNotAllowed: return "Downloading of assets is not allowed"; break;
			case Reason::Idle: return "Inactivity"; break;
			default: return "Unknown reason"; break;
		}
	}

	ConnectionResult NetworkManagerBase::OnPeerConnected(const Peer& peer, std::uint32_t clientData)
	{
		return _handler->OnPeerConnected(peer, clientData);
	}

	void NetworkManagerBase::OnPeerDisconnected(const Peer& peer, Reason reason)
	{
		_handler->OnPeerDisconnected(peer, reason);

		if (peer && _state == NetworkState::Listening) {
			std::unique_lock lock(_lock);
			for (std::size_t i = 0; i < _peers.size(); i++) {
				if (peer == _peers[i]) {
					_peers.eraseUnordered(i);
					break;
				}
			}
		}
	}

	void NetworkManagerBase::InitializeBackend()
	{
		if (++_initializeCount == 1) {
			std::int32_t error = enet_initialize();
			RETURN_ASSERT_MSG(error == 0, "enet_initialize() failed with error %i", error);
		}
	}

	void NetworkManagerBase::ReleaseBackend()
	{
		if (--_initializeCount == 0) {
			enet_deinitialize();
		}
	}

	void NetworkManagerBase::OnClientThread(void* param)
	{
		Thread::SetCurrentName("Multiplayer client");

		NetworkManagerBase* _this = static_cast<NetworkManagerBase*>(param);
		INetworkHandler* handler = _this->_handler;

		ENetHost* host = nullptr;
		_this->_host = host;

		// Try to connect to each specified endpoint
		ENetEvent ev{};
		for (std::int32_t i = 0; i < std::int32_t(_this->_desiredEndpoints.size()) && _this->_state != NetworkState::None; i++) {
			ENetAddress& addr = _this->_desiredEndpoints[i];
			LOGI("[MP] Connecting to %s (%i/%i)", AddressToString(addr, true).data(), addr.port, i + 1, std::int32_t(_this->_desiredEndpoints.size()));
			
			if (host != nullptr) {
				enet_host_destroy(host);
			}

			host = enet_host_create(nullptr, 1, std::size_t(NetworkChannel::Count), 0, 0);
			_this->_host = host;
			if (host == nullptr) {
				LOGE("[MP] Failed to create client");
				_this->OnPeerDisconnected({}, Reason::InvalidParameter);
				return;
			}

			ENetPeer* peer = enet_host_connect(host, &addr, std::size_t(NetworkChannel::Count), _this->_clientData);
			if (peer == nullptr) {
				continue;
			}

			std::int32_t n = 10;
			while (n > 0) {
				if (_this->_state == NetworkState::None) {
					n = 0;
					break;
				}

				LOGD("enet_host_service() is trying to connect: %u", enet_time_get());
				if (enet_host_service(host, &ev, 1000) > 0 && ev.type == ENET_EVENT_TYPE_CONNECT) {
					break;
				}

				n--;
			}

			if (n != 0) {
				_this->_peers.push_back(ev.peer);
				break;
			}
		}

		Reason reason;
		if (_this->_peers.empty()) {
			LOGE("[MP] Failed to connect to the server");
			_this->_state = NetworkState::None;
			reason = Reason::ConnectionTimedOut;
		} else {
			_this->_state = NetworkState::Connected;
			_this->OnPeerConnected(ev.peer, ev.data);
			reason = Reason::Unknown;

			while DEATH_LIKELY(_this->_state != NetworkState::None) {
				std::int32_t result;
				{
					std::unique_lock lock(_this->_lock);
					result = enet_host_service(host, &ev, 0);
				}

				if DEATH_UNLIKELY(result <= 0) {
					if DEATH_UNLIKELY(result < 0) {
						LOGE("[MP] enet_host_service() returned %i", result);
						reason = Reason::ConnectionLost;
						break;
					}
					Thread::Sleep(ProcessingIntervalMs);
					continue;
				}

				switch (ev.type) {
					case ENET_EVENT_TYPE_RECEIVE: {
						auto data = arrayView(ev.packet->data, ev.packet->dataLength);
						handler->OnPacketReceived(ev.peer, ev.channelID, data[0], data.exceptPrefix(1));
						enet_packet_destroy(ev.packet);
						break;
					}
					case ENET_EVENT_TYPE_DISCONNECT:
						_this->_state = NetworkState::None;
						reason = (Reason)ev.data;
						break;
					case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
						_this->_state = NetworkState::None;
						reason = Reason::ConnectionLost;
						break;
				}
			}
		}

		if (!_this->_peers.empty()) {
			_this->OnPeerDisconnected(_this->_peers[0], reason);

			for (ENetPeer* peer : _this->_peers) {
				enet_peer_disconnect_now(peer, (std::uint32_t)Reason::Disconnected);
			}
			_this->_peers.clear();
		} else {
			_this->OnPeerDisconnected({}, reason);
		}

		enet_host_destroy(_this->_host);
		_this->_host = nullptr;
		_this->_handler = nullptr;

		_this->_thread.Detach();

		LOGD("[MP] Client thread exited: %s (%u)", NetworkManagerBase::ReasonToString(reason), (std::uint32_t)reason);
	}

	void NetworkManagerBase::OnServerThread(void* param)
	{
		Thread::SetCurrentName("Multiplayer server");

		NetworkManagerBase* _this = static_cast<NetworkManagerBase*>(param);
		INetworkHandler* handler = _this->_handler;
		ENetHost* host = _this->_host;

		_this->_peers.reserve(16);

		ENetEvent ev{};
		while DEATH_LIKELY(_this->_state != NetworkState::None) {
			std::int32_t result;
			{
				std::unique_lock lock(_this->_lock);
				result = enet_host_service(host, &ev, 0);
			}

			if DEATH_UNLIKELY(result <= 0) {
				if DEATH_UNLIKELY(result < 0) {
					LOGE("[MP] enet_host_service() returned %i", result);

					// Server failed, try to recreate it
					for (auto& peer : _this->_peers) {
						_this->OnPeerDisconnected(peer, Reason::ConnectionLost);
					}
					_this->_peers.clear();

					ENetAddress addr = host->address;
					{
						std::unique_lock lock(_this->_lock);
						enet_host_destroy(host);
						host = enet_host_create(&addr, MaxPeerCount, std::size_t(NetworkChannel::Count), 0, 0);
						_this->_host = host;
					}

					if (host == nullptr) {
						LOGE("[MP] Failed to recreate the server");
						break;
					}
				}
				Thread::Sleep(ProcessingIntervalMs);
				continue;
			}

			switch (ev.type) {
				case ENET_EVENT_TYPE_CONNECT: {
					ConnectionResult result = _this->OnPeerConnected(ev.peer, ev.data);
					if DEATH_LIKELY(result.IsSuccessful()) {
						std::unique_lock lock(_this->_lock);
						bool alreadyExists = false;
						for (std::size_t i = 0; i < _this->_peers.size(); i++) {
							if (ev.peer == _this->_peers[i]) {
								alreadyExists = true;
								break;
							}
						}
						if DEATH_UNLIKELY(alreadyExists) {
							LOGW("Peer is already connected [%08llx]", (std::uint64_t)ev.peer);
						} else {
							_this->_peers.push_back(ev.peer);
						}
					} else {
						std::unique_lock lock(_this->_lock);
						enet_peer_disconnect(ev.peer, std::uint32_t(result.FailureReason));
					}
					break;
				}
				case ENET_EVENT_TYPE_RECEIVE: {
					auto data = arrayView(ev.packet->data, ev.packet->dataLength);
					handler->OnPacketReceived(ev.peer, ev.channelID, data[0], data.exceptPrefix(1));
					enet_packet_destroy(ev.packet);
					break;
				}
				case ENET_EVENT_TYPE_DISCONNECT:
					_this->OnPeerDisconnected(ev.peer, Reason(ev.data));
					break;
				case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
					_this->OnPeerDisconnected(ev.peer, Reason::ConnectionLost);
					break;
			}
		}

		for (ENetPeer* peer : _this->_peers) {
			enet_peer_disconnect_now(peer, std::uint32_t(Reason::ServerStopped));
		}
		_this->_peers.clear();

		enet_host_destroy(_this->_host);
		_this->_host = nullptr;
		_this->_handler = nullptr;

		_this->_thread.Detach();

		LOGD("[MP] Server thread exited");
	}
}

#endif