#pragma once
#include "Overlay.h"
#include "Engine.h"
#include <deque>
#include <cfloat>
#include <cmath>

void cerrf(const char* Format, ...);

using namespace SDK;

struct Colors
{
	static constexpr uint32_t White = 0xFFFFFFFF;
	static constexpr uint32_t Black = 0xFF000000;
	static constexpr uint32_t Red = 0xFF0000FF;
	static constexpr uint32_t Green = 0xFF00FF00;
	static constexpr uint32_t Blue = 0xFFFF0000;
	static constexpr uint32_t Yellow = 0xFF00FFFF;
	static constexpr uint32_t Cyan = 0xFFFFFF00;
	static constexpr uint32_t Magenta = 0xFFFF00FF;
	static constexpr uint32_t Gray = 0xFF808080;
	static constexpr uint32_t Orange = 0xFF00A5FF;
	static constexpr uint32_t Purple = 0xFF800080;
	static constexpr uint32_t Pink = 0xFFFFC0CB;
};

enum class ETeam
{
	TEAM_CIVILIAN = 0,
	TEAM_SUSPECT,
	TEAM_SWAT,
	TEAM_MAX
};

// Per-player cheat settings
struct PlayerCheatData
{
	bool GodMode = false;
	bool InfAmmo = false;
	bool GodModeConfigured = false;
	bool InfAmmoConfigured = false;

	// Constructor
	PlayerCheatData() = default;
};

// Global map to store per-player cheat data
inline std::unordered_map<APlayerCharacter*, PlayerCheatData> PlayerCheatMap;

enum class EPlayerMenuAction
{
	GodMode,
	InfiniteAmmo,
	Teleport,
	Speed
};

struct PlayerMenuEntry
{
	APlayerCharacter* Player = nullptr;
	std::string Name;
	bool CanModify = false;
	bool GodMode = false;
	bool InfiniteAmmo = false;
};

struct PlayerMenuRequest
{
	APlayerCharacter* Player = nullptr;
	EPlayerMenuAction Action = EPlayerMenuAction::GodMode;
};

inline std::mutex PlayerMenuMutex;
inline std::vector<PlayerMenuEntry> PlayerMenuEntries;
inline std::deque<PlayerMenuRequest> PlayerMenuRequests;
inline std::recursive_mutex SettingsMutex;

inline float GetDistance(AActor* Actor, FVector AActorLocation)
{
	if (!Actor || !Actor->RootComponent)
		return -1.0f;
	const auto RootComponent = Actor->RootComponent;
	auto deltaX = (float)(RootComponent->RelativeLocation.X - AActorLocation.X);
	auto deltaY = (float)(RootComponent->RelativeLocation.Y - AActorLocation.Y);
	auto deltaZ = (float)(RootComponent->RelativeLocation.Z - AActorLocation.Z);

	return (float)std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
}

struct Utils
{
	static UWorld* GetWorldSafe(); // can return nullptr
	static APlayerController* GetPlayerController(); // can return nullptr
	static unsigned ConvertColorToPacked(Overlay::FloatColor Color);
	static void PrintActors(const char* Exclude);
	static FRotator VectorToRotation(const FVector& Vec);
	static AActor* GetBestTarget(APlayerController* ViewPoint, bool TargetCivs, bool TargetArrested, bool TargetSurrendered, bool TargetDead, float MaxFOV, bool RequiresLOS, const std::string& TargetBone, bool TargetAll, float MinDistance = 0.0f, float MaxDistance = FLT_MAX);
	static void DrawFOV(float MaxFOV, float Thickness);
	static void DrawSnapLine(FVector TargetPos, float Thickness);
	static FVector FRotatorToVector(const FRotator& Rot);
	static PlayerCheatData& GetPlayerCheats(APlayerCharacter* Player);
	static bool IsValidActor(AActor* Actor);
	static float GetFOVFromScreenCoords(const Overlay::Point& ScreenLocation);
	static Overlay::Point FVector2DToScreenPoint(FVector2D Vector);
	static FRotator GetRotationToTarget(const FVector& Start, const FVector& Target);
	static FVector2D ScreenPointToFVector2D(Overlay::Point Vector);
	static ACharacter* GetNearestCharacter(ETeam Team);
	static void Error(std::string msg);
	static bool SafeProjectWorldLocationToScreen(APlayerController* Controller, const FVector& WorldPosition, FVector2D* OutScreenPosition, const bool PlayerViewportRelative);
};

struct Variables // horrible naming scheme holy shit
{
	APlayerController* PlayerController = nullptr;
	bool HasAuthority = false;
	FMinimalViewInfo* POV = nullptr;
	APawn* Pawn = nullptr;
	ACharacter* Character = nullptr;
	AReadyOrNotCharacter* ReadyOrNotChar = nullptr;
	UWorld* World = nullptr;
	AReadyOrNotGameState* GameState = nullptr;
	TArray<APlayerCharacter*> Players = TArray<APlayerCharacter*>();
	ULevel* Level = nullptr;
	Overlay::Point ScreenSize;

	// Constructor to initialize variables safely
	Variables() {
		AutoSetVariables();
	}

	void AutoSetVariables() {

		// Get PlayerController first
		APlayerController* currentPC = Utils::GetPlayerController();
		if (!currentPC) {

			// Clear all dependent variables if PlayerController is null
			this->PlayerController = nullptr;
			this->POV = nullptr;
			this->Pawn = nullptr;
			this->Character = nullptr;
			this->ReadyOrNotChar = nullptr;
			this->World = Utils::GetWorldSafe();
			if (this->World && this->World->GameState && this->World->GameState->IsA(AReadyOrNotGameState::StaticClass()))
				this->GameState = this->World ? static_cast<AReadyOrNotGameState*>(this->World->GameState) : nullptr;
			else
				this->GameState = nullptr;
			this->Players = this->GameState ? this->GameState->AllPlayerCharacters : TArray<APlayerCharacter*>();
			this->Level = this->World ? this->World->PersistentLevel : nullptr;
			this->HasAuthority = false;
			return;
		}

		this->HasAuthority = currentPC->HasAuthority();
		// Update PlayerController if changed
		if (this->PlayerController != currentPC) {
			this->PlayerController = currentPC;
			// Reset dependent variables when PlayerController changes
			this->Pawn = nullptr;
			this->Character = nullptr;
			this->ReadyOrNotChar = nullptr;
		}

		// Update Pawn
		if (this->PlayerController && this->Pawn != this->PlayerController->Pawn) {
			this->Pawn = this->PlayerController->Pawn;
			// Reset Character-dependent variables when Pawn changes
			this->Character = nullptr;
			this->ReadyOrNotChar = nullptr;
		}

		// Update Character
		if (this->PlayerController && this->Character != this->PlayerController->Character) {
			this->Character = this->PlayerController->Character;
			// Reset ReadyOrNotChar when Character changes
			this->ReadyOrNotChar = nullptr;
		}

		// Update ReadyOrNotChar
		if (this->Character) {
			AReadyOrNotCharacter* newReadyOrNotChar = static_cast<AReadyOrNotCharacter*>(this->Character);
			if (this->ReadyOrNotChar != newReadyOrNotChar) {
				this->ReadyOrNotChar = newReadyOrNotChar;
			}
		}

		// Update World
		UWorld* currentWorld = Utils::GetWorldSafe();
		if (this->World != currentWorld) {
			this->World = currentWorld;
			this->Level = nullptr; // Reset Level when World changes
			this->GameState = nullptr; // Reset GameState when World changes
			this->Players = TArray<APlayerCharacter*>(); // Reset Players when World changes
		}

		// Update Level
		if (this->World && this->Level != this->World->PersistentLevel) {
			this->Level = this->World->PersistentLevel;
		}

		// Update GameState
		if (this->World && this->World->GameState && this->GameState != this->World->GameState && this->World->GameState->IsA(AReadyOrNotGameState::StaticClass())) 
			this->GameState = static_cast<AReadyOrNotGameState*>(this->World->GameState);

		if (this->PlayerController && this->PlayerController->PlayerCameraManager) {
			this->POV = &this->PlayerController->PlayerCameraManager->CameraCachePrivate.POV;
		}
		else {
			this->POV = nullptr;
		}

	}

	void Cleanup() {
		this->PlayerController = nullptr;
		this->POV = nullptr;
		this->Pawn = nullptr;
		this->Character = nullptr;
		this->ReadyOrNotChar = nullptr;
		this->World = nullptr;
		this->GameState = nullptr;
		this->Players = TArray<APlayerCharacter*>();
		this->Level = nullptr;
		this->HasAuthority = false;
	}
} inline GVars;