#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENALC
#include "../CommonHeaders.h"
#endif

#include "IAudioDevice.h"

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
#	include <CommonWindows.h>
#	include <mmdeviceapi.h>
#	include <audiopolicy.h>
#endif

#include <Containers/SmallVector.h>
#include <Containers/String.h>

using namespace Death::Containers;

namespace nCine
{
	/// OpenAL audio device backend implementation
	class ALAudioDevice : public IAudioDevice
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		, public IMMNotificationClient
#endif
	{
	public:
		ALAudioDevice();
		~ALAudioDevice() override;

		ALAudioDevice(const ALAudioDevice&) = delete;
		ALAudioDevice& operator=(const ALAudioDevice&) = delete;

		bool isValid() const override;

		const char* name() const override;

		float gain() const override {
			return gain_;
		}
		void setGain(float gain) override;

		inline std::uint32_t maxNumPlayers() const override {
			return MaxSources;
		}
		inline std::uint32_t numPlayers() const override {
			return std::uint32_t(players_.size());
		}
		const IAudioPlayer* player(unsigned int index) const override;

		void stopPlayers() override;
		void pausePlayers() override;
		void stopPlayers(PlayerType playerType) override;
		void pausePlayers(PlayerType playerType) override;

		void freezePlayers() override;
		void unfreezePlayers() override;

		std::uint32_t registerPlayer(IAudioPlayer* player) override;
		void unregisterPlayer(IAudioPlayer* player) override;
		void updatePlayers() override;
		
		const Vector3f& getListenerPosition() const override;
		void updateListener(const Vector3f& position, const Vector3f& velocity) override;

		std::int32_t nativeFrequency() override;

		void suspendDevice() override;
		void resumeDevice() override;

	private:
		/// Maximum number of OpenAL sources
#if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_EMSCRIPTEN) || defined(DEATH_TARGET_IOS)
		static const std::int32_t MaxSources = 32;
#else
		static const std::int32_t MaxSources = 64;
#endif

		/// The OpenAL device
		ALCdevice* device_;
		/// The OpenAL context for the device
		ALCcontext* context_;
		/// The listener gain value (master volume)
		ALfloat gain_;
		/// The array of all audio sources
		ALuint sources_[MaxSources];
		/// The array of currently inactive audio sources
		SmallVector<ALuint, MaxSources> sourcePool_;
		/// The array of currently active audio players
		SmallVector<IAudioPlayer*, MaxSources> players_;
		/// Listener position
		Vector3f _listenerPos;
		/// native device frequency
		std::int32_t nativeFreq_;

		/// The OpenAL device name string
		const char* deviceName_;

		void Init();

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		static constexpr std::uint64_t DeviceChangeLimitMs = 250;

		LPALCREOPENDEVICESOFT alcReopenDeviceSOFT_;
		IMMDeviceEnumerator* pEnumerator_;
		std::uint64_t lastDeviceChangeTime_;
		String lastDeviceId_;
		bool shouldRecreate_;

		void recreateAudioDevice();
		void registerAudioEvents();
		void unregisterAudioEvents();

		// IMMNotificationClient implementation
		IFACEMETHODIMP_(ULONG) AddRef() override;
		IFACEMETHODIMP_(ULONG) Release() override;
		IFACEMETHODIMP QueryInterface(REFIID iid, void** object) override;
		IFACEMETHODIMP OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) override;
		IFACEMETHODIMP OnDeviceAdded(LPCWSTR pwstrDeviceId) override;
		IFACEMETHODIMP OnDeviceRemoved(LPCWSTR pwstrDeviceId) override;
		IFACEMETHODIMP OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) override;
		IFACEMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) override;
#endif
	};

}
