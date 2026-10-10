#include "pch.h"
#include <Windows.h>
#include <vector>

#define IMGUI_DEFINE_MATH_OPERATORS

#include "Cheats.h"
#include "Utils.h"
#include "Overlay.h"

#include "SDK/Engine_classes.hpp"
#include <chrono>

#define VAR_NAME(x) #x

using namespace SDK;

FRChatMessage Msg;
TArray<AActor*> ActorsCopy;

std::vector<bool> CheatToggles;

void Cheats::ToggleGodMode() {
	if (!GVars.PlayerController) return;
	if (!GVars.ReadyOrNotChar) return;
	static APlayerCharacter* LastPlayer = nullptr;
	static UWorld* LastWorld = nullptr;
	static ULevel* LastLevel = nullptr;
	static bool LastRequestedState = false;

	auto* Player = reinterpret_cast<APlayerCharacter*>(GVars.ReadyOrNotChar);
	const bool ContextChanged = Player != LastPlayer || GVars.World != LastWorld || GVars.Level != LastLevel;
	if (ContextChanged)
		Player->bGodMode = false;

	const bool WasGodMode = Player->bGodMode;
	const bool RequestChanged = CVars.GodMode != LastRequestedState;
	if (!ContextChanged && !RequestChanged && WasGodMode == CVars.GodMode)
		return;

	const bool HasAuthority = Player->HasAuthority();
	bool GodModeApplied = false;
	if (WasGodMode != CVars.GodMode)
	{
		if (HasAuthority)
		{
			Player->Server_ToggleGodMode();
			Player->bGodMode = CVars.GodMode;
			GodModeApplied = true;
		}
	}

	if (Player->CharacterHealth && (CVars.GodMode || LastRequestedState || WasGodMode) &&
		(ContextChanged || RequestChanged || GodModeApplied))
	{
		Player->CharacterHealth->bEnableIncapacitation = !CVars.GodMode;
		Player->CharacterHealth->bUnlimited = CVars.GodMode;
		if (HasAuthority)
			Player->CharacterHealth->Server_SetUnlimitedResource(CVars.GodMode);
		else
			Player->CharacterHealth->SetUnlimitedResource(CVars.GodMode);
	}
	if (CVars.GodMode && !WasGodMode && (ContextChanged || RequestChanged || GodModeApplied))
		Player->IncreaseHealth(100000.0f);

	LastPlayer = Player;
	LastWorld = GVars.World;
	LastLevel = GVars.Level;
	LastRequestedState = CVars.GodMode;
}

void Cheats::ToggleInfAmmo()
{
	static ABaseMagazineWeapon* LastInfiniteAmmoWeapon = nullptr;
	static UWorld* LastInfiniteAmmoWorld = nullptr;
	static ULevel* LastInfiniteAmmoLevel = nullptr;
	static bool WasEnabled = false;

	if (!CVars.InfAmmo && !WasEnabled)
		return;

	if (!GVars.World || !GVars.Level || !GVars.ReadyOrNotChar ||
		!Utils::IsValidActor(GVars.ReadyOrNotChar))
	{
		LastInfiniteAmmoWeapon = nullptr;
		LastInfiniteAmmoWorld = nullptr;
		LastInfiniteAmmoLevel = nullptr;
		WasEnabled = false;
		return;
	}

	ABaseMagazineWeapon* Gun = GVars.ReadyOrNotChar->GetEquippedWeapon();

	
	if (!Gun || !Utils::IsValidActor(Gun))
	{
		LastInfiniteAmmoWeapon = nullptr;
		WasEnabled = CVars.InfAmmo; 
		return;
	}

	if (GVars.World != LastInfiniteAmmoWorld || GVars.Level != LastInfiniteAmmoLevel)
	{
		LastInfiniteAmmoWeapon = nullptr;
		LastInfiniteAmmoWorld = GVars.World;
		LastInfiniteAmmoLevel = GVars.Level;
	}

	Gun->bInfiniteAmmo = CVars.InfAmmo;

	if (CVars.InfAmmo)
	{
		Gun->ReplenishAmmo();

		
		if (Gun->GetAmmo() < 1.0f)
		{
			FMagazine NewMag;
			NewMag.Ammo = Gun->MagazineCountDefault;
			NewMag.AmmoType = 1;
			Gun->Server_AddMagazine(NewMag);
		}
	}

	LastInfiniteAmmoWeapon = Gun;
	WasEnabled = CVars.InfAmmo;
}

void Cheats::PenetrateWalls()
{
	if (!GVars.ReadyOrNotChar)
	{
		Utils::Error("Character Invalid: RemoveSpread ; Misc.cpp");
		return;
	}
	if (!GVars.ReadyOrNotChar->GetEquippedWeapon())
	{
		Utils::Error("Weapon Invalid: RemoveSpread ; Misc.cpp");
		return;
	}
	auto* Character = reinterpret_cast<APlayerCharacter*>(GVars.ReadyOrNotChar);
	auto* Gun = Character->GetEquippedWeapon();

	Gun->CurrentAmmoType.PenetrationDistance = 100000;
	Gun->CurrentAmmoType.PenetrationLevel = 100;
	Gun->PenetrationDistance = 100000; 
	Gun->bArmorPiercing = true;

}

void Cheats::AddAutoFire()
{
	if (!GVars.ReadyOrNotChar)
	{
		Utils::Error("Character Invalid: AddAutoFire ; Misc.cpp");
		return;
	}
	if (!GVars.ReadyOrNotChar->GetEquippedWeapon())
	{
		Utils::Error("Weapon Invalid: AddAutoFire ; Misc.cpp");
		return;
	}
	auto* Character = reinterpret_cast<APlayerCharacter*>(GVars.ReadyOrNotChar);
	auto* Gun = Character->GetEquippedWeapon();

	for (const EFireMode FireMode : Gun->AvailableFireModes)
	{
		if (FireMode == EFireMode::FM_Auto)
			return;
	}
	Gun->AvailableFireModes.Add(EFireMode::FM_Auto);
}

void Cheats::InstaKill()
{
	if (!GVars.ReadyOrNotChar)
	{
		Utils::Error("Character Invalid: InstaKill ; Misc.cpp");
		return;
	}
	if (!GVars.ReadyOrNotChar->GetEquippedWeapon())
	{
		Utils::Error("Weapon Invalid: InstaKill ; Misc.cpp");
		return;
	}

	auto* Character = reinterpret_cast<APlayerCharacter*>(GVars.ReadyOrNotChar);
	auto* Gun = Character->GetEquippedWeapon();
	Gun->CurrentAmmoType.DismembermentDamage = 100000;
	Gun->CurrentAmmoType.Damage = 10000;
	Gun->CurrentAmmoType.DurabilityDamage = 10000;
}

void Cheats::SetFireRate(float FireRateMult)
{
	if (!GVars.ReadyOrNotChar)
	{
		Utils::Error("Character Invalid: SetFireRate ; Misc.cpp");
		return;
	}
	if (!GVars.ReadyOrNotChar->GetEquippedWeapon())
	{
		Utils::Error("Weapon Invalid: SetFireRate ; Misc.cpp");
		return;
	}

	auto* Character = reinterpret_cast<APlayerCharacter*>(GVars.ReadyOrNotChar);
	auto* Gun = Character->GetEquippedWeapon();
	
	printf("Original Fire Rate: %f\n", Gun->FireRate);

	Gun->FireRate = 60.0f / FireRateMult; // Fire rate has to be adjusted from RPM to delay between shots
}

void Cheats::RemoveSpread()
{
	if (!GVars.ReadyOrNotChar)
		return;

	ABaseMagazineWeapon* Gun = GVars.ReadyOrNotChar->GetEquippedWeapon();
	if (!Gun || !Utils::IsValidActor(Gun))
		return;

	// Core spread
	Gun->SpreadPattern = FRotator();
	Gun->PendingSpread = FRotator();
	Gun->SpreadReturnRate = 0.0f;
	Gun->FirstShotSpread = 0.0f;
	Gun->FirstShotResetTime = 0.0f;
	Gun->VelocitySpreadMultiplier = 0.0f;
	Gun->ADSSpreadMultiplier = 0.0f;
	Gun->bIgnoreAmmoTypeSpread = true;

	// Also feeds the cone (especially hip-fire / movement)
	Gun->VelocityRecoilMultiplier = 0.0f;
}

void Cheats::RemoveRecoil()
{
	if (!GVars.ReadyOrNotChar)
	{
		Utils::Error("Character Invalid: RemoveRecoil ; Misc.cpp");
		return;
	}
	if (!GVars.ReadyOrNotChar->GetEquippedWeapon())
	{
		Utils::Error("Weapon Invalid: RemoveRecoil ; Misc.cpp");
		return;
	}

	APlayerCharacter* Character = reinterpret_cast<APlayerCharacter*>(GVars.ReadyOrNotChar);
	ABaseMagazineWeapon * Gun = GVars.ReadyOrNotChar->GetEquippedWeapon();

	// Recoil removal
	Gun->RecoilMultiplierPitch = 0.0f;
	Gun->RecoilMultiplierYaw = 0.0f;
	Gun->FirstShotRecoil = 0.0f;
	Gun->RecoilFireStrength = 0.0f;
	Gun->RecoilFireStrengthFirst = 0.0f;
	Gun->RecoilAngleStrength = 0.0f;
	Gun->RecoilRandomness = 0.0f;
	Gun->RecoilFireADSModifier = 0.0f;
	Gun->RecoilAngleADSModifier = 0.0f;
	Gun->RecoilBuildupADSModifier = 0.0f;
	Gun->RecoilHasBuildup = false;
	Gun->RecoilBuildupDampStrength = 0.0f;
	Gun->Wobble = 0.0f;
	Gun->FirstShotRecoil = 0.0f;
	Gun->FirstShotResetTime = 0.0f;
	Gun->RecoilPattern = TArray<FRotator>();
	Gun->RecoilPositionBuildup = FVector();
	Gun->RecoilRotationBuildup = FRotator();
	Gun->VelocityRecoilMultiplier = 0.0f;
	Gun->ADSRecoilMultiplier = 0.0f;
	Gun->FireCameraShake = nullptr;
	Gun->FireCameraShakeInst = nullptr;
	Gun->Reload_CameraShake = nullptr;
	Gun->ReloadEmpty_CameraShake = nullptr;
	Gun->ProcRecoil_Trans = FVector();
	Gun->ProcRecoil_Rot = FRotator();
	Gun->ProcRecoil_Trans_Buildup = FVector();
	Gun->ProcRecoil_Rot_Buildup = FRotator();
	Gun->bCalculateProcRecoil = false;
	Gun->CurrentHighTimer = 0.0f;
	Gun->FireHighTimer = 0.0f;
	Gun->RecoilFireTime = 0.0f;
	Gun->RecoilReturnRate = 0.0f;
	Gun->RecoilReturnInterpSpeed = 0.0f;
	Gun->RecoilReturnPercentage = 0.0f;
	Gun->RecoilDampStrength = 0.0f;
	Character->WeaponBobRot = FRotator();
	Character->WeaponBobTrans = FVector();
	Gun->bUseFireLoopAnims = false;
	Character->CameraBobRot = FRotator();
	Character->CameraBobTrans = FVector();
	Character->MeshspaceRecoilMovementMultiplier = FVector();
	Character->RecoilSpeed = 0.0f;
}

void Cheats::SetPlayerSpeed()
{
	if (!GVars.ReadyOrNotChar || !GVars.PlayerController) return;

	APlayerCharacter* PlayerChar = reinterpret_cast<APlayerCharacter*>(GVars.ReadyOrNotChar);
	if (!PlayerChar) return;

	static APlayerCharacter* LastPlayer = nullptr;
	static UWorld* LastWorld = nullptr;
	static ULevel* LastLevel = nullptr;
	static float LastWalkSpeed = 0.0f;
	static float LastCrouchSpeed = 0.0f;
	static bool LastSpeedEnabled = false;
	static ULONGLONG LastUpdateMs = 0;
	const ULONGLONG Now = GetTickCount64();
	if (!CVars.SpeedEnabled && !LastSpeedEnabled)
		return;

	const float RequestedWalkSpeed = CVars.SpeedEnabled ? 240.0f * CVars.Speed : 240.0f;
	const float RequestedCrouchSpeed = CVars.SpeedEnabled ? 1000.0f * CVars.Speed : 1000.0f;
	if (PlayerChar == LastPlayer && GVars.World == LastWorld && GVars.Level == LastLevel &&
		RequestedWalkSpeed == LastWalkSpeed && RequestedCrouchSpeed == LastCrouchSpeed &&
		CVars.SpeedEnabled == LastSpeedEnabled && Now - LastUpdateMs < 1000)
		return;

	PlayerChar->Server_SetWalkSpeed(RequestedWalkSpeed, RequestedCrouchSpeed);

	LastPlayer = PlayerChar;
	LastWorld = GVars.World;
	LastLevel = GVars.Level;
	LastWalkSpeed = RequestedWalkSpeed;
	LastCrouchSpeed = RequestedCrouchSpeed;
	LastSpeedEnabled = CVars.SpeedEnabled;
	LastUpdateMs = Now;
}

void Cheats::ApplyPersistentWeaponSettings()
{
	static UWorld* LastWorld = nullptr;
	static ULevel* LastLevel = nullptr;
	static AReadyOrNotCharacter* LastCharacter = nullptr;
	static ABaseMagazineWeapon* LastWeapon = nullptr;
	static bool LastNoRecoil = false;
	static bool LastNoSpread = false;
	static bool LastAutoFire = false;
	static bool LastPenetration = false;
	static bool LastInstaKill = false;
	static bool LastHighFireRate = false;

	if (!GVars.World || !GVars.ReadyOrNotChar)
	{
		LastWeapon = nullptr;
		return;
	}

	ABaseMagazineWeapon* Weapon = GVars.ReadyOrNotChar->GetEquippedWeapon();
	if (!Weapon || !Utils::IsValidActor(Weapon))
	{
		LastWeapon = nullptr;
		return;
	}

	const bool SettingsChanged =
		LastNoRecoil != CVars.KeepNoRecoil ||
		LastNoSpread != CVars.KeepNoSpread ||
		LastAutoFire != CVars.KeepAutoFire ||
		LastPenetration != CVars.KeepPenetration ||
		LastInstaKill != CVars.KeepInstaKill ||
		LastHighFireRate != CVars.KeepHighFireRate;

	const bool ContextChanged =
		GVars.World != LastWorld ||
		GVars.Level != LastLevel ||
		GVars.ReadyOrNotChar != LastCharacter ||
		Weapon != LastWeapon;

	// Always re-apply spread/recoil every tick while enabled
	// (game rewrites these on fire / reload / movement)
	if (CVars.KeepNoSpread)
		RemoveSpread();
	if (CVars.KeepNoRecoil)
		RemoveRecoil();

	// One-shot style cheats only need to run on change
	if (!ContextChanged && !SettingsChanged)
		return;

	LastWorld = GVars.World;
	LastLevel = GVars.Level;
	LastCharacter = GVars.ReadyOrNotChar;
	LastWeapon = Weapon;
	LastNoRecoil = CVars.KeepNoRecoil;
	LastNoSpread = CVars.KeepNoSpread;
	LastAutoFire = CVars.KeepAutoFire;
	LastPenetration = CVars.KeepPenetration;
	LastInstaKill = CVars.KeepInstaKill;
	LastHighFireRate = CVars.KeepHighFireRate;

	if (CVars.KeepAutoFire)
		AddAutoFire();
	if (CVars.KeepPenetration)
		PenetrateWalls();
	if (CVars.KeepInstaKill)
		InstaKill();
	if (CVars.KeepHighFireRate)
		SetFireRate(3000.0f);
}

void Cheats::AddMag()
{
	if (!GVars.ReadyOrNotChar || !GVars.PlayerController || !GVars.HasAuthority) 
		return;

	auto* Gun = GVars.ReadyOrNotChar->GetEquippedWeapon();
	if (!Gun) return;

	FMagazine NewMag;
	NewMag.Ammo = Gun->MagazineCountDefault;
	NewMag.AmmoType = 1;
	Gun->Server_AddMagazine(NewMag);
}

enum class EArrestStage : uint8_t
{
	Surrender,
	Arrest,
	ArrestComplete,
	WaitForArrested,
	ReportToTOC
};

struct FPendingArrest
{
	ACyberneticCharacter* Char = nullptr;
	EArrestStage Stage = EArrestStage::Surrender;
	ULONGLONG LastStepMs = 0;
	int WaitTicks = 0;
};

std::vector<FPendingArrest> GPendingArrests;

constexpr ULONGLONG ArrestStepDelayMs = 120;
constexpr int MaxArrestOpsPerFrame = 2;
constexpr int MaxWaitTicks = 15;

bool MatchesTeam(AActor* Actor, ETeam Team)
{
	return (Team == ETeam::TEAM_CIVILIAN && Actor->IsA(ACivilianCharacter::StaticClass())) ||
		(Team == ETeam::TEAM_SUSPECT && Actor->IsA(ASuspectCharacter::StaticClass())) ||
		(Team == ETeam::TEAM_SWAT && Actor->IsA(ASWATCharacter::StaticClass()));
}

void Cheats::ArrestAll(ETeam Team)
{
	if (!GVars.Level || !GVars.ReadyOrNotChar || !GVars.PlayerController || !GVars.HasAuthority)
		return;

	ULevel* Level = GVars.Level;
	if (!Level)
		return;

	ActorsCopy = Level->Actors;
	if (!ActorsCopy || ActorsCopy.Num() == 0)
		return;

	for (AActor* Actor : ActorsCopy)
	{
		if (!Actor || !Utils::IsValidActor(Actor) || !GVars.ReadyOrNotChar)
			continue;

		if (!MatchesTeam(Actor, Team))
			continue;

		ACyberneticCharacter* Char = reinterpret_cast<ACyberneticCharacter*>(Actor);
		if (!Char || Char->IsArrested())
			continue;

		bool bAlreadyQueued = false;
		for (const FPendingArrest& Entry : GPendingArrests)
		{
			if (Entry.Char == Char)
			{
				bAlreadyQueued = true;
				break;
			}
		}
		if (bAlreadyQueued)
			continue;

		Char->ForceComplianceStrength = 0.0f;
		Char->AbuseCount = 0;

		FPendingArrest NewEntry;
		NewEntry.Char = Char;
		NewEntry.Stage = EArrestStage::Surrender;
		NewEntry.LastStepMs = 0;
		NewEntry.WaitTicks = 0;
		GPendingArrests.push_back(NewEntry);
	}
}

void Cheats::ProcessArrestQueue()
{
	if (GPendingArrests.empty())
		return;

	if (!GVars.ReadyOrNotChar || !GVars.PlayerController || !GVars.HasAuthority)
	{
		GPendingArrests.clear();
		return;
	}

	const ULONGLONG Now = GetTickCount64();
	int OpsThisFrame = 0;

	for (size_t i = 0; i < GPendingArrests.size() && OpsThisFrame < MaxArrestOpsPerFrame; )
	{
		FPendingArrest& Entry = GPendingArrests[i];
		ACyberneticCharacter* Char = Entry.Char;

		if (!Char || !Utils::IsValidActor(Char))
		{
			GPendingArrests.erase(GPendingArrests.begin() + static_cast<long long>(i));
			continue;
		}

		if (Now - Entry.LastStepMs < ArrestStepDelayMs)
		{
			++i;
			continue;
		}

		switch (Entry.Stage)
		{
		case EArrestStage::Surrender:
			Char->Surrender();
			Entry.Stage = EArrestStage::Arrest;
			Entry.LastStepMs = Now;
			++OpsThisFrame;
			++i;
			break;

		case EArrestStage::Arrest:
			Char->Arrest(GVars.ReadyOrNotChar);
			Entry.Stage = EArrestStage::ArrestComplete;
			Entry.LastStepMs = Now;
			++OpsThisFrame;
			++i;
			break;

		case EArrestStage::ArrestComplete:
			Char->ArrestComplete(GVars.ReadyOrNotChar, nullptr);
			Entry.Stage = EArrestStage::WaitForArrested;
			Entry.LastStepMs = Now;
			++OpsThisFrame;
			++i;
			break;

		case EArrestStage::WaitForArrested:
			if (Char->IsArrested())
			{
				Entry.Stage = EArrestStage::ReportToTOC;
				Entry.LastStepMs = Now;
			}
			else
			{
				Entry.WaitTicks++;
				if (Entry.WaitTicks >= MaxWaitTicks)
				{
					Char->Arrest(GVars.ReadyOrNotChar);
					Char->ArrestComplete(GVars.ReadyOrNotChar, nullptr);
					Entry.WaitTicks = 0;
					Entry.LastStepMs = Now;
					++OpsThisFrame;
				}
			}
			++i;
			break;

		case EArrestStage::ReportToTOC:
			if (Char->IsArrested())
				GVars.ReadyOrNotChar->Server_ReportToTOC(Char, false, false);

			GPendingArrests.erase(GPendingArrests.begin() + static_cast<long long>(i));
			++OpsThisFrame;
			break;
		}
	}
}

void Cheats::KillAll(ETeam Team)
{
	if (!GVars.Level) return;
	if (ULevel* Level = GVars.Level) {
		ActorsCopy = Level->Actors; // snapshot to prevent mid-iteration changes causing crashes
		if (!ActorsCopy || ActorsCopy.Num() == 0) return;

		for (AActor* Actor : ActorsCopy)
		{
			if (!Actor || !Utils::IsValidActor(Actor) || !GVars.ReadyOrNotChar) continue;

			if ((Team == ETeam::TEAM_CIVILIAN && Actor->IsA(ACivilianCharacter::StaticClass())) ||
				(Team == ETeam::TEAM_SUSPECT && Actor->IsA(ASuspectCharacter::StaticClass())) ||
				(Team == ETeam::TEAM_SWAT && Actor->IsA(ASWATCharacter::StaticClass())))
			{
				if (GVars.HasAuthority)
				{
					reinterpret_cast<APlayerCharacter*>(Actor)->Server_Kill();
					reinterpret_cast<APlayerCharacter*>(Actor)->Server_ReportToTOC(Actor, false, false);
				}
				else
				{
					reinterpret_cast<APlayerCharacter*>(Actor)->Kill();
					reinterpret_cast<APlayerCharacter*>(Actor)->Server_ReportToTOC(Actor, false, false);
				}
					
			}
		}
	}
}

void Cheats::DrawReticle()
{
	if (!CVars.Reticle || !GVars.ReadyOrNotChar) return;

	if (MiscSettings.ReticleWhenThrowing && !reinterpret_cast<APlayerCharacter*>(GVars.ReadyOrNotChar)->bQuickThrowing) 
		return;

	if (MiscSettings.CrossReticle)
	{
		const Overlay::Color Color = Overlay::FromPackedColor(Utils::ConvertColorToPacked(MiscSettings.ReticleColor));
		const float CenterX = GVars.ScreenSize.x / 2 + MiscSettings.ReticlePosition.x;
		const float CenterY = GVars.ScreenSize.y / 2 + MiscSettings.ReticlePosition.y;
		Overlay::Line(CenterX - 5, CenterY, CenterX + 5, CenterY, Color);
		Overlay::Line(CenterX, CenterY - 5, CenterX, CenterY + 5, Color);
	}
	else
	{
		const Overlay::Color Color = Overlay::FromPackedColor(Utils::ConvertColorToPacked(MiscSettings.ReticleColor));
		Overlay::Circle(GVars.ScreenSize.x / 2 + MiscSettings.ReticlePosition.x,
			GVars.ScreenSize.y / 2 + MiscSettings.ReticlePosition.y, MiscSettings.ReticleSize, Color, 1.0f, true);
	}
}

void Cheats::GetAllEvidence()
{
	if (!GVars.Level || !GVars.ReadyOrNotChar)
		return;

	ActorsCopy = GVars.Level->Actors;
	if (!ActorsCopy || ActorsCopy.Num() == 0)
		return;

	for (AActor* Actor : ActorsCopy)
	{
		if (!Actor || !Utils::IsValidActor(Actor)) 
			continue;
		
		if (Actor->IsA(ABaseWeapon::StaticClass()))
		{
			ABaseWeapon* Weapon = reinterpret_cast<ABaseWeapon*>(Actor);

			if (Weapon && Weapon->EvidenceComponent && Weapon->EvidenceComponent->CanBeCollected())
			{
				GVars.ReadyOrNotChar->PickupEvidence(Weapon);
			}
		}
	}
}

void Cheats::TriggerBot()
{
	static auto NextTrace = std::chrono::steady_clock::time_point{};
	if (!CVars.TriggerBot || !GVars.PlayerController || !GVars.ReadyOrNotChar)
		return;

	const auto Now = std::chrono::steady_clock::now();
	if (Now < NextTrace)
		return;
	NextTrace = Now + std::chrono::milliseconds(8);

	APlayerCameraManager* CameraManager = GVars.PlayerController->PlayerCameraManager;
	if (!GVars.World || !CameraManager)
		return;

	ABaseMagazineWeapon* Weapon = GVars.ReadyOrNotChar->GetEquippedWeapon();
	if (!Weapon)
		return;

	const FVector CameraLocation = CameraManager->GetCameraLocation();
	const FVector CameraForward = Utils::FRotatorToVector(CameraManager->GetCameraRotation());
	const FVector TraceEnd = CameraLocation + (CameraForward * 10000.0f);
	static const TArray<AActor*> IgnoreActors;
	FHitResult HitResult;

	if (UKismetSystemLibrary::LineTraceSingle(
		GVars.World,
		CameraLocation,
		TraceEnd,
		ETraceTypeQuery::TraceTypeQuery1,
		false,
		IgnoreActors,
		EDrawDebugTrace::None,
		&HitResult,
		true,
		FLinearColor(0, 0, 0, 0),
		FLinearColor(0, 0, 0, 0),
		5.0f
	))
	{
		if (HitResult.bBlockingHit && HitResult.HitObjectHandle.Actor.Get())
		{
			AActor* HitActor = HitResult.HitObjectHandle.Actor.Get();
			if (HitActor && (HitActor->IsA(ASuspectCharacter::StaticClass()) || MiscSettings.TriggerBotTargetsCivilians && HitActor->IsA(ACivilianCharacter::StaticClass())))
			{
				AReadyOrNotCharacter* Target = reinterpret_cast<AReadyOrNotCharacter*>(HitActor);

				if (Target->IsDeadOrUnconscious()
					|| Target->IsArrestedOrSurrendered()
					|| Target->IsDowned()
					|| Target->IsIncapacitated()
					|| Target->IsSurrendered())
					return;

				if (MiscSettings.TriggerBotUsesSilentAim)
				{
					Weapon->OnFire(FRotator(), HitResult.ImpactPoint);
					return;
				}
				else
				{
					Weapon->OnFire(
						CameraManager->GetCameraRotation(), // Direction: if we don't set this the bullet just chills
						CameraLocation); // Start location
					return;
				}
			}
		}
	}
}

void Cheats::RenderEnabledOptions()
{
	if (!CVars.RenderOptions) return;
	const Overlay::Color Color{ 80, 220, 255, 255 };
	float Y = 30.0f;
	Overlay::Text(10.0f, Y, Overlay::Color{ 255, 255, 255, 255 }, "Enabled Options:");
	Y += 20.0f;
	auto AddEnabled = [&Y, Color](bool Enabled, const std::string& Label)
	{
		if (!Enabled)
			return;
		Overlay::Text(10.0f, Y, Color, Label);
		Y += 18.0f;
	};
	AddEnabled(CVars.GodMode, "God Mode");
	AddEnabled(CVars.InfAmmo, "Infinite Ammo");
	AddEnabled(CVars.Aimbot, "Aimbot");
	AddEnabled(CVars.ESP, "ESP");
	if (CVars.SpeedEnabled)
	{
		char SpeedText[64]{};
		snprintf(SpeedText, sizeof(SpeedText), "Speed x%.1f", CVars.Speed);
		AddEnabled(true, SpeedText);
	}
	AddEnabled(CVars.SilentAim, "Silent Aim");
	AddEnabled(CVars.Reticle, "Reticle");
	AddEnabled(CVars.TriggerBot, "Trigger Bot");
	AddEnabled(CVars.TriggerBotHoldKey, "Trigger Bot Hold Key");
	AddEnabled(CVars.AntiSway, "Anti Sway");
	AddEnabled(CVars.KeepNoRecoil, "Persistent No Recoil");
	AddEnabled(CVars.KeepNoSpread, "Persistent No Spread");
	AddEnabled(CVars.KeepAutoFire, "Persistent Auto Fire");
	AddEnabled(CVars.KeepPenetration, "Persistent Penetration");
	AddEnabled(CVars.KeepInstaKill, "Persistent Insta Kill");
	AddEnabled(CVars.KeepHighFireRate, "Persistent High Fire Rate");
	AddEnabled(CVars.BulletTime, "Bullet Time");
	AddEnabled(CVars.ShootFromReticle, "Shoot From Reticle");
}

void Cheats::ChangeFOV()
{
	if (!GVars.ReadyOrNotChar || !GVars.PlayerController) return;

	static APlayerController* LastController = nullptr;
	static UWorld* LastWorld = nullptr;
	static ULevel* LastLevel = nullptr;
	static float LastFOV = 0.0f;
	if (LastController == GVars.PlayerController && LastWorld == GVars.World &&
		LastLevel == GVars.Level && LastFOV == CVars.FOV)
		return;

	GVars.PlayerController->FOV(CVars.FOV);
	LastController = GVars.PlayerController;
	LastWorld = GVars.World;
	LastLevel = GVars.Level;
	LastFOV = CVars.FOV;
}

void Cheats::AutoWin()
{
	if (!GVars.GameState || !GVars.ReadyOrNotChar || !GVars.PlayerController || !GVars.HasAuthority) return;

	AReadyOrNotGameState* GameState = GVars.GameState;

	ArrestAll(ETeam::TEAM_CIVILIAN);
	ArrestAll(ETeam::TEAM_SUSPECT);
	GetAllEvidence();

	for (AEvidenceActor* Evidence : GameState->AllEvidenceActors)
	{
		if (!Evidence) 
			continue;

		if (Evidence->EvidenceComponent && Evidence->EvidenceComponent->CanBeCollected())
		{
			for (FScoreBonus& Bonus : Evidence->ScoringComponent->ScoringData.Bonuses)
			{
				Bonus.bEnabled = true;
				Bonus.bGiven = true;
				Bonus.Score = 10000;
			}

			Evidence->OnEvidenceStateChanged(EEvidenceActorState::Collected);
		}
	}
	for (AReportableActor* Actor : GameState->AllReportableActors)
	{
		GVars.ReadyOrNotChar->Server_ReportToTOC(Actor, false ,false);
		GVars.ReadyOrNotChar->Server_ReportTarget(Actor);
		Actor->InteractableComponent->OnInteract(((APlayerCharacter*)GVars.ReadyOrNotChar));
	}
	for (AObjective* Objective : GameState->MissionObjectives)
	{
		if (!Objective) 
			continue;

		for (FScoreBonus &Bonus : Objective->ScoringComponent->ScoringData.Bonuses)
		{
			Bonus.bEnabled = true;
			Bonus.bGiven = true;
			Bonus.Score = 10000;
		}

		Objective->ObjectiveCompleted();
		Objective->OnObjectiveCompleted();
	}
}

void Cheats::UnlockDoors()
{
	if (!GVars.GameState) return;

	AReadyOrNotGameState* GameState = GVars.GameState;

	if (!GameState) return;

	for (SDK::Adoor* Door : GameState->AllDoors)
	{
		if (!Door) continue;

		Door->bLocked = false;
	}
}

void Cheats::ListPlayers()
{
	if (!GVars.GameState || !GVars.PlayerController || !GVars.Level) return;
	Overlay::Text(10.0f, 30.0f, Overlay::Color{ 255, 255, 255, 255 }, "Players:");

	TArray<APlayerCharacter*> Players = GVars.GameState->AllPlayerCharacters;
	if (!Players || Players.Num() == 0)
		return;

	float Y = 50.0f;
	for (APlayerCharacter* Player : Players)
	{
		if (!Player || !Utils::IsValidActor(Player)) continue;
		if (!Player->PlayerState) continue;
		if (!Player->PlayerState->GetPlayerName()) continue;

		const std::string PlayerLabel = Player->PlayerState->GetPlayerName().ToString();
		Overlay::Text(10.0f, Y, Overlay::Color{ 80, 220, 255, 255 }, PlayerLabel);
		Y += 18.0f;

		if (Player->GetController() && Player->GetController() == GVars.PlayerController || Player == GVars.ReadyOrNotChar) continue; // skip ourselves

		if (GVars.PlayerController && GVars.HasAuthority)
		{
		}
	}
}

void Cheats::SurrenderAll(ETeam Team)
{
	if (!GVars.Level || !GVars.PlayerController || !GVars.HasAuthority) 
		return; // Crashes if you aren't host because we use a replicate function.

	ULevel* Level = GVars.Level;
	TArray<AActor*> ActorsSnapshot = Level->Actors;
	for (AActor* Actor : ActorsSnapshot)
	{
		if (!Actor || !Utils::IsValidActor(Actor))
			continue;

		const bool IsRequestedTeam =
			(Team == ETeam::TEAM_CIVILIAN && Actor->IsA(ACivilianCharacter::StaticClass())) ||
			(Team == ETeam::TEAM_SUSPECT && Actor->IsA(ASuspectCharacter::StaticClass()));
		if (!IsRequestedTeam)
			continue;

		AReadyOrNotCharacter* Char = reinterpret_cast<AReadyOrNotCharacter*>(Actor);
		if (!Char || Char->IsArrestedOrSurrendered())
			continue;
		Char->bSurrendered = true;
		Char->bSurrenderComplete = true;
		Char->OnRep_Surrendered();
		Sleep(10);
	}
}

void Cheats::AntiSway()
{
	if (!GVars.ReadyOrNotChar) return;
	auto* Weapon = GVars.ReadyOrNotChar->GetEquippedWeapon();
	if (!Weapon) return;

	Weapon->CameraBobScaleH = 0.0f;
	Weapon->DrawCameraShake = nullptr;
	Weapon->bCalculateProcRecoil = false;
	Weapon->InertiaDragStrafeRotation = 0.0f;
	Weapon->InertiaDragStrafeLocation = 0.0f;
	Weapon->InertiaDragAimLocation = 0.0f;
	Weapon->InertiaDragAimRotation = 0.0f;
	Weapon->CameraBobScaleV = 0.0f;
	Weapon->CameraBobSpeedScaleH = 0.0f;
	Weapon->CameraBobSpeedScaleV = 0.0f;
	Weapon->CameraBobAmplitudeBaseSpeed = 0.0f;
	Weapon->CameraBobAmplitudeWalkScale = 0.0f;
	Weapon->CameraBobAmplitudeSprintScale = 0.0f;
}

void Cheats::GiveAchievements()
{
	if (!GVars.ReadyOrNotChar || !GVars.ReadyOrNotChar->PlayerState) return;
	if (!GVars.ReadyOrNotChar->PlayerState->IsA(AReadyOrNotPlayerState::StaticClass())) return;

	AReadyOrNotPlayerState* PlayerState = reinterpret_cast<AReadyOrNotPlayerState*>(GVars.ReadyOrNotChar->PlayerState);

	for (int i = 0; i < static_cast<int>(EAchievement::EAchievement_MAX); i++)
	{
		PlayerState->Client_GrantAchievement(static_cast<EAchievement>(i));
		if (i < static_cast<int>(EAchievementStats::EAchievementStats_MAX))
			PlayerState->Client_IncreaseAchievementStat(static_cast<EAchievementStats>(i), 100000, GVars.ReadyOrNotChar, GVars.ReadyOrNotChar);
	}
	/*for (int i = 0; i < 47; i++)
	{
		PlayerState->Client_IncreaseAchievementStat(static_cast<EAchievementStats>(i), 100000, GVars.ReadyOrNotChar, GVars.ReadyOrNotChar);
	}*/
}
