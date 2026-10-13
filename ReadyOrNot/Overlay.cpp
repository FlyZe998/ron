#include "pch.h"
#include "Overlay.h"

#include <gdiplus.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

namespace
{
	enum class DrawType
	{
		Line,
		Rectangle,
		Circle,
		Text
	};

	struct DrawCommand
	{
		DrawType Type;
		float X1;
		float Y1;
		float X2;
		float Y2;
		float Thickness;
		bool Filled;
		Overlay::Color Tint;
		std::string Value;
	};

	std::mutex DrawMutex;
	std::vector<DrawCommand> DrawCommands;
	std::atomic<HWND> OverlayWindow{ nullptr };
	ULONG_PTR GdiPlusToken = 0;
	bool ClassRegistered = false;
	HINSTANCE OverlayInstance = nullptr;
	HANDLE OverlayThreadHandle = nullptr;
	HANDLE OverlayReadyEvent = nullptr;
	DWORD OverlayThreadId = 0;
	std::atomic<bool> OverlayThreadInitialized{ false };
	std::atomic<bool> OverlayVisible{ false };
	std::atomic<bool> RenderRequestPending{ false };
	std::atomic<int64_t> LastRenderRequestNanoseconds{ 0 };
	constexpr int64_t MinimumRenderIntervalNanoseconds = 20'200'200;
	const wchar_t* OverlayClassName = L"ReadyOrNotIndependentOverlay";
	constexpr UINT WM_OVERLAY_RENDER = WM_APP + 1;
	constexpr UINT WM_OVERLAY_HIDE = WM_APP + 2;

	bool RenderOnOverlayThread(HWND Window, HWND GameWindow);

	struct OverlaySurface
	{
		HDC MemoryDc = nullptr;
		HBITMAP Bitmap = nullptr;
		HGDIOBJ PreviousBitmap = nullptr;
		BYTE* Pixels = nullptr;
		int Width = 0;
		int Height = 0;

		bool Ensure(HDC ScreenDc, int NewWidth, int NewHeight)
		{
			if (MemoryDc && Bitmap && Pixels && Width == NewWidth && Height == NewHeight)
				return true;

			Reset();
			MemoryDc = CreateCompatibleDC(ScreenDc);
			if (!MemoryDc)
				return false;

			BITMAPINFO BitmapInfo{};
			BitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			BitmapInfo.bmiHeader.biWidth = NewWidth;
			BitmapInfo.bmiHeader.biHeight = -NewHeight;
			BitmapInfo.bmiHeader.biPlanes = 1;
			BitmapInfo.bmiHeader.biBitCount = 32;
			BitmapInfo.bmiHeader.biCompression = BI_RGB;
			Bitmap = CreateDIBSection(ScreenDc, &BitmapInfo, DIB_RGB_COLORS,
				reinterpret_cast<void**>(&Pixels), nullptr, 0);
			if (!Bitmap || !Pixels)
			{
				Reset();
				return false;
			}

			PreviousBitmap = SelectObject(MemoryDc, Bitmap);
			if (!PreviousBitmap || PreviousBitmap == HGDI_ERROR)
			{
				PreviousBitmap = nullptr;
				Reset();
				return false;
			}
			Width = NewWidth;
			Height = NewHeight;
			return true;
		}

		void Reset()
		{
			if (MemoryDc && PreviousBitmap)
				SelectObject(MemoryDc, PreviousBitmap);
			if (Bitmap)
				DeleteObject(Bitmap);
			if (MemoryDc)
				DeleteDC(MemoryDc);
			MemoryDc = nullptr;
			Bitmap = nullptr;
			PreviousBitmap = nullptr;
			Pixels = nullptr;
			Width = 0;
			Height = 0;
		}
	};

	OverlaySurface RenderSurface;

	namespace HudStyle
	{
		constexpr bool CornerBrackets = true;
		constexpr float BracketRatio = 0.28f;
		constexpr float BracketMinLength = 5.0f;
		constexpr float BracketMaxLength = 26.0f;
		constexpr int BoxFillAlpha = 12;
		constexpr bool Glow = true;
		constexpr size_t GlowCommandLimit = 450;
		constexpr int GlowAlphaPercent = 30;
		constexpr float GlowExtraWidth = 3.0f;
		constexpr float TextPaddingX = 5.0f;
		constexpr float TextCut = 4.0f;
		constexpr float FontSize = 14.0f;
		constexpr float RingDashMinRadius = 28.0f;   // big unfilled circles become segmented HUD rings
	}

	struct HudFonts
	{
		std::unique_ptr<Gdiplus::Font> Text;
		std::unique_ptr<Gdiplus::StringFormat> Format;

		void Ensure()
		{
			if (Text && Format)
				return;

			Text.reset();
			const wchar_t* Candidates[] = { L"Bahnschrift", L"Segoe UI" };
			for (const wchar_t* Name : Candidates)
			{
				Gdiplus::FontFamily Family(Name);
				if (!Family.IsAvailable())
					continue;
				const INT Style = Family.IsStyleAvailable(Gdiplus::FontStyleBold)
					? Gdiplus::FontStyleBold
					: Gdiplus::FontStyleRegular;
				auto Candidate = std::make_unique<Gdiplus::Font>(&Family, HudStyle::FontSize, Style, Gdiplus::UnitPixel);
				if (Candidate->IsAvailable())
				{
					Text = std::move(Candidate);
					break;
				}
			}
			if (!Text)
				Text = std::make_unique<Gdiplus::Font>(L"Segoe UI", HudStyle::FontSize, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

			Format = std::make_unique<Gdiplus::StringFormat>();
			Format->SetAlignment(Gdiplus::StringAlignmentNear);
			Format->SetLineAlignment(Gdiplus::StringAlignmentNear);
			Format->SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsNoClip);
		}

		void Release()
		{
			Text.reset();
			Format.reset();
		}
	};

	HudFonts HudFontCache;

	BYTE ScaleAlpha(BYTE Alpha, int Percent)
	{
		return static_cast<BYTE>((static_cast<int>(Alpha) * Percent) / 100);
	}

	void AddChamferedPolygon(Gdiplus::GraphicsPath& Path, float Left, float Top, float Right, float Bottom, float Cut)
	{
		const Gdiplus::PointF Points[6] = {
			Gdiplus::PointF(Left + Cut, Top),
			Gdiplus::PointF(Right, Top),
			Gdiplus::PointF(Right, Bottom - Cut),
			Gdiplus::PointF(Right - Cut, Bottom),
			Gdiplus::PointF(Left, Bottom),
			Gdiplus::PointF(Left, Top + Cut)
		};
		Path.AddPolygon(Points, 6);
	}

	void AddCornerBrackets(Gdiplus::GraphicsPath& Path, float Left, float Top, float Right, float Bottom, float Length)
	{
		Path.StartFigure();
		Path.AddLine(Left, Top + Length, Left, Top);
		Path.AddLine(Left, Top, Left + Length, Top);
		Path.StartFigure();
		Path.AddLine(Right - Length, Top, Right, Top);
		Path.AddLine(Right, Top, Right, Top + Length);
		Path.StartFigure();
		Path.AddLine(Right, Bottom - Length, Right, Bottom);
		Path.AddLine(Right, Bottom, Right - Length, Bottom);
		Path.StartFigure();
		Path.AddLine(Left + Length, Bottom, Left, Bottom);
		Path.AddLine(Left, Bottom, Left, Bottom - Length);
	}

	LRESULT CALLBACK OverlayWindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
	{
		if (Message == WM_NCHITTEST)
			return HTTRANSPARENT;
		if (Message == WM_OVERLAY_RENDER)
		{
			static bool RenderFailureLogged = false;
			if (OverlayVisible.load() &&
				!RenderOnOverlayThread(Window, reinterpret_cast<HWND>(WParam)) &&
				!RenderFailureLogged)
			{
				OutputDebugStringA("[Overlay] Failed to render a queued frame.\n");
				RenderFailureLogged = true;
			}
			RenderRequestPending.store(false);
			return 0;
		}
		if (Message == WM_OVERLAY_HIDE)
		{
			if (!OverlayVisible.load())
				ShowWindow(Window, SW_HIDE);
			return 0;
		}

		return DefWindowProcW(Window, Message, WParam, LParam);
	}

	Gdiplus::Color ToGdiColor(Overlay::Color Tint)
	{
		return Gdiplus::Color(Tint.A, Tint.R, Tint.G, Tint.B);
	}

	std::wstring ToWideText(const std::string& Text)
	{
		if (Text.empty())
			return {};
		int Length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Text.data(), static_cast<int>(Text.size()), nullptr, 0);
		UINT CodePage = CP_UTF8;
		if (Length == 0)
		{
			CodePage = CP_ACP;
			Length = MultiByteToWideChar(CodePage, 0, Text.data(), static_cast<int>(Text.size()), nullptr, 0);
		}
		if (Length == 0)
			return {};
		std::wstring Result(static_cast<size_t>(Length), L'\0');
		MultiByteToWideChar(CodePage, 0, Text.data(), static_cast<int>(Text.size()), Result.data(), Length);
		return Result;
	}

	DWORD WINAPI OverlayThread(void* Parameter)
	{
		OverlayInstance = static_cast<HINSTANCE>(Parameter);
		Gdiplus::GdiplusStartupInput StartupInput;
		if (Gdiplus::GdiplusStartup(&GdiPlusToken, &StartupInput, nullptr) != Gdiplus::Ok)
		{
			SetEvent(OverlayReadyEvent);
			return 1;
		}

		WNDCLASSEXW WindowClass{};
		WindowClass.cbSize = sizeof(WindowClass);
		WindowClass.lpfnWndProc = OverlayWindowProc;
		WindowClass.hInstance = OverlayInstance;
		WindowClass.lpszClassName = OverlayClassName;
		WindowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		if (!RegisterClassExW(&WindowClass))
		{
			Gdiplus::GdiplusShutdown(GdiPlusToken);
			GdiPlusToken = 0;
			SetEvent(OverlayReadyEvent);
			return 1;
		}
		ClassRegistered = true;

		HWND Window = CreateWindowExW(
			WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
			OverlayClassName,
			L"",
			WS_POPUP,
			0,
			0,
			1,
			1,
			nullptr,
			nullptr,
			OverlayInstance,
			nullptr);
		if (!Window)
		{
			UnregisterClassW(OverlayClassName, OverlayInstance);
			ClassRegistered = false;
			Gdiplus::GdiplusShutdown(GdiPlusToken);
			GdiPlusToken = 0;
			SetEvent(OverlayReadyEvent);
			return 1;
		}

		OverlayWindow.store(Window);
		OverlayThreadInitialized.store(true);
		SetEvent(OverlayReadyEvent);

		MSG Message{};
		while (GetMessageW(&Message, nullptr, 0, 0) > 0)
		{
			TranslateMessage(&Message);
			DispatchMessageW(&Message);
		}

		OverlayVisible.store(false);
		OverlayWindow.store(nullptr);
		DestroyWindow(Window);
		HudFontCache.Release();
		RenderSurface.Reset();
		UnregisterClassW(OverlayClassName, OverlayInstance);
		ClassRegistered = false;
		Gdiplus::GdiplusShutdown(GdiPlusToken);
		GdiPlusToken = 0;
		return 0;
	}
}

bool Overlay::Initialize(HINSTANCE Instance)
{
	if (OverlayWindow.load())
		return true;

	OverlayReadyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!OverlayReadyEvent)
		return false;

	OverlayThreadInitialized.store(false);
	OverlayThreadHandle = CreateThread(nullptr, 0, OverlayThread, Instance, 0, &OverlayThreadId);
	if (!OverlayThreadHandle)
	{
		CloseHandle(OverlayReadyEvent);
		OverlayReadyEvent = nullptr;
		return false;
	}

	const DWORD WaitResult = WaitForSingleObject(OverlayReadyEvent, 5000);
	CloseHandle(OverlayReadyEvent);
	OverlayReadyEvent = nullptr;
	if (WaitResult != WAIT_OBJECT_0 || !OverlayThreadInitialized.load())
	{
		if (OverlayThreadHandle)
		{
			PostThreadMessageW(OverlayThreadId, WM_QUIT, 0, 0);
			WaitForSingleObject(OverlayThreadHandle, INFINITE);
			CloseHandle(OverlayThreadHandle);
			OverlayThreadHandle = nullptr;
		}
		OverlayThreadId = 0;
		return false;
	}

	return true;
}

void Overlay::Shutdown()
{
	if (OverlayThreadHandle)
	{
		PostThreadMessageW(OverlayThreadId, WM_QUIT, 0, 0);
		WaitForSingleObject(OverlayThreadHandle, INFINITE);
		CloseHandle(OverlayThreadHandle);
		OverlayThreadHandle = nullptr;
	}
	OverlayThreadId = 0;
	OverlayThreadInitialized.store(false);
	RenderRequestPending.store(false);
	std::lock_guard<std::mutex> Lock(DrawMutex);
	DrawCommands.clear();
}

void Overlay::Hide()
{
	OverlayVisible.store(false);
	if (HWND Window = OverlayWindow.load())
		PostMessageW(Window, WM_OVERLAY_HIDE, 0, 0);
}

Overlay::Color Overlay::FromPackedColor(uint32_t Packed)
{
	return Color{
		static_cast<BYTE>(Packed & 0xFF),
		static_cast<BYTE>((Packed >> 8) & 0xFF),
		static_cast<BYTE>((Packed >> 16) & 0xFF),
		static_cast<BYTE>((Packed >> 24) & 0xFF)
	};
}

void Overlay::BeginFrame(int, int)
{
	std::lock_guard<std::mutex> Lock(DrawMutex);
	DrawCommands.clear();
}

void Overlay::Line(double X1, double Y1, double X2, double Y2, Color Tint, float Thickness)
{
	std::lock_guard<std::mutex> Lock(DrawMutex);
	DrawCommands.push_back({
		DrawType::Line,
		static_cast<float>(X1),
		static_cast<float>(Y1),
		static_cast<float>(X2),
		static_cast<float>(Y2),
		Thickness,
		false,
		Tint,
		{}
		});
}

void Overlay::Rectangle(double X1, double Y1, double X2, double Y2, Color Tint, float Thickness)
{
	std::lock_guard<std::mutex> Lock(DrawMutex);
	DrawCommands.push_back({
		DrawType::Rectangle,
		static_cast<float>(X1),
		static_cast<float>(Y1),
		static_cast<float>(X2),
		static_cast<float>(Y2),
		Thickness,
		false,
		Tint,
		{}
		});
}

void Overlay::Circle(double X, double Y, double Radius, Color Tint, float Thickness, bool Filled)
{
	std::lock_guard<std::mutex> Lock(DrawMutex);
	DrawCommands.push_back({
		DrawType::Circle,
		static_cast<float>(X),
		static_cast<float>(Y),
		static_cast<float>(Radius),
		0.0f,
		Thickness,
		Filled,
		Tint,
		{}
		});
}

void Overlay::Text(double X, double Y, Color Tint, const std::string& Value)
{
	std::lock_guard<std::mutex> Lock(DrawMutex);
	DrawCommands.push_back({
		DrawType::Text,
		static_cast<float>(X),
		static_cast<float>(Y),
		0.0f,
		0.0f,
		1.0f,
		false,
		Tint,
		Value
		});
}

bool Overlay::RequestRender(HWND GameWindow)
{
	HWND Window = OverlayWindow.load();
	if (!Window || !IsWindow(GameWindow))
		return false;

	OverlayVisible.store(true);
	if (RenderRequestPending.load())
		return true;

	const int64_t NowNanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
	int64_t PreviousRequest = LastRenderRequestNanoseconds.load();
	while (NowNanoseconds - PreviousRequest >= MinimumRenderIntervalNanoseconds)
	{
		if (LastRenderRequestNanoseconds.compare_exchange_weak(PreviousRequest, NowNanoseconds))
			break;
	}
	if (NowNanoseconds - PreviousRequest < MinimumRenderIntervalNanoseconds)
		return true;

	if (RenderRequestPending.exchange(true))
		return true;
	if (!PostMessageW(Window, WM_OVERLAY_RENDER, reinterpret_cast<WPARAM>(GameWindow), 0))
	{
		RenderRequestPending.store(false);
		return false;
	}
	return true;
}

namespace
{
	bool RenderOnOverlayThread(HWND Window, HWND GameWindow)
	{
		if (!IsWindow(GameWindow))
			return false;

		static bool LastStreamProof = false;
		static bool StreamProofAttempted = false;
		bool StreamProofEnabled = false;
		bool HasSettingsSnapshot = false;
		{
			std::unique_lock<std::recursive_mutex> SettingsLock(SettingsMutex, std::try_to_lock);
			if (SettingsLock.owns_lock())
			{
				StreamProofEnabled = MiscSettings.StreamProof;
				HasSettingsSnapshot = true;
			}
		}
		if (HasSettingsSnapshot && (!StreamProofAttempted || LastStreamProof != StreamProofEnabled))
		{
			const DWORD Affinity = StreamProofEnabled ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE;
			if (!SetWindowDisplayAffinity(Window, Affinity))
			{
				OutputDebugStringA("[Overlay] Could not update display-capture affinity.\n");
			}
			LastStreamProof = StreamProofEnabled;
			StreamProofAttempted = true;
		}

		RECT ClientRect{};
		if (!GetClientRect(GameWindow, &ClientRect))
			return false;
		POINT TopLeft{ ClientRect.left, ClientRect.top };
		if (!ClientToScreen(GameWindow, &TopLeft))
			return false;

		const int Width = ClientRect.right - ClientRect.left;
		const int Height = ClientRect.bottom - ClientRect.top;
		if (Width <= 0 || Height <= 0)
		{
			ShowWindow(Window, SW_HIDE);
			return true;
		}

		std::vector<DrawCommand> Commands;
		{
			std::lock_guard<std::mutex> Lock(DrawMutex);
			Commands = DrawCommands;
		}

		HDC ScreenDc = GetDC(nullptr);
		if (!ScreenDc)
			return false;
		if (!RenderSurface.Ensure(ScreenDc, Width, Height))
		{
			ReleaseDC(nullptr, ScreenDc);
			return false;
		}
		bool DrawSucceeded = true;
		{
			Gdiplus::Bitmap Surface(Width, Height, Width * 4, PixelFormat32bppPARGB, RenderSurface.Pixels);
			Gdiplus::Graphics Graphics(&Surface);
			Graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
			Graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
			Graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
			Graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
			Graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
			Graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
			DrawSucceeded = Graphics.GetLastStatus() == Gdiplus::Ok;

			HudFontCache.Ensure();
			const Gdiplus::Font* const TextFont = HudFontCache.Text.get();
			const Gdiplus::StringFormat* const TextFormat = HudFontCache.Format.get();
			const bool DrawGlow = HudStyle::Glow && Commands.size() <= HudStyle::GlowCommandLimit;
			const auto MakeRound = [](Gdiplus::Pen& Target)
				{
					Target.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
					Target.SetLineJoin(Gdiplus::LineJoinRound);
				};

			for (const DrawCommand& Command : Commands)
			{
				const Gdiplus::Color Tint = ToGdiColor(Command.Tint);
				const Gdiplus::Color GlowTint(
					ScaleAlpha(Command.Tint.A, HudStyle::GlowAlphaPercent),
					Command.Tint.R, Command.Tint.G, Command.Tint.B);
				const bool UseGlow = DrawGlow && Command.Tint.A > 0;
				const float Thickness = (Command.Thickness > 0.0f) ? Command.Thickness : 1.0f;
				const float GlowThickness = Thickness + HudStyle::GlowExtraWidth;
				Gdiplus::Pen Pen(Tint, Thickness);
				MakeRound(Pen);

				switch (Command.Type)
				{
				case DrawType::Line:
				{
					if (UseGlow)
					{
						Gdiplus::Pen GlowPen(GlowTint, GlowThickness);
						MakeRound(GlowPen);
						Graphics.DrawLine(&GlowPen, Command.X1, Command.Y1, Command.X2, Command.Y2);
					}
					Graphics.DrawLine(&Pen, Command.X1, Command.Y1, Command.X2, Command.Y2);
					break;
				}
				case DrawType::Rectangle:
				{
					const float Left = (std::min)(Command.X1, Command.X2);
					const float Top = (std::min)(Command.Y1, Command.Y2);
					const float Right = (std::max)(Command.X1, Command.X2);
					const float Bottom = (std::max)(Command.Y1, Command.Y2);
					const float Width = Right - Left;
					const float Height = Bottom - Top;

					if (HudStyle::BoxFillAlpha > 0 && Width > 0.0f && Height > 0.0f)
					{
						const BYTE FillAlpha = static_cast<BYTE>((std::min)(static_cast<int>(Command.Tint.A), HudStyle::BoxFillAlpha));
						Gdiplus::SolidBrush FillBrush(Gdiplus::Color(FillAlpha, Command.Tint.R, Command.Tint.G, Command.Tint.B));
						Graphics.FillRectangle(&FillBrush, Left, Top, Width, Height);
					}

					const float Shorter = (std::min)(Width, Height);
					if (HudStyle::CornerBrackets && Shorter >= HudStyle::BracketMinLength * 2.0f)
					{
						const float Length = std::clamp(Shorter * HudStyle::BracketRatio,
							HudStyle::BracketMinLength, HudStyle::BracketMaxLength);
						Gdiplus::GraphicsPath Brackets;
						AddCornerBrackets(Brackets, Left, Top, Right, Bottom, Length);
						if (UseGlow)
						{
							Gdiplus::Pen GlowPen(GlowTint, GlowThickness);
							MakeRound(GlowPen);
							Graphics.DrawPath(&GlowPen, &Brackets);
						}
						Graphics.DrawPath(&Pen, &Brackets);
					}
					else
					{
						if (UseGlow)
						{
							Gdiplus::Pen GlowPen(GlowTint, GlowThickness);
							MakeRound(GlowPen);
							Graphics.DrawRectangle(&GlowPen, Left, Top, Width, Height);
						}
						Graphics.DrawRectangle(&Pen, Left, Top, Width, Height);
					}
					break;
				}
				case DrawType::Circle:
				{
					const float Radius = Command.X2;
					const float Diameter = Radius * 2.0f;
					if (UseGlow)
					{
						Gdiplus::Pen GlowPen(GlowTint, Command.Filled ? (1.0f + HudStyle::GlowExtraWidth) : GlowThickness);
						MakeRound(GlowPen);
						Graphics.DrawEllipse(&GlowPen, Command.X1 - Radius, Command.Y1 - Radius, Diameter, Diameter);
					}
					if (Command.Filled)
					{
						Gdiplus::SolidBrush Brush(Tint);
						Graphics.FillEllipse(&Brush, Command.X1 - Radius, Command.Y1 - Radius, Diameter, Diameter);
					}
					else if (Radius >= HudStyle::RingDashMinRadius)
					{
						Gdiplus::Pen BasePen(Gdiplus::Color(ScaleAlpha(Command.Tint.A, 35), Command.Tint.R, Command.Tint.G, Command.Tint.B), 1.0f);
						Graphics.DrawEllipse(&BasePen, Command.X1 - Radius, Command.Y1 - Radius, Diameter, Diameter);
						Gdiplus::Pen DashPen(Tint, Thickness);
						const Gdiplus::REAL Pattern[2] = { 8.0f / Thickness, 5.0f / Thickness };
						DashPen.SetDashPattern(Pattern, 2);
						Graphics.DrawEllipse(&DashPen, Command.X1 - Radius, Command.Y1 - Radius, Diameter, Diameter);
					}
					else
						Graphics.DrawEllipse(&Pen, Command.X1 - Radius, Command.Y1 - Radius, Diameter, Diameter);
					break;
				}
				case DrawType::Text:
				{
					std::wstring WideText = ToWideText(Command.Value);
					if (WideText.empty())
						break;

					const Gdiplus::PointF Origin(Command.X1, Command.Y1);

					// Dark chamfered tag with a neon edge bar, sized to the text.
					Gdiplus::RectF TextBounds;
					if (Graphics.MeasureString(WideText.c_str(), -1, TextFont, Origin, TextFormat, &TextBounds) == Gdiplus::Ok &&
						TextBounds.Width > 0.0f && TextBounds.Height > 0.0f)
					{
						const float Left = Origin.X - HudStyle::TextPaddingX;
						const float Top = TextBounds.Y + 1.0f;
						const float Right = TextBounds.X + TextBounds.Width;
						const float Bottom = TextBounds.Y + TextBounds.Height - 1.0f;
						if (Right > Left && Bottom > Top + HudStyle::TextCut)
						{
							Gdiplus::GraphicsPath Tag;
							AddChamferedPolygon(Tag, Left, Top, Right, Bottom, HudStyle::TextCut);
							Gdiplus::SolidBrush TagBrush(Gdiplus::Color(ScaleAlpha(Command.Tint.A, 60), 6, 8, 12));
							Graphics.FillPath(&TagBrush, &Tag);
							Gdiplus::SolidBrush BarBrush(Tint);
							Graphics.FillRectangle(&BarBrush, Left, Top + HudStyle::TextCut, 2.0f, (Bottom - Top) - HudStyle::TextCut);
						}
					}

					// One soft drop shadow instead of the old 8-pass outline.
					Gdiplus::SolidBrush ShadowBrush(Gdiplus::Color(ScaleAlpha(Command.Tint.A, 70), 0, 0, 0));
					Graphics.DrawString(
						WideText.c_str(), -1, TextFont,
						Gdiplus::PointF(Origin.X + 1.0f, Origin.Y + 1.0f),
						TextFormat, &ShadowBrush);

					Gdiplus::SolidBrush Brush(Tint);
					Graphics.DrawString(WideText.c_str(), -1, TextFont, Origin, TextFormat, &Brush);
					break;
				}
				}
			}
		}

		POINT SourcePoint{ 0, 0 };
		SIZE SurfaceSize{ Width, Height };
		BLENDFUNCTION Blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
		const bool LayerUpdated = UpdateLayeredWindow(
			Window, ScreenDc, &TopLeft, &SurfaceSize,
			RenderSurface.MemoryDc, &SourcePoint, 0, &Blend, ULW_ALPHA) != FALSE;

		SetWindowPos(
			Window, HWND_TOPMOST,
			TopLeft.x, TopLeft.y, Width, Height,
			SWP_NOACTIVATE | (LayerUpdated ? SWP_SHOWWINDOW : 0));

		ReleaseDC(nullptr, ScreenDc);
		return DrawSucceeded && LayerUpdated;
	}
}