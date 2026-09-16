#include "ArcStyle.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <Windows.h>

#include "Settings.h"

namespace ArcStyle
{
	namespace
	{
		constexpr unsigned kRecheckEveryMs = 5000;
		// ImGuiCol order is the same in ImGui 1.80 (ours) and 1.92 (arcdps') up to ResizeGripActive; after
		// that 1.92 inserted colours, so only the first block is taken from the newer blob.
		constexpr int kSharedColors = ImGuiCol_ResizeGripActive + 1;

		Values                s_Values;
		std::string           s_Path;
		std::filesystem::file_time_type s_WriteTime;
		unsigned              s_LastCheckMs = 0;
		int                   s_PushedColors = 0;
		int                   s_PushedVars   = 0;

		std::filesystem::path FindIni()
		{
			wchar_t module[MAX_PATH] = {};
			if (GetModuleFileNameW(nullptr, module, MAX_PATH) == 0) { return {}; }
			std::filesystem::path gameDir = std::filesystem::path(module).parent_path();
			for (const std::filesystem::path& candidate : { gameDir / "addons" / "arcdps" / "arcdps.ini", gameDir / "arcdps.ini" })
			{
				std::error_code ec;
				if (std::filesystem::exists(candidate, ec)) { return candidate; }
			}
			return {};
		}

		std::vector<unsigned char> DecodeBase64(const std::string& aText)
		{
			auto value = [](char c) -> int
			{
				if (c >= 'A' && c <= 'Z') { return c - 'A'; }
				if (c >= 'a' && c <= 'z') { return c - 'a' + 26; }
				if (c >= '0' && c <= '9') { return c - '0' + 52; }
				if (c == '+') { return 62; }
				if (c == '/') { return 63; }
				return -1;
			};

			std::vector<unsigned char> out;
			int bits = 0, count = 0;
			for (char c : aText)
			{
				int v = value(c);
				if (v < 0) { continue; }
				bits = (bits << 6) | v;
				count += 6;
				if (count >= 8)
				{
					count -= 8;
					out.push_back(static_cast<unsigned char>((bits >> count) & 0xFF));
				}
			}
			return out;
		}

		std::vector<float> Floats(const std::string& aBase64)
		{
			std::vector<unsigned char> bytes = DecodeBase64(aBase64);
			std::vector<float> floats(bytes.size() / sizeof(float));
			std::memcpy(floats.data(), bytes.data(), floats.size() * sizeof(float));
			return floats;
		}

		bool Sane(float aValue, float aMax) { return aValue >= 0.0f && aValue <= aMax; }

		void ReadColors(const std::vector<float>& aFloats, int aFirst, int aLast)
		{
			for (int color = aFirst; color <= aLast && color < ImGuiCol_COUNT; color++)
			{
				size_t index = static_cast<size_t>(color) * 4;
				if (index + 3 >= aFloats.size()) { return; }
				s_Values.Colors[color] = ImVec4(aFloats[index], aFloats[index + 1], aFloats[index + 2], aFloats[index + 3]);
				s_Values.HasColor[color] = true;
			}
		}

		// arcdps' ImGuiStyle blob. Only the leading fields are read; their order has been stable and each
		// value is range-checked, so a layout change in a future arcdps just means the defaults are kept.
		void ReadStyle(const std::vector<float>& aFloats)
		{
			if (aFloats.size() < 10) { return; }
			float fontSize = aFloats[0];
			float paddingX = aFloats[5], paddingY = aFloats[6];
			float rounding = aFloats[7], border = aFloats[8];
			if (!Sane(fontSize, 64) || !Sane(paddingX, 40) || !Sane(paddingY, 40) || !Sane(rounding, 32) || !Sane(border, 8)) { return; }
			if (fontSize >= 6.0f) { s_Values.FontSize = fontSize; }
			s_Values.WindowPadding = ImVec2(paddingX, paddingY);
			s_Values.WindowRounding = rounding;
			s_Values.WindowBorder = border;
			s_Values.HasStyle = true;
		}

		void Read(const std::filesystem::path& aPath)
		{
			std::ifstream in(aPath, std::ios::binary);
			if (!in) { return; }

			// Parsed into a copy and only taken over if it worked. arcdps rewrites this file while the game
			// runs, so a read can land on a half-written file; clearing the values first would drop the font
			// size back to Nexus' for one read and back again on the next, and every one of those changes makes
			// Nexus rebuild the font atlas for all addons, which stalls the game.
			Values previous = s_Values;
			s_Values = Values{};
			std::string colors192, colors180, style192, style180;
			float fontSize = 0.0f;

			std::string line;
			while (std::getline(in, line))
			{
				size_t eq = line.find('=');
				if (eq == std::string::npos) { continue; }
				std::string key = line.substr(0, eq);
				std::string value = line.substr(eq + 1);
				while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) { value.pop_back(); }

				if (key == "appearance_imgui_colours192") { colors192 = value; }
				else if (key == "appearance_imgui_colours180") { colors180 = value; }
				else if (key == "appearance_imgui_style192") { style192 = value; }
				else if (key == "appearance_imgui_style180") { style180 = value; }
				else if (key == "font_size") { fontSize = static_cast<float>(std::atof(value.c_str())); }
			}

			// The 1.80 blob matches our ImGui exactly; the 1.92 one is newer and shares the first colours.
			if (!colors180.empty()) { ReadColors(Floats(colors180), 0, ImGuiCol_COUNT - 1); }
			if (!colors192.empty()) { ReadColors(Floats(colors192), 0, kSharedColors - 1); }
			if (!style192.empty()) { ReadStyle(Floats(style192)); }
			else if (!style180.empty()) { ReadStyle(Floats(style180)); }
			if (s_Values.FontSize <= 0.0f && fontSize >= 6.0f) { s_Values.FontSize = fontSize; }

			s_Values.Ok = !colors192.empty() || !colors180.empty() || s_Values.HasStyle;
			// A read that found nothing usable, or lost the font size, keeps what the last good read found.
			if (previous.Ok && (!s_Values.Ok || s_Values.FontSize <= 0.0f)) { s_Values = previous; }
		}
	}

	void Update(unsigned aNowMs)
	{
		if (s_LastCheckMs != 0 && aNowMs - s_LastCheckMs < kRecheckEveryMs) { return; }
		s_LastCheckMs = aNowMs;

		std::filesystem::path path = s_Path.empty() ? FindIni() : std::filesystem::path(s_Path);
		if (path.empty()) { return; }
		std::error_code ec;
		std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(path, ec);
		if (ec) { return; }
		if (!s_Path.empty() && writeTime == s_WriteTime) { return; }

		s_Path = path.string();
		s_WriteTime = writeTime;
		Read(path);
	}

	const Values& Get() { return s_Values; }

	const char* SourcePath() { return s_Path.c_str(); }

	void Push()
	{
		s_PushedColors = 0;
		s_PushedVars = 0;
		if (!s_Values.Ok || !Settings::Current.MatchArcDps) { return; }

		for (int color = 0; color < ImGuiCol_COUNT; color++)
		{
			if (!s_Values.HasColor[color]) { continue; }
			ImGui::PushStyleColor(color, s_Values.Colors[color]);
			s_PushedColors++;
		}
		if (s_Values.HasStyle)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, s_Values.WindowRounding);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, s_Values.WindowBorder);
			s_PushedVars = 2;
		}
	}

	void Pop()
	{
		if (s_PushedVars) { ImGui::PopStyleVar(s_PushedVars); }
		if (s_PushedColors) { ImGui::PopStyleColor(s_PushedColors); }
		s_PushedColors = 0;
		s_PushedVars = 0;
	}
}
