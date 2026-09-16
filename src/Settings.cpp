#include "Settings.h"

#include <cstdlib>
#include <fstream>

namespace Settings
{
	Values Current;

	namespace
	{
		constexpr unsigned kFlushEveryMs = 1000;

		std::filesystem::path s_File;
		bool                  s_Dirty       = false;
		unsigned              s_LastFlushMs = 0;

		bool ParseBool(const std::string& aValue) { return aValue == "1" || aValue == "true"; }

		float ParseFloat(const std::string& aValue, float aDefault)
		{
			try { return std::stof(aValue); }
			catch (...) { return aDefault; }
		}
	}

	void Load(const std::filesystem::path& aFile)
	{
		s_File = aFile;
		std::ifstream in(aFile, std::ios::binary);
		if (!in) { return; }

		Values values;
		std::string line;
		while (std::getline(in, line))
		{
			if (!line.empty() && line.back() == '\r') { line.pop_back(); }
			size_t eq = line.find('=');
			if (eq == std::string::npos) { continue; }
			std::string key = line.substr(0, eq);
			std::string value = line.substr(eq + 1);

			if (key == "order") { if (!value.empty()) { values.Order.push_back(value); } }
			else if (key == "nick")
			{
				// nick=<account>=<nickname>
				size_t split = value.find('=');
				if (split != std::string::npos && split > 0 && split + 1 < value.size())
				{
					values.Nicknames[value.substr(0, split)] = value.substr(split + 1);
				}
			}
			else if (key == "overlay_visible") { values.OverlayVisible = ParseBool(value); }
			else if (key == "overlay_locked") { values.OverlayLocked = ParseBool(value); }
			else if (key == "overlay_only_wvw") { values.OverlayOnlyWvw = ParseBool(value); }
			else if (key == "overlay_bg_alpha") { values.OverlayBgAlpha = ParseFloat(value, values.OverlayBgAlpha); }
			else if (key == "overlay_scale") { values.OverlayScale = ParseFloat(value, values.OverlayScale); }
			else if (key == "overlay_x") { values.OverlayX = ParseFloat(value, values.OverlayX); }
			else if (key == "overlay_y") { values.OverlayY = ParseFloat(value, values.OverlayY); }
			else if (key == "font") { values.Font = static_cast<FontSource>(std::atoi(value.c_str())); }
			else if (key == "font_file") { values.FontFile = value; }
			else if (key == "match_arcdps") { values.MatchArcDps = ParseBool(value); }
			else if (key == "overlay_title_bar") { values.OverlayTitleBar = ParseBool(value); }
			else if (key == "overlay_scroll_bar") { values.OverlayScrollBar = ParseBool(value); }
			else if (key == "overlay_background") { values.OverlayBackground = ParseBool(value); }
			else if (key == "overlay_width") { values.OverlayWidth = ParseFloat(value, values.OverlayWidth); }
			else if (key == "overlay_height") { values.OverlayHeight = ParseFloat(value, values.OverlayHeight); }
			else if (key == "overlay_max_name_length") { values.OverlayMaxNameLength = std::atoi(value.c_str()); }
			else if (key == "overlay_max_rows") { values.OverlayMaxRows = std::atoi(value.c_str()); }
			else if (key == "prefer_account") { values.PreferAccount = ParseBool(value); }
			else if (key == "alert_on_changes") { values.AlertOnChanges = ParseBool(value); }
			else if (key == "editor_all_professions") { values.EditorAllProfessions = ParseBool(value); }
		}
		Current = values;
	}

	void MarkDirty()
	{
		s_Dirty = true;
	}

	void Flush(unsigned aNowMs, bool aForce)
	{
		if (!s_Dirty || s_File.empty()) { return; }
		if (!aForce && aNowMs - s_LastFlushMs < kFlushEveryMs) { return; }
		s_LastFlushMs = aNowMs;
		s_Dirty = false;

		std::error_code ec;
		std::filesystem::create_directories(s_File.parent_path(), ec);
		std::filesystem::path temp = s_File;
		temp += ".tmp";
		{
			std::ofstream out(temp, std::ios::binary | std::ios::trunc);
			if (!out) { return; }
			const Values& v = Current;
			for (const std::string& account : v.Order) { out << "order=" << account << "\n"; }
			for (const auto& [account, nickname] : v.Nicknames) { out << "nick=" << account << "=" << nickname << "\n"; }
			out << "overlay_visible=" << v.OverlayVisible << "\n";
			out << "overlay_locked=" << v.OverlayLocked << "\n";
			out << "overlay_only_wvw=" << v.OverlayOnlyWvw << "\n";
			out << "overlay_bg_alpha=" << v.OverlayBgAlpha << "\n";
			out << "overlay_scale=" << v.OverlayScale << "\n";
			out << "overlay_x=" << v.OverlayX << "\n";
			out << "overlay_y=" << v.OverlayY << "\n";
			out << "font=" << static_cast<int>(v.Font) << "\n";
			out << "font_file=" << v.FontFile << "\n";
			out << "match_arcdps=" << v.MatchArcDps << "\n";
			out << "overlay_title_bar=" << v.OverlayTitleBar << "\n";
			out << "overlay_scroll_bar=" << v.OverlayScrollBar << "\n";
			out << "overlay_background=" << v.OverlayBackground << "\n";
			out << "overlay_width=" << v.OverlayWidth << "\n";
			out << "overlay_height=" << v.OverlayHeight << "\n";
			out << "overlay_max_name_length=" << v.OverlayMaxNameLength << "\n";
			out << "overlay_max_rows=" << v.OverlayMaxRows << "\n";
			out << "prefer_account=" << v.PreferAccount << "\n";
			out << "alert_on_changes=" << v.AlertOnChanges << "\n";
			out << "editor_all_professions=" << v.EditorAllProfessions << "\n";
		}
		std::filesystem::rename(temp, s_File, ec);
	}
}
