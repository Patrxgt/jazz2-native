﻿#pragma once

#include "Application.h"

namespace nCine
{
	/** @brief Native argument type, `wchar_t*` on Windows, otherwise `char*` */
#if defined(DEATH_TARGET_WINDOWS)
	typedef wchar_t* NativeArgument;
#else
	typedef char* NativeArgument;
#endif

#if defined(WITH_QT5)
	class Qt5Widget;
#endif

	/// Main entry point and handler for standard (desktop) applications
	class MainApplication : public Application
	{
	public:
		/** @brief Entry point method to be called in the `main()`/`wWinMain()` function */ 
		static int Run(CreateAppEventHandlerDelegate createAppEventHandler, int argc, NativeArgument* argv);

		void Quit() override;

		bool EnablePlayStationExtendedSupport(bool enable) override;
		String GetUserName() override;
		bool OpenUrl(StringView url) override;
		
		bool CanShowScreenKeyboard() override;
		bool ToggleScreenKeyboard() override;
		bool ShowScreenKeyboard() override;
		bool HideScreenKeyboard() override;

	private:
		bool wasSuspended_;

#if defined(WITH_QT5)
		Qt5Widget* qt5Widget_;
#endif

		/** @brief Must be called at the beginning to initialize the application */
		void Init(CreateAppEventHandlerDelegate createAppEventHandler, int argc, NativeArgument* argv);
		/** @brief Must be called continuously to keep the application running */
		void ProcessStep();
#if defined(WITH_GLFW) || defined(WITH_SDL)
		/** @brief Processes events inside the game loop */
		void ProcessEvents();
#endif
#if defined(DEATH_TARGET_EMSCRIPTEN)
		static void EmscriptenStep();
#endif

		MainApplication()
			:
			Application(),
#if defined(WITH_QT5)
			qt5Widget_(nullptr),
#endif
			wasSuspended_(false)
		{
		}

		~MainApplication() = default;

		MainApplication(const MainApplication&) = delete;
		MainApplication& operator=(const MainApplication&) = delete;

		friend Application& theApplication();
#if defined(WITH_QT5)
		friend class Qt5Widget;
#endif
	};

	/// Returns application instance
	Application& theApplication();

}
