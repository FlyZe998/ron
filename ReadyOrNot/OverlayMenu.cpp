#include "pch.h"
#include "OverlayMenu.h"

#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "UxTheme.lib")

extern std::atomic<bool> Cleaning;

namespace
{
	constexpr wchar_t WindowClassName[] = L"ReadyOrNotNativeSettings";
	constexpr wchar_t PageWindowClassName[] = L"ReadyOrNotSettingsPage";
	constexpr int TabControlId = 1;
	constexpr int FirstControlId = 100;
	constexpr int MenuHotKeyId = 1;
	constexpr int UninjectHotKeyId = 2;
	constexpr UINT_PTR PageWheelSubclassId = 1;
	constexpr DWORD DwmwaUseImmersiveDarkMode = 20;
	constexpr COLORREF MenuBackgroundColor = RGB(12, 15, 22);
	constexpr COLORREF MenuPanelColor = RGB(20, 25, 35);
	constexpr COLORREF MenuPanelHoverColor = RGB(29, 37, 51);
	constexpr COLORREF MenuBorderColor = RGB(39, 49, 66);
	constexpr COLORREF MenuTextColor = RGB(232, 238, 247);
	constexpr COLORREF MenuMutedTextColor = RGB(132, 147, 168);
	constexpr COLORREF MenuAccentColor = RGB(72, 176, 224);
	constexpr COLORREF MenuEnabledColor = RGB(31, 93, 125);

	enum class BindingType
	{
		Boolean,
		Float,
		Integer,
		String
	};

	struct Binding
	{
		BindingType Type;
		void* Address;
		float Minimum = 0.0f;
		float Maximum = 1.0f;
	};

	struct ComboBinding
	{
		std::string* Value;
		std::vector<std::string> Options;
	};

	enum class Action
	{
		Magazine,
		KillSuspects,
		SurrenderSuspects,
		ArrestSuspects,
		KillCivilians,
		SurrenderCivilians,
		ArrestCivilians,
		CollectEvidence,
		AutoWin,
		UnlockDoors,
		GiveAchievements,
		SaveSettings,
		LoadSettings,
		PrintActors
	};

	HINSTANCE ModuleInstance = nullptr;
	HWND MenuWindow = nullptr;
	HWND TabControl = nullptr;
	HWND PageContainer = nullptr;
	HWND ToolTipWindow = nullptr;
	HANDLE MenuThreadHandle = nullptr;
	HANDLE MenuReadyEvent = nullptr;
	HFONT MenuFont = nullptr;
	HFONT HeaderFont = nullptr;
	HBRUSH MenuBackgroundBrush = nullptr;
	HBRUSH MenuPanelBrush = nullptr;
	HWND HeaderLabel = nullptr;
	HWND SessionLabel = nullptr;
	DWORD MenuThreadId = 0;
	std::unordered_map<int, Binding> Bindings;
	std::unordered_map<int, Action> Actions;
	std::unordered_map<int, ComboBinding> ComboStrings;
	std::unordered_map<int, int*> KeyBindings;
	std::unordered_map<int, Overlay::FloatColor*> ColorBindings;
	std::unordered_map<int, PlayerMenuRequest> PlayerActions;
	std::unordered_map<int, std::string> PlayerNamesToCopy;
	std::vector<std::unique_ptr<wchar_t[]>> ToolTipTexts;
	int NextControlId = FirstControlId;
	int CurrentPage = 0;
	int CurrentY = 44;
	int PageScrollOffset = 0;
	int PageContentHeight = 0;
	struct PageControlPosition
	{
		HWND Window;
		RECT Bounds;
	};
	std::vector<PageControlPosition> PageControls;
	std::string BoneNames[] = { "Head", "neck_1", "spine_2", "spine_1", "pelvis", "clavicle_LE", "lowerarm_LE", "hand_LE", "clavicle_RI", "lowerarm_RI", "hand_RI" };

	void AddToolTip(HWND Control, const wchar_t* Hint)
	{
		if (!ToolTipWindow || !Control || !Hint || !*Hint)
			return;

		const size_t Length = wcslen(Hint) + 1;
		auto Text = std::make_unique<wchar_t[]>(Length);
		wmemcpy(Text.get(), Hint, Length);
		TOOLINFOW ToolInfo{};
		ToolInfo.cbSize = sizeof(ToolInfo);
		ToolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
		ToolInfo.hwnd = MenuWindow;
		ToolInfo.uId = reinterpret_cast<UINT_PTR>(Control);
		ToolInfo.lpszText = Text.get();
		if (SendMessageW(ToolTipWindow, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ToolInfo)))
			ToolTipTexts.push_back(std::move(Text));
	}

	void StyleControl(HWND Control)
	{
		if (!Control)
			return;
		if (MenuFont)
			SendMessageW(Control, WM_SETFONT, reinterpret_cast<WPARAM>(MenuFont), TRUE);
		SetWindowTheme(Control, L"DarkMode_Explorer", nullptr);
	}

	void DrawRoundedRect(HDC DeviceContext, const RECT& Bounds, COLORREF Fill, COLORREF Border, int Radius = 8)
	{
		HBRUSH Brush = CreateSolidBrush(Fill);
		HPEN Pen = CreatePen(PS_SOLID, 1, Border);
		HGDIOBJ PreviousBrush = SelectObject(DeviceContext, Brush);
		HGDIOBJ PreviousPen = SelectObject(DeviceContext, Pen);
		RoundRect(DeviceContext, Bounds.left, Bounds.top, Bounds.right, Bounds.bottom, Radius, Radius);
		SelectObject(DeviceContext, PreviousBrush);
		SelectObject(DeviceContext, PreviousPen);
		DeleteObject(Brush);
		DeleteObject(Pen);
	}

	void DrawOwnerButton(const DRAWITEMSTRUCT& DrawItem)
	{
		std::lock_guard<std::recursive_mutex> SettingsLock(SettingsMutex);
		const int Id = static_cast<int>(DrawItem.CtlID);
		const auto BindingIt = Bindings.find(Id);
		const bool IsCheck = BindingIt != Bindings.end() &&
			BindingIt->second.Type == BindingType::Boolean;
		const auto ColorIt = ColorBindings.find(Id);
		const bool IsColorButton = ColorIt != ColorBindings.end();
		const bool IsChecked = IsCheck &&
			*static_cast<const bool*>(BindingIt->second.Address);
		const bool IsDisabled = (DrawItem.itemState & ODS_DISABLED) != 0;
		const bool IsHot = (DrawItem.itemState & ODS_HOTLIGHT) != 0;
		const bool IsPressed = (DrawItem.itemState & ODS_SELECTED) != 0;
		const COLORREF Fill = IsDisabled ? MenuBackgroundColor
			: IsChecked ? MenuEnabledColor
			: IsHot ? MenuPanelHoverColor : MenuPanelColor;

		RECT Bounds = DrawItem.rcItem;
		InflateRect(&Bounds, -1, -1);
		DrawRoundedRect(DrawItem.hDC, Bounds, Fill,
			IsChecked ? MenuAccentColor : IsHot ? RGB(57, 75, 99) : MenuBorderColor,
			IsCheck ? 12 : 8);
		SetBkMode(DrawItem.hDC, TRANSPARENT);
		SetTextColor(DrawItem.hDC, IsDisabled ? MenuMutedTextColor : MenuTextColor);

		if (IsCheck)
		{
			const int ToggleWidth = 34;
			const int ToggleHeight = 18;
			const int ToggleLeft = Bounds.right - ToggleWidth - 12;
			const int ToggleTop = Bounds.top + ((Bounds.bottom - Bounds.top) - ToggleHeight) / 2;
			RECT Toggle{ ToggleLeft, ToggleTop, ToggleLeft + ToggleWidth, ToggleTop + ToggleHeight };
			DrawRoundedRect(DrawItem.hDC, Toggle,
				IsChecked ? MenuAccentColor : RGB(43, 53, 69),
				IsChecked ? MenuAccentColor : RGB(43, 53, 69), ToggleHeight);
			const int KnobDiameter = 12;
			const int KnobLeft = IsChecked ? Toggle.right - KnobDiameter - 3 : Toggle.left + 3;
			const int KnobTop = Toggle.top + (ToggleHeight - KnobDiameter) / 2;
			HBRUSH KnobBrush = CreateSolidBrush(RGB(246, 249, 252));
			HGDIOBJ PreviousBrush = SelectObject(DrawItem.hDC, KnobBrush);
			HGDIOBJ PreviousPen = SelectObject(DrawItem.hDC, GetStockObject(NULL_PEN));
			Ellipse(DrawItem.hDC, KnobLeft, KnobTop, KnobLeft + KnobDiameter, KnobTop + KnobDiameter);
			SelectObject(DrawItem.hDC, PreviousBrush);
			SelectObject(DrawItem.hDC, PreviousPen);
			DeleteObject(KnobBrush);

			RECT TextBounds = Bounds;
			TextBounds.left += 12;
			TextBounds.right = ToggleLeft - 10;
			const int TextLength = GetWindowTextLengthW(DrawItem.hwndItem);
			std::wstring Text(static_cast<size_t>(TextLength) + 1, L'\0');
			if (TextLength > 0)
				GetWindowTextW(DrawItem.hwndItem, Text.data(), TextLength + 1);
			DrawTextW(DrawItem.hDC, Text.c_str(), TextLength, &TextBounds, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
		}
		else
		{
			RECT TextBounds = Bounds;
			InflateRect(&TextBounds, -12, 0);
			if (IsColorButton)
			{
				const Overlay::FloatColor& Color = *ColorIt->second;
				RECT Swatch{ Bounds.left + 10, Bounds.top + 8, Bounds.left + 31, Bounds.bottom - 8 };
				const COLORREF SwatchColor = RGB(
					static_cast<BYTE>(std::clamp(Color.x, 0.0f, 1.0f) * 255.0f),
					static_cast<BYTE>(std::clamp(Color.y, 0.0f, 1.0f) * 255.0f),
					static_cast<BYTE>(std::clamp(Color.z, 0.0f, 1.0f) * 255.0f));
				DrawRoundedRect(DrawItem.hDC, Swatch, SwatchColor, MenuBorderColor, 5);
				TextBounds.left += 26;
			}
			if (IsPressed)
				OffsetRect(&TextBounds, 0, 1);
			const int TextLength = GetWindowTextLengthW(DrawItem.hwndItem);
			std::wstring Text(static_cast<size_t>(TextLength) + 1, L'\0');
			if (TextLength > 0)
				GetWindowTextW(DrawItem.hwndItem, Text.data(), TextLength + 1);
			DrawTextW(DrawItem.hDC, Text.c_str(), TextLength, &TextBounds, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_END_ELLIPSIS);
		}

		if (DrawItem.itemState & ODS_FOCUS)
		{
			RECT FocusBounds = Bounds;
			InflateRect(&FocusBounds, -4, -4);
			DrawFocusRect(DrawItem.hDC, &FocusBounds);
		}
	}

	void DrawOwnerTab(const DRAWITEMSTRUCT& DrawItem)
	{
		RECT Bounds = DrawItem.rcItem;
		InflateRect(&Bounds, -3, -3);
		const bool IsSelected = static_cast<int>(DrawItem.itemID) == CurrentPage;
		const bool IsHot = (DrawItem.itemState & ODS_HOTLIGHT) != 0;
		const COLORREF Fill = IsSelected ? MenuEnabledColor
			: IsHot ? MenuPanelHoverColor : MenuPanelColor;
		DrawRoundedRect(DrawItem.hDC, Bounds, Fill,
			IsSelected ? MenuAccentColor : MenuBorderColor, 9);

		TCITEMW Item{};
		wchar_t Text[64]{};
		Item.mask = TCIF_TEXT;
		Item.pszText = Text;
		Item.cchTextMax = static_cast<int>(std::size(Text));
		if (!TabCtrl_GetItem(DrawItem.hwndItem, static_cast<int>(DrawItem.itemID), &Item))
			return;
		SetBkMode(DrawItem.hDC, TRANSPARENT);
		SetTextColor(DrawItem.hDC, IsSelected ? RGB(255, 255, 255) : MenuMutedTextColor);
		DrawTextW(DrawItem.hDC, Text, -1, &Bounds, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_END_ELLIPSIS);
	}

	void ResetToolTips()
	{
		if (ToolTipWindow)
			DestroyWindow(ToolTipWindow);
		ToolTipTexts.clear();
		ToolTipWindow = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
			WS_POPUP | TTS_ALWAYSTIP | TTS_BALLOON, CW_USEDEFAULT, CW_USEDEFAULT,
			CW_USEDEFAULT, CW_USEDEFAULT, MenuWindow, nullptr, ModuleInstance, nullptr);
		if (ToolTipWindow)
		{
			SendMessageW(ToolTipWindow, TTM_SETMAXTIPWIDTH, 0, 360);
			SendMessageW(ToolTipWindow, TTM_SETDELAYTIME, TTDT_INITIAL, 350);
		}
	}

	void AddLabel(const wchar_t* Text, int X = 18, int Width = 300, const wchar_t* Hint = nullptr)
	{
		HWND Control = CreateWindowExW(0, L"STATIC", Text, WS_CHILD | WS_VISIBLE, X, CurrentY + 4, Width, 22, PageContainer, nullptr, ModuleInstance, nullptr);
		StyleControl(Control);
		AddToolTip(Control, Hint);
	}

	void AddCheck(const wchar_t* Label, bool* Value, const wchar_t* Hint = nullptr)
	{
		const int Id = NextControlId++;
		HWND Control = CreateWindowExW(0, L"BUTTON", Label, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
			18, CurrentY, 650, 32, PageContainer, reinterpret_cast<HMENU>(static_cast<INT_PTR>(Id)), ModuleInstance, nullptr);
		StyleControl(Control);
		AddToolTip(Control, Hint);
		Bindings.emplace(Id, Binding{ BindingType::Boolean, Value });
		CurrentY += 37;
	}

	void AddSlider(const wchar_t* Label, float* Value, float Minimum, float Maximum, const wchar_t* Hint = nullptr)
	{
		const int Id = NextControlId++;
		AddLabel(Label, 18, 230, Hint);
		HWND Control = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
			250, CurrentY, 330, 28, PageContainer, reinterpret_cast<HMENU>(static_cast<INT_PTR>(Id)), ModuleInstance, nullptr);
		StyleControl(Control);
		AddToolTip(Control, Hint);
		SendMessageW(Control, TBM_SETRANGE, TRUE, MAKELPARAM(0, 1000));
		const float Ratio = (Maximum > Minimum) ? (*Value - Minimum) / (Maximum - Minimum) : 0.0f;
		SendMessageW(Control, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::clamp(Ratio, 0.0f, 1.0f) * 1000.0f));
		Bindings.emplace(Id, Binding{ BindingType::Float, Value, Minimum, Maximum });
		CurrentY += 39;
	}

	void AddIntegerSlider(const wchar_t* Label, int* Value, int Minimum, int Maximum, const wchar_t* Hint = nullptr)
	{
		const int Id = NextControlId++;
		AddLabel(Label, 18, 230, Hint);
		HWND Control = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
			250, CurrentY, 330, 28, PageContainer, reinterpret_cast<HMENU>(static_cast<INT_PTR>(Id)), ModuleInstance, nullptr);
		StyleControl(Control);
		AddToolTip(Control, Hint);
		SendMessageW(Control, TBM_SETRANGE, TRUE, MAKELPARAM(Minimum, Maximum));
		SendMessageW(Control, TBM_SETPOS, TRUE, *Value);
		Bindings.emplace(Id, Binding{ BindingType::Integer, Value, static_cast<float>(Minimum), static_cast<float>(Maximum) });
		CurrentY += 39;
	}

	void AddButton(const wchar_t* Label, Action Callback, int Width = 280, const wchar_t* Hint = nullptr)
	{
		const int Id = NextControlId++;
		HWND Control = CreateWindowExW(0, L"BUTTON", Label, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
			18, CurrentY, Width, 32, PageContainer, reinterpret_cast<HMENU>(static_cast<INT_PTR>(Id)), ModuleInstance, nullptr);
		StyleControl(Control);
		AddToolTip(Control, Hint);
		Actions.emplace(Id, Callback);
		CurrentY += 38;
	}

	void AddTextInput(const wchar_t* Label, std::string* Value, const wchar_t* Hint = nullptr)
	{
		const int Id = NextControlId++;
		AddLabel(Label, 18, 230, Hint);
		HWND Control = CreateWindowExW(0, L"EDIT", L"",
			WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_BORDER, 250, CurrentY, 390, 28,
			PageContainer, reinterpret_cast<HMENU>(static_cast<INT_PTR>(Id)), ModuleInstance, nullptr);
		StyleControl(Control);
		AddToolTip(Control, Hint);
		Bindings.emplace(Id, Binding{ BindingType::String, Value });
		if (!Value->empty())
		{
			const int WideLength = MultiByteToWideChar(CP_UTF8, 0, Value->data(), static_cast<int>(Value->size()), nullptr, 0);
			if (WideLength > 0)
			{
				std::wstring WideValue(static_cast<size_t>(WideLength), L'\0');
				MultiByteToWideChar(CP_UTF8, 0, Value->data(), static_cast<int>(Value->size()), WideValue.data(), WideLength);
				SetWindowTextW(Control, WideValue.c_str());
			}
		}
		CurrentY += 31;
	}

	void AddCombo(const wchar_t* Label, std::string* Value, bool IsBone, const wchar_t* Hint = nullptr)
	{
		const int Id = NextControlId++;
		AddLabel(Label, 18, 230, Hint);
		HWND Control = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
			250, CurrentY, 330, 240, PageContainer, reinterpret_cast<HMENU>(static_cast<INT_PTR>(Id)), ModuleInstance, nullptr);
		StyleControl(Control);
		AddToolTip(Control, Hint);
		ComboBinding Binding{ Value, {} };
		if (IsBone)
		{
			for (size_t Index = 0; Index < std::size(BoneNames); ++Index)
			{
				const std::wstring Item(BoneNames[Index].begin(), BoneNames[Index].end());
				SendMessageW(Control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Item.c_str()));
				Binding.Options.push_back(BoneNames[Index]);
			}
			for (int Index = 0; Index < static_cast<int>(Binding.Options.size()); ++Index)
				if (*Value == Binding.Options[Index])
					SendMessageW(Control, CB_SETCURSEL, Index, 0);
		}
		ComboStrings.emplace(Id, std::move(Binding));
		CurrentY += 34;
	}

	void AddKeyCombo(const wchar_t* Label, int* Value, const wchar_t* Hint = nullptr)
	{
		const int Id = NextControlId++;
		AddLabel(Label, 18, 230, Hint);
		HWND Control = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
			250, CurrentY, 330, 240, PageContainer, reinterpret_cast<HMENU>(static_cast<INT_PTR>(Id)), ModuleInstance, nullptr);
		StyleControl(Control);
		AddToolTip(Control, Hint);
		const LRESULT NoneIndex = SendMessageW(Control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"None"));
		SendMessageW(Control, CB_SETITEMDATA, NoneIndex, 0);
		if (*Value == 0)
			SendMessageW(Control, CB_SETCURSEL, NoneIndex, 0);
		for (int Key = 1; Key < 256; ++Key)
		{
			wchar_t Name[64]{};
			if (Key == VK_LBUTTON) wcscpy_s(Name, L"Mouse Left");
			else if (Key == VK_RBUTTON) wcscpy_s(Name, L"Mouse Right");
			else if (Key == VK_MBUTTON) wcscpy_s(Name, L"Mouse Middle");
			else
			{
				const UINT Scan = MapVirtualKeyW(static_cast<UINT>(Key), MAPVK_VK_TO_VSC) << 16;
				if (GetKeyNameTextW(static_cast<LONG>(Scan), Name, static_cast<int>(std::size(Name))) <= 0)
					continue;
			}
			const LRESULT Index = SendMessageW(Control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Name));
			SendMessageW(Control, CB_SETITEMDATA, Index, Key);
			if (Key == *Value)
				SendMessageW(Control, CB_SETCURSEL, Index, 0);
		}
		KeyBindings.emplace(Id, Value);
		CurrentY += 34;
	}

	void AddColor(const wchar_t* Label, Overlay::FloatColor* Value, const wchar_t* Hint = nullptr)
	{
		const int Id = NextControlId++;
		HWND Control = CreateWindowExW(0, L"BUTTON", Label, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
			18, CurrentY, 280, 32, PageContainer, reinterpret_cast<HMENU>(static_cast<INT_PTR>(Id)), ModuleInstance, nullptr);
		StyleControl(Control);
		AddToolTip(Control, Hint);
		ColorBindings.emplace(Id, Value);
		CurrentY += 38;
	}

	void AddPlayerActionButton(const wchar_t* Label, APlayerCharacter* Player, EPlayerMenuAction Action, int X, bool Enabled)
	{
		const int Id = NextControlId++;
		HWND Control = CreateWindowExW(0, L"BUTTON", Label, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
			X, CurrentY, 130, 30, PageContainer, reinterpret_cast<HMENU>(static_cast<INT_PTR>(Id)), ModuleInstance, nullptr);
		StyleControl(Control);
		EnableWindow(Control, Enabled);
		switch (Action)
		{
		case EPlayerMenuAction::GodMode:
			AddToolTip(Control, L"Toggle God Mode for this player. Availability depends on game authority.");
			break;
		case EPlayerMenuAction::InfiniteAmmo:
			AddToolTip(Control, L"Toggle Infinite Ammo for this player. Availability depends on game authority.");
			break;
		case EPlayerMenuAction::Teleport:
			AddToolTip(Control, L"Teleport your character to this player's position.");
			break;
		case EPlayerMenuAction::Speed:
			AddToolTip(Control, L"Apply the configured movement speed to this player, if permitted.");
			break;
		}
		PlayerActions.emplace(Id, PlayerMenuRequest{ Player, Action });
	}

	std::wstring ToWide(const std::string& Text);

	void AddCopyPlayerNameButton(const std::string& PlayerName, int X)
	{
		const int Id = NextControlId++;
		HWND Control = CreateWindowExW(0, L"BUTTON", L"Copy Name", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
			X, CurrentY, 120, 30, PageContainer, reinterpret_cast<HMENU>(static_cast<INT_PTR>(Id)), ModuleInstance, nullptr);
		StyleControl(Control);
		AddToolTip(Control, L"Copy this player's displayed name to the clipboard on this computer.");
		PlayerNamesToCopy.emplace(Id, PlayerName);
	}

	bool CopyPlayerNameToClipboard(const std::string& PlayerName)
	{
		const std::wstring WideName = ToWide(PlayerName);
		if (WideName.empty())
			return false;

		const SIZE_T ByteCount = (WideName.size() + 1) * sizeof(wchar_t);
		HGLOBAL ClipboardData = GlobalAlloc(GMEM_MOVEABLE, ByteCount);
		if (!ClipboardData)
			return false;

		void* Buffer = GlobalLock(ClipboardData);
		if (!Buffer)
		{
			GlobalFree(ClipboardData);
			return false;
		}
		memcpy(Buffer, WideName.c_str(), ByteCount);
		GlobalUnlock(ClipboardData);

		if (!OpenClipboard(MenuWindow))
		{
			GlobalFree(ClipboardData);
			return false;
		}

		bool Copied = false;
		if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, ClipboardData))
			Copied = true;
		CloseClipboard();
		if (!Copied)
			GlobalFree(ClipboardData);
		return Copied;
	}

	BOOL CALLBACK ApplyMenuFont(HWND Control, LPARAM Font)
	{
		SendMessageW(Control, WM_SETFONT, Font, TRUE);
		return TRUE;
	}

	void UpdateSessionStatus()
	{
		if (!SessionLabel)
			return;

		std::lock_guard<std::recursive_mutex> SettingsLock(SettingsMutex);
		const wchar_t* Status = !GVars.PlayerController
			? L"ROLE: WAITING FOR SESSION"
			: GVars.HasAuthority
				? L"ROLE: HOST / AUTHORITY"
				: L"ROLE: CLIENT";
		SetWindowTextW(SessionLabel, Status);
	}

	void ApplyPageScroll()
	{
		if (PageControls.empty())
			return;

		HDWP DeferredPositions = BeginDeferWindowPos(static_cast<int>(PageControls.size()));
		for (const PageControlPosition& Control : PageControls)
		{
			if (!IsWindow(Control.Window))
				continue;
			RECT Bounds = Control.Bounds;
			Bounds.top -= PageScrollOffset;
			Bounds.bottom -= PageScrollOffset;
			if (DeferredPositions)
				DeferredPositions = DeferWindowPos(DeferredPositions, Control.Window, nullptr,
					Bounds.left, Bounds.top, Bounds.right - Bounds.left, Bounds.bottom - Bounds.top,
					SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
			else
				SetWindowPos(Control.Window, nullptr, Bounds.left, Bounds.top,
					Bounds.right - Bounds.left, Bounds.bottom - Bounds.top,
					SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
		}
		if (DeferredPositions)
			EndDeferWindowPos(DeferredPositions);

		RedrawWindow(PageContainer, nullptr, nullptr,
			RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
	}

	void UpdatePageScrollBar()
	{
		if (!PageContainer)
			return;

		RECT Client{};
		GetClientRect(PageContainer, &Client);
		SCROLLINFO ScrollInfo{};
		ScrollInfo.cbSize = sizeof(ScrollInfo);
		ScrollInfo.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
		ScrollInfo.nMin = 0;
		ScrollInfo.nMax = (std::max)(0, PageContentHeight - 1);
		ScrollInfo.nPage = static_cast<UINT>((std::max)(0L, Client.bottom));
		const int MaximumOffset = (std::max)(0, PageContentHeight - static_cast<int>(ScrollInfo.nPage));
		PageScrollOffset = std::clamp(PageScrollOffset, 0, MaximumOffset);
		ScrollInfo.nPos = PageScrollOffset;
		SetScrollInfo(PageContainer, SB_VERT, &ScrollInfo, TRUE);
		ApplyPageScroll();
	}

	LRESULT CALLBACK PageControlProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam,
		UINT_PTR SubclassId, DWORD_PTR ReferenceData);

	void CapturePageControls()
	{
		PageControls.clear();
		EnumChildWindows(PageContainer, [](HWND Child, LPARAM) -> BOOL
		{
			if (GetParent(Child) != PageContainer)
				return TRUE;
			RECT Bounds{};
			GetWindowRect(Child, &Bounds);
			MapWindowPoints(nullptr, PageContainer, reinterpret_cast<POINT*>(&Bounds), 2);
			PageControls.push_back({ Child, Bounds });
			if (!SetWindowSubclass(Child, PageControlProc, PageWheelSubclassId, 0))
				OutputDebugStringA("[Settings] Failed to enable page scrolling for a menu control.\n");
			return TRUE;
		}, 0);
	}

	LRESULT CALLBACK PageControlProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam,
		UINT_PTR SubclassId, DWORD_PTR)
	{
		if (Message == WM_MOUSEWHEEL)
			return SendMessageW(PageContainer, Message, WParam, LParam);
		if (Message == WM_NCDESTROY)
			RemoveWindowSubclass(Window, PageControlProc, SubclassId);
		return DefSubclassProc(Window, Message, WParam, LParam);
	}

	LRESULT CALLBACK PageContainerProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
	{
		switch (Message)
		{
		case WM_ERASEBKGND:
		{
			RECT Client{};
			GetClientRect(Window, &Client);
			FillRect(reinterpret_cast<HDC>(WParam), &Client, MenuBackgroundBrush);
			return TRUE;
		}
		case WM_COMMAND:
		case WM_HSCROLL:
		case WM_DRAWITEM:
		case WM_VSCROLL:
		case WM_CTLCOLORSTATIC:
		case WM_CTLCOLORBTN:
		case WM_CTLCOLOREDIT:
		case WM_CTLCOLORLISTBOX:
		case WM_NOTIFY:
		case WM_MOUSEWHEEL:
			return SendMessageW(MenuWindow, Message, WParam, LParam);
		}
		return DefWindowProcW(Window, Message, WParam, LParam);
	}

	std::wstring ToWide(const std::string& Text)
	{
		if (Text.empty())
			return {};
		const int Length = MultiByteToWideChar(CP_UTF8, 0, Text.data(), static_cast<int>(Text.size()), nullptr, 0);
		if (Length <= 0)
			return std::wstring(Text.begin(), Text.end());
		std::wstring Result(static_cast<size_t>(Length), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, Text.data(), static_cast<int>(Text.size()), Result.data(), Length);
		return Result;
	}

	void CreatePage(int Page, bool ResetScroll = true)
	{
		std::lock_guard<std::recursive_mutex> SettingsLock(SettingsMutex);
		Bindings.clear();
		Actions.clear();
		ComboStrings.clear();
		KeyBindings.clear();
		ColorBindings.clear();
		PlayerActions.clear();
		PlayerNamesToCopy.clear();
		NextControlId = FirstControlId;
		CurrentY = 12;
		if (ResetScroll)
			PageScrollOffset = 0;
		PageControls.clear();
		HWND Child = nullptr;
		while ((Child = GetWindow(PageContainer, GW_CHILD)) != nullptr)
			DestroyWindow(Child);
		ResetToolTips();

		switch (Page)
		{
		case 0:
			AddCheck(L"God Mode", &CVars.GodMode, L"Enable player damage protection where supported by the current game session.");
			AddCheck(L"Aimbot", &CVars.Aimbot, L"Automatically aim at eligible targets using the settings on the Aimbot tab.");
			AddCheck(L"Silent Aim", &CVars.SilentAim, L"Redirect shots toward eligible targets using the Silent Aim settings.");
			AddCheck(L"ESP", &CVars.ESP, L"Show configured player, trap, and objective overlays.");
			AddCheck(L"Enable Speed", &CVars.SpeedEnabled, L"Apply the configured movement speed to your character.");
			AddSlider(L"Player Speed", &CVars.Speed, 1.0f, 30.0f, L"Movement-speed multiplier used while Enable Speed is on.");
			AddSlider(L"Player FOV", &CVars.FOV, 10.0f, 179.9f, L"Set the player camera field of view.");
			AddCheck(L"Instant MultiTool", &CVars.InstantMultiTool, L"Reduce the time required to use the multitool.");
			AddButton(L"Give All Achievements", Action::GiveAchievements, 280, L"Request that all achievements be granted. Game/session support may vary.");
			break;
		case 1:
			AddCheck(L"Infinite Ammo", &CVars.InfAmmo, L"Keep ammunition available for the equipped weapon when the game permits it.");
			AddIntegerSlider(L"Multi Fire", &CVars.MultiFire, 0, 20, L"Configure the multi-fire value; 0 disables the additional shots.");
			AddCheck(L"Keep No Recoil", &CVars.KeepNoRecoil, L"Reapply no-recoil behavior when changing weapons.");
			AddCheck(L"Anti Sway", &CVars.AntiSway, L"Reduce weapon sway while aiming.");
			AddCheck(L"Keep No Spread", &CVars.KeepNoSpread, L"Reapply no-spread behavior when changing weapons.");
			AddCheck(L"Keep Auto Fire", &CVars.KeepAutoFire, L"Keep the weapon's automatic-fire setting enabled across weapon changes.");
			AddCheck(L"Keep Penetration", &CVars.KeepPenetration, L"Reapply the penetration setting to newly equipped weapons.");
			AddCheck(L"Keep Insta Kill", &CVars.KeepInstaKill, L"Reapply the instant-kill setting to newly equipped weapons.");
			AddCheck(L"Keep High Fire Rate", &CVars.KeepHighFireRate, L"Reapply the high-fire-rate setting to newly equipped weapons.");
			AddCheck(L"Shoot From Reticle", &CVars.ShootFromReticle, L"Align shot direction with the screen reticle.");
			AddButton(L"Add Magazine", Action::Magazine, 280, L"Request an additional magazine for the current weapon.");
			AddCheck(L"Trigger Bot", &CVars.TriggerBot, L"Automatically fire when the triggerbot detects an eligible target.");
			break;
		case 2:
			AddButton(L"Kill All Suspects", Action::KillSuspects, 280, L"Request that all suspects be eliminated in the current level.");
			AddButton(L"Surrender All Suspects", Action::SurrenderSuspects, 280, L"Request that all suspects surrender in the current level.");
			AddButton(L"Arrest All Suspects", Action::ArrestSuspects, 280, L"Request that all suspects be arrested in the current level.");
			AddButton(L"Kill All Civilians", Action::KillCivilians, 280, L"Request that all civilians be eliminated in the current level.");
			AddButton(L"Surrender All Civilians", Action::SurrenderCivilians, 280, L"Request that all civilians surrender in the current level.");
			AddButton(L"Arrest All Civilians", Action::ArrestCivilians, 280, L"Request that all civilians be arrested in the current level.");
			AddButton(L"Collect All Evidence", Action::CollectEvidence, 280, L"Request collection of all evidence in the current level.");
			AddButton(L"Auto Win", Action::AutoWin, 280, L"Request immediate mission completion. Availability depends on game/session authority.");
			AddButton(L"Unlock All Doors", Action::UnlockDoors, 280, L"Request that locked doors in the current level be unlocked.");
			AddCheck(L"Bullet Time", &CVars.BulletTime, L"Enable slowed time using the speed value below.");
			AddSlider(L"Bullet Time Speed", &CVars.BulletTimeSpeed, 0.05f, 1.0f, L"Time-scale multiplier while Bullet Time is enabled; lower values slow time further.");
			break;
		case 3:
			AddSlider(L"Aimbot FOV", &AimbotSettings.MaxFOV, 0.01f, 180.0f, L"Maximum angle from the crosshair for selecting an aimbot target.");
			AddCheck(L"Require Line of Sight", &AimbotSettings.LOS, L"Only acquire targets with a clear line of sight.");
			AddCheck(L"Target Civilians", &AimbotSettings.TargetCivilians, L"Allow civilians to be selected as targets.");
			AddCheck(L"Target Dead", &AimbotSettings.TargetDead, L"Allow dead characters to be selected as targets.");
			AddCheck(L"Target Arrested", &AimbotSettings.TargetArrested, L"Allow arrested characters to be selected as targets.");
			AddCheck(L"Target Surrendered", &AimbotSettings.TargetSurrendered, L"Allow surrendered characters to be selected as targets.");
			AddCheck(L"Target All", &AimbotSettings.TargetAll, L"Include every character category, overriding the individual target-category choices.");
			AddSlider(L"Maximum Distance", &AimbotSettings.MaxDistance, 0.0f, 300.0f, L"Ignore targets farther away than this distance; 0 means no maximum.");
			AddSlider(L"Minimum Distance", &AimbotSettings.MinDistance, 0.0f, 100.0f, L"Ignore targets closer than this distance.");
			AddCheck(L"Smoothing", &AimbotSettings.Smooth, L"Gradually move aim toward the target instead of snapping directly.");
			AddSlider(L"Smoothing Vector", &AimbotSettings.SmoothingVector, 1.0f, 20.0f, L"Adjust smoothing strength; higher values produce slower, smoother movement.");
			AddCheck(L"Prediction", &AimbotSettings.Prediction, L"Compensate for target movement when aiming.");
			AddSlider(L"Prediction Strength", &AimbotSettings.PredictionMultiplier, 0.0f, 2.0f, L"Scale the amount of movement prediction.");
			AddCheck(L"Target Lock", &AimbotSettings.TargetLock, L"Keep the current target selected while it remains eligible.");
			AddCheck(L"Draw Arrow", &AimbotSettings.DrawArrow, L"Draw a visual indicator toward the selected target.");
			AddCheck(L"Draw FOV", &AimbotSettings.DrawFOV, L"Display the configured target-selection field of view.");
			AddCombo(L"Target Bone", &TextVars.AimbotBone, true, L"Choose the bone used for target positioning.");
			AddCheck(L"Require Hotkey", &AimbotSettings.RequireKeyHeld, L"Only aim while the selected Aimbot Hotkey is held.");
			AddKeyCombo(L"Aimbot Hotkey", &AimbotSettings.AimbotVirtualKey, L"Select the key used when Require Hotkey is enabled.");
			break;
		case 4:
			AddCheck(L"Show Team", &ESPSettings.ShowTeam, L"Include your team in the ESP display.");
			AddCheck(L"Show Box", &ESPSettings.ShowBox, L"Draw boxes around displayed characters.");
			AddCheck(L"Show Traps", &ESPSettings.ShowTraps, L"Display detected traps.");
			AddCheck(L"Show Enemy Distance", &ESPSettings.ShowEnemyDistance, L"Display the distance to enemy characters.");
			AddCheck(L"Show Bones", &ESPSettings.Bones, L"Draw the character bone/skeleton overlay.");
			AddColor(L"Suspect Color", &ESPSettings.SuspectColor, L"Choose the ESP color used for suspects.");
			AddColor(L"Civilian Color", &ESPSettings.CivilianColor, L"Choose the ESP color used for civilians.");
			AddColor(L"Dead Color", &ESPSettings.DeadColor, L"Choose the ESP color used for dead characters.");
			AddColor(L"Team Color", &ESPSettings.TeamColor, L"Choose the ESP color used for your team.");
			AddColor(L"Arrested Color", &ESPSettings.ArrestColor, L"Choose the ESP color used for arrested characters.");
			AddCheck(L"Require Line of Sight", &ESPSettings.LOS, L"Only display character ESP when there is a clear line of sight.");
			AddSlider(L"Bone Opacity", &ESPSettings.BoneOpacity, 0.0f, 1.0f, L"Set the opacity of bone lines; this also updates character-color opacity.");
			AddCheck(L"Show Objectives", &ESPSettings.ShowObjectives, L"Display objective markers and information.");
			break;
		case 5:
			AddSlider(L"Silent Aim FOV", &SilentAimSettings.MaxFOV, 0.0f, 180.0f, L"Maximum angle from the crosshair for Silent Aim target selection.");
			AddCheck(L"Target Civilians", &SilentAimSettings.TargetCivilians, L"Allow civilians to be selected.");
			AddCheck(L"Target Dead", &SilentAimSettings.TargetDead, L"Allow dead characters to be selected.");
			AddCheck(L"Target Surrendered", &SilentAimSettings.TargetSurrendered, L"Allow surrendered characters to be selected.");
			AddCheck(L"Target Arrested", &SilentAimSettings.TargetArrested, L"Allow arrested characters to be selected.");
			AddCheck(L"Target All", &SilentAimSettings.TargetAll, L"Include every character category, overriding the individual category choices.");
			AddCheck(L"Require Line of Sight", &SilentAimSettings.RequiresLOS, L"Only select targets with a clear line of sight.");
			AddCheck(L"Draw FOV", &SilentAimSettings.DrawFOV, L"Display the Silent Aim target-selection field of view.");
			AddSlider(L"FOV Line Thickness", &SilentAimSettings.FOVThickness, 0.1f, 10.0f, L"Set the thickness of the displayed FOV outline.");
			AddCheck(L"Draw Snap Line", &SilentAimSettings.DrawArrow, L"Draw a line indicator toward the selected target.");
			AddSlider(L"Snap Line Thickness", &SilentAimSettings.ArrowThickness, 0.1f, 10.0f, L"Set the thickness of the target indicator line.");
			AddSlider(L"Hit Chance", &SilentAimSettings.HitChance, 0.0f, 100.0f, L"Chance for Silent Aim to redirect a shot; 0 disables and 100 always redirects eligible shots.");
			AddCombo(L"Target Bone", &TextVars.SilentAimBone, true, L"Choose the bone used for Silent Aim target positioning.");
			break;
		case 6:
			AddCheck(L"Reticle", &CVars.Reticle, L"Show the custom reticle overlay.");
			AddColor(L"Reticle Color", &MiscSettings.ReticleColor, L"Choose the custom reticle color.");
			AddSlider(L"Reticle Opacity", &MiscSettings.ReticleColor.w, 0.0f, 1.0f, L"Set custom reticle transparency.");
			AddSlider(L"Reticle Size", &MiscSettings.ReticleSize, 1.0f, 15.0f, L"Set the custom reticle size.");
			AddCheck(L"Cross Reticle", &MiscSettings.CrossReticle, L"Use a cross-shaped reticle instead of the alternate reticle style.");
			AddCheck(L"Reticle Only While Throwing", &MiscSettings.ReticleWhenThrowing, L"Only show the reticle while holding a throwable.");
			AddCheck(L"Trigger Bot Targets Civilians", &MiscSettings.TriggerBotTargetsCivilians, L"Allow the triggerbot to activate on civilians.");
			AddCheck(L"Trigger Bot Uses Silent Aim", &MiscSettings.TriggerBotUsesSilentAim, L"Use Silent Aim target selection when determining triggerbot targets.");
			AddCheck(L"Trigger Bot Hold Key", &CVars.TriggerBotHoldKey, L"Require the Trigger Bot Key to remain held; when off, the key toggles triggerbot activation.");
			AddCheck(L"Show Enabled Options", &CVars.RenderOptions, L"Show a compact on-screen list of enabled options.");
			AddCheck(L"List Players", &CVars.ListPlayers, L"Enable player listing for the Players tab.");
			AddCheck(L"Stream Proof", &MiscSettings.StreamProof, L"Hide the overlay from supported capture methods; compatibility may vary.");
			AddCheck(L"Auto Save Settings", &MiscSettings.ShouldAutoSave, L"Periodically save the current configuration.");
			AddCheck(L"Save Enabled Cheats", &MiscSettings.ShouldSaveCVars, L"Include enabled cheat options when saving configuration.");
			AddCheck(L"Save Debug Output", &CVars.SaveDebugToFile, L"Write debug output to the configured log file.");
			AddSlider(L"Reticle X Position", &MiscSettings.ReticlePosition.x, -100.0f, 100.0f, L"Move the custom reticle horizontally from the screen center.");
			AddSlider(L"Reticle Y Position", &MiscSettings.ReticlePosition.y, -100.0f, 100.0f, L"Move the custom reticle vertically from the screen center.");
			AddKeyCombo(L"Menu Toggle Key", &MiscSettings.MenuButton, L"Choose the global key used to show or hide this menu.");
			AddKeyCombo(L"Uninject Key", &MiscSettings.UninjectButton, L"Choose the global key used to unload the module.");
			AddKeyCombo(L"Trigger Bot Key", &TriggerBotVirtualKey, L"Choose the triggerbot activation key.");
			AddKeyCombo(L"ESP Key", &ESPKeyVK, L"Choose the key used by ESP hotkey behavior.");
			AddButton(L"Save Settings", Action::SaveSettings, 280, L"Save the current configuration to disk.");
			AddButton(L"Load Settings", Action::LoadSettings, 280, L"Load the saved configuration from disk.");
			break;
		case 7:
			AddCheck(L"Debug", &CVars.Debug, L"Enable verbose diagnostics; logging every engine event can significantly reduce performance.");
			AddCheck(L"Secret Features", &CVars.SecretFeatures, L"Enable experimental features that may be unstable.");
			AddTextInput(L"Function name filter", &TextVars.DebugFunctionNameMustInclude, L"Only include debug function entries matching this text.");
			AddTextInput(L"Object name filter", &TextVars.DebugFunctionObjectMustInclude, L"Only include debug object entries matching this text.");
			AddButton(L"Print Actors", Action::PrintActors, 280, L"Write the currently discovered actors to the debug output.");
			break;
		case 8:
		{
			std::vector<PlayerMenuEntry> Players;
			{
				std::lock_guard<std::mutex> Lock(PlayerMenuMutex);
				Players = PlayerMenuEntries;
			}
			if (Players.empty())
			{
				AddLabel(L"Players connected: 0", 18, 500);
				CurrentY += 24;
				AddLabel(L"Player list is not available yet.", 18, 500);
				break;
			}
			const size_t DisplayCount = std::min<size_t>(Players.size(), 10);
			std::wstring PlayerCountText = L"Players connected: " + std::to_wstring(Players.size());
			if (Players.size() > DisplayCount)
				PlayerCountText += L"  |  Showing first 10";
			AddLabel(PlayerCountText.c_str(), 18, 650);
			CurrentY += 24;
			for (size_t Index = 0; Index < DisplayCount; ++Index)
			{
				const PlayerMenuEntry& Player = Players[Index];
				std::wstring Label = ToWide(Player.Name);
				if (Player.GodMode)
					Label += L"   [God Mode]";
				if (Player.InfiniteAmmo)
					Label += L"   [Infinite Ammo]";
				CreateWindowExW(0, L"STATIC", Label.c_str(), WS_CHILD | WS_VISIBLE,
					18, CurrentY, 650, 22, PageContainer, nullptr, ModuleInstance, nullptr);
				CurrentY += 22;
				AddPlayerActionButton(L"God Mode", Player.Player, EPlayerMenuAction::GodMode, 18, Player.CanModify);
				AddPlayerActionButton(L"Infinite Ammo", Player.Player, EPlayerMenuAction::InfiniteAmmo, 160, Player.CanModify);
				AddPlayerActionButton(L"Teleport To Self", Player.Player, EPlayerMenuAction::Teleport, 302, Player.CanModify);
				AddPlayerActionButton(L"Speed", Player.Player, EPlayerMenuAction::Speed, 444, Player.CanModify);
				AddCopyPlayerNameButton(Player.Name, 586);
				CurrentY += 30;
			}
			break;
		}
		}

		PageContentHeight = CurrentY + 12;
		CapturePageControls();
		UpdatePageScrollBar();
		if (MenuFont)
			EnumChildWindows(PageContainer, ApplyMenuFont, reinterpret_cast<LPARAM>(MenuFont));
	}

	void ScrollPage(int Command, int TrackPosition = 0)
	{
		if (!PageContainer)
			return;

		RECT Client{};
		GetClientRect(PageContainer, &Client);
		const int PageStep = (std::max)(1L, Client.bottom - 36);
		switch (Command)
		{
		case SB_LINEUP: PageScrollOffset -= 36; break;
		case SB_LINEDOWN: PageScrollOffset += 36; break;
		case SB_PAGEUP: PageScrollOffset -= PageStep; break;
		case SB_PAGEDOWN: PageScrollOffset += PageStep; break;
		case SB_THUMBPOSITION:
		case SB_THUMBTRACK: PageScrollOffset = TrackPosition; break;
		case SB_TOP: PageScrollOffset = 0; break;
		case SB_BOTTOM: PageScrollOffset = PageContentHeight; break;
		default: return;
		}
		UpdatePageScrollBar();
	}

	void RunAction(Action Selected)
	{
		switch (Selected)
		{
		case Action::Magazine: CVars.QueuedAction = EQueuedAction::AddMagazine; break;
		case Action::KillSuspects: CVars.QueuedAction = EQueuedAction::KillAllSuspects; break;
		case Action::SurrenderSuspects: CVars.QueuedAction = EQueuedAction::SurrenderAllSuspects; break;
		case Action::ArrestSuspects: CVars.QueuedAction = EQueuedAction::ArrestAllSuspects; break;
		case Action::KillCivilians: CVars.QueuedAction = EQueuedAction::KillAllCivilians; break;
		case Action::SurrenderCivilians: CVars.QueuedAction = EQueuedAction::SurrenderAllCivilians; break;
		case Action::ArrestCivilians: CVars.QueuedAction = EQueuedAction::ArrestAllCivilians; break;
		case Action::CollectEvidence: CVars.QueuedAction = EQueuedAction::CollectAllEvidence; break;
		case Action::AutoWin: CVars.QueuedAction = EQueuedAction::AutoWin; break;
		case Action::UnlockDoors: CVars.QueuedAction = EQueuedAction::UnlockAllDoors; break;
		case Action::GiveAchievements: CVars.QueuedAction = EQueuedAction::GiveAchievements; break;
		case Action::SaveSettings: CVars.QueuedAction = EQueuedAction::SaveConfiguration; break;
		case Action::LoadSettings: CVars.QueuedAction = EQueuedAction::LoadConfiguration; break;
		case Action::PrintActors: CVars.QueuedAction = EQueuedAction::PrintActors; break;
		}
	}

	bool UpdateRegisteredHotKey(HWND Window, int HotKeyId, int& StoredKey, int SelectedKey)
	{
		const int PreviousKey = StoredKey;
		UnregisterHotKey(Window, HotKeyId);
		if (RegisterHotKey(Window, HotKeyId, MOD_NOREPEAT, static_cast<UINT>(SelectedKey)))
		{
			StoredKey = SelectedKey;
			return true;
		}
		if (!RegisterHotKey(Window, HotKeyId, MOD_NOREPEAT, static_cast<UINT>(PreviousKey)))
			OutputDebugStringA("[Settings] Failed to restore the previous hotkey.\n");
		return false;
	}

	void ChooseColor(HWND Owner, Overlay::FloatColor* Color)
	{
		Overlay::FloatColor Selected;
		{
			std::lock_guard<std::recursive_mutex> SettingsLock(SettingsMutex);
			Selected = *Color;
		}
		static COLORREF CustomColors[16]{};
		CHOOSECOLORW Dialog{};
		Dialog.lStructSize = sizeof(Dialog);
		Dialog.hwndOwner = Owner;
		Dialog.rgbResult = RGB(
			static_cast<BYTE>(std::clamp(Selected.x, 0.0f, 1.0f) * 255.0f),
			static_cast<BYTE>(std::clamp(Selected.y, 0.0f, 1.0f) * 255.0f),
			static_cast<BYTE>(std::clamp(Selected.z, 0.0f, 1.0f) * 255.0f));
		Dialog.lpCustColors = CustomColors;
		Dialog.Flags = CC_FULLOPEN | CC_RGBINIT;
		if (ChooseColorW(&Dialog))
		{
			std::lock_guard<std::recursive_mutex> SettingsLock(SettingsMutex);
			Color->x = static_cast<float>(GetRValue(Dialog.rgbResult)) / 255.0f;
			Color->y = static_cast<float>(GetGValue(Dialog.rgbResult)) / 255.0f;
			Color->z = static_cast<float>(GetBValue(Dialog.rgbResult)) / 255.0f;
		}
	}

	LRESULT CALLBACK MenuWindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
	{
		switch (Message)
		{
		case WM_CREATE:
		{
			MenuWindow = Window;
			const BOOL UseDarkTitleBar = TRUE;
			DwmSetWindowAttribute(Window, DwmwaUseImmersiveDarkMode, &UseDarkTitleBar, sizeof(UseDarkTitleBar));
			RECT Client{};
			GetClientRect(Window, &Client);
			TabControl = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_OWNERDRAWFIXED | TCS_FIXEDWIDTH,
				10, 10, Client.right - 20, 34, Window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(TabControlId)), ModuleInstance, nullptr);
			StyleControl(TabControl);
			SendMessageW(TabControl, TCM_SETITEMSIZE, 0, MAKELPARAM(77, 32));
			HeaderLabel = CreateWindowExW(0, L"STATIC",
				L"CONTROL CENTER  /  HOVER AN OPTION FOR DETAILS",
				WS_CHILD | WS_VISIBLE, 18, 51, 420, 22, Window, nullptr, ModuleInstance, nullptr);
			if (HeaderFont)
				SendMessageW(HeaderLabel, WM_SETFONT, reinterpret_cast<WPARAM>(HeaderFont), TRUE);
			SessionLabel = CreateWindowExW(0, L"STATIC", L"ROLE: WAITING FOR SESSION",
				WS_CHILD | WS_VISIBLE | SS_RIGHT, 445, 51, Client.right - 463, 22,
				Window, nullptr, ModuleInstance, nullptr);
			StyleControl(SessionLabel);
			PageContainer = CreateWindowExW(0, PageWindowClassName, L"",
				WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
				10, 78, Client.right - 20, Client.bottom - 88, Window, nullptr, ModuleInstance, nullptr);
			if (!TabControl || !HeaderLabel || !SessionLabel || !PageContainer)
				return -1;
			SetWindowTheme(PageContainer, L"DarkMode_Explorer", nullptr);
			const wchar_t* Tabs[] = { L"Player", L"Weapon", L"World", L"Aimbot", L"ESP", L"Silent Aim", L"Misc", L"Debug", L"Players" };
			for (int Index = 0; Index < static_cast<int>(std::size(Tabs)); ++Index)
			{
				TCITEMW Item{};
				Item.mask = TCIF_TEXT;
				Item.pszText = const_cast<wchar_t*>(Tabs[Index]);
				TabCtrl_InsertItem(TabControl, Index, &Item);
			}
			CreatePage(0);
			UpdateSessionStatus();
			SetTimer(Window, 1, 1000, nullptr);
			return 0;
		}
		case WM_NOTIFY:
			if (reinterpret_cast<NMHDR*>(LParam)->idFrom == TabControlId &&
				reinterpret_cast<NMHDR*>(LParam)->code == TCN_SELCHANGE)
			{
				CurrentPage = TabCtrl_GetCurSel(TabControl);
				CreatePage(CurrentPage);
				InvalidateRect(TabControl, nullptr, TRUE);
			}
			break;
		case WM_DRAWITEM:
		{
			const auto* DrawItem = reinterpret_cast<const DRAWITEMSTRUCT*>(LParam);
			if (DrawItem->CtlType == ODT_TAB)
			{
				DrawOwnerTab(*DrawItem);
				return TRUE;
			}
			if (DrawItem->CtlType == ODT_BUTTON)
			{
				DrawOwnerButton(*DrawItem);
				return TRUE;
			}
			break;
		}
		case WM_TIMER:
			UpdateSessionStatus();
			if (WParam == 1 && CurrentPage == 8)
				CreatePage(CurrentPage, false);
			return 0;
		case WM_SIZE:
		{
			const int Width = LOWORD(LParam);
			const int Height = HIWORD(LParam);
			if (TabControl)
				MoveWindow(TabControl, 10, 10, (std::max)(0, Width - 20), 34, TRUE);
			if (HeaderLabel)
				MoveWindow(HeaderLabel, 18, 51, (std::max)(0, Width - 315), 22, TRUE);
			if (SessionLabel)
			{
				const int SessionLeft = (std::max)(18, Width - 285);
				MoveWindow(SessionLabel, SessionLeft, 51, (std::max)(0, Width - SessionLeft - 18), 22, TRUE);
			}
			if (PageContainer)
			{
				MoveWindow(PageContainer, 10, 78, (std::max)(0, Width - 20), (std::max)(0, Height - 88), TRUE);
				UpdatePageScrollBar();
			}
			return 0;
		}
		case WM_VSCROLL:
			if (LParam == 0 || reinterpret_cast<HWND>(LParam) == PageContainer)
			{
				SCROLLINFO ScrollInfo{};
				ScrollInfo.cbSize = sizeof(ScrollInfo);
				ScrollInfo.fMask = SIF_TRACKPOS;
				GetScrollInfo(PageContainer, SB_VERT, &ScrollInfo);
				ScrollPage(LOWORD(WParam), ScrollInfo.nTrackPos);
				return 0;
			}
			break;
		case WM_MOUSEWHEEL:
			ScrollPage(GET_WHEEL_DELTA_WPARAM(WParam) > 0 ? SB_LINEUP : SB_LINEDOWN);
			return 0;
		case WM_CTLCOLORSTATIC:
		{
			HDC DeviceContext = reinterpret_cast<HDC>(WParam);
			const HWND Control = reinterpret_cast<HWND>(LParam);
			SetTextColor(DeviceContext,
				(Control == HeaderLabel || Control == SessionLabel) ? MenuAccentColor : MenuTextColor);
			SetBkColor(DeviceContext, MenuBackgroundColor);
			SetBkMode(DeviceContext, OPAQUE);
			return reinterpret_cast<LRESULT>(MenuBackgroundBrush);
		}
		case WM_CTLCOLORBTN:
		case WM_CTLCOLOREDIT:
		case WM_CTLCOLORLISTBOX:
		{
			HDC DeviceContext = reinterpret_cast<HDC>(WParam);
			SetTextColor(DeviceContext, MenuTextColor);
			const COLORREF ControlBackground = Message == WM_CTLCOLOREDIT
				? MenuPanelColor
				: MenuBackgroundColor;
			SetBkColor(DeviceContext, ControlBackground);
			return reinterpret_cast<LRESULT>(
				Message == WM_CTLCOLOREDIT ? MenuPanelBrush : MenuBackgroundBrush);
		}
		case WM_COMMAND:
		{
			const int Id = LOWORD(WParam);
			auto BindingIt = Bindings.find(Id);
			if (BindingIt != Bindings.end() && BindingIt->second.Type == BindingType::Boolean)
			{
				std::lock_guard<std::recursive_mutex> SettingsLock(SettingsMutex);
				bool* Value = static_cast<bool*>(BindingIt->second.Address);
				*Value = !*Value;
				InvalidateRect(reinterpret_cast<HWND>(LParam), nullptr, TRUE);
				if (BindingIt->second.Address == &CVars.GodMode)
					CVars.QueuedAction = EQueuedAction::ToggleGodMode;
				else if (BindingIt->second.Address == &CVars.InfAmmo)
					CVars.QueuedAction = EQueuedAction::ToggleInfAmmo;
				return 0;
			}
			if (BindingIt != Bindings.end() && BindingIt->second.Type == BindingType::String && HIWORD(WParam) == EN_CHANGE)
			{
				const int Length = GetWindowTextLengthW(reinterpret_cast<HWND>(LParam));
				std::wstring WideValue(static_cast<size_t>(Length) + 1, L'\0');
				if (Length > 0)
					GetWindowTextW(reinterpret_cast<HWND>(LParam), WideValue.data(), Length + 1);
				WideValue.resize(static_cast<size_t>(Length));
				const int Utf8Length = WideCharToMultiByte(CP_UTF8, 0, WideValue.data(), Length, nullptr, 0, nullptr, nullptr);
				std::string Utf8Value(static_cast<size_t>(Utf8Length), '\0');
				if (Utf8Length > 0)
					WideCharToMultiByte(CP_UTF8, 0, WideValue.data(), Length, Utf8Value.data(), Utf8Length, nullptr, nullptr);
				{
					std::lock_guard<std::recursive_mutex> SettingsLock(SettingsMutex);
					*static_cast<std::string*>(BindingIt->second.Address) = std::move(Utf8Value);
				}
				return 0;
			}
			auto ComboIt = ComboStrings.find(Id);
			if (ComboIt != ComboStrings.end() && HIWORD(WParam) == CBN_SELCHANGE)
			{
				const int Selection = static_cast<int>(SendMessageW(reinterpret_cast<HWND>(LParam), CB_GETCURSEL, 0, 0));
				if (Selection >= 0 && static_cast<size_t>(Selection) < ComboIt->second.Options.size())
				{
					std::lock_guard<std::recursive_mutex> SettingsLock(SettingsMutex);
					*ComboIt->second.Value = ComboIt->second.Options[Selection];
				}
				return 0;
			}
			auto KeyIt = KeyBindings.find(Id);
			if (KeyIt != KeyBindings.end() && HIWORD(WParam) == CBN_SELCHANGE)
			{
				const LRESULT Selection = SendMessageW(reinterpret_cast<HWND>(LParam), CB_GETCURSEL, 0, 0);
				if (Selection >= 0)
				{
					std::lock_guard<std::recursive_mutex> SettingsLock(SettingsMutex);
					const int SelectedKey = static_cast<int>(SendMessageW(reinterpret_cast<HWND>(LParam), CB_GETITEMDATA, Selection, 0));
					if (KeyIt->second == &MiscSettings.MenuButton)
					{
						if (!UpdateRegisteredHotKey(Window, MenuHotKeyId, MiscSettings.MenuButton, SelectedKey))
							OutputDebugStringA("[Settings] Failed to register the selected menu-toggle hotkey.\n");
					}
					else if (KeyIt->second == &MiscSettings.UninjectButton)
					{
						if (!UpdateRegisteredHotKey(Window, UninjectHotKeyId, MiscSettings.UninjectButton, SelectedKey))
							OutputDebugStringA("[Settings] Failed to register the selected uninject hotkey.\n");
					}
					else
						*KeyIt->second = SelectedKey;
				}
				return 0;
			}
			auto ColorIt = ColorBindings.find(Id);
			if (ColorIt != ColorBindings.end())
			{
				ChooseColor(Window, ColorIt->second);
				InvalidateRect(reinterpret_cast<HWND>(LParam), nullptr, TRUE);
				return 0;
			}
			auto ActionIt = Actions.find(Id);
			if (ActionIt != Actions.end())
			{
				RunAction(ActionIt->second);
				return 0;
			}
			auto PlayerActionIt = PlayerActions.find(Id);
			if (PlayerActionIt != PlayerActions.end())
			{
				std::lock_guard<std::mutex> Lock(PlayerMenuMutex);
				PlayerMenuRequests.push_back(PlayerActionIt->second);
				return 0;
			}
			auto PlayerNameIt = PlayerNamesToCopy.find(Id);
			if (PlayerNameIt != PlayerNamesToCopy.end())
			{
				if (!CopyPlayerNameToClipboard(PlayerNameIt->second))
					OutputDebugStringA("[Settings] Failed to copy the player name to the clipboard.\n");
				return 0;
			}
			return 0;
		}
		case WM_HSCROLL:
		{
			std::lock_guard<std::recursive_mutex> SettingsLock(SettingsMutex);
			HWND Control = reinterpret_cast<HWND>(LParam);
			const int Id = GetDlgCtrlID(Control);
			auto It = Bindings.find(Id);
			if (It == Bindings.end())
				return 0;
			const int Position = static_cast<int>(SendMessageW(Control, TBM_GETPOS, 0, 0));
			if (It->second.Type == BindingType::Float)
			{
				const float Ratio = static_cast<float>(Position) / 1000.0f;
				*static_cast<float*>(It->second.Address) = It->second.Minimum + Ratio * (It->second.Maximum - It->second.Minimum);
				if (It->second.Address == &CVars.FOV)
					CVars.QueuedAction = EQueuedAction::ChangeFOV;
			}
			else if (It->second.Type == BindingType::Integer)
				*static_cast<int*>(It->second.Address) = Position;
			if (It->second.Address == &ESPSettings.BoneOpacity)
			{
				const float Opacity = ESPSettings.BoneOpacity;
				ESPSettings.SuspectColor.w = Opacity;
				ESPSettings.CivilianColor.w = Opacity;
				ESPSettings.DeadColor.w = Opacity;
				ESPSettings.TeamColor.w = Opacity;
				ESPSettings.ArrestColor.w = Opacity;
			}
			return 0;
		}
		case WM_HOTKEY:
			if (WParam == MenuHotKeyId)
			{
				ShowWindow(Window, IsWindowVisible(Window) ? SW_HIDE : SW_SHOW);
				return 0;
			}
			if (WParam == UninjectHotKeyId)
			{
				Cleaning.store(true);
				return 0;
			}
			break;
		case WM_CLOSE:
			ShowWindow(Window, SW_HIDE);
			return 0;
		case WM_DESTROY:
			KillTimer(Window, 1);
			PostQuitMessage(0);
			return 0;
		}
		return DefWindowProcW(Window, Message, WParam, LParam);
	}

	DWORD WINAPI MenuThread(void*)
	{
		INITCOMMONCONTROLSEX Controls{ sizeof(Controls), ICC_BAR_CLASSES | ICC_TAB_CLASSES | ICC_WIN95_CLASSES };
		InitCommonControlsEx(&Controls);
		MenuBackgroundBrush = CreateSolidBrush(MenuBackgroundColor);
		MenuPanelBrush = CreateSolidBrush(MenuPanelColor);
		MenuFont = CreateFontW(-17, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
		HeaderFont = CreateFontW(-19, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
		WNDCLASSEXW PageClass{};
		PageClass.cbSize = sizeof(PageClass);
		PageClass.lpfnWndProc = PageContainerProc;
		PageClass.hInstance = ModuleInstance;
		PageClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		PageClass.hbrBackground = MenuBackgroundBrush;
		PageClass.lpszClassName = PageWindowClassName;
		if (!RegisterClassExW(&PageClass))
		{
			if (MenuFont) DeleteObject(MenuFont);
			if (HeaderFont) DeleteObject(HeaderFont);
			if (MenuBackgroundBrush) DeleteObject(MenuBackgroundBrush);
			if (MenuPanelBrush) DeleteObject(MenuPanelBrush);
			MenuFont = nullptr;
			HeaderFont = nullptr;
			MenuBackgroundBrush = nullptr;
			MenuPanelBrush = nullptr;
			SetEvent(MenuReadyEvent);
			return 1;
		}
		WNDCLASSEXW WindowClass{};
		WindowClass.cbSize = sizeof(WindowClass);
		WindowClass.lpfnWndProc = MenuWindowProc;
		WindowClass.hInstance = ModuleInstance;
		WindowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		WindowClass.hbrBackground = MenuBackgroundBrush;
		WindowClass.lpszClassName = WindowClassName;
		if (!RegisterClassExW(&WindowClass))
		{
			UnregisterClassW(PageWindowClassName, ModuleInstance);
			if (MenuFont) DeleteObject(MenuFont);
			if (HeaderFont) DeleteObject(HeaderFont);
			if (MenuBackgroundBrush) DeleteObject(MenuBackgroundBrush);
			if (MenuPanelBrush) DeleteObject(MenuPanelBrush);
			MenuFont = nullptr;
			HeaderFont = nullptr;
			MenuBackgroundBrush = nullptr;
			MenuPanelBrush = nullptr;
			SetEvent(MenuReadyEvent);
			return 1;
		}

		MenuWindow = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, WindowClassName, L"RON | Control Deck",
			WS_OVERLAPPEDWINDOW, 100, 100, 750, 760, nullptr, nullptr, ModuleInstance, nullptr);
		if (!MenuWindow)
		{
			UnregisterClassW(WindowClassName, ModuleInstance);
			UnregisterClassW(PageWindowClassName, ModuleInstance);
			if (MenuFont) DeleteObject(MenuFont);
			if (HeaderFont) DeleteObject(HeaderFont);
			if (MenuBackgroundBrush) DeleteObject(MenuBackgroundBrush);
			if (MenuPanelBrush) DeleteObject(MenuPanelBrush);
			MenuFont = nullptr;
			HeaderFont = nullptr;
			MenuBackgroundBrush = nullptr;
			MenuPanelBrush = nullptr;
			SetEvent(MenuReadyEvent);
			return 1;
		}

		ShowWindow(MenuWindow, SW_SHOW);
		UpdateWindow(MenuWindow);
		if (!RegisterHotKey(MenuWindow, MenuHotKeyId, MOD_NOREPEAT, MiscSettings.MenuButton))
			OutputDebugStringA("[Settings] Failed to register the menu-toggle hotkey.\n");
		if (!RegisterHotKey(MenuWindow, UninjectHotKeyId, MOD_NOREPEAT, MiscSettings.UninjectButton))
			OutputDebugStringA("[Settings] Failed to register the uninject hotkey.\n");
		SetEvent(MenuReadyEvent);

		MSG Message{};
		while (GetMessageW(&Message, nullptr, 0, 0) > 0)
		{
			TranslateMessage(&Message);
			DispatchMessageW(&Message);
		}

		if (MenuWindow)
		{
			UnregisterHotKey(MenuWindow, MenuHotKeyId);
			UnregisterHotKey(MenuWindow, UninjectHotKeyId);
			DestroyWindow(MenuWindow);
			MenuWindow = nullptr;
		}
		PageContainer = nullptr;
		TabControl = nullptr;
		HeaderLabel = nullptr;
		SessionLabel = nullptr;
		if (ToolTipWindow)
		{
			DestroyWindow(ToolTipWindow);
			ToolTipWindow = nullptr;
		}
		ToolTipTexts.clear();
		if (MenuFont) DeleteObject(MenuFont);
		if (HeaderFont) DeleteObject(HeaderFont);
		if (MenuBackgroundBrush) DeleteObject(MenuBackgroundBrush);
		if (MenuPanelBrush) DeleteObject(MenuPanelBrush);
		MenuFont = nullptr;
		HeaderFont = nullptr;
		MenuBackgroundBrush = nullptr;
		MenuPanelBrush = nullptr;
		UnregisterClassW(WindowClassName, ModuleInstance);
		UnregisterClassW(PageWindowClassName, ModuleInstance);
		return 0;
	}
}

bool OverlayMenu::Initialize(HINSTANCE Instance)
{
	if (MenuThreadHandle)
		return true;
	ModuleInstance = Instance;
	MenuReadyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!MenuReadyEvent)
		return false;
	MenuThreadHandle = CreateThread(nullptr, 0, MenuThread, nullptr, 0, &MenuThreadId);
	if (!MenuThreadHandle)
	{
		CloseHandle(MenuReadyEvent);
		MenuReadyEvent = nullptr;
		return false;
	}
	const DWORD WaitResult = WaitForSingleObject(MenuReadyEvent, 5000);
	CloseHandle(MenuReadyEvent);
	MenuReadyEvent = nullptr;
	return WaitResult == WAIT_OBJECT_0 && MenuWindow != nullptr;
}

void OverlayMenu::Shutdown()
{
	if (MenuThreadId)
		PostThreadMessageW(MenuThreadId, WM_QUIT, 0, 0);
	if (MenuThreadHandle)
	{
		WaitForSingleObject(MenuThreadHandle, INFINITE);
		CloseHandle(MenuThreadHandle);
		MenuThreadHandle = nullptr;
	}
	MenuThreadId = 0;
}
