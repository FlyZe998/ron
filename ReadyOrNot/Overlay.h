#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>

namespace Overlay
{
	struct Point
	{
		float x = 0.0f;
		float y = 0.0f;
	};

	struct FloatColor
	{
		float x = 1.0f;
		float y = 1.0f;
		float z = 1.0f;
		float w = 1.0f;
	};

	struct Color
	{
		BYTE R = 255;
		BYTE G = 255;
		BYTE B = 255;
		BYTE A = 255;
	};

	bool Initialize(HINSTANCE Instance);
	void Shutdown();
	void Hide();
	Color FromPackedColor(uint32_t Packed);
	void BeginFrame(int Width, int Height);
	void Line(double X1, double Y1, double X2, double Y2, Color Tint, float Thickness = 1.0f);
	void Rectangle(double X1, double Y1, double X2, double Y2, Color Tint, float Thickness = 1.0f);
	void Circle(double X, double Y, double Radius, Color Tint, float Thickness = 1.0f, bool Filled = false);
	void Text(double X, double Y, Color Tint, const std::string& Value);
	bool RequestRender(HWND GameWindow);
}
