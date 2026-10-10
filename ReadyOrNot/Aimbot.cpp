#include "pch.h"
#include "Cheats.h"
#include "Utils.h"
#include <chrono>
#include <cmath>
#include <numbers>

bool Init = false;

void Cheats::Aimbot(bool DrawVisuals)
{
	static AActor* LastTarget = nullptr;
	static std::chrono::steady_clock::time_point NextTargetSearch{};
	static UWorld* LastWorld = nullptr;
	static AReadyOrNotCharacter* LastPlayer = nullptr;

	if (!CVars.Aimbot)
	{
		LastTarget = nullptr;
		return;
	}

	if (DrawVisuals && AimbotSettings.DrawFOV)
		Utils::DrawFOV(AimbotSettings.MaxFOV, AimbotSettings.FOVThickness);

	if (!GVars.POV || !GVars.PlayerController || !GVars.Level || !GVars.ReadyOrNotChar) return;
	if (GVars.World != LastWorld || GVars.ReadyOrNotChar != LastPlayer)
	{
		LastWorld = GVars.World;
		LastPlayer = GVars.ReadyOrNotChar;
		LastTarget = nullptr;
		NextTargetSearch = {};
	}

	if (AimbotSettings.RequireKeyHeld && !AimbotKeyDown)
	{
		LastTarget = nullptr;
		return;
	}

	const auto Now = std::chrono::steady_clock::now();
	static auto NextAimUpdate = std::chrono::steady_clock::time_point{};
	if (Now < NextAimUpdate)
		return;
	NextAimUpdate = Now + std::chrono::milliseconds(4);

	static std::string CachedBoneName;
	static FName BoneName;
	if (CachedBoneName != TextVars.AimbotBone)
	{
		const std::wstring WideString = UtfN::StringToWString(TextVars.AimbotBone);
		BoneName = UKismetStringLibrary::Conv_StringToName(WideString.c_str());
		CachedBoneName = TextVars.AimbotBone;
	}

	if (LastTarget)
	{
		if (!Utils::IsValidActor(LastTarget) || !LastTarget->IsA(AReadyOrNotCharacter::StaticClass()))
		{
			LastTarget = nullptr;
		}
		else
		{
			auto* LockedCharacter = reinterpret_cast<AReadyOrNotCharacter*>(LastTarget);
			if (!LockedCharacter->Mesh)
			{
				LastTarget = nullptr;
			}
			else
			{
				const FVector LockedTargetPos = LockedCharacter->Mesh
					->GetBoneTransform(BoneName, ERelativeTransformSpace::RTS_World).Translation;
				const double LockedDistance = GVars.POV->Location.GetDistanceToInMeters(LockedTargetPos);
				if (LockedDistance < AimbotSettings.MinDistance || LockedDistance > AimbotSettings.MaxDistance)
					LastTarget = nullptr;
			}
		}
		if (!LastTarget)
			NextTargetSearch = Now;
	}

	const bool ShouldSearch = !AimbotSettings.TargetLock || !LastTarget;
	if (ShouldSearch && Now >= NextTargetSearch)
	{
		LastTarget = Utils::GetBestTarget(
			GVars.PlayerController,
			AimbotSettings.TargetCivilians,
			AimbotSettings.TargetArrested,
			AimbotSettings.TargetSurrendered,
			AimbotSettings.TargetDead,
			AimbotSettings.MaxFOV,
			AimbotSettings.LOS,
			TextVars.AimbotBone,
			AimbotSettings.TargetAll,
			AimbotSettings.MinDistance,
			AimbotSettings.MaxDistance
		);
		NextTargetSearch = Now + std::chrono::milliseconds(30);
	}

	AActor* Target = LastTarget;
	if (!Target || !Utils::IsValidActor(Target) || !Target->IsA(AReadyOrNotCharacter::StaticClass())) return;

	auto* TargetCharacter = reinterpret_cast<AReadyOrNotCharacter*>(Target);
	if (!TargetCharacter->Mesh) return;

	FVector CameraPos = GVars.POV->Location;
	FVector TargetPos = TargetCharacter->Mesh->GetBoneTransform(BoneName, ERelativeTransformSpace::RTS_World).Translation;
	const double Distance = CameraPos.GetDistanceToInMeters(TargetPos);
	if (Distance < AimbotSettings.MinDistance || Distance > AimbotSettings.MaxDistance)
	{
		LastTarget = nullptr;
		NextTargetSearch = Now;
		return;
	}

	if (AimbotSettings.Prediction)
	{
		float ProjectileSpeed = 37000.0f;
		if (GVars.ReadyOrNotChar->GetEquippedWeapon())
		{
			float WeaponProjectileSpeed = GVars.ReadyOrNotChar->GetEquippedWeapon()->ProjectileMovementSpeed;
			if (WeaponProjectileSpeed > 0.0f)
				ProjectileSpeed = WeaponProjectileSpeed;
		}

		const double Distance = TargetPos.GetDistanceTo(GVars.ReadyOrNotChar->K2_GetActorLocation());

		const double TimeToHit = Distance / ProjectileSpeed;

		FVector PredictedPos = TargetPos + ((Target->GetVelocity() * TimeToHit) * AimbotSettings.PredictionMultiplier);

		TargetPos = PredictedPos;
	}

	if (DrawVisuals && AimbotSettings.DrawArrow)
		Utils::DrawSnapLine(TargetPos, AimbotSettings.ArrowThickness);

	FVector AngleDiff = {
	TargetPos.X - CameraPos.X,
	TargetPos.Y - CameraPos.Y,
	TargetPos.Z - CameraPos.Z
	};

	// compute desired yaw/pitch from AngleDiff
	float DesiredYaw = (float)(atan2(AngleDiff.Y, AngleDiff.X) * 180.0f / std::numbers::pi);
	float DesiredPitch = (float)(atan2(AngleDiff.Z, std::sqrt(AngleDiff.X * AngleDiff.X + AngleDiff.Y * AngleDiff.Y)) * 180.0f / std::numbers::pi);

	if (AimbotSettings.Smooth)
	{
		FRotator CurrentRot = GVars.PlayerController->ControlRotation;
		const float SmoothFactor = AimbotSettings.SmoothingVector > 1.0f
			? AimbotSettings.SmoothingVector
			: 1.0f;
		const float Alpha = 1.0f / SmoothFactor;
		double YawDelta = DesiredYaw - CurrentRot.Yaw;
		while (YawDelta > 180.0f) YawDelta -= 360.0f;
		while (YawDelta < -180.0f) YawDelta += 360.0f;

		CurrentRot.Yaw += YawDelta * Alpha;
		CurrentRot.Pitch += (DesiredPitch - CurrentRot.Pitch) * Alpha;
		CurrentRot.Normalize();
		GVars.PlayerController->ControlRotation.Yaw = CurrentRot.Yaw;
		GVars.PlayerController->ControlRotation.Pitch = CurrentRot.Pitch;
	}
	else
	{
		// No smoothing - snap directly
		GVars.PlayerController->ControlRotation.Yaw = DesiredYaw;
		GVars.PlayerController->ControlRotation.Pitch = DesiredPitch;
	}
}