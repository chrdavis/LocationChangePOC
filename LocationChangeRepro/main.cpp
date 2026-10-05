//=================================================================================
// LocationChangeRepro
//
// Proof of concept for how HUD's indicator window floods EVENT_OBJECT_LOCATIONCHANGE.
//
// A normal top-level window (opaque, so occlusion trackers such as Chrome/Edge's
// NativeWindowOcclusionTrackerWin hook this process for EVENT_OBJECT_LOCATIONCHANGE)
// hosts a WS_EX_LAYERED child window that is repainted ~30 times a second with
// UpdateLayeredWindowIndirect(), just like HUD's IndicatorWindowUI::Paint().
//
//   Space  - toggle passing UPDATELAYEREDWINDOWINFO::psize on every frame.
//            ON  : every frame raises EVENT_OBJECT_LOCATIONCHANGE (the bug)
//            OFF : psize only passed when the size changes (the fix)
//   P      - pause/resume repainting
//   Esc    - exit
//
// The window title shows the mode and how many EVENT_OBJECT_LOCATIONCHANGE events
// this process observed for the child window (via an in-process WinEvent hook).
//=================================================================================

#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <cstdio>
#include <cstdint>

namespace
{
	constexpr wchar_t c_wzFrameClass[] = L"LocationChangeReproFrame";
	constexpr wchar_t c_wzIndicatorClass[] = L"LocationChangeReproIndicator";
	constexpr UINT_PTR c_timerPaint = 1;
	constexpr UINT c_frameIntervalMs = 31;	// ~32 FPS, same cadence seen from HUD

	HWND g_hwndFrame = nullptr;
	HWND g_hwndIndicator = nullptr;
	bool g_passSizeEveryFrame = true;
	bool g_paused = false;
	SIZE g_layeredSize{};
	uint64_t g_cFrames = 0;
	uint64_t g_cLocationChanges = 0;
	HWINEVENTHOOK g_hook = nullptr;

	//-----------------------------------------------------------------------------
	// counts EVENT_OBJECT_LOCATIONCHANGE for the indicator window
	//-----------------------------------------------------------------------------
	void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG, DWORD, DWORD)
	{
		if ((event == EVENT_OBJECT_LOCATIONCHANGE) && (hwnd == g_hwndIndicator) && (idObject == OBJID_WINDOW))
			++g_cLocationChanges;
	}

	void UpdateTitle()
	{
		wchar_t wz[256];
		swprintf_s(wz, L"LocationChange repro - psize every frame: %s%s - frames: %llu, LOCATIONCHANGE: %llu  [Space=toggle, P=pause]",
			g_passSizeEveryFrame ? L"ON (bug)" : L"OFF (fix)", g_paused ? L" (paused)" : L"", g_cFrames, g_cLocationChanges);
		SetWindowText(g_hwndFrame, wz);
	}

	//-----------------------------------------------------------------------------
	// draws a changing value into a 32bpp DIB and pushes it to the layered child
	//-----------------------------------------------------------------------------
	void PaintIndicator()
	{
		RECT rc{};
		GetClientRect(g_hwndIndicator, &rc);
		const SIZE size{rc.right - rc.left, rc.bottom - rc.top};
		if ((size.cx <= 0) || (size.cy <= 0))
			return;

		HDC hdcScreen = GetDC(nullptr);
		HDC hdcMem = CreateCompatibleDC(hdcScreen);

		BITMAPINFO bmi{};
		bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
		bmi.bmiHeader.biWidth = size.cx;
		bmi.bmiHeader.biHeight = -size.cy;	// top-down
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;
		void* pvBits = nullptr;
		HBITMAP hbm = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
		HGDIOBJ hbmOld = SelectObject(hdcMem, hbm);

		// opaque dark background (premultiplied alpha = 255)
		auto* pPixels = static_cast<uint32_t*>(pvBits);
		for (LONG i = 0; i < size.cx * size.cy; ++i)
			pPixels[i] = 0xFF202020;

		// text that changes every frame, like HUD's live counters
		wchar_t wz[128];
		swprintf_s(wz, L"CPU Usage: %llu.%llu%%\nFrame: %llu", (g_cFrames * 7) % 100, g_cFrames % 10, g_cFrames);
		SetBkMode(hdcMem, TRANSPARENT);
		SetTextColor(hdcMem, RGB(255, 255, 255));
		RECT rcText{8, 8, size.cx - 8, size.cy - 8};
		DrawText(hdcMem, wz, -1, &rcText, DT_LEFT | DT_TOP);
		// GDI clears the alpha channel of what it draws; make everything opaque again
		for (LONG i = 0; i < size.cx * size.cy; ++i)
			pPixels[i] |= 0xFF000000;

		const POINT ptSrc{};
		BLENDFUNCTION blend{};
		blend.SourceConstantAlpha = 255;
		blend.AlphaFormat = AC_SRC_ALPHA;

		UPDATELAYEREDWINDOWINFO ulwi{};
		ulwi.cbSize = sizeof(ulwi);
		ulwi.pptSrc = &ptSrc;
		ulwi.pblend = &blend;
		ulwi.dwFlags = ULW_ALPHA;
		ulwi.hdcSrc = hdcMem;
		// This is the difference: passing a size makes user32 reposition the window, which raises
		// EVENT_OBJECT_LOCATIONCHANGE even though nothing moved.
		if (g_passSizeEveryFrame || (size.cx != g_layeredSize.cx) || (size.cy != g_layeredSize.cy))
		{
			ulwi.psize = &size;
			g_layeredSize = size;
		}
		if (!UpdateLayeredWindowIndirect(g_hwndIndicator, &ulwi))
			g_layeredSize = {};

		SelectObject(hdcMem, hbmOld);
		DeleteObject(hbm);
		DeleteDC(hdcMem);
		ReleaseDC(nullptr, hdcScreen);

		++g_cFrames;
	}

	LRESULT CALLBACK IndicatorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}

	LRESULT CALLBACK FrameWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		switch (msg)
		{
			case WM_CREATE:
			{
				// a layered *child* window, like HUD's HUDIndicator inside HUDIndicatorView (Windows 8+)
				g_hwndIndicator = CreateWindowEx(WS_EX_LAYERED, c_wzIndicatorClass, L"", WS_CHILD | WS_VISIBLE,
					0, 0, 0, 0, hwnd, nullptr, reinterpret_cast<CREATESTRUCT*>(lParam)->hInstance, nullptr);
				if (g_hwndIndicator == nullptr)
					return -1;
				SetTimer(hwnd, c_timerPaint, c_frameIntervalMs, nullptr);
				return 0;
			}
			case WM_SIZE:
				MoveWindow(g_hwndIndicator, 8, 8, LOWORD(lParam) - 16, HIWORD(lParam) - 16, false);
				return 0;
			case WM_TIMER:
				if ((wParam == c_timerPaint) && !g_paused)
				{
					PaintIndicator();
					UpdateTitle();
				}
				return 0;
			case WM_KEYDOWN:
				if (wParam == VK_SPACE)
				{
					g_passSizeEveryFrame = !g_passSizeEveryFrame;
					g_cLocationChanges = 0;
					g_cFrames = 0;
				}
				else if (wParam == 'P')
					g_paused = !g_paused;
				else if (wParam == VK_ESCAPE)
					DestroyWindow(hwnd);
				UpdateTitle();
				return 0;
			case WM_ERASEBKGND:
			{
				RECT rc;
				GetClientRect(hwnd, &rc);
				FillRect(reinterpret_cast<HDC>(wParam), &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
				return 1;
			}
			case WM_DESTROY:
				KillTimer(hwnd, c_timerPaint);
				PostQuitMessage(0);
				return 0;
		}
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	WNDCLASSEX wc{};
	wc.cbSize = sizeof(wc);
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

	wc.lpfnWndProc = FrameWndProc;
	wc.lpszClassName = c_wzFrameClass;
	if (!RegisterClassEx(&wc))
		return 1;

	wc.lpfnWndProc = IndicatorWndProc;
	wc.lpszClassName = c_wzIndicatorClass;
	if (!RegisterClassEx(&wc))
		return 1;

	// small, topmost, opaque tool window like HUD's indicator view
	g_hwndFrame = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, c_wzFrameClass, L"", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, 520, 160, nullptr, nullptr, hInstance, nullptr);
	if (g_hwndFrame == nullptr)
		return 1;

	// count our own location change events (out of context so the callback runs from our message loop)
	g_hook = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, WinEventProc,
		GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);

	ShowWindow(g_hwndFrame, nCmdShow);
	UpdateTitle();

	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0) > 0)
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	if (g_hook != nullptr)
		UnhookWinEvent(g_hook);
	return static_cast<int>(msg.wParam);
}
