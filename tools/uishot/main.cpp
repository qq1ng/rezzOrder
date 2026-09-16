// Renders the addon's real UI code offscreen and saves a PNG per scenario, so layout work can be checked
// without launching Guild Wars 2. Same ImGui version, same overlay code, same icons; only the game, Nexus
// and the live event feed are replaced (tools/uishot/Host.cpp).
//
//   build/release/rezz_uishot.exe [--out <dir>] [--only <substring>] [--font <ttf>]
//
// Writes <dir>/<scenario>.png, <dir>/report.txt (a diffable dump of what each layout drew) and
// <dir>/README.md. Exit code 1 if a scenario drew nothing.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <Windows.h>
#include <d3d11.h>
#include <wincodec.h>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_dx11.h"

#include "Demo.h"
#include "Host.h"
#include "Notify.h"
#include "OrderUi.h"
#include "Scenarios.h"
#include "Settings.h"

namespace
{
	constexpr int   kWidth    = 1920;
	constexpr int   kHeight   = 1080;
	constexpr float kFontSize = 16.0f;
	constexpr float kOriginX  = 40.0f;
	constexpr float kOriginY  = 40.0f;

	ID3D11Device*           s_Device  = nullptr;
	ID3D11DeviceContext*    s_Context = nullptr;
	ID3D11Texture2D*        s_Target  = nullptr;
	ID3D11RenderTargetView* s_Rtv     = nullptr;
	ID3D11Texture2D*        s_Staging = nullptr;

	bool CreateDevice()
	{
		UINT flags = 0;
		D3D_FEATURE_LEVEL level{};
		const D3D_FEATURE_LEVEL wanted[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
		// Hardware first; WARP keeps this working on a machine without a usable GPU (or over remote desktop).
		HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, wanted, 2,
			D3D11_SDK_VERSION, &s_Device, &level, &s_Context);
		if (FAILED(hr))
		{
			hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, wanted, 2, D3D11_SDK_VERSION,
				&s_Device, &level, &s_Context);
		}
		if (FAILED(hr)) { std::printf("D3D11CreateDevice failed (0x%08lx)\n", hr); return false; }

		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = kWidth;
		desc.Height = kHeight;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		if (FAILED(s_Device->CreateTexture2D(&desc, nullptr, &s_Target))) { return false; }
		if (FAILED(s_Device->CreateRenderTargetView(s_Target, nullptr, &s_Rtv))) { return false; }

		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		return SUCCEEDED(s_Device->CreateTexture2D(&desc, nullptr, &s_Staging));
	}

	// Saves a region of the render target as a PNG.
	bool SavePng(const std::wstring& aPath, int aX, int aY, int aWidth, int aHeight)
	{
		aX = aX < 0 ? 0 : aX;
		aY = aY < 0 ? 0 : aY;
		if (aX + aWidth > kWidth) { aWidth = kWidth - aX; }
		if (aY + aHeight > kHeight) { aHeight = kHeight - aY; }
		if (aWidth <= 0 || aHeight <= 0) { return false; }

		s_Context->CopyResource(s_Staging, s_Target);
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(s_Context->Map(s_Staging, 0, D3D11_MAP_READ, 0, &mapped))) { return false; }

		std::vector<unsigned char> pixels(static_cast<size_t>(aWidth) * aHeight * 4);
		for (int y = 0; y < aHeight; y++)
		{
			const unsigned char* source = static_cast<const unsigned char*>(mapped.pData) +
				static_cast<size_t>(y + aY) * mapped.RowPitch + static_cast<size_t>(aX) * 4;
			std::memcpy(pixels.data() + static_cast<size_t>(y) * aWidth * 4, source, static_cast<size_t>(aWidth) * 4);
		}
		// ImGui's blending leaves the destination alpha partly transparent; the screenshot is what the player
		// sees on screen, so it is saved opaque.
		for (size_t i = 3; i < pixels.size(); i += 4) { pixels[i] = 255; }
		s_Context->Unmap(s_Staging, 0);

		IWICImagingFactory* factory = nullptr;
		if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
		{
			return false;
		}
		bool ok = false;
		IWICBitmap* bitmap = nullptr;
		IWICStream* stream = nullptr;
		IWICBitmapEncoder* encoder = nullptr;
		IWICBitmapFrameEncode* frame = nullptr;
		IWICFormatConverter* converter = nullptr;
		if (SUCCEEDED(factory->CreateBitmapFromMemory(aWidth, aHeight, GUID_WICPixelFormat32bppRGBA,
				static_cast<UINT>(aWidth) * 4, static_cast<UINT>(pixels.size()), pixels.data(), &bitmap)) &&
			SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
			SUCCEEDED(converter->Initialize(bitmap, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr,
				0.0, WICBitmapPaletteTypeCustom)) &&
			SUCCEEDED(factory->CreateStream(&stream)) &&
			SUCCEEDED(stream->InitializeFromFilename(aPath.c_str(), GENERIC_WRITE)) &&
			SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
			SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
			SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) &&
			SUCCEEDED(frame->Initialize(nullptr)) &&
			SUCCEEDED(frame->SetSize(aWidth, aHeight)))
		{
			WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
			ok = SUCCEEDED(frame->SetPixelFormat(&format)) && SUCCEEDED(frame->WriteSource(converter, nullptr)) &&
				SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
		}
		if (converter) { converter->Release(); }
		if (frame) { frame->Release(); }
		if (encoder) { encoder->Release(); }
		if (stream) { stream->Release(); }
		if (bitmap) { bitmap->Release(); }
		factory->Release();
		return ok;
	}

	// A dark backdrop so a transparent overlay is readable in the screenshot, roughly as bright as a WvW
	// night scene.
	void Backdrop()
	{
		ImDrawList* draw = ImGui::GetBackgroundDrawList();
		draw->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(kWidth, kHeight), IM_COL32(28, 32, 38, 255),
			IM_COL32(34, 38, 44, 255), IM_COL32(20, 23, 27, 255), IM_COL32(24, 28, 33, 255));
	}

	// What must be true of every render, whatever the layout. These catch the mistakes a screenshot makes
	// easy to miss: a row silently dropped, our own place not marked, a window wider than the screen.
	int Validate(const Shots::Scenario& aScenario, const Rezz::SessionView& aView, const OrderUi::FrameInfo& aInfo)
	{
		int failures = 0;
		auto fail = [&](const std::string& aWhat)
		{
			std::printf("FAILED %s: %s\n", aScenario.Name.c_str(), aWhat.c_str());
			failures++;
		};
		auto mentions = [](const std::string& aText, const std::string& aNeedle)
		{
			return !aNeedle.empty() && aText.find(aNeedle) != std::string::npos;
		};

		size_t order = aView.Order.size();
		size_t max = Settings::Current.OverlayMaxRows > 0 ? static_cast<size_t>(Settings::Current.OverlayMaxRows) : order;
		// "next up" shows the player who is up on its card and counts "max displayed" for the rows under it.
		if (aInfo.Layout == "nextup" && Settings::Current.OverlayMaxRows > 0) { max += 1; }
		size_t expected = std::min(order, max);
		if (aInfo.Rows.size() != expected)
		{
			fail("drew " + std::to_string(aInfo.Rows.size()) + " rows, expected " + std::to_string(expected));
		}
		if (order > 0 && aInfo.Banner.empty()) { fail("no banner line"); }

		// The banner says who has to act: us, or the player who is up, or that nobody can.
		int up = aView.Turn.UpIndex;
		bool selfIsUp = up >= 0 && aView.Turn.Rows[up].Account == aView.SelfAccount;
		if (selfIsUp && !mentions(aInfo.Banner, "YOUR TURN")) { fail("we are up but the banner is '" + aInfo.Banner + "'"); }
		if (up < 0 && order > 0 && !mentions(aInfo.Banner, "Nobody ready"))
		{
			fail("nobody can revive but the banner is '" + aInfo.Banner + "'");
		}

		// Each drawn row names its player and its state.
		for (const std::string& row : aInfo.Rows)
		{
			if (row.find_last_of(' ') == std::string::npos || row.size() < 5) { fail("row too short: '" + row + "'"); }
		}
		// A width the player sets is the narrowest the window may be; content that needs more room widens it.
		if (Settings::Current.OverlayWidth > 0 && aInfo.Width < Settings::Current.OverlayWidth - 1.0f)
		{
			char message[128];
			std::snprintf(message, sizeof(message), "width %.0f, narrower than the %.0f asked for", aInfo.Width,
				Settings::Current.OverlayWidth);
			fail(message);
		}
		if (aInfo.Width > kWidth * 0.75f || aInfo.Height > kHeight * 0.9f) { fail("the window is bigger than the shot"); }

		return failures;
	}

	std::wstring Widen(const std::string& aText)
	{
		int size = MultiByteToWideChar(CP_UTF8, 0, aText.c_str(), -1, nullptr, 0);
		std::wstring wide(size > 0 ? size - 1 : 0, L'\0');
		if (size > 0) { MultiByteToWideChar(CP_UTF8, 0, aText.c_str(), -1, wide.data(), size); }
		return wide;
	}

	// aFrame moves the clock on: a flash is a pulse over time, and a frozen clock would catch it at zero.
	void Frame(const Shots::Scenario& aScenario, int aFrame)
	{
		ImGui_ImplDX11_NewFrame();
		ImGui::NewFrame();
		Backdrop();

		OrderUi::Context context;
		context.NowMs = static_cast<unsigned>(Shots::kNowMs) + static_cast<unsigned>(aFrame) * 100;
		context.IsGameplay = true;
		context.InWvw = true;
		OrderUi::Render(context);

		ImGui::Render();
		const float clear[4] = { 0.10f, 0.11f, 0.13f, 1.0f };
		s_Context->OMSetRenderTargets(1, &s_Rtv, nullptr);
		s_Context->ClearRenderTargetView(s_Rtv, clear);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		(void)aScenario;
	}
}

int main(int argc, char** argv)
{
	std::string outDir = "docs/shots";
	std::string only;
	std::string font = "C:/Windows/Fonts/segoeui.ttf";
	for (int i = 1; i < argc; i++)
	{
		std::string arg = argv[i];
		if (arg == "--out" && i + 1 < argc) { outDir = argv[++i]; }
		else if (arg == "--only" && i + 1 < argc) { only = argv[++i]; }
		else if (arg == "--font" && i + 1 < argc) { font = argv[++i]; }
		else { std::printf("usage: rezz_uishot [--out <dir>] [--only <substring>] [--font <ttf>]\n"); return 2; }
	}

	CreateDirectoryA(outDir.c_str(), nullptr);
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (!CreateDevice()) { return 2; }
	Host::SetDevice(s_Device);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(static_cast<float>(kWidth), static_cast<float>(kHeight));
	io.DeltaTime = 1.0f / 60.0f;
	io.IniFilename = nullptr;
	io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX); // nothing hovered: no tooltips in the shots
	for (int i = 0; i < ImGuiKey_COUNT; i++) { io.KeyMap[i] = i; }
	ImGui::StyleColorsDark();
	Host::BuildFonts(font.c_str(), kFontSize);
	ImGui_ImplDX11_Init(s_Device, s_Context);

	std::string report;
	std::string index = "# Turn window renders\n\nMade by `build/release/rezz_uishot.exe` from the addon's own"
		" UI code: same ImGui, same layouts, same icons as in the game. `report.txt` says what each one drew.\n\n"
		"`python tools/sheet.py` puts them together:\n\n"
		"![every layout](all-layouts.png)\n\n![states and options](all-states.png)\n\n## Every render\n\n";
	int failures = 0;
	int written = 0;

	for (const Shots::Scenario& scenario : Shots::All())
	{
		if (!only.empty() && scenario.Name.find(only) == std::string::npos) { continue; }

		Settings::Current = Settings::Values{};
		Settings::Current.OverlayX = kOriginX;
		Settings::Current.OverlayY = kOriginY;
		Settings::Current.OverlayBgAlpha = 0.55f;
		Settings::Current.MatchArcDps = false;
		if (scenario.Tweak) { scenario.Tweak(Settings::Current); }

		// Demo mode is per-scenario state: only the scenarios that ask for it run with it.
		Rezz::Demo::Stop();
		Notify::Reset(); // each scenario is its own moment: no signal carries over
		Rezz::SessionView view = scenario.Build();
		Settings::Current.Order = view.Order;
		Host::SetView(view);
		bool editor = scenario.Shows == Shots::Scenario::Window::Editor;
		OrderUi::ShowEditor = editor;
		Settings::Current.OverlayVisible = !editor;

		// The window settles its size on the first frame; the third is what gets saved.
		for (int i = 0; i < 3; i++) { Frame(scenario, i); }

		static OrderUi::FrameInfo wholeScreen;
		wholeScreen = OrderUi::FrameInfo{};
		wholeScreen.Drawn = true;
		wholeScreen.Layout = "screen";
		wholeScreen.Width = static_cast<float>(kWidth) - 48.0f;   // the crop adds the margin back
		wholeScreen.Height = static_cast<float>(kHeight) - 48.0f;
		wholeScreen.X = 24.0f;
		wholeScreen.Y = 24.0f;

		const OrderUi::FrameInfo& info = scenario.Shows == Shots::Scenario::Window::Screen ? wholeScreen
			: scenario.Shows == Shots::Scenario::Window::Editor ? OrderUi::LastEditorFrame
			: scenario.Shows == Shots::Scenario::Window::Share ? OrderUi::LastShareFrame
			: scenario.Shows == Shots::Scenario::Window::Request ? OrderUi::LastRequestFrame : OrderUi::LastFrame;
		if (!info.Drawn)
		{
			std::printf("FAILED %s: the window drew nothing\n", scenario.Name.c_str());
			failures++;
			continue;
		}
		const int margin = 24;
		int x = static_cast<int>(info.X) - margin;
		int y = static_cast<int>(info.Y) - margin;
		int width = static_cast<int>(info.Width) + margin * 2;
		int height = static_cast<int>(info.Height) + margin * 2;
		if (scenario.Shows == Shots::Scenario::Window::Overlay) { failures += Validate(scenario, view, info); }

		std::string file = outDir + "/" + scenario.Name + ".png";
		if (!SavePng(Widen(file), x, y, width, height))
		{
			std::printf("FAILED %s: could not write %s\n", scenario.Name.c_str(), file.c_str());
			failures++;
			continue;
		}
		written++;

		char header[256];
		std::snprintf(header, sizeof(header), "== %s [%s] %.0fx%.0f\n", scenario.Name.c_str(),
			info.Layout.empty() ? "editor" : info.Layout.c_str(), info.Width, info.Height);
		report += header;
		if (!info.Banner.empty()) { report += "   banner: " + info.Banner + "\n"; }
		for (const std::string& row : info.Rows) { report += "   " + row + "\n"; }

		index += "### " + scenario.Name + "\n" + scenario.Note + "\n\n![" + scenario.Name + "](" +
			scenario.Name + ".png)\n\n";
		std::printf("ok   %s\n", scenario.Name.c_str());
	}

	std::string reportPath = outDir + "/report.txt";
	if (FILE* out = nullptr; fopen_s(&out, reportPath.c_str(), "wb") == 0 && out)
	{
		std::fwrite(report.data(), 1, report.size(), out);
		std::fclose(out);
	}
	std::string indexPath = outDir + "/README.md";
	if (FILE* out = nullptr; fopen_s(&out, indexPath.c_str(), "wb") == 0 && out)
	{
		std::fwrite(index.data(), 1, index.size(), out);
		std::fclose(out);
	}

	ImGui_ImplDX11_Shutdown();
	Host::ReleaseIcons();
	ImGui::DestroyContext();
	if (s_Staging) { s_Staging->Release(); }
	if (s_Rtv) { s_Rtv->Release(); }
	if (s_Target) { s_Target->Release(); }
	if (s_Context) { s_Context->Release(); }
	if (s_Device) { s_Device->Release(); }
	CoUninitialize();

	std::printf("%s: %d renders, %d failed\n", outDir.c_str(), written, failures);
	return failures == 0 ? 0 : 1;
}
