#include "pch.h"
#include "Engine.h"

Engine::tPresent Engine::oPresent = nullptr;

kiero::Status::Enum Engine::HookPresent()
{
	const bool IsDirect3D12 = GetModuleHandleW(L"d3d12.dll") != nullptr;
	const kiero::RenderType::Enum RenderType = IsDirect3D12
		? kiero::RenderType::D3D12
		: kiero::RenderType::D3D11;
	const kiero::Status::Enum InitStatus = kiero::init(RenderType);
	if (InitStatus != kiero::Status::Success)
	{
		const std::string Message = "Failed to initialize the Present hook. Error code: " + std::to_string(InitStatus);
		MessageBoxA(nullptr, Message.c_str(), "ReadyOrNot Overlay", MB_OK | MB_ICONERROR);
		return InitStatus;
	}

	const uint16_t PresentIndex = IsDirect3D12 ? 140 : 8;
	oPresent = reinterpret_cast<tPresent>(kiero::getMethodsTable()[PresentIndex]);
	const kiero::Status::Enum BindStatus = kiero::bind(
		PresentIndex,
		reinterpret_cast<void**>(&oPresent),
		hkPresent);
	if (BindStatus != kiero::Status::Success)
		MessageBoxA(nullptr, "Failed to bind the Present hook.", "ReadyOrNot Overlay", MB_OK | MB_ICONERROR);
	return BindStatus;
}
