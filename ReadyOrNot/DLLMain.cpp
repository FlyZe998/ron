#include "pch.h"
#include "Engine.h"
#include "Overlay.h"
#include "OverlayMenu.h"
#include <kiero/kiero.h>
#include <mutex>
#include <cstddef>
#include <type_traits>
#include <utility>

std::atomic<HMODULE> g_hModule{ nullptr };
std::atomic<int> g_PresentCount{ 0 };
std::atomic<bool> Cleaning{ false };
std::recursive_mutex RenderMutex;

static void Cleanup(HMODULE hModule);
void SaveSettings();
void LoadSettings();

namespace
{
	struct PersistedCVarsStruct
	{
		float Speed;
		float FOV;
		float BulletTimeSpeed;
		bool Debug;
		bool SecretFeatures;
		bool GodMode;
		bool InfAmmo;
		bool Aimbot;
		bool ESP;
		bool SpeedEnabled;
		bool SilentAim;
		bool Reticle;
		bool TriggerBot;
		bool RenderOptions;
		bool ListPlayers;
		bool ShootFromReticle;
		bool SaveDebugToFile;
		bool BulletTime;
		bool AntiSway;
		bool InstantMultiTool;
		int MultiFire;
		EQueuedAction QueuedAction;
		bool KeepNoRecoil;
		bool KeepNoSpread;
		bool KeepAutoFire;
		bool KeepPenetration;
		bool KeepInstaKill;
		bool KeepHighFireRate;
		bool TriggerBotHoldKey;
	};

	struct PersistentWeaponPersistedCVarsStruct
	{
		float Speed;
		float FOV;
		float BulletTimeSpeed;
		bool Debug;
		bool SecretFeatures;
		bool GodMode;
		bool InfAmmo;
		bool Aimbot;
		bool ESP;
		bool SpeedEnabled;
		bool SilentAim;
		bool Reticle;
		bool TriggerBot;
		bool RenderOptions;
		bool ListPlayers;
		bool ShootFromReticle;
		bool SaveDebugToFile;
		bool BulletTime;
		bool AntiSway;
		bool InstantMultiTool;
		int MultiFire;
		EQueuedAction QueuedAction;
		bool KeepNoRecoil;
		bool KeepNoSpread;
		bool KeepAutoFire;
		bool KeepPenetration;
		bool KeepInstaKill;
		bool KeepHighFireRate;
	};

	struct LegacyPersistedCVarsStruct
	{
		float Speed;
		float FOV;
		float BulletTimeSpeed;
		bool Debug;
		bool SecretFeatures;
		bool GodMode;
		bool InfAmmo;
		bool Aimbot;
		bool ESP;
		bool SpeedEnabled;
		bool SilentAim;
		bool Reticle;
		bool TriggerBot;
		bool RenderOptions;
		bool ListPlayers;
		bool ShootFromReticle;
		bool SaveDebugToFile;
		bool BulletTime;
		bool AntiSway;
		bool InstantMultiTool;
		int MultiFire;
		EQueuedAction QueuedAction;
	};

	static_assert(sizeof(PersistedCVarsStruct) == sizeof(CVarsStruct));
	static_assert(offsetof(PersistedCVarsStruct, QueuedAction) == offsetof(CVarsStruct, QueuedAction));
	static_assert(offsetof(PersistentWeaponPersistedCVarsStruct, KeepNoRecoil) == offsetof(CVarsStruct, KeepNoRecoil));
	static_assert(sizeof(LegacyPersistedCVarsStruct) == offsetof(CVarsStruct, KeepNoRecoil));

	template <typename T>
	bool ReadBinarySetting(const char* Path, T& Destination)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		std::ifstream File(Path, std::ios::binary | std::ios::ate);
		if (!File.is_open())
		{
			printf("[INFO] %s could not be opened; keeping current settings.\n", Path);
			return false;
		}

		const std::streampos FileSize = File.tellg();
		if (FileSize < 0 || FileSize != static_cast<std::streamoff>(sizeof(T)))
		{
			printf("[ERROR] %s has an invalid size; keeping current settings.\n", Path);
			return false;
		}

		File.seekg(0);
		T Loaded{};
		File.read(reinterpret_cast<char*>(&Loaded), sizeof(Loaded));
		if (!File)
		{
			printf("[ERROR] Failed to read %s; keeping current settings.\n", Path);
			return false;
		}

		Destination = Loaded;
		return true;
	}

	bool ReadTextSetting(std::ifstream& File, std::string& Value)
	{
		constexpr size_t MaxSettingLength = 4096;
		size_t Length = 0;
		File.read(reinterpret_cast<char*>(&Length), sizeof(Length));
		if (!File || Length > MaxSettingLength)
			return false;

		std::string Loaded(Length, '\0');
		if (Length > 0)
			File.read(Loaded.data(), static_cast<std::streamsize>(Length));
		if (!File)
			return false;

		Value = std::move(Loaded);
		return true;
	}

	void ApplyCVars(const PersistedCVarsStruct& Saved)
	{
		CVars.Speed = Saved.Speed;
		CVars.FOV = Saved.FOV;
		CVars.BulletTimeSpeed = Saved.BulletTimeSpeed;
		CVars.Debug = Saved.Debug;
		CVars.SecretFeatures = Saved.SecretFeatures;
		CVars.GodMode = Saved.GodMode;
		CVars.InfAmmo = Saved.InfAmmo;
		CVars.Aimbot = Saved.Aimbot;
		CVars.ESP = Saved.ESP;
		CVars.SpeedEnabled = Saved.SpeedEnabled;
		CVars.SilentAim = Saved.SilentAim;
		CVars.Reticle = Saved.Reticle;
		CVars.TriggerBot = Saved.TriggerBot;
		CVars.RenderOptions = Saved.RenderOptions;
		CVars.ListPlayers = Saved.ListPlayers;
		CVars.ShootFromReticle = Saved.ShootFromReticle;
		CVars.SaveDebugToFile = Saved.SaveDebugToFile;
		CVars.BulletTime = Saved.BulletTime;
		CVars.AntiSway = Saved.AntiSway;
		CVars.InstantMultiTool = Saved.InstantMultiTool;
		CVars.MultiFire = Saved.MultiFire;
		CVars.QueuedAction = EQueuedAction::None;
		CVars.KeepNoRecoil = Saved.KeepNoRecoil;
		CVars.KeepNoSpread = Saved.KeepNoSpread;
		CVars.KeepAutoFire = Saved.KeepAutoFire;
		CVars.KeepPenetration = Saved.KeepPenetration;
		CVars.KeepInstaKill = Saved.KeepInstaKill;
		CVars.KeepHighFireRate = Saved.KeepHighFireRate;
		CVars.TriggerBotHoldKey = Saved.TriggerBotHoldKey;
	}

	bool LoadCVars()
	{
		std::ifstream File("CVars.bin", std::ios::binary | std::ios::ate);
		if (!File.is_open())
		{
			printf("[INFO] CVars.bin could not be opened; keeping current settings.\n");
			return false;
		}

		const std::streampos FileSize = File.tellg();
		File.seekg(0);
		if (FileSize == static_cast<std::streamoff>(sizeof(PersistedCVarsStruct)))
		{
			PersistedCVarsStruct Saved{};
			File.read(reinterpret_cast<char*>(&Saved), sizeof(Saved));
			if (!File)
			{
				printf("[ERROR] Failed to read CVars.bin; keeping current settings.\n");
				return false;
			}
			if (Saved.QueuedAction < EQueuedAction::None || Saved.QueuedAction > EQueuedAction::LoadConfiguration)
			{
				printf("[ERROR] CVars.bin contains an invalid queued action; keeping current feature settings.\n");
				return false;
			}
			ApplyCVars(Saved);
			return true;
		}

		if (FileSize == static_cast<std::streamoff>(sizeof(PersistentWeaponPersistedCVarsStruct)))
		{
			PersistentWeaponPersistedCVarsStruct Previous{};
			File.read(reinterpret_cast<char*>(&Previous), sizeof(Previous));
			if (!File)
			{
				printf("[ERROR] Failed to read previous CVars.bin; keeping current settings.\n");
				return false;
			}
			PersistedCVarsStruct Saved{
				Previous.Speed, Previous.FOV, Previous.BulletTimeSpeed,
				Previous.Debug, Previous.SecretFeatures, Previous.GodMode, Previous.InfAmmo,
				Previous.Aimbot, Previous.ESP, Previous.SpeedEnabled, Previous.SilentAim,
				Previous.Reticle, Previous.TriggerBot, Previous.RenderOptions, Previous.ListPlayers,
				Previous.ShootFromReticle, Previous.SaveDebugToFile, Previous.BulletTime,
				Previous.AntiSway, Previous.InstantMultiTool, Previous.MultiFire,
				Previous.QueuedAction, Previous.KeepNoRecoil, Previous.KeepNoSpread,
				Previous.KeepAutoFire, Previous.KeepPenetration, Previous.KeepInstaKill,
				Previous.KeepHighFireRate, false
			};
			if (Previous.QueuedAction < EQueuedAction::None || Previous.QueuedAction > EQueuedAction::LoadConfiguration)
			{
				printf("[ERROR] Previous CVars.bin contains an invalid queued action; keeping current settings.\n");
				return false;
			}
			ApplyCVars(Saved);
			return true;
		}

		if (FileSize == static_cast<std::streamoff>(sizeof(LegacyPersistedCVarsStruct)))
		{
			LegacyPersistedCVarsStruct Legacy{};
			File.read(reinterpret_cast<char*>(&Legacy), sizeof(Legacy));
			if (!File)
			{
				printf("[ERROR] Failed to read legacy CVars.bin; keeping current settings.\n");
				return false;
			}
			PersistedCVarsStruct Saved{
				Legacy.Speed, Legacy.FOV, Legacy.BulletTimeSpeed,
				Legacy.Debug, Legacy.SecretFeatures, Legacy.GodMode, Legacy.InfAmmo,
				Legacy.Aimbot, Legacy.ESP, Legacy.SpeedEnabled, Legacy.SilentAim,
				Legacy.Reticle, Legacy.TriggerBot, Legacy.RenderOptions, Legacy.ListPlayers,
				Legacy.ShootFromReticle, Legacy.SaveDebugToFile, Legacy.BulletTime,
				Legacy.AntiSway, Legacy.InstantMultiTool, Legacy.MultiFire,
				Legacy.QueuedAction, false, false, false, false, false, false
			};
			if (Legacy.QueuedAction < EQueuedAction::None || Legacy.QueuedAction > EQueuedAction::LoadConfiguration)
			{
				printf("[ERROR] Legacy CVars.bin contains an invalid queued action; keeping current feature settings.\n");
				return false;
			}
			ApplyCVars(Saved);
			return true;
		}

		printf("[ERROR] CVars.bin has an invalid size; keeping current settings.\n");
		return false;
	}
}

HRESULT __stdcall Engine::hkPresent(IDXGISwapChain* SwapChain, UINT SyncInterval, UINT Flags)
{
	struct PresentGuardStruct
	{
		PresentGuardStruct() { g_PresentCount.fetch_add(1); }
		~PresentGuardStruct() { g_PresentCount.fetch_sub(1); }
	} PresentGuard;
	std::unique_lock<std::recursive_mutex> Lock(RenderMutex, std::try_to_lock);
	auto ForwardPresent = [&]() -> HRESULT
	{
		if (Lock.owns_lock())
			Lock.unlock();
		return Engine::oPresent(SwapChain, SyncInterval, Flags);
	};
	if (!Lock.owns_lock() || Cleaning.load() || (Flags & DXGI_PRESENT_TEST))
		return ForwardPresent();

	DXGI_SWAP_CHAIN_DESC OverlaySwapChainDesc{};
	if (FAILED(SwapChain->GetDesc(&OverlaySwapChainDesc)) || !OverlaySwapChainDesc.OutputWindow)
		return ForwardPresent();
	const HWND GameWindow = OverlaySwapChainDesc.OutputWindow;
	RECT ClientRect{};
	if (!GetClientRect(GameWindow, &ClientRect))
		return ForwardPresent();
	const int ClientWidth = ClientRect.right - ClientRect.left;
	const int ClientHeight = ClientRect.bottom - ClientRect.top;
	GVars.ScreenSize = Overlay::Point{ static_cast<float>(ClientWidth), static_cast<float>(ClientHeight) };

	if (GetForegroundWindow() != GameWindow || IsIconic(GameWindow))
	{
		Overlay::Hide();
		return ForwardPresent();
	}

	static bool OverlayRenderFailureLogged = false;
	if (!Overlay::RequestRender(GameWindow) && !OverlayRenderFailureLogged)
	{
		OutputDebugStringA("[Overlay] Failed to queue a frame for the independent overlay window.\n");
		OverlayRenderFailureLogged = true;
	}
	return ForwardPresent();

}

static DWORD WINAPI MainThread(LPVOID Parameter)
{
	HMODULE hModule = static_cast<HMODULE>(Parameter);
	AllocConsole();
	FILE* Dummy;
	freopen_s(&Dummy, "CONOUT$", "w", stdout);

	printf("Cheat Injecting...\n");

	g_hModule.store(hModule);

	MH_STATUS Status = MH_Initialize();
	if (Status != MH_OK)
	{
		printf("[ERROR] MinHook failed to init: %d\n", Status);
		Cleaning.store(true);
		Cleanup(hModule);
		return 1;
	}

	int Attempts = 0;
	while (!GetModuleHandleW(L"d3d12.dll") && !GetModuleHandleW(L"d3d11.dll") && Attempts < 300)
	{
		++Attempts;
		Sleep(100);
	}
	if (!GetModuleHandleW(L"d3d12.dll") && !GetModuleHandleW(L"d3d11.dll"))
	{
		printf("[ERROR] Timed out waiting for Direct3D to load.\n");
		Cleaning.store(true);
		Cleanup(hModule);
		return 1;
	}

	if (Engine::HookPresent() != kiero::Status::Success)
	{
		printf("[ERROR] Failed to initialize engine hooks.\n");
		Cleaning.store(true);
		Cleanup(hModule);
		return 1;
	}
	else
		printf("Graphics hooks initialized; waiting for the game engine.\n");

	Attempts = 0;
	while (!UEngine::GetEngine() && Attempts < 300)
	{
		++Attempts;
		Sleep(100);
	}
	if (!UEngine::GetEngine())
	{
		printf("[ERROR] Timed out waiting for the game engine.\n");
		Cleaning.store(true);
		Cleanup(hModule);
		return 1;
	}

	printf("Cheat Injected\n");

	LoadSettings();

	if (!Overlay::Initialize(hModule))
	{
		printf("[ERROR] Failed to initialize the independent overlay renderer.\n");
		Cleaning.store(true);
		Cleanup(hModule);
		return 1;
	}
	if (!OverlayMenu::Initialize(hModule))
	{
		printf("[ERROR] Failed to initialize the native settings window.\n");
		Cleaning.store(true);
		Cleanup(hModule);
		return 1;
	}

	if (!Hooks::HookProcessEvent())
	{
		printf("[ERROR] Failed to initialize ProcessEvent hook.\n");
		Cleaning.store(true);
		Cleanup(hModule);
		return 1;
	}

	while (!Cleaning.load())
		Sleep(100);

	Cleanup(hModule);

	return 0;
}

// DLL entry point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
	switch (reason) {
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hModule);
		if (HANDLE MainThreadHandle = CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr))
			CloseHandle(MainThreadHandle);
		break;
	case DLL_PROCESS_DETACH:
		Cleaning.store(true);
		break;
	}
	
	return TRUE;
}

void SaveSettings()
{
	// Save MiscSettings (binary)
	std::ofstream MiscSettingsfile("MiscSettings.bin", std::ios::binary);
	if (MiscSettingsfile.is_open())
	{
		MiscSettingsfile.write(reinterpret_cast<char*>(&MiscSettings), sizeof(MiscSettings));
		MiscSettingsfile.close();
	}
	else
		printf("[ERROR] Could not open MiscSettings.bin for writing!\n");

	// Save AimbotSettings (binary)
	std::ofstream AimbotSettingsfile("AimbotSettings.bin", std::ios::binary);
	if (AimbotSettingsfile.is_open())
	{
		AimbotSettingsfile.write(reinterpret_cast<char*>(&AimbotSettings), sizeof(AimbotSettings));
		AimbotSettingsfile.close();
	}
	else
		printf("[ERROR] Could not open AimbotSettings.bin for writing!\n");

	// Save ESPSettings (binary)
	std::ofstream ESPSettingsfile("ESPSettings.bin", std::ios::binary);
	if (ESPSettingsfile.is_open())
	{
		ESPSettingsfile.write(reinterpret_cast<char*>(&ESPSettings), sizeof(ESPSettings));
		ESPSettingsfile.close();
	}
	else
		printf("[ERROR] Could not open ESPSettings.bin for writing!\n");

	// Save SilentAimSettings (binary)
	std::ofstream SilentAimSettingsfile("SilentAimSettings.bin", std::ios::binary);
	if (SilentAimSettingsfile.is_open())
	{
		SilentAimSettingsfile.write(reinterpret_cast<char*>(&SilentAimSettings), sizeof(SilentAimSettings));
		SilentAimSettingsfile.close();
	}
	else
		printf("[ERROR] Could not open SilentAimSettings.bin for writing!\n");

	// Save TextVars strings
	std::ofstream TextVarsfile("TextVars.bin", std::ios::binary);
	if (TextVarsfile.is_open())
	{
		// Save AimbotBone
		size_t len = TextVars.AimbotBone.size();
		TextVarsfile.write(reinterpret_cast<char*>(&len), sizeof(len));
		TextVarsfile.write(TextVars.AimbotBone.data(), len);

		// Save SilentAimBone
		len = TextVars.SilentAimBone.size();
		TextVarsfile.write(reinterpret_cast<char*>(&len), sizeof(len));
		TextVarsfile.write(TextVars.SilentAimBone.data(), len);

		len = TextVars.DebugFunctionNameMustInclude.size();
		TextVarsfile.write(reinterpret_cast<char*>(&len), sizeof(len));
		TextVarsfile.write(TextVars.DebugFunctionNameMustInclude.data(), len);

		len = TextVars.DebugFunctionObjectMustInclude.size();
		TextVarsfile.write(reinterpret_cast<char*>(&len), sizeof(len));
		TextVarsfile.write(TextVars.DebugFunctionObjectMustInclude.data(), len);

		TextVarsfile.close();
	}
	else
		printf("[ERROR] Could not open TextVars.bin for writing!\n");

	if (MiscSettings.ShouldSaveCVars)
	{
		std::ofstream CVarsinfile("CVars.bin", std::ios::binary);
		if (CVarsinfile.is_open())
		{
			const PersistedCVarsStruct Saved{
				CVars.Speed,
				CVars.FOV,
				CVars.BulletTimeSpeed,
				CVars.Debug,
				CVars.SecretFeatures,
				CVars.GodMode,
				CVars.InfAmmo,
				CVars.Aimbot,
				CVars.ESP,
				CVars.SpeedEnabled,
				CVars.SilentAim,
				CVars.Reticle,
				CVars.TriggerBot,
				CVars.RenderOptions,
				CVars.ListPlayers,
				CVars.ShootFromReticle,
				CVars.SaveDebugToFile,
				CVars.BulletTime,
				CVars.AntiSway,
				CVars.InstantMultiTool,
				CVars.MultiFire,
				EQueuedAction::None,
				CVars.KeepNoRecoil,
				CVars.KeepNoSpread,
				CVars.KeepAutoFire,
				CVars.KeepPenetration,
				CVars.KeepInstaKill,
				CVars.KeepHighFireRate,
				CVars.TriggerBotHoldKey
			};
			CVarsinfile.write(reinterpret_cast<const char*>(&Saved), sizeof(Saved));
			if (!CVarsinfile)
				printf("[ERROR] Failed to write CVars.bin.\n");
		}
		else
			printf("[ERROR] Could not open CVars.bin for writing!\n");
	}
}


void LoadSettings()
{
	if (!Settings.ShouldLoad)
		return;

	if (!ReadBinarySetting("MiscSettings.bin", MiscSettings))
		return;
	if (!ReadBinarySetting("AimbotSettings.bin", AimbotSettings))
		return;
	if (!ReadBinarySetting("ESPSettings.bin", ESPSettings))
		return;
	if (!ReadBinarySetting("SilentAimSettings.bin", SilentAimSettings))
		return;

	std::ifstream TextVarsinfile("TextVars.bin", std::ios::binary);
	if (!TextVarsinfile.is_open())
	{
		printf("[INFO] TextVars.bin could not be opened; keeping current settings.\n");
		return;
	}

	std::string AimbotBone;
	std::string SilentAimBone;
	std::string DebugFunctionFilter;
	std::string DebugObjectFilter;
	if (!ReadTextSetting(TextVarsinfile, AimbotBone) ||
		!ReadTextSetting(TextVarsinfile, SilentAimBone) ||
		!ReadTextSetting(TextVarsinfile, DebugFunctionFilter) ||
		!ReadTextSetting(TextVarsinfile, DebugObjectFilter))
	{
		printf("[ERROR] TextVars.bin is incomplete or contains an invalid string length; keeping current settings.\n");
		return;
	}
	TextVars.AimbotBone = std::move(AimbotBone);
	TextVars.SilentAimBone = std::move(SilentAimBone);
	TextVars.DebugFunctionNameMustInclude = std::move(DebugFunctionFilter);
	TextVars.DebugFunctionObjectMustInclude = std::move(DebugObjectFilter);

	if (MiscSettings.ShouldSaveCVars)
		LoadCVars();
}

void Cleanup(HMODULE hModule)
{
	Cleaning.store(true);
	std::cout << "Cleaning up...\n";

	MH_DisableHook(MH_ALL_HOOKS);
	while (g_PresentCount.load() != 0)
		Sleep(1);
	Hooks::WaitForProcessEvent();
	MH_DisableHook(MH_ALL_HOOKS);
	while (g_PresentCount.load() != 0)
		Sleep(1);
	Hooks::WaitForProcessEvent();
	OverlayMenu::Shutdown();
	Overlay::Shutdown();

	GVars.Cleanup();

	kiero::shutdown();

	MH_Uninitialize();

	std::cout << "Cleanup complete. Unloading DLL...\n";
	std::flush(std::cout);

	// Clean up console
	FreeConsole();
	FreeLibraryAndExitThread(hModule, 0);
}
