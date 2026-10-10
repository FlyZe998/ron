#pragma once

#include "SDK/ReadyOrNot_parameters.hpp"
#include "SDK/ReadyOrNot_classes.hpp"
#include "SDK/Engine_classes.hpp"
#include "SDK/CoreUObject_classes.hpp"
#include "SDK/CoreUObject_parameters.hpp"
#include "SDK/Engine_parameters.hpp"
#include "SDK/Basic.hpp"

#include "kiero/kiero.h"
#include "Overlay.h"
#include <Windows.h>
#include <dxgi.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
#include <numbers>
#include <string>
#include <unordered_map>
#include <vector>

#include "minhook/include/MinHook.h"

struct Engine
{
	using tPresent = HRESULT(__stdcall*)(IDXGISwapChain* SwapChain, UINT SyncInterval, UINT Flags);

	static kiero::Status::Enum HookPresent();
	static tPresent oPresent;
	static HRESULT __stdcall hkPresent(IDXGISwapChain* SwapChain, UINT SyncInterval, UINT Flags);
};

struct Hooks
{
	static void** vTable;
	static bool HookProcessEvent();
	static void WaitForProcessEvent();
};

#include "Cheats.h"
#include "Utils.h"
