#include "Fonts.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <Windows.h>

#include "imgui/imgui.h"
#include "nexus/Nexus.h"

#include "ArcStyle.h"
#include "Banner.h"
#include "Settings.h"

namespace Fonts
{
	namespace
	{
		constexpr float    kBigRatio      = 1.5f;
		constexpr unsigned kResizeAfterMs = 400; // wait for the size slider to settle before rebuilding

		AddonAPI_t*      s_Api    = nullptr;
		NexusLinkData_t* s_Link   = nullptr;
		ImFont*          s_Normal = nullptr;
		ImFont*          s_Big    = nullptr;
		ImFont*          s_Banner = nullptr;

		// Nexus keeps fonts by identifier and has no "replace"; a new identifier is used per source change.
		int         s_Generation = 0;
		std::string s_NormalId;
		std::string s_BigId;
		std::string s_BannerId;
		float       s_BannerRequested    = 0.0f;
		float       s_BannerPending      = 0.0f;
		unsigned    s_BannerPendingSince = 0;

		Settings::FontSource s_Source = Settings::FontSource::Nexus;
		std::string          s_SourceFile;
		bool                 s_Requested = false;
		bool                 s_Failed    = false;
		float                s_RequestedSize  = 0.0f;
		float                s_PendingSize    = 0.0f;
		unsigned             s_PendingSinceMs = 0;
		int                  s_Pushed = 0;    // 1: Push() pushed a font that Pop() has to pop
		std::vector<char>    s_FontData;      // TTF bytes (Nexus copies whatever it is given)

		void OnNormal(const char*, void* aFont) { s_Normal = static_cast<ImFont*>(aFont); }
		void OnBig(const char*, void* aFont)    { s_Big = static_cast<ImFont*>(aFont); }
		void OnBanner(const char*, void* aFont) { s_Banner = static_cast<ImFont*>(aFont); }

		ImFont* NexusFont() { return s_Link ? static_cast<ImFont*>(s_Link->Font) : nullptr; }

		// Text size the overlay is built at: arcdps' own font size when matching it, else Nexus' size.
		float BaseSize()
		{
			const ArcStyle::Values& arc = ArcStyle::Get();
			if (Settings::Current.MatchArcDps && arc.Ok && arc.FontSize >= 6.0f) { return arc.FontSize; }
			ImFont* base = NexusFont();
			return base ? base->FontSize : 0.0f;
		}

		// The largest text any banner draws: the Illusion countdown's figures, usually.
		float BannerFontPixels()
		{
			float base = BaseSize();
			if (base <= 0.0f) { base = ImGui::GetFontSize(); }
			const Settings::Values& s = Settings::Current;
			float largest = std::max(std::clamp(s.BannerInfoSize, 0.5f, 5.0f),
				std::clamp(s.BannerAlertSize, 0.5f, 5.0f) * Banner::kCountdownNumberScale);
			return std::max(6.0f, std::round(base * largest));
		}

		// ArcDPS renders with the TTF next to the game executable if there is one, else with ImGui's built-in
		// font. Users who picked a font for arcdps get the same one here.
		std::filesystem::path ArcDpsFontFile()
		{
			wchar_t module[MAX_PATH] = {};
			if (GetModuleFileNameW(nullptr, module, MAX_PATH) == 0) { return {}; }
			std::filesystem::path gameDir = std::filesystem::path(module).parent_path();
			for (const std::filesystem::path& candidate : {
				gameDir / "arcdps_font.ttf",
				gameDir / "addons" / "arcdps" / "arcdps_font.ttf" })
			{
				std::error_code ec;
				if (std::filesystem::exists(candidate, ec)) { return candidate; }
			}
			return {};
		}

		// ImGui's built-in font (ProggyClean) decompressed in a temporary atlas: what arcdps shows by default.
		bool LoadBuiltInFont()
		{
			ImFontAtlas atlas;
			if (atlas.AddFontDefault() == nullptr || atlas.ConfigData.Size == 0) { return false; }
			const ImFontConfig& config = atlas.ConfigData[0];
			if (config.FontData == nullptr || config.FontDataSize <= 0) { return false; }
			const char* data = static_cast<const char*>(config.FontData);
			s_FontData.assign(data, data + config.FontDataSize);
			return true;
		}

		bool LoadNexusFont()
		{
			ImFont* base = NexusFont();
			const ImFontConfig* config = base ? base->ConfigData : nullptr;
			if (config == nullptr || config->FontData == nullptr || config->FontDataSize <= 0) { return false; }
			const char* data = static_cast<const char*>(config->FontData);
			s_FontData.assign(data, data + config->FontDataSize);
			return true;
		}

		bool LoadFile(const std::filesystem::path& aPath)
		{
			if (aPath.empty()) { return false; }
			FILE* file = nullptr;
			if (_wfopen_s(&file, aPath.c_str(), L"rb") != 0 || file == nullptr) { return false; }
			std::fseek(file, 0, SEEK_END);
			long size = std::ftell(file);
			std::fseek(file, 0, SEEK_SET);
			bool ok = size > 0;
			if (ok)
			{
				s_FontData.resize(static_cast<size_t>(size));
				ok = std::fread(s_FontData.data(), 1, s_FontData.size(), file) == s_FontData.size();
			}
			std::fclose(file);
			return ok;
		}

		// Fills s_FontData for the chosen source, falling back to Nexus' font.
		bool LoadSource(Settings::FontSource aSource, const std::string& aFile)
		{
			switch (aSource)
			{
				case Settings::FontSource::ArcDps:
				{
					std::filesystem::path arcFont = ArcDpsFontFile();
					if (!arcFont.empty() && LoadFile(arcFont)) { return true; }
					if (LoadBuiltInFont()) { return true; }
					break;
				}
				case Settings::FontSource::File:
					if (LoadFile(std::filesystem::path(aFile))) { return true; }
					break;
				default:
					break;
			}
			return LoadNexusFont();
		}

		void Release()
		{
			if (!s_Requested) { return; }
			s_Api->Fonts_Release(s_NormalId.c_str(), OnNormal);
			s_Api->Fonts_Release(s_BigId.c_str(), OnBig);
			s_Api->Fonts_Release(s_BannerId.c_str(), OnBanner);
			s_Requested = false;
			s_Normal = s_Big = s_Banner = nullptr;
		}

		void Request(float aSize)
		{
			s_Generation++;
			s_NormalId = "REZZORDER_OVERLAY_" + std::to_string(s_Generation);
			s_BigId = "REZZORDER_OVERLAY_BIG_" + std::to_string(s_Generation);
			s_BannerId = "REZZORDER_BANNER_" + std::to_string(s_Generation);
			s_Normal = s_Big = s_Banner = nullptr;
			s_Api->Fonts_AddFromMemory(s_NormalId.c_str(), aSize, s_FontData.data(), s_FontData.size(), OnNormal, nullptr);
			s_Api->Fonts_AddFromMemory(s_BigId.c_str(), std::round(aSize * kBigRatio), s_FontData.data(), s_FontData.size(), OnBig, nullptr);
			s_BannerRequested = s_BannerPending = BannerFontPixels();
			s_Api->Fonts_AddFromMemory(s_BannerId.c_str(), s_BannerRequested, s_FontData.data(), s_FontData.size(), OnBanner, nullptr);
			s_Requested = true;
			s_RequestedSize = s_PendingSize = aSize;
		}
	}

	void Init(AddonAPI_t* aApi, NexusLinkData_t* aNexusLink)
	{
		s_Api = aApi;
		s_Link = aNexusLink;
	}

	void Shutdown()
	{
		if (s_Api) { Release(); }
		s_FontData.clear();
		s_Api = nullptr;
		s_Link = nullptr;
	}

	void Update(float aScale, unsigned aNowMs)
	{
		ImFont* base = NexusFont();
		if (s_Api == nullptr || base == nullptr) { return; }
		float size = std::round(BaseSize() * aScale);
		const Settings::Values& settings = Settings::Current;
		if (size < 6.0f) { return; }
		bool sourceChanged = settings.Font != s_Source || settings.FontFile != s_SourceFile;

		if (!s_Requested || sourceChanged)
		{
			if (sourceChanged) { s_Failed = false; }
			if (s_Failed) { return; }
			s_Source = settings.Font;
			s_SourceFile = settings.FontFile;
			if (!LoadSource(s_Source, s_SourceFile))
			{
				s_Failed = true; // nothing to build from: keep scaling Nexus' font
				return;
			}
			Release();
			Request(size);
			return;
		}

		// The banner font follows its own size setting. A change waits until the slider settles, like the
		// overlay's: every resize rebuilds Nexus' font atlas for all addons.
		float bannerSize = BannerFontPixels();
		if (bannerSize == s_BannerRequested) { s_BannerPending = bannerSize; }
		else if (bannerSize != s_BannerPending) { s_BannerPending = bannerSize; s_BannerPendingSince = aNowMs; }
		else if (aNowMs - s_BannerPendingSince >= kResizeAfterMs)
		{
			s_Api->Fonts_Resize(s_BannerId.c_str(), bannerSize);
			s_BannerRequested = bannerSize;
		}

		if (size == s_RequestedSize) { s_PendingSize = size; return; }
		if (size != s_PendingSize)
		{
			s_PendingSize = size;
			s_PendingSinceMs = aNowMs;
			return;
		}
		if (aNowMs - s_PendingSinceMs >= kResizeAfterMs)
		{
			s_Api->Fonts_Resize(s_NormalId.c_str(), size);
			s_Api->Fonts_Resize(s_BigId.c_str(), std::round(size * kBigRatio));
			s_RequestedSize = size;
		}
	}

	void Push(float aScale, bool aBig)
	{
		float baseSize = BaseSize();
		if (baseSize <= 0.0f) { baseSize = ImGui::GetFontSize(); }
		float wanted = baseSize * aScale * (aBig ? kBigRatio : 1.0f);

		ImFont* font = aBig ? s_Big : s_Normal;
		if (font != nullptr && font->FontSize > 0.0f)
		{
			ImGui::PushFont(font);
			s_Pushed = 1;
		}
		else
		{
			font = ImGui::GetFont();
			s_Pushed = 0;
		}
		// Exact while the built size matches; scaled only while the size slider is still moving.
		ImGui::SetWindowFontScale(wanted / font->FontSize);
	}

	void Pop()
	{
		ImGui::SetWindowFontScale(1.0f);
		if (s_Pushed) { ImGui::PopFont(); }
		s_Pushed = 0;
	}

	float BannerBasePixels()
	{
		float base = BaseSize();
		return base > 0.0f ? base : ImGui::GetFontSize();
	}

	ImFont* BannerFont()
	{
		if (s_Banner != nullptr && s_Banner->FontSize > 0.0f) { return s_Banner; }
		// Until ours is built: the biggest font there is, scaled.
		if (s_Big != nullptr && s_Big->FontSize > 0.0f) { return s_Big; }
		ImFont* nexus = NexusFont();
		return nexus ? nexus : ImGui::GetFont();
	}
}
