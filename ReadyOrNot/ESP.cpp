#include "pch.h"
#include "Cheats.h"
#include "Utils.h"
#include "Overlay.h"

#include <chrono>
#include <array>
#include <vector>
#include <vcruntime_string.h>
#include <algorithm>

struct BonePair { int Parent; int Child; };

FVector TransformComponentPoint(const FVector& Point, const FTransform& Transform)
{
	const FVector ScaledPoint{
		Point.X * Transform.Scale3D.X,
		Point.Y * Transform.Scale3D.Y,
		Point.Z * Transform.Scale3D.Z
	};
	const FVector Axis{ Transform.Rotation.X, Transform.Rotation.Y, Transform.Rotation.Z };
	const FVector TwiceCross{
		2.0 * (Axis.Y * ScaledPoint.Z - Axis.Z * ScaledPoint.Y),
		2.0 * (Axis.Z * ScaledPoint.X - Axis.X * ScaledPoint.Z),
		2.0 * (Axis.X * ScaledPoint.Y - Axis.Y * ScaledPoint.X)
	};
	const FVector RotatedPoint{
		ScaledPoint.X + Transform.Rotation.W * TwiceCross.X +
			(Axis.Y * TwiceCross.Z - Axis.Z * TwiceCross.Y),
		ScaledPoint.Y + Transform.Rotation.W * TwiceCross.Y +
			(Axis.Z * TwiceCross.X - Axis.X * TwiceCross.Z),
		ScaledPoint.Z + Transform.Rotation.W * TwiceCross.Z +
			(Axis.X * TwiceCross.Y - Axis.Y * TwiceCross.X)
	};
	return FVector(
		RotatedPoint.X + Transform.Translation.X,
		RotatedPoint.Y + Transform.Translation.Y,
		RotatedPoint.Z + Transform.Translation.Z);
}

BonePair SuspectSkeletonBones_1[] = {
    {50, 51}, // neck_1 -> Head
    {9, 50}, // spine_3 -> neck_1
    {8, 9}, // spine_2 -> spine_3
    {7, 8}, // spine_1 -> spine_2
    {10, 11}, // clavicle_LE -> upperarm_LE
    {11, 12}, // upperarm_LE -> lowerarm_LE
    {12, 15}, // lowerarm_LE -> hand_LE
    {62, 63}, // clavicle_RI -> upperarm_RI
    {63, 64}, // upperarm_RI -> lowerarm_RI
    {64, 67}, // lowerarm_RI -> hand_RI
    {103, 104}, // thigh_LE -> calf_LE
    {104, 107}, // calf_LE -> foot_LE
    {115, 116}, // thigh_RI -> calf_RI
    {116, 119}, // calf_RI -> foot_RI
};

BonePair SuspectSkeletonBones_2[] = {
    {44, 45}, // Neck -> Head
    {5, 44},  // Spine3 -> Neck
    {3, 5},   // Spine2 -> Spine3
    {2, 3},   // Spine1 -> Spine2
    {6, 7},   // Left clavicle -> UpperArm_LE
    {7, 8},   // UpperArm_LE -> LowerArm_LE
    {8, 11},  // LowerArm_LE -> Hand_LE
    {49, 50}, // Right clavicle -> UpperArm_RI
    {50, 51}, // UpperArm_RI -> LowerArm_RI
    {51, 54}, // LowerArm_RI -> Hand_RI
    {111, 112}, // Thigh_LE -> Calf_LE
    {112, 115}, // Calf_LE -> Foot_LE
    {99, 100},  // Thigh_RI -> Calf_RI
    {100, 103}  // Calf_RI -> Foot_RI
};

BonePair SuspectSkeletonBones[] = {
    {44, 45}, // Neck -> Head
    {5, 44},  // Spine3 -> Neck
    {3, 5},   // Spine2 -> Spine3
    {2, 3},   // Spine1 -> Spine2
    {6, 7},   // Left clavicle -> UpperArm_LE
    {7, 8},   // UpperArm_LE -> LowerArm_LE
    {8, 11},  // LowerArm_LE -> Hand_LE
    {49, 50}, // Right clavicle -> UpperArm_RI
    {50, 51}, // UpperArm_RI -> LowerArm_RI
    {51, 54}, // LowerArm_RI -> Hand_RI
    {111, 112}, // Thigh_LE -> Calf_LE
    {112, 115}, // Calf_LE -> Foot_LE
    {99, 100},  // Thigh_RI -> Calf_RI
    {100, 103}  // Calf_RI -> Foot_RI
};

BonePair CivilianSkeletonBones_1[] = {
    {50, 51}, // Neck -> Head
    {9, 50},  // Spine3 -> Neck
    {7, 9},   // Spine2 -> Spine3
    {1, 7},   // Spine1 -> Spine2
    {10, 11}, // Left clavicle -> UpperArm_LE
    {11, 12}, // UpperArm_LE -> LowerArm_LE
    {12, 15}, // LowerArm_LE -> Hand_LE
    {62, 63}, // Right clavicle -> UpperArm_RI
    {63, 64}, // UpperArm_RI -> LowerArm_RI
    {64, 67}, // LowerArm_RI -> Hand_RI
    {103, 104}, // Thigh_LE -> Calf_LE
    {104, 107}, // Calf_LE -> Foot_LE
    {115, 116}, // Thigh_RI -> Calf_RI
    {116, 119}  // Calf_RI -> Foot_RI
};

BonePair CivilianSkeletonBones_2[] = {
    {44, 45}, // neck_1 -> Head
    {5, 44}, // spine_3 -> neck_1
    {4, 5}, // spine_2 -> spine_3
    {3, 4}, // spine_1 -> spine_2
    {6, 7}, // clavicle_LE -> upperarm_LE
    {7, 8}, // upperarm_LE -> lowerarm_LE
    {8, 11}, // lowerarm_LE -> hand_LE
    {49, 50}, // clavicle_RI -> upperarm_RI
    {50, 51}, // upperarm_RI -> lowerarm_RI
    {51, 54}, // lowerarm_RI -> hand_RI
    {111, 112}, // thigh_LE -> calf_LE
    {112, 115}, // calf_LE -> foot_LE
    {99, 100}, // thigh_RI -> calf_RI
    {100, 103}, // calf_RI -> foot_RI
};

BonePair CivilianSkeletonBones[] = {
    {44, 45}, // neck_1 -> Head
    {5, 44}, // spine_3 -> neck_1
    {4, 5}, // spine_2 -> spine_3
    {3, 4}, // spine_1 -> spine_2
    {6, 7}, // clavicle_LE -> upperarm_LE
    {7, 8}, // upperarm_LE -> lowerarm_LE
    {8, 11}, // lowerarm_LE -> hand_LE
    {49, 50}, // clavicle_RI -> upperarm_RI
    {50, 51}, // upperarm_RI -> lowerarm_RI
    {51, 54}, // lowerarm_RI -> hand_RI
    {111, 112}, // thigh_LE -> calf_LE
    {112, 115}, // calf_LE -> foot_LE
    {99, 100}, // thigh_RI -> calf_RI
    {100, 103}, // calf_RI -> foot_RI
};

bool IsSuspect = false;
bool IsSwat = false;
bool IsPlayer = false;
int32 ViewportX = 0;
int32 ViewportY = 0;

		uint32_t RenderColor = Colors::White;

void Cheats::RenderESP()
{
	if (!CVars.ESP) return;
    if (!GVars.PlayerController || !GVars.Level || !GVars.POV) return;

    ULevel* Level = GVars.Level;
    if (!Level) return; 

    TArray<AActor*> ActorsCopy = Level->Actors; // snapshot to prevent mid-iteration changes causing crashes
	if (ActorsCopy.Num() == 0) return;

	TArray<AActor*> IgnoreActors;
	if (GVars.ReadyOrNotChar)
		IgnoreActors.Add(GVars.ReadyOrNotChar);

    for (AActor* Actor : ActorsCopy)
    {
        if (!Actor || !Utils::IsValidActor(Actor)) continue;

        if (ESPSettings.ShowTraps)
        {
	        FVector2D TrapScreen;

        	if (Actor->IsA(ATrapActor::StaticClass()) && Utils::SafeProjectWorldLocationToScreen(GVars.PlayerController, Actor->K2_GetActorLocation(), &TrapScreen, true))
        	{
				const char* TrapTypeName = "Unknown Trap";
				if (((ATrapActor*)Actor)->TrapType == ETrapType::Explosive)
					TrapTypeName = "Explosive Trap";
				else if (((ATrapActor*)Actor)->TrapType == ETrapType::Flashbang)
                    TrapTypeName = "FlashBang Trap";
				else if (((ATrapActor*)Actor)->TrapType == ETrapType::Alarm)
					TrapTypeName = "Alarm Trap";

				Overlay::Circle(TrapScreen.X, TrapScreen.Y, 3, Overlay::FromPackedColor(Colors::Red), 1.0f, true);
				Overlay::Text(TrapScreen.X + 5, TrapScreen.Y - 5, Overlay::FromPackedColor(Colors::Red), TrapTypeName);
                continue; // We can continue as there is no chance this can be a Ready Or Not character anymore so we save some performance by skipping.
        	}
        }

		if (ESPSettings.ShowObjectives)
        {
            if (Actor->IsA(AReportableActor::StaticClass()))
            {
                AReportableActor* ReportableActor = reinterpret_cast<AReportableActor*>(Actor);

                FVector2D ObjectiveScreen;

                if (Utils::SafeProjectWorldLocationToScreen(GVars.PlayerController, ReportableActor->K2_GetActorLocation(), &ObjectiveScreen, true))
                {
					uint32_t ObjectiveColor = ReportableActor->bReportableEnabled ? Colors::Gray : Colors::Green;
					Overlay::Circle(ObjectiveScreen.X, ObjectiveScreen.Y, 3, Overlay::FromPackedColor(ObjectiveColor), 1.0f, true);
					Overlay::Text(ObjectiveScreen.X + 5, ObjectiveScreen.Y - 5, Overlay::FromPackedColor(ObjectiveColor), ReportableActor->ReportableName.ToString());
                    continue; // We can continue as there is no chance this can be a Ready Or Not character anymore so we save some performance by skipping.
                }
            }
            else if (Actor->IsA(ABombActor::StaticClass())) // credit to CrimsonSpark for the bomb ESP idea
            {
                ABombActor* BombActor = reinterpret_cast<ABombActor*>(Actor);
                FVector2D BombScreen;
                if (Utils::SafeProjectWorldLocationToScreen(GVars.PlayerController, BombActor->K2_GetActorLocation(), &BombScreen, true))
                {
					uint32_t BombColor;
					if (BombActor->BombState == EBombState::BS_Disabled || BombActor->BombState == EBombState::BS_Exploded || BombActor->BombState == EBombState::BS_HiddenAndFullyDisabled)
                        BombColor = Colors::Green;
                    else if (BombActor->BombState == EBombState::BS_Active)
                        BombColor = Colors::Red;
                    else
                        BombColor = Colors::Gray;
					Overlay::Circle(BombScreen.X, BombScreen.Y, 3, Overlay::FromPackedColor(BombColor), 1.0f, true);
					Overlay::Text(BombScreen.X + 5, BombScreen.Y - 5, Overlay::FromPackedColor(BombColor), "Bomb");
                    continue; // We can continue as there is no chance this can be a Ready Or Not character anymore so we save some performance by skipping.
                }
			}
        }

        AReadyOrNotCharacter* TargetActor = nullptr;

        if (Actor->IsA(ASuspectCharacter::StaticClass()) || Actor->IsA(ACivilianCharacter::StaticClass()))
        {
			TargetActor = reinterpret_cast<AReadyOrNotCharacter*>(Actor);
			IsSwat = false;
            IsPlayer = false;
        }
        else if (Actor->IsA(APlayerCharacter::StaticClass()))
        {
            TargetActor = reinterpret_cast<AReadyOrNotCharacter*>(Actor);
            IsSuspect = false;
            IsSwat = true;
            IsPlayer = true;
        }
		else if (ESPSettings.ShowTeam && Actor->IsA(ASWATCharacter::StaticClass())) 
		{
			TargetActor = reinterpret_cast<AReadyOrNotCharacter*>(Actor);
            IsSuspect = false;
			IsSwat = true;
            IsPlayer = false;
		}
    	else
			continue;

        if (!TargetActor) continue;

        if (GVars.ReadyOrNotChar && GVars.ReadyOrNotChar == TargetActor) continue;

        if (!IsSwat && TargetActor->IsSuspect())
            IsSuspect = true;
        else
			IsSuspect = false;

		if (IsSuspect) RenderColor = Utils::ConvertColorToPacked(ESPSettings.SuspectColor);
		else RenderColor = Utils::ConvertColorToPacked(ESPSettings.CivilianColor);

		if (IsSwat) RenderColor = Utils::ConvertColorToPacked(ESPSettings.TeamColor);

		if (TargetActor->IsDeadOrUnconscious() || TargetActor->IsIncapacitated()) RenderColor = Utils::ConvertColorToPacked(ESPSettings.DeadColor);
		else if (!IsSwat && TargetActor->IsArrestedOrSurrendered() || !IsSwat && TargetActor->bIsBeingArrested) RenderColor = Utils::ConvertColorToPacked(ESPSettings.ArrestColor);

        USkeletalMeshComponent* Mesh = TargetActor->Mesh;
        if (!Mesh) continue;

        const bool DrawSkeleton = ESPSettings.Bones || ESPSettings.ShowBox;
        const BonePair* SkeletonBones = nullptr;
        size_t SkeletonBoneCount = 0;
        if (DrawSkeleton)
        {
            static const FName HeadBoneName =
                UKismetStringLibrary::Conv_StringToName(L"Head");
            const bool HasHeadAtIndex45 = Mesh->GetBoneName(45) == HeadBoneName;
            if (IsSuspect)
            {
                SkeletonBones = HasHeadAtIndex45 ? SuspectSkeletonBones_2 : SuspectSkeletonBones_1;
                SkeletonBoneCount = std::size(SuspectSkeletonBones_1);
            }
            else
            {
                SkeletonBones = HasHeadAtIndex45 ? CivilianSkeletonBones_2 : CivilianSkeletonBones_1;
                SkeletonBoneCount = std::size(CivilianSkeletonBones_1);
            }
        }

        auto GetBoneScreenPosition = [&](int BoneIndex, FVector2D& ScreenPosition)
        {
                if (BoneIndex < 0 || BoneIndex >= Mesh->GetNumBones())
            {
                return false;
            }

            const FName BoneName = Mesh->GetBoneName(BoneIndex);
            const FTransform BoneTransform = Mesh->GetBoneTransform(BoneName, ERelativeTransformSpace::RTS_World);
            if (BoneTransform.Translation.IsZero())
            {
                return false;
            }

            const FVector BoneLocation = BoneTransform.Translation;
            if (!Utils::SafeProjectWorldLocationToScreen(GVars.PlayerController, BoneLocation, &ScreenPosition, true))
            {
                return false;
            }

            return true;
        };
		bool HasBoxBounds = false;
		FVector2D TopLeft{};
		FVector2D BottomRight{};
		bool HasVisibleBone = !ESPSettings.LOS;
		if (ESPSettings.Bones)
			GVars.PlayerController->GetViewportSize(&ViewportX, &ViewportY);
		if (ESPSettings.LOS)
		{
			static const FName VisibilityBone =
				UKismetStringLibrary::Conv_StringToName(L"Head");
			const FVector VisibilityPoint = Mesh->GetBoneTransform(
				VisibilityBone,
				ERelativeTransformSpace::RTS_World).Translation;
			FHitResult HitResult;
			UKismetSystemLibrary::LineTraceSingle(
				GVars.World,
				GVars.POV->Location,
				VisibilityPoint,
				ETraceTypeQuery::TraceTypeQuery1,
				true,
				IgnoreActors,
				EDrawDebugTrace::None,
				&HitResult,
				true,
				FLinearColor(),
				FLinearColor(),
				0.0f);

			auto* HitComponent = HitResult.Component.Get();
			AActor* HitActor = HitComponent ? HitComponent->GetOwner() : nullptr;
			HasVisibleBone = !HitResult.bBlockingHit || HitActor == TargetActor;
			if (!HasVisibleBone)
				continue;
		}

		if (DrawSkeleton)
		{
			for (size_t BoneIndex = 0; BoneIndex < SkeletonBoneCount; ++BoneIndex)
			{
				const BonePair& Pair = SkeletonBones[BoneIndex];
				FVector2D ParentScreen, ChildScreen;

				if (GetBoneScreenPosition(Pair.Parent, ParentScreen) &&
					GetBoneScreenPosition(Pair.Child, ChildScreen))
				{
					if (ESPSettings.ShowBox)
					{
						if (!HasBoxBounds)
						{
							TopLeft = ParentScreen;
							BottomRight = ParentScreen;
							HasBoxBounds = true;
						}
						for (const FVector2D& Position : { ParentScreen, ChildScreen })
						{
							TopLeft.X = (std::min)(TopLeft.X, Position.X);
							TopLeft.Y = (std::min)(TopLeft.Y, Position.Y);
							BottomRight.X = (std::max)(BottomRight.X, Position.X);
							BottomRight.Y = (std::max)(BottomRight.Y, Position.Y);
						}
					}

					if (ESPSettings.Bones)
					{
						if ((ParentScreen.X == 0.0 && ParentScreen.Y == 0.0) ||
							ParentScreen.X > ViewportX || ParentScreen.Y > ViewportY)
							continue;
						Overlay::Line(ParentScreen.X, ParentScreen.Y, ChildScreen.X, ChildScreen.Y,
							Overlay::FromPackedColor(RenderColor), 1.5f);
					}
				}
			}
		}

		if (HasVisibleBone && ESPSettings.ShowEnemyDistance)
		{
			FVector ActorLocation = TargetActor->K2_GetActorLocation();
			const double Distance = GVars.POV->Location.GetDistanceToInMeters(ActorLocation);
			FVector2D DistanceScreen;
			if (Distance >= 0.0f && Utils::SafeProjectWorldLocationToScreen(GVars.PlayerController, ActorLocation, &DistanceScreen, true))
			{
				char DistanceText[32];
				snprintf(DistanceText, sizeof(DistanceText), "%.1f m", Distance);
				Overlay::Text(DistanceScreen.X, DistanceScreen.Y + 35, Overlay::FromPackedColor(RenderColor), DistanceText);
			}
		}

		FVector2D ActorScreen;
		if (HasVisibleBone && ESPSettings.ShowTeam && IsPlayer && TargetActor->PlayerState && TargetActor->PlayerState->GetPlayerName() &&
			Utils::SafeProjectWorldLocationToScreen(GVars.PlayerController, Actor->K2_GetActorLocation(), &ActorScreen, true))
            {
				Overlay::Text(ActorScreen.X, ActorScreen.Y + 50, Overlay::FromPackedColor(RenderColor),
					TargetActor->PlayerState->GetPlayerName().ToString());
            }

        if (ESPSettings.ShowBox && HasBoxBounds)
        {
			Overlay::Rectangle(TopLeft.X, TopLeft.Y, BottomRight.X, BottomRight.Y,
				Overlay::FromPackedColor(RenderColor), 1.5f);
        }
    }
}
