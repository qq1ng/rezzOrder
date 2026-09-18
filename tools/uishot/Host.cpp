// The pieces of the addon that only exist inside the game, replaced so the UI code can run in a plain
// process: icons become D3D11 textures decoded here, fonts come from a TTF on disk, and the live session is
// whatever the current scenario built.

#include "Host.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#include <Windows.h>
#include <d3d11.h>
#include <wincodec.h>

#include "imgui/imgui.h"

#include "ArcStyle.h"
#include "Banner.h"
#include "Fonts.h"
#include "Icons.h"
#include "Live.h"
#include "Settings.h"

namespace
{
	ID3D11Device*        s_Device = nullptr;
	Rezz::SessionView    s_View;

	struct IconBlob
	{
		uint32_t             Profession;
		uint32_t             Specialization;
		const unsigned char* Data;
		size_t               Size;
	};

#include "IconData.inc"

	std::unordered_map<uint32_t, ID3D11ShaderResourceView*> s_Icons;

	// Decodes a PNG in memory into a texture, the way Nexus does it in the game.
	ID3D11ShaderResourceView* CreateTexture(const unsigned char* aData, size_t aSize)
	{
		IWICImagingFactory* factory = nullptr;
		if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
		{
			return nullptr;
		}

		ID3D11ShaderResourceView* view = nullptr;
		IWICStream* stream = nullptr;
		IWICBitmapDecoder* decoder = nullptr;
		IWICBitmapFrameDecode* frame = nullptr;
		IWICFormatConverter* converter = nullptr;
		UINT width = 0, height = 0;

		if (SUCCEEDED(factory->CreateStream(&stream)) &&
			SUCCEEDED(stream->InitializeFromMemory(const_cast<BYTE*>(aData), static_cast<DWORD>(aSize))) &&
			SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) &&
			SUCCEEDED(decoder->GetFrame(0, &frame)) &&
			SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
			SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
				WICBitmapPaletteTypeCustom)) &&
			SUCCEEDED(frame->GetSize(&width, &height)))
		{
			std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
			if (SUCCEEDED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data())))
			{
				D3D11_TEXTURE2D_DESC desc{};
				desc.Width = width;
				desc.Height = height;
				desc.MipLevels = 1;
				desc.ArraySize = 1;
				desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				desc.SampleDesc.Count = 1;
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

				D3D11_SUBRESOURCE_DATA init{};
				init.pSysMem = pixels.data();
				init.SysMemPitch = width * 4;

				ID3D11Texture2D* texture = nullptr;
				if (SUCCEEDED(s_Device->CreateTexture2D(&desc, &init, &texture)))
				{
					D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
					srv.Format = desc.Format;
					srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
					srv.Texture2D.MipLevels = 1;
					s_Device->CreateShaderResourceView(texture, &srv, &view);
					texture->Release();
				}
			}
		}

		if (converter) { converter->Release(); }
		if (frame) { frame->Release(); }
		if (decoder) { decoder->Release(); }
		if (stream) { stream->Release(); }
		factory->Release();
		return view;
	}
}

namespace Host
{
	void SetDevice(ID3D11Device* aDevice) { s_Device = aDevice; }
	void SetView(const Rezz::SessionView& aView) { s_View = aView; }

	void ReleaseIcons()
	{
		for (auto& [key, view] : s_Icons) { if (view) { view->Release(); } }
		s_Icons.clear();
	}
}

// ---------------------------------------------------------------------------------------------- icons

namespace Icons
{
	void Init(AddonAPI_t*) {}
	void Shutdown() { Host::ReleaseIcons(); }

	void* Get(uint32_t aProfession, uint32_t aElite)
	{
		if (s_Device == nullptr || aProfession == 0) { return nullptr; }
		const IconBlob* found = nullptr;
		const IconBlob* core = nullptr;
		for (const IconBlob& blob : kIconBlobs)
		{
			if (blob.Specialization != 0 && blob.Specialization == aElite && blob.Profession == aProfession) { found = &blob; }
			if (blob.Specialization == 0 && blob.Profession == aProfession) { core = &blob; }
		}
		if (found == nullptr) { found = core; }
		if (found == nullptr) { return nullptr; }

		uint32_t key = found->Specialization ? 1000 + found->Specialization : found->Profession;
		auto it = s_Icons.find(key);
		if (it != s_Icons.end()) { return it->second; }
		ID3D11ShaderResourceView* view = CreateTexture(found->Data, found->Size);
		s_Icons[key] = view;
		return view;
	}
}

// ---------------------------------------------------------------------------------------------- fonts

namespace
{
	// One font per size the overlay asks for. In the game Nexus builds these; here they are built up front
	// because ImGui can only add fonts between frames.
	std::unordered_map<int, ImFont*> s_Fonts;
	float                            s_BaseSize = 16.0f;
	int                              s_Pushed   = 0;

	ImFont* FontAt(float aSize)
	{
		int key = static_cast<int>(std::lround(aSize));
		auto it = s_Fonts.find(key);
		return it == s_Fonts.end() ? nullptr : it->second;
	}
}

namespace Host
{
	void BuildFonts(const char* aTtfFile, float aBaseSize)
	{
		s_BaseSize = aBaseSize;
		ImGuiIO& io = ImGui::GetIO();
		// Every size the scenarios can ask for: the overlay font at each scale, and the bigger fonts the
		// layouts push (1.25x, 1.5x banner, 1.6x focus card).
		std::vector<float> scales;
		for (float scale : { 0.7f, 1.0f, 1.25f, 1.6f, 2.0f, 2.5f })
		{
			for (float factor : { 1.0f, 1.25f, 1.5f, 1.6f, 2.4f }) { scales.push_back(scale * factor); }
		}
		for (float scale : scales)
		{
			int size = static_cast<int>(std::lround(aBaseSize * scale));
			if (size < 6 || s_Fonts.count(size)) { continue; }
			ImFont* font = nullptr;
			if (aTtfFile && *aTtfFile) { font = io.Fonts->AddFontFromFileTTF(aTtfFile, static_cast<float>(size)); }
			if (font == nullptr)
			{
				ImFontConfig config;
				config.SizePixels = static_cast<float>(size);
				font = io.Fonts->AddFontDefault(&config);
			}
			s_Fonts[size] = font;
		}
	}
}

namespace Fonts
{
	void Init(AddonAPI_t*, NexusLinkData_t*) {}
	void Shutdown() {}
	void Update(float, unsigned) {}

	void Push(float aScale, bool aBig)
	{
		float wanted = s_BaseSize * aScale * (aBig ? 1.5f : 1.0f);
		ImFont* font = FontAt(wanted);
		if (font == nullptr) { font = ImGui::GetFont(); s_Pushed = 0; }
		else { ImGui::PushFont(font); s_Pushed = 1; }
		ImGui::SetWindowFontScale(wanted / font->FontSize);
	}

	void Pop()
	{
		ImGui::SetWindowFontScale(1.0f);
		if (s_Pushed) { ImGui::PopFont(); }
		s_Pushed = 0;
	}

	float BannerBasePixels() { return s_BaseSize; }

	// As in the game: one font at the largest size a banner uses, drawn smaller for everything else.
	ImFont* BannerFont()
	{
		const Settings::Values& s = Settings::Current;
		float biggest = std::max(s.BannerInfoSize, s.BannerAlertSize * Banner::kCountdownNumberScale);
		ImFont* font = FontAt(s_BaseSize * biggest);
		if (font != nullptr) { return font; }
		ImFont* largest = ImGui::GetFont();
		for (const auto& [size, built] : s_Fonts) { if (built && built->FontSize > largest->FontSize) { largest = built; } }
		return largest;
	}
}

// ------------------------------------------------------------------------------------------- arc style

namespace ArcStyle
{
	namespace { Values s_Values; }

	void Update(unsigned) {}
	const Values& Get() { return s_Values; }
	const char* SourcePath() { return ""; }
	void Push() {}
	void Pop() {}
}

// ------------------------------------------------------------------------------------------ live session

namespace Live
{
	std::vector<Rezz::FightStat> s_Fights;
	void OnCombatSquad(const ArcDps::EvCombatData*) {}
	void OnAgentUpdate(const ArcDps::EvAgentUpdate*) {}
	void OnSquadUpdate(const UserInfo*, uint64_t) {}
	void OnChatMessage(const SquadMessageInfo*) {}
	void Tick() {}

	void SetOrder(const std::vector<std::string>& aAccounts) { s_View.Order = aAccounts; }
	void SetPrecast(const std::vector<std::string>& aAccounts) { s_View.Precast = aAccounts; }
	void SetBench(int aSubgroup) { s_View.Bench = static_cast<uint16_t>(aSubgroup); }
	std::vector<Rezz::FightStat> GetFights() { return s_Fights; }
	void SetShareRules(bool, bool) {}
	void SetAnswerRule(int) {}
	void SetSubstitutes(bool) {}
	void AcceptShare() { s_View.HasShare = false; }
	void DismissShare() { s_View.HasShare = false; }
	void ClearRequest(const std::string& aAccount)
	{
		if (aAccount.empty()) { s_View.Requests.clear(); return; }
		std::erase_if(s_View.Requests, [&](const Rezz::JoinRequest& aRequest) { return aRequest.Account == aAccount; });
	}

	Rezz::SessionView GetView() { return s_View; }
	std::vector<Rezz::Notice> TakeNotices() { return {}; }
}

namespace Host
{
	void SetFights(std::vector<Rezz::FightStat> aFights) { Live::s_Fights = std::move(aFights); }
}
