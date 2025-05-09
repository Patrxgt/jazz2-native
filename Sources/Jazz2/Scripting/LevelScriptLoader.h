﻿#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "ScriptLoader.h"
#include "JJ2PlusDefinitions.h"
#include "../ILevelHandler.h"
#include "../../nCine/Base/BitArray.h"

namespace Jazz2::UI
{
	class HUD;
}

namespace Jazz2::Scripting
{
	namespace Legacy
	{
		class jjPLAYER;
	}

	/** @brief Specifies a part to be drawn in @ref LevelScriptLoader::OnDraw() */
	enum class DrawType
	{
		WeaponAmmo,
		Health,
		Lives,
		PlayerTimer,
		Score,
		GameModeHUD
	};

	/**
		@brief Binds an **AngelScript** script file to @ref LevelHandler and executes the script
		
		@experimental
	*/
	class LevelScriptLoader : public ScriptLoader
	{
		friend class Legacy::jjPLAYER;

	public:
		LevelScriptLoader(LevelHandler* levelHandler, StringView scriptPath);

		/** @brief Returns list of players */
		ArrayView<Actors::Player* const> GetPlayers() const;

		/** @brief Returns script backing store for specified player */
		jjPLAYER* GetPlayerBackingStore(Actors::Player* player);
		/** @overload */
		jjPLAYER* GetPlayerBackingStore(std::int32_t playerIndex);

		/** @brief Called when a level is loaded */
		void OnLevelLoad();
		/** @brief Called when a level begins */
		void OnLevelBegin();
		/** @brief Called when a level reloads */
		void OnLevelReload();
		/** @brief Called when a level updates (on the beginning of each frame) */
		void OnLevelUpdate(float timeMult);
		/** @brief Called when a level callback is triggered */
		void OnLevelCallback(Actors::ActorBase* initiator, std::uint8_t* eventParams);
		/** @brief Called when a part of viewport or HUD is drawn */
		bool OnDraw(UI::HUD* hud, Actors::Player* player, const Rectf& view, DrawType type);
		/** @brief Called when a player dies */
		void OnPlayerDied(Actors::Player* player, Actors::ActorBase* collider);

	protected:
		String OnProcessInclude(StringView includePath, StringView scriptPath) override;
		void OnProcessPragma(StringView content, ScriptContextType& contextType) override;

		/** @brief Called before a script function is called */
		void OnBeforeScriptCall();
		/** @brief Called after a script function is called */
		void OnAfterScriptCall();

	private:
		LevelHandler* _levelHandler;
		asIScriptFunction* _onLevelUpdate;
		std::int32_t _onLevelUpdateLastFrame;
		asIScriptFunction* _onDrawAmmo;
		asIScriptFunction* _onDrawHealth;
		asIScriptFunction* _onDrawLives;
		asIScriptFunction* _onDrawPlayerTimer;
		asIScriptFunction* _onDrawScore;
		asIScriptFunction* _onDrawGameModeHUD;
		HashMap<std::int32_t, asITypeInfo*> _eventTypeToTypeInfo;
		BitArray _enabledCallbacks;
		HashMap<std::uint8_t, std::unique_ptr<jjPLAYER>> _playerBackingStore;

		// Global scripting variables
		static constexpr std::int32_t FLAG_HFLIPPED_TILE = 0x1000;
		static constexpr std::int32_t FLAG_VFLIPPED_TILE = 0x2000;
		static constexpr std::int32_t FLAG_ANIMATED_TILE = 0x4000;

		jjPAL jjPalette;
		jjPAL jjBackupPalette;

		std::int32_t jjObjectCount = 0;
		std::int32_t jjObjectMax = 0;

		std::int32_t gameMode = 0;
		std::int32_t customMode = 0;
		std::int32_t partyMode = 0;

		bool jjAutoWeaponChange = true;
		std::uint32_t jjScriptModuleID = 0;
		std::uint32_t gameTicksSpentWhileActive = 0;
		std::int32_t renderFrame = 0;

		bool versionTSF = true;
		bool isServer = false;
		bool jjDeactivatingBecauseOfDeath = false;

		std::int32_t DifficultyForNextLevel = 0;
		std::int32_t DifficultyAtLevelStart = 0;

		std::uint32_t numberOfTiles = 0;

		bool parLowDetail = false;
		std::int32_t colorDepth = 0;
		std::int32_t checkedMaxSubVideoWidth = 0;
		std::int32_t checkedMaxSubVideoHeight = 0;
		std::int32_t realVideoW = 0;
		std::int32_t realVideoH = 0;
		std::int32_t subVideoW = 0;
		std::int32_t subVideoH = 0;

		bool snowing = false;
		bool snowingOutdoors = false;
		std::uint8_t snowingIntensity = 0;
		std::int32_t snowingType = 0;

		std::int32_t maxScore = 0;

		std::int32_t waterLightMode = 0;
		std::int32_t waterInteraction = 0;

		std::uint8_t ChatKey = 0;

		bool soundEnabled = false;
		bool soundFXActive = false;
		bool musicActive = false;
		std::int32_t soundFXVolume = false;
		std::int32_t musicVolume = false;
		std::int32_t levelEcho = 0;

		bool warpsTransmuteCoins = false;
		bool delayGeneratedCrateOrigins = false;

		bool g_levelHasFood = false;
		std::int32_t enforceAmbientLighting = 0;

		LevelScriptLoader(const LevelScriptLoader&) = delete;
		LevelScriptLoader& operator=(const LevelScriptLoader&) = delete;

		Actors::ActorBase* CreateActorInstance(StringView typeName);

		static void RegisterBuiltInFunctions(asIScriptEngine* engine);
		void RegisterLegacyFunctions(asIScriptEngine* engine);
		void RegisterStandardFunctions(asIScriptEngine* engine, asIScriptModule* module);

		static std::uint8_t asGetDifficulty();
		static bool asIsReforged();
		static std::int32_t asGetLevelWidth();
		static std::int32_t asGetLevelHeight();
		static float asGetElapsedFrames();
		static float asGetAmbientLight();
		static void asSetAmbientLight(float value);
		static float asGetWaterLevel();
		static void asSetWaterLevel(float value);

		static void asPreloadMetadata(const String& path);
		static void asRegisterSpawnable(std::int32_t eventType, const String& typeName);
		static std::shared_ptr<Actors::ActorBase> asRegisterSpawnableCallback(const Actors::ActorActivationDetails& details);
		static void asSpawnEvent(std::int32_t eventType, std::int32_t x, std::int32_t y);
		static void asSpawnEventParams(std::int32_t eventType, std::int32_t x, std::int32_t y, const CScriptArray& eventParams);
		static void asSpawnType(const String& typeName, std::int32_t x, std::int32_t y);
		static void asSpawnTypeParams(const String& typeName, std::int32_t x, std::int32_t y, const CScriptArray& eventParams);

		static void asChangeLevel(std::int32_t exitType, const String& path);
		static void asShowLevelText(const String& text);
		static void asSetWeather(std::uint8_t weatherType, std::uint8_t intensity);

		static std::int32_t GetDifficulty();
		static std::int32_t SetDifficulty(std::int32_t value);

		static String get_jjMusicFileName();

		static String get_jjHelpStrings(std::uint32_t index);
		static void set_jjHelpStrings(std::uint32_t index, const String& text);

		static void jjAlert(const String& text, bool sendToAll, std::uint32_t size);
		static void jjPrint(const String& text, bool timestamp);
		static void jjDebug(const String& text, bool timestamp);
		static void jjChat(const String& text, bool teamchat);
		static void jjConsole(const String& text, bool sendToAll);
		static void jjSpy(const String& text);

		static float get_layerXOffset(std::uint8_t id);
		static float set_layerXOffset(std::uint8_t id, float value);
		static float get_layerYOffset(std::uint8_t id);
		static float set_layerYOffset(std::uint8_t id, float value);
		static std::int32_t get_layerWidth(std::uint8_t id);
		static std::int32_t get_layerRealWidth(std::uint8_t id);
		static std::int32_t get_layerRoundedWidth(std::uint8_t id);
		static std::int32_t get_layerHeight(std::uint8_t id);
		static float get_layerXSpeed(std::uint8_t id);
		static float set_layerXSpeed(std::uint8_t id, float value);
		static float get_layerYSpeed(std::uint8_t id);
		static float set_layerYSpeed(std::uint8_t id, float value);
		static float get_layerXAutoSpeed(std::uint8_t id);
		static float set_layerXAutoSpeed(std::uint8_t id, float value);
		static float get_layerYAutoSpeed(std::uint8_t id);
		static float set_layerYAutoSpeed(std::uint8_t id, float value);
		static bool get_layerHasTiles(std::uint8_t id);
		static bool set_layerHasTiles(std::uint8_t id, bool value);
		static bool get_layerTileHeight(std::uint8_t id);
		static bool set_layerTileHeight(std::uint8_t id, bool value);
		static bool get_layerTileWidth(std::uint8_t id);
		static bool set_layerTileWidth(std::uint8_t id, bool value);
		static bool get_layerLimitVisibleRegion(std::uint8_t id);
		static bool set_layerLimitVisibleRegion(std::uint8_t id, bool value);

		static void setLayerXSpeedSeamlessly(std::uint8_t id, float newspeed, bool newSpeedIsAnAutoSpeed);
		static void setLayerYSpeedSeamlessly(std::uint8_t id, float newspeed, bool newSpeedIsAnAutoSpeed);

		static void jjDrawPixel(float xPixel, float yPixel, std::uint8_t color, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
		static void jjDrawRectangle(float xPixel, float yPixel, std::int32_t width, std::int32_t height, std::uint8_t color, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
		static void jjDrawSprite(float xPixel, float yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, std::int8_t direction, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
		static void jjDrawSpriteFromCurFrame(float xPixel, float yPixel, std::uint32_t sprite, std::int8_t direction, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
		static void jjDrawResizedSprite(float xPixel, float yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, float xScale, float yScale, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
		static void jjDrawResizedSpriteFromCurFrame(float xPixel, float yPixel, std::uint32_t sprite, float xScale, float yScale, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
		static void jjDrawRotatedSprite(float xPixel, float yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, std::int32_t angle, float xScale, float yScale, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
		static void jjDrawRotatedSpriteFromCurFrame(float xPixel, float yPixel, std::uint32_t sprite, std::int32_t angle, float xScale, float yScale, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
		static void jjDrawSwingingVineSpriteFromCurFrame(float xPixel, float yPixel, std::uint32_t sprite, std::int32_t length, std::int32_t curvature, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
		static void jjDrawTile(float xPixel, float yPixel, std::uint16_t tile, std::uint32_t tileQuadrant, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
		static void jjDrawString(float xPixel, float yPixel, const String& text, std::uint32_t size, std::uint32_t mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
		static void jjDrawStringEx(float xPixel, float yPixel, const String& text, std::uint32_t size, const jjTEXTAPPEARANCE& appearance, std::uint8_t param1, spriteType spriteMode, std::uint8_t param2, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
		static std::int32_t jjGetStringWidth(const String& text, std::uint32_t size, const jjTEXTAPPEARANCE& style);

		static bool get_jjTriggers(std::uint8_t id);
		static bool set_jjTriggers(std::uint8_t id, bool value);
		static bool jjSwitchTrigger(std::uint8_t id);

		static bool isNumberedASFunctionEnabled(std::uint8_t id);
		static bool setNumberedASFunctionEnabled(std::uint8_t id, bool value);
		static void reenableAllNumberedASFunctions();

		static float getWaterLevel();
		static float getWaterLevel2();
		static float setWaterLevel(float value, bool instant);
		static float get_waterChangeSpeed();
		static float set_waterChangeSpeed(float value);
		static std::int32_t get_waterLayer();
		static std::int32_t set_waterLayer(std::int32_t value);
		static void setWaterGradient(std::uint8_t red1, std::uint8_t green1, std::uint8_t blue1, std::uint8_t red2, std::uint8_t green2, std::uint8_t blue2);
		static void setWaterGradientFromColors(jjPALCOLOR color1, jjPALCOLOR color2);
		static void setWaterGradientToTBG();
		static void resetWaterGradient();
		static void triggerRock(std::uint8_t id);
		static void cycleTo(const String& filename, bool warp, bool fast);
		static void jjNxt(bool warp, bool fast);
		static bool jjMusicLoad(const String& filename, bool forceReload, bool temporary);
		static void jjMusicStop();
		static void jjMusicPlay();
		static void jjMusicPause();
		static void jjMusicResume();

		static bool jjSendPacket(const jjSTREAM& packet, std::int32_t toClientID, std::uint32_t toScriptModuleID);

	};
}

#endif