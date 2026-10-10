#include "pch.h"
#include "Engine.h"
#include "Overlay.h"

void SaveSettings();
void LoadSettings();

void** vTable = nullptr;

using tProcessEvent = void(*)(const UObject*, UFunction*, void*);
tProcessEvent oProcessEvent = nullptr;

inline tProcessEvent oPlayerProcessEvent = nullptr;
bool PlayerHooked = false;
static std::atomic<int> ActiveProcessEventHooks{ 0 };

namespace
{
	bool IsReceiveTickFunction(const UFunction* Function)
	{
		if (!Function)
			return false;

		static std::atomic<uint64_t> ReceiveTickName{ UINT64_MAX };
		const uint64_t FunctionName =
			(static_cast<uint64_t>(static_cast<uint32_t>(Function->Name.ComparisonIndex)) << 32) |
			Function->Name.Number;
		const uint64_t CachedName = ReceiveTickName.load(std::memory_order_relaxed);
		if (CachedName != UINT64_MAX)
			return CachedName == FunctionName;

		if (Function->Name.ToString().find("ReceiveTick") == std::string::npos)
			return false;

		uint64_t Uncached = UINT64_MAX;
		ReceiveTickName.compare_exchange_strong(
			Uncached, FunctionName, std::memory_order_relaxed);
		return true;
	}

	struct ProcessEventHookGuard
	{
		ProcessEventHookGuard()
		{
			ActiveProcessEventHooks.fetch_add(1);
		}

		~ProcessEventHookGuard()
		{
			ActiveProcessEventHooks.fetch_sub(1);
		}
	};
}

void hkPlayerProcessEvent(const SDK::UObject* Object, SDK::UFunction* Function, void* Params)
{
	ProcessEventHookGuard Guard;
	if (!Object || !Function)
		return oPlayerProcessEvent(Object, Function, Params);

	if (IsReceiveTickFunction(Function))
	{
		std::lock_guard<std::recursive_mutex> SettingsLock(SettingsMutex);
		static auto NextVisualUpdate = std::chrono::steady_clock::time_point{};
		static auto NextPlayerSnapshot = std::chrono::steady_clock::time_point{};
		static auto NextAutoSave = std::chrono::steady_clock::now() + std::chrono::minutes(5);

		GVars.AutoSetVariables();
		Cheats::ChangeFOV();
		const auto Now = std::chrono::steady_clock::now();
		const bool UpdateVisuals = Now >= NextVisualUpdate;
		if (UpdateVisuals)
		{
			NextVisualUpdate = Now + std::chrono::milliseconds(50);
			Overlay::BeginFrame(
				static_cast<int>(GVars.ScreenSize.x),
				static_cast<int>(GVars.ScreenSize.y));
		}

		if (MiscSettings.ShouldAutoSave && Now >= NextAutoSave &&
			CVars.QueuedAction.load() == EQueuedAction::None)
		{
			CVars.QueuedAction = EQueuedAction::SaveConfiguration;
			NextAutoSave = Now + std::chrono::minutes(5);
		}
		AimbotKeyDown = AimbotSettings.AimbotVirtualKey == 0 ||
			(GetAsyncKeyState(AimbotSettings.AimbotVirtualKey) & 0x8000) != 0;
		if (CVars.TriggerBotHoldKey)
			CVars.TriggerBot = TriggerBotVirtualKey != 0 &&
			(GetAsyncKeyState(TriggerBotVirtualKey) & 0x8000) != 0;
		else if (TriggerBotVirtualKey && (GetAsyncKeyState(TriggerBotVirtualKey) & 1))
			CVars.TriggerBot = !CVars.TriggerBot;
		if (ESPKeyVK && (GetAsyncKeyState(ESPKeyVK) & 1))
			CVars.ESP = !CVars.ESP;

		if (UpdateVisuals)
		{
			if (CVars.RenderOptions)
				Cheats::RenderEnabledOptions();
			if (CVars.ListPlayers)
				Cheats::ListPlayers();
			if (CVars.ESP)
				Cheats::RenderESP();
			if (CVars.SilentAim)
			{
				if (SilentAimSettings.DrawFOV)
					Utils::DrawFOV(SilentAimSettings.MaxFOV, SilentAimSettings.FOVThickness);
				AActor* TargetActor = Utils::GetBestTarget(
					GVars.PlayerController,
					SilentAimSettings.TargetCivilians,
					SilentAimSettings.TargetArrested,
					SilentAimSettings.TargetSurrendered,
					SilentAimSettings.TargetDead,
					SilentAimSettings.MaxFOV,
					SilentAimSettings.RequiresLOS,
					TextVars.SilentAimBone,
					SilentAimSettings.TargetAll);
				if (TargetActor && SilentAimSettings.DrawArrow && TargetActor->IsA(AReadyOrNotCharacter::StaticClass()))
				{
					auto* TargetCharacter = reinterpret_cast<AReadyOrNotCharacter*>(TargetActor);
					if (TargetCharacter->Mesh)
					{
						const std::wstring WideBoneName = UtfN::StringToWString(TextVars.SilentAimBone);
						const FName BoneName = UKismetStringLibrary::Conv_StringToName(WideBoneName.c_str());
						const FVector TargetLocation = TargetCharacter->Mesh
							->GetBoneTransform(BoneName, ERelativeTransformSpace::RTS_World).Translation;
						Utils::DrawSnapLine(TargetLocation, SilentAimSettings.ArrowThickness);
					}
				}
			}
			if (CVars.Reticle)
				Cheats::DrawReticle();
		}
		if (CVars.Aimbot)
			Cheats::Aimbot(UpdateVisuals);

		if (Now >= NextPlayerSnapshot)
		{
			NextPlayerSnapshot = Now + std::chrono::seconds(1);
			std::vector<PlayerMenuEntry> PlayerSnapshot;
			if (GVars.GameState)
			{
				for (APlayerCharacter* Player : GVars.GameState->AllPlayerCharacters)
				{
					if (!Player || !Utils::IsValidActor(Player) || !Player->PlayerState)
						continue;
					PlayerMenuEntry Entry;
					Entry.Player = Player;
					Entry.Name = Player->PlayerState->GetPlayerName().ToString();
					Entry.CanModify = GVars.HasAuthority && Player != GVars.ReadyOrNotChar;
					const PlayerCheatData& PlayerSettings = Utils::GetPlayerCheats(Player);
					Entry.GodMode = PlayerSettings.GodMode;
					Entry.InfiniteAmmo = PlayerSettings.InfAmmo;
					PlayerSnapshot.push_back(std::move(Entry));
				}
			}
			{
				std::lock_guard<std::mutex> Lock(PlayerMenuMutex);
				PlayerMenuEntries = std::move(PlayerSnapshot);
			}
		}
		std::deque<PlayerMenuRequest> PlayerRequests;
		{
			std::lock_guard<std::mutex> Lock(PlayerMenuMutex);
			PlayerRequests.swap(PlayerMenuRequests);
		}
		for (const PlayerMenuRequest& Request : PlayerRequests)
		{
			APlayerCharacter* Player = Request.Player;
			if (!GVars.HasAuthority || !Player || !Utils::IsValidActor(Player))
				continue;
			switch (Request.Action)
			{
			case EPlayerMenuAction::GodMode:
			{
				PlayerCheatData& PlayerSettings = Utils::GetPlayerCheats(Player);
				PlayerSettings.GodMode = !PlayerSettings.GodMode;
				PlayerSettings.GodModeConfigured = true;
				Player->bGodMode = PlayerSettings.GodMode;
				break;
			}
			case EPlayerMenuAction::InfiniteAmmo:
			{
				PlayerCheatData& PlayerSettings = Utils::GetPlayerCheats(Player);
				PlayerSettings.InfAmmo = !PlayerSettings.InfAmmo;
				PlayerSettings.InfAmmoConfigured = true;
				break;
			}
			case EPlayerMenuAction::Teleport:
				if (GVars.ReadyOrNotChar)
				{
					const FVector Location = GVars.ReadyOrNotChar->K2_GetActorLocation();
					Player->Server_TeleportPlayerToLocation(Location, Location);
				}
				break;
			case EPlayerMenuAction::Speed:
				Player->Server_SetWalkSpeed(240.0f * 10, 1000);
				break;
			}
		}
		if (GVars.GameState)
		{
			for (APlayerCharacter* Player : GVars.GameState->AllPlayerCharacters)
			{
				if (!Player || !Utils::IsValidActor(Player))
					continue;
				const auto SettingsIt = PlayerCheatMap.find(Player);
				if (SettingsIt == PlayerCheatMap.end())
					continue;
				if (SettingsIt->second.GodModeConfigured &&
					Player->bGodMode != SettingsIt->second.GodMode)
					Player->bGodMode = SettingsIt->second.GodMode;

				if (!SettingsIt->second.InfAmmoConfigured)
					continue;
				ABaseMagazineWeapon* Weapon = Player->GetEquippedWeapon();
				if (!Weapon)
					continue;
				if (Weapon->bInfiniteAmmo != SettingsIt->second.InfAmmo)
				{
					Weapon->bInfiniteAmmo = SettingsIt->second.InfAmmo;
					if (SettingsIt->second.InfAmmo)
						Weapon->ReplenishAmmo();
				}
			}
		}

		switch (CVars.QueuedAction)
		{
		case EQueuedAction::GiveAchievements:
		{
			Cheats::GiveAchievements();
			CVars.QueuedAction = EQueuedAction::None;
			break;
		}
		case EQueuedAction::ToggleGodMode:
		{
			Cheats::ToggleGodMode();
			CVars.QueuedAction = EQueuedAction::None;
			break;
		}
		case EQueuedAction::ToggleInfAmmo:
		{
			Cheats::ToggleInfAmmo();
			CVars.QueuedAction = EQueuedAction::None;
			break;
		}
		case EQueuedAction::RemoveRecoil:
		{
			Cheats::RemoveRecoil();
			CVars.QueuedAction = EQueuedAction::None;
			break;
		}
		case EQueuedAction::RemoveSpread:
		{
			Cheats::RemoveSpread();
			CVars.QueuedAction = EQueuedAction::None;
			break;
		}
		case EQueuedAction::AddAutoFire:
		{
			Cheats::AddAutoFire();
			CVars.QueuedAction = EQueuedAction::None;
			break;
		}
		case EQueuedAction::AddPenetration:
		{
			Cheats::PenetrateWalls();
			CVars.QueuedAction = EQueuedAction::None;
			break;
		}
		case EQueuedAction::AddMagazine:
		{
			Cheats::AddMag();
			CVars.QueuedAction = EQueuedAction::None;
			break;
		}
		case EQueuedAction::SetFireRate:
		{
			Cheats::SetFireRate(3000.0f);
			CVars.QueuedAction = EQueuedAction::None;
			break;
		}
		case EQueuedAction::InstaKill:
		{
			Cheats::InstaKill();
			CVars.QueuedAction = EQueuedAction::None;
			break;
		}
		case EQueuedAction::KillAllSuspects:
		{
			Cheats::KillAll(ETeam::TEAM_SUSPECT);
			CVars.QueuedAction = EQueuedAction::None;
			break;
		}
		case EQueuedAction::SurrenderAllSuspects:
			Cheats::SurrenderAll(ETeam::TEAM_SUSPECT);
			CVars.QueuedAction = EQueuedAction::None;
			break;
		case EQueuedAction::ArrestAllSuspects:
			Cheats::ArrestAll(ETeam::TEAM_SUSPECT);
			CVars.QueuedAction = EQueuedAction::None;
			break;
		case EQueuedAction::KillAllCivilians:
			Cheats::KillAll(ETeam::TEAM_CIVILIAN);
			CVars.QueuedAction = EQueuedAction::None;
			break;
		case EQueuedAction::SurrenderAllCivilians:
			Cheats::SurrenderAll(ETeam::TEAM_CIVILIAN);
			CVars.QueuedAction = EQueuedAction::None;
			break;
		case EQueuedAction::ArrestAllCivilians:
			Cheats::ArrestAll(ETeam::TEAM_CIVILIAN);
			CVars.QueuedAction = EQueuedAction::None;
			break;
		case EQueuedAction::CollectAllEvidence:
			Cheats::GetAllEvidence();
			CVars.QueuedAction = EQueuedAction::None;
			break;
		case EQueuedAction::AutoWin:
			Cheats::AutoWin();
			CVars.QueuedAction = EQueuedAction::None;
			break;
		case EQueuedAction::UnlockAllDoors:
			Cheats::UnlockDoors();
			CVars.QueuedAction = EQueuedAction::None;
			break;
		case EQueuedAction::PrintActors:
			Utils::PrintActors(nullptr);
			CVars.QueuedAction = EQueuedAction::None;
			break;
		case EQueuedAction::ChangeFOV:
			Cheats::ChangeFOV();
			CVars.QueuedAction = EQueuedAction::None;
			break;
		case EQueuedAction::SaveConfiguration:
			SaveSettings();
			CVars.QueuedAction = EQueuedAction::None;
			break;
		case EQueuedAction::LoadConfiguration:
			LoadSettings();
			CVars.QueuedAction = EQueuedAction::None;
			break;
		}

		if (CVars.TriggerBot)
			Cheats::TriggerBot();

		Cheats::SetPlayerSpeed();

		Cheats::ToggleGodMode();
		Cheats::ToggleInfAmmo();
		Cheats::ApplyPersistentWeaponSettings();

		if (CVars.AntiSway)
			Cheats::AntiSway();

		Cheats::ProcessArrestQueue();

		if (CVars.BulletTime && GVars.World)
			GVars.World->K2_GetWorldSettings()->TimeDilation = CVars.BulletTimeSpeed; // Slow-mo
		else if (GVars.World)
			GVars.World->K2_GetWorldSettings()->TimeDilation = 1.0f; // Normal
	}

	oPlayerProcessEvent(Object, Function, Params);
}

void hkProcessEvent(const UObject* Object, UFunction* Function, void* Params)
{
	ProcessEventHookGuard Guard;
	static thread_local uint32_t CallCount = 0;
	++CallCount;
	static thread_local bool JustFired = false;

	if (!Object || !Object->Class || !Function)
		return oProcessEvent(Object, Function, Params);

	std::unique_lock<std::recursive_mutex> SettingsLock(SettingsMutex, std::try_to_lock);
	if (!SettingsLock.owns_lock())
		return oProcessEvent(Object, Function, Params);

	if (Function)
	{
		const bool IsReceiveTick = IsReceiveTickFunction(Function);
		if (IsReceiveTick && (CallCount % 100 == 0))
			GVars.AutoSetVariables();

		if (CVars.Debug)
		{
			const std::string FuncName = Function->GetName();
			const std::string ObjName = Object->GetName();

			bool bFunctionPass =
				TextVars.DebugFunctionNameMustInclude.empty() ||
				FuncName.find(TextVars.DebugFunctionNameMustInclude) != std::string::npos;

			bool bObjectPass =
				TextVars.DebugFunctionObjectMustInclude.empty() ||
				ObjName.find(TextVars.DebugFunctionObjectMustInclude) != std::string::npos;

			if (bFunctionPass && bObjectPass && !CVars.SaveDebugToFile)
			{
				printf(
					"Function: %s\nClass: %s\nObject: %s\n\n",
					FuncName.c_str(),
					Object->Class->GetName().c_str(),
					ObjName.c_str()
				);
			}
			if (bFunctionPass && bObjectPass && CVars.SaveDebugToFile)
			{
				std::ofstream debugFile("ProcessEventLog.txt", std::ios::app);
				if (debugFile.is_open())
				{
					debugFile << "Function: " << FuncName << "\n";
					debugFile << "Class: " << Object->Class->GetName() << "\n";
					debugFile << "Object: " << ObjName << "\n\n";
					debugFile.close();
				}
			}
		}

		if (!PlayerHooked && IsReceiveTick && Object->IsA(APlayerCharacter::StaticClass()))
		{
			void** VTable = *reinterpret_cast<void***>(const_cast<UObject*>(Object));
			void* ProcessEventAddr = VTable[SDK::Offsets::ProcessEventIdx];

			MH_STATUS status = MH_CreateHook(
				ProcessEventAddr,
				&hkPlayerProcessEvent,
				reinterpret_cast<void**>(&oPlayerProcessEvent));

			if (status != MH_OK)
			{
				printf("[ERROR] Failed to create ProcessEvent hook! %d\n", status);
				SettingsLock.unlock();
				return oProcessEvent(Object, Function, Params);
			}

			status = MH_EnableHook(ProcessEventAddr);
			if (status != MH_OK)
			{
				printf("[ERROR] Failed to enable ProcessEvent hook! %d\n", status);
				MH_RemoveHook(ProcessEventAddr);
				SettingsLock.unlock();
				return oProcessEvent(Object, Function, Params);
			}

			PlayerHooked = true;

			SettingsLock.unlock();
			return oProcessEvent(Object, Function, Params);
		}

		if (CVars.InstantMultiTool && Params && Function->GetName() == "GetMultitoolUseTime") // credit to CrimsonSpark for this instant multitool use time
		{
			SettingsLock.unlock();
			oProcessEvent(Object, Function, Params);
			if (reinterpret_cast<Params::CanUseMultitoolOn_GetMultitoolUseTime*>(Params)->ReturnValue < 0.1f)
				return;

			reinterpret_cast<Params::CanUseMultitoolOn_GetMultitoolUseTime*>(Params)->ReturnValue = 0.01f;
			return;
		}
		else if ((CVars.SilentAim || CVars.ShootFromReticle))
		{
			if (Params && Object->IsA(ABaseMagazineWeapon::StaticClass()) && strcmp(Function->GetName().c_str(), "Server_OnFire") == 0)
			{
				auto* Weapon = const_cast<ABaseMagazineWeapon*>(reinterpret_cast<const ABaseMagazineWeapon*>(Object));
				bool OwnerIsLocalPlayer = GVars.ReadyOrNotChar && GVars.PlayerController &&
					Weapon->Owner == GVars.ReadyOrNotChar;

				if (OwnerIsLocalPlayer)
				{
					auto* FireParams =
						reinterpret_cast<Params::BaseMagazineWeapon_Server_OnFire*>(Params);

					if (CVars.ShootFromReticle && OwnerIsLocalPlayer)
					{
						FVector SpawnLoc{};
						FVector Direction{};
						if (GVars.PlayerController->DeprojectScreenPositionToWorld(
							GVars.ScreenSize.x / 2.0f + MiscSettings.ReticlePosition.x,
							GVars.ScreenSize.y / 2.0f + MiscSettings.ReticlePosition.y,
							&SpawnLoc,
							&Direction
						))
						{
							FireParams->SpawnLoc = SpawnLoc;
							FireParams->Direction = UKismetMathLibrary::Conv_VectorToRotator(Direction);
						}
					}

					if (CVars.SilentAim && OwnerIsLocalPlayer)
						Cheats::SilentAim(FireParams);

					if (!JustFired)
					{
						JustFired = true;
						for (int i = 0; i < CVars.MultiFire; i++)
						{
							Weapon->Server_OnFire(FireParams->Direction, FireParams->SpawnLoc, FireParams->Seed);
						}
						JustFired = false;
					}
				}
			}
		}
	}

	// Call original
	SettingsLock.unlock();
	oProcessEvent(Object, Function, Params);
}

bool Hooks::HookProcessEvent()
{
	void* ProcessEventAddr = reinterpret_cast<void*>(InSDKUtils::GetImageBase() + Offsets::ProcessEvent);

	if (MH_CreateHook(
		ProcessEventAddr,
		&hkProcessEvent,
		reinterpret_cast<void**>(&oProcessEvent)
	) != MH_OK)
	{
		return false;
	}

	if (MH_EnableHook(ProcessEventAddr) != MH_OK)
		return false;

	return true;
}

void Hooks::WaitForProcessEvent()
{
	while (ActiveProcessEventHooks.load() != 0)
		Sleep(1);
}