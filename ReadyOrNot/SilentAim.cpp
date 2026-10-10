#include "pch.h"
#include "Engine.h"
#include <algorithm>

void Cheats::SilentAim(Params::BaseMagazineWeapon_Server_OnFire* FireParams)
{
	if (!CVars.SilentAim || !FireParams || !GVars.ReadyOrNotChar || !GVars.PlayerController) return;
	const float HitChance = std::clamp(SilentAimSettings.HitChance, 0.0f, 100.0f);
	if (HitChance <= 0.0f)
		return;

	AActor* TargetActor =
		Utils::GetBestTarget(
			GVars.PlayerController,
			SilentAimSettings.TargetCivilians,
			SilentAimSettings.TargetArrested,
			SilentAimSettings.TargetSurrendered,
			SilentAimSettings.TargetDead,
			SilentAimSettings.MaxFOV,
			SilentAimSettings.RequiresLOS,
			TextVars.SilentAimBone,
			SilentAimSettings.TargetAll);

	if (!TargetActor || !TargetActor->IsA(AReadyOrNotCharacter::StaticClass())) return;

	if (HitChance < 100.0f &&
		UKismetMathLibrary::RandomFloatInRange(0.0, 100.0) >= HitChance)
		return;

	auto* Character = reinterpret_cast<AReadyOrNotCharacter*>(TargetActor);
	if (!Character->Mesh) return;

	std::wstring WideString = UtfN::StringToWString(TextVars.SilentAimBone);
	FName BoneName = UKismetStringLibrary::Conv_StringToName(WideString.c_str());
	FVector TargetLocation = Character->Mesh->GetBoneTransform(BoneName, ERelativeTransformSpace::RTS_World).Translation;

	FireParams->SpawnLoc = TargetLocation;
}
