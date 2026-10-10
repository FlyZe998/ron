#include "pch.h"
#include "Engine.h"
#include "Overlay.h"
#include <algorithm>

using namespace SDK;

// can return nullptr
UWorld* Utils::GetWorldSafe() 
{
    UEngine* Engine = UEngine::GetEngine();
    if (!Engine || !Engine->GameViewport)
        return nullptr;

    return Engine->GameViewport->World;
}

// can return nullptr
APlayerController* Utils::GetPlayerController()
{
	APlayerController* PlayerController = nullptr;
    
    UWorld* World = GetWorldSafe();
    if (!World)
        return nullptr;

    UGameInstance* GameInstance = World->OwningGameInstance;
    if (!GameInstance)
        return nullptr;

    if (GameInstance->LocalPlayers.Num() <= 0)
        return nullptr;

    ULocalPlayer* LocalPlayer = GameInstance->LocalPlayers[0];
    if (!LocalPlayer)
        return nullptr;

    PlayerController = LocalPlayer->PlayerController;
    if (!PlayerController)
        return nullptr;

    if (!Utils::IsValidActor(PlayerController))
        return nullptr;
    
    return PlayerController;
}

unsigned Utils::ConvertColorToPacked(Overlay::FloatColor Color)
{
	const uint32_t R = static_cast<uint32_t>(std::clamp(Color.x, 0.0f, 1.0f) * 255.0f);
	const uint32_t G = static_cast<uint32_t>(std::clamp(Color.y, 0.0f, 1.0f) * 255.0f);
	const uint32_t B = static_cast<uint32_t>(std::clamp(Color.z, 0.0f, 1.0f) * 255.0f);
	const uint32_t A = static_cast<uint32_t>(std::clamp(Color.w, 0.0f, 1.0f) * 255.0f);
	return R | (G << 8) | (B << 16) | (A << 24);
}

void Utils::PrintActors(const char* Exclude)
{
	ULevel* Level = GVars.Level;
	if (Level)
	{
        TArray<AActor*> Actors = Level->Actors;
        for (int i = 0; i < Actors.Num(); i++)
        {
            AActor* Actor = Actors[i];
            if (Actor)
            {
                if (!Utils::IsValidActor(Actor)) continue;
                if (Exclude && Actor->GetName().find(Exclude) != std::string::npos)
					continue;

                printf("Actor %d: %s - Class: %s\n", i, Actor->GetName().c_str(), Actor->Class->Name.ToString().c_str());
            }
		}
	}
}

FRotator Utils::VectorToRotation(const FVector& Vec)
{
    FRotator Rot;
    Rot.Yaw = std::atan2(Vec.Y, Vec.X) * (180.0 / std::numbers::pi);
    Rot.Pitch = std::atan2(Vec.Z, std::sqrt(Vec.X * Vec.X + Vec.Y * Vec.Y)) * (180.0 / std::numbers::pi);
    Rot.Roll = 0.0;
    return Rot;
}

FVector Utils::FRotatorToVector(const FRotator& Rot)
{
    double PitchRad = Rot.Pitch * (std::numbers::pi / 180.0);
    double YawRad = Rot.Yaw * (std::numbers::pi / 180.0);

    double CP = cos(PitchRad);
    double SP = sin(PitchRad);
    double CY = cos(YawRad);
    double SY = sin(YawRad);

    return FVector(
        CP * CY,   // X
        CP * SY,   // Y
        SP         // Z
    ).GetNormalized(); // normalize just in case
}

// Helper function to get or create player cheat data
PlayerCheatData& Utils::GetPlayerCheats(APlayerCharacter* Player)
{
    return PlayerCheatMap[Player];
}

bool Utils::IsValidActor(AActor* Actor)
{
    if (!Actor) return false;

    if (!Actor->Class || !Actor->VTable)
        return false;

    constexpr uint32_t InvalidObjectFlags =
        static_cast<uint32_t>(EObjectFlags::TagGarbageTemp) |
        static_cast<uint32_t>(EObjectFlags::BeginDestroyed) |
        static_cast<uint32_t>(EObjectFlags::FinishDestroyed) |
        static_cast<uint32_t>(EObjectFlags::MirroredGarbage);
    if ((static_cast<uint32_t>(Actor->Flags) & InvalidObjectFlags) != 0)
        return false;

    if (Actor->bActorIsBeingDestroyed)
        return false;

    return true;
}

float Utils::GetFOVFromScreenCoords(const Overlay::Point& ScreenLocation)
{
	Overlay::Point ScreenCenter{ GVars.ScreenSize.x / 2.0f, GVars.ScreenSize.y / 2.0f };

    float DeltaX = ScreenLocation.x - ScreenCenter.x;
    float DeltaY = ScreenLocation.y - ScreenCenter.y;

    return std::sqrt(DeltaX * DeltaX + DeltaY * DeltaY);
}

Overlay::Point Utils::FVector2DToScreenPoint(FVector2D Vector)
{
	return Overlay::Point{ static_cast<float>(Vector.X), static_cast<float>(Vector.Y) };
}

FRotator Utils::GetRotationToTarget(const FVector& Start, const FVector& Target)
{
    FVector Delta = Target - Start;
    Delta.Normalize(); // Important for safe calculations

    FRotator Rotation;
    Rotation.Pitch = std::atan2(Delta.Z, std::sqrt(Delta.X * Delta.X + Delta.Y * Delta.Y)) * (180.0f / std::numbers::pi);
    Rotation.Yaw = std::atan2(Delta.Y, Delta.X) * (180.0f / std::numbers::pi);
    Rotation.Roll = 0.0f;

    return Rotation;
}

FVector2D Utils::ScreenPointToFVector2D(Overlay::Point Vector)
{
	return FVector2D(Vector.x, Vector.y);
}

AActor* Utils::GetBestTarget(APlayerController* ViewPoint, bool TargetCivs, bool TargetArrested, bool TargetSurrendered, bool TargetDead, float MaxFOV, bool RequiresLOS, const std::string& TargetBone, bool TargetAll, float MinDistance, float MaxDistance)
{
    if (!GVars.World || !GVars.Level || !ViewPoint || !ViewPoint->PlayerCameraManager) return nullptr;

    static std::string CachedBoneName;
    static FName BoneName;
    if (CachedBoneName != TargetBone)
    {
        const std::wstring WideString = UtfN::StringToWString(TargetBone);
        BoneName = UKismetStringLibrary::Conv_StringToName(WideString.c_str());
        CachedBoneName = TargetBone;
    }

    AActor* BestTarget = nullptr;
    if (MaxFOV <= 0.0f)
        return nullptr;

    const FVector2D ViewportSize = Utils::ScreenPointToFVector2D(GVars.ScreenSize);
    if (ViewportSize.Y <= 0.0)
        return nullptr;
    const FVector2D ViewportCenter = ViewportSize / 2.0;
    const float MaxFOVRadius = (MaxFOV / 90.0f) * static_cast<float>(ViewportSize.Y * 0.5);
    const float MaxFOVRadiusSquared = MaxFOVRadius * MaxFOVRadius;
    double BestFOVSquared = MaxFOVRadiusSquared;
    const FVector CameraLocation = ViewPoint->PlayerCameraManager->CameraCachePrivate.POV.Location;
    TArray<AActor*> IgnoreActors;
    if (GVars.ReadyOrNotChar)
        IgnoreActors.Add(GVars.ReadyOrNotChar);

	TArray<AActor*> Actors = GVars.Level->Actors;
	if (!Actors || !Actors.IsValid() || Actors.Num() <= 0)
		return nullptr;
	if (GVars.ScreenSize.y <= 0.0f)
		return nullptr;

    for (AActor* Actor : Actors)
    {
        if (!Actors)
            break;
        if (!Actor || !Utils::IsValidActor(Actor))
            continue;
		if (Actor == GVars.ReadyOrNotChar)
			continue;

        AReadyOrNotCharacter* ReadyOrNotChar;

        if (Actor->IsA(AReadyOrNotCharacter::StaticClass()))
        {
            ReadyOrNotChar = reinterpret_cast<AReadyOrNotCharacter*>(Actor);
            if (!ReadyOrNotChar)
                continue;
            if (!TargetAll)
            {
	            bool bIsCivilian = ReadyOrNotChar->IsCivilian();
            	bool bIsSuspect = ReadyOrNotChar->IsSuspect();

            	// Keep target if suspect OR (civilian AND TargetCivs)
            	if (!(bIsSuspect || (bIsCivilian && TargetCivs)))
            		continue;
            }
        }
        else
            continue;

        if (!ReadyOrNotChar)
            continue;
        if (!TargetDead && (ReadyOrNotChar->IsDeadOrUnconscious() || ReadyOrNotChar->IsIncapacitated()))
            continue;
        if (!TargetArrested && ReadyOrNotChar->IsArrested())
            continue;
        if (!TargetSurrendered && ReadyOrNotChar->IsSurrendered())
            continue;
		if (!ReadyOrNotChar->Mesh)
			continue;

        // Get the target bone location
        FVector BoneLocation = ReadyOrNotChar->Mesh->GetBoneTransform(BoneName, ERelativeTransformSpace::RTS_World).Translation;
		const double Distance = CameraLocation.GetDistanceToInMeters(BoneLocation);
		if (Distance < MinDistance || Distance > MaxDistance)
			continue;

        FVector2D ScreenLocation;
        if (!ViewPoint->ProjectWorldLocationToScreen(BoneLocation, &ScreenLocation, true))
            continue;

        FVector2D Delta = ScreenLocation - ViewportCenter;
        const double FOVSquared = Delta.X * Delta.X + Delta.Y * Delta.Y;
        if (FOVSquared >= BestFOVSquared)
            continue;

        if (RequiresLOS)
        {
            FHitResult HitResult;
            UKismetSystemLibrary::LineTraceSingle(
                GVars.World,
                CameraLocation,
                BoneLocation,
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
            if (HitResult.bBlockingHit && HitActor != ReadyOrNotChar)
                continue;
        }

        if (FOVSquared < BestFOVSquared)
        {
            BestFOVSquared = FOVSquared;
            BestTarget = Actor;
        }
    }
    if (BestTarget)
        return BestTarget;
    return nullptr;
}

void Utils::DrawFOV(float MaxFOV, float Thickness = 1.0f)
{
    FVector2D ViewportSize = Utils::ScreenPointToFVector2D(GVars.ScreenSize);
    FVector2D Center = ViewportSize * 0.5f;

    float MaxFOVNormalized = MaxFOV / 90.0f;
    const double RadiusPixels = MaxFOVNormalized * (ViewportSize.Y * 0.5f);

	Overlay::Circle(Center.X, Center.Y, RadiusPixels, Overlay::Color{ 255, 0, 0, 255 }, Thickness);
}

void Utils::DrawSnapLine(FVector TargetPos, float Thickness = 2.0f)
{
    FVector2D ScreenPos;

    if (!GVars.PlayerController->ProjectWorldLocationToScreen(TargetPos, &ScreenPos, true))
        return;

	Overlay::Line(GVars.ScreenSize.x / 2, GVars.ScreenSize.y / 2, ScreenPos.X, ScreenPos.Y, Overlay::Color{ 255, 255, 255, 255 }, Thickness);
	Overlay::Circle(ScreenPos.X, ScreenPos.Y, 2.0f, Overlay::Color{ 0, 255, 0, 255 }, 1.0f, true);
}

void Utils::Error(std::string msg)
{
    printf("[Error] %s\n", msg.c_str());
}

void cerrf(const char* Format, ...)
{
    va_list Args;
    va_start(Args, Format);

    // Print to stderr
    vfprintf(stderr, Format, Args);

    va_end(Args);
}

ACharacter* Utils::GetNearestCharacter(ETeam Team)
{
    if (!GVars.Level || !GVars.ReadyOrNotChar) return nullptr;
    ACharacter* NearestCharacter = nullptr;
    double NearestDistance = FLT_MAX;
    for (AActor* Actor : GVars.Level->Actors)
    {
        if (!Actor || !Utils::IsValidActor(Actor))
            continue;
        if (Actor == GVars.ReadyOrNotChar)
            continue;
        AReadyOrNotCharacter* ReadyOrNotChar;
        if (Actor->IsA(AReadyOrNotCharacter::StaticClass()))
        {
            ReadyOrNotChar = reinterpret_cast<AReadyOrNotCharacter*>(Actor);
            if (!ReadyOrNotChar)
                continue;
            bool bIsCivilian = ReadyOrNotChar->IsCivilian();
            bool bIsSuspect = ReadyOrNotChar->IsSuspect();
            if (Team == ETeam::TEAM_CIVILIAN && !bIsCivilian)
                continue;
            if (Team == ETeam::TEAM_SUSPECT && !bIsSuspect)
                continue;
            if (Team == ETeam::TEAM_SWAT && (bIsCivilian || bIsSuspect))
                continue;
        }
        else
            continue;
        FVector PlayerLocation = GVars.ReadyOrNotChar->K2_GetActorLocation();
        FVector TargetLocation = ReadyOrNotChar->K2_GetActorLocation();
        const double Distance = PlayerLocation.GetDistanceTo(TargetLocation);
        if (Distance < NearestDistance)
        {
            NearestDistance = Distance;
            NearestCharacter = ReadyOrNotChar;
        }
    }
	return NearestCharacter;
}

bool Utils::SafeProjectWorldLocationToScreen(APlayerController* Controller, const FVector& WorldPosition, FVector2D* OutScreenPosition, const bool PlayerViewportRelative)
{
    if (Controller)
    {
        return Controller->ProjectWorldLocationToScreen(WorldPosition, OutScreenPosition, PlayerViewportRelative);
    }

    return false;
}
