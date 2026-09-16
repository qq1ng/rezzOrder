#include "Settings.h"

#include <cstdlib>
#include <fstream>
#include <string>

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

		// Colours are written as "r,g,b", each 0-1.
		void ParseColor(const std::string& aValue, float aColor[3])
		{
			size_t start = 0;
			for (int part = 0; part < 3 && start <= aValue.size(); part++)
			{
				size_t comma = aValue.find(',', start);
				std::string piece = aValue.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
				aColor[part] = ParseFloat(piece, aColor[part]);
				if (comma == std::string::npos) { break; }
				start = comma + 1;
			}
		}

		std::string WriteColor(const float aColor[3])
		{
			return std::to_string(aColor[0]) + "," + std::to_string(aColor[1]) + "," + std::to_string(aColor[2]);
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
			else if (key == "precast") { if (!value.empty()) { values.Precast.push_back(value); } }
			else if (key == "nick")
			{
				// nick=<account>=<nickname>
				size_t split = value.find('=');
				if (split != std::string::npos && split > 0 && split + 1 < value.size())
				{
					values.Nicknames[value.substr(0, split)] = value.substr(split + 1);
				}
			}
			else if (key == "preset")
			{
				// preset=<name>=<account>><account>...
				size_t split = value.find('=');
				if (split != std::string::npos && split > 0)
				{
					std::string name = value.substr(0, split);
					std::vector<std::string> accounts;
					std::string token;
					for (char character : value.substr(split + 1) + ">")
					{
						if (character == '>') { if (!token.empty()) { accounts.push_back(token); } token.clear(); }
						else { token += character; }
					}
					if (!accounts.empty()) { values.Presets[name] = accounts; }
				}
			}
			else if (key == "overlay_layout") { values.Layout = static_cast<OverlayLayout>(std::atoi(value.c_str())); }
			else if (key == "cooldown_bar") { values.CooldownBar = ParseBool(value); }
			else if (key == "cooldown_seconds") { values.CooldownSeconds = ParseBool(value); }
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
			else if (key == "overlay_background") { values.OverlayBackground = ParseBool(value); }
			else if (key == "overlay_width") { values.OverlayWidth = ParseFloat(value, values.OverlayWidth); }
			else if (key == "overlay_filled_width") { values.OverlayFilledWidth = ParseFloat(value, values.OverlayFilledWidth); }
			else if (key == "overlay_filled_height") { values.OverlayFilledHeight = ParseFloat(value, values.OverlayFilledHeight); }
			else if (key == "overlay_max_name_length") { values.OverlayMaxNameLength = std::atoi(value.c_str()); }
			else if (key == "overlay_max_rows") { values.OverlayMaxRows = std::atoi(value.c_str()); }
			else if (key == "prefer_account") { values.PreferAccount = ParseBool(value); }
			else if (key == "share_from_leaders") { values.ShareFromLeaders = ParseBool(value); }
			else if (key == "share_from_anyone") { values.ShareFromAnyone = ParseBool(value); }
			else if (key == "answer_requests") { values.AnswerRequests = std::atoi(value.c_str()); }
			else if (key == "banner_on_leave") { values.BannerOnLeave = ParseBool(value); }
			else if (key == "banner_on_swap") { values.BannerOnSwap = ParseBool(value); }
			else if (key == "banner_on_share") { values.BannerOnShare = ParseBool(value); }
			else if (key == "banner_on_ask") { values.BannerOnAsk = ParseBool(value); }
			else if (key == "up_banner") { values.UpBanner = ParseBool(value); }
			else if (key == "backup_banner") { values.BackupBanner = ParseBool(value); }
			else if (key == "up_sound") { values.UpSound = std::atoi(value.c_str()); }
			else if (key == "backup_sound") { values.BackupSound = std::atoi(value.c_str()); }
			else if (key == "sound_volume") { values.SoundVolume = std::atoi(value.c_str()); }
			else if (key == "sound_file") { values.SoundFile = value; }
			else if (key == "up_flash") { values.UpFlash = ParseBool(value); }
			else if (key == "backup_flash") { values.BackupFlash = ParseBool(value); }
			else if (key == "flash_strength") { values.FlashStrength = ParseFloat(value, values.FlashStrength); }
			else if (key == "up_flash_color") { ParseColor(value, values.UpFlashColor); }
			else if (key == "backup_flash_color") { ParseColor(value, values.BackupFlashColor); }
			else if (key == "last_seen_version") { values.LastSeenVersion = value; }
			else if (key == "editor_all_professions") { values.EditorAllProfessions = ParseBool(value); }
			else if (key == "overlay_messages") { values.OverlayMessages = std::atoi(value.c_str()); }
			else if (key == "banner_style") { values.BannerStyle = std::atoi(value.c_str()); }
			else if (key == "banner_info_size") { values.BannerInfoSize = ParseFloat(value, values.BannerInfoSize); }
			else if (key == "banner_info_x") { values.BannerInfoX = ParseFloat(value, values.BannerInfoX); }
			else if (key == "banner_info_y") { values.BannerInfoY = ParseFloat(value, values.BannerInfoY); }
			else if (key == "banner_alert_size") { values.BannerAlertSize = ParseFloat(value, values.BannerAlertSize); }
			else if (key == "banner_alert_x") { values.BannerAlertX = ParseFloat(value, values.BannerAlertX); }
			else if (key == "banner_alert_y") { values.BannerAlertY = ParseFloat(value, values.BannerAlertY); }
			else if (key == "banner_info_color") { ParseColor(value, values.BannerInfoColor); }
			else if (key == "banner_up_color") { ParseColor(value, values.BannerUpColor); }
			else if (key == "banner_backup_color") { ParseColor(value, values.BannerBackupColor); }
			else if (key == "banner_illusion_color") { ParseColor(value, values.BannerIllusionColor); }
			else if (key == "banner_seconds") { values.BannerSeconds = ParseFloat(value, values.BannerSeconds); }
			else if (key == "illusion_countdown") { values.IllusionCountdown = ParseBool(value); }
			else if (key == "illusion_warn_seconds") { values.IllusionWarnSeconds = std::atoi(value.c_str()); }
			else if (key == "illusion_sound") { values.IllusionSound = std::atoi(value.c_str()); }
			else if (key == "illusion_flash") { values.IllusionFlash = ParseBool(value); }
			else if (key == "illusion_flash_color") { ParseColor(value, values.IllusionFlashColor); }
			else if (key == "illusion_number_below") { values.IllusionNumberBelow = ParseBool(value); }
			else if (key == "record_field_logs") { values.RecordFieldLogs = ParseBool(value); }
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
			for (const std::string& account : v.Precast) { out << "precast=" << account << "\n"; }
			for (const auto& [account, nickname] : v.Nicknames) { out << "nick=" << account << "=" << nickname << "\n"; }
			for (const auto& [name, accounts] : v.Presets)
			{
				out << "preset=" << name << "=";
				for (size_t i = 0; i < accounts.size(); i++) { out << (i ? ">" : "") << accounts[i]; }
				out << "\n";
			}
			out << "overlay_layout=" << static_cast<int>(v.Layout) << "\n";
			out << "cooldown_bar=" << v.CooldownBar << "\n";
			out << "cooldown_seconds=" << v.CooldownSeconds << "\n";
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
			out << "overlay_background=" << v.OverlayBackground << "\n";
			out << "overlay_width=" << v.OverlayWidth << "\n";
			out << "overlay_filled_width=" << v.OverlayFilledWidth << "\n";
			out << "overlay_filled_height=" << v.OverlayFilledHeight << "\n";
			out << "overlay_max_name_length=" << v.OverlayMaxNameLength << "\n";
			out << "overlay_max_rows=" << v.OverlayMaxRows << "\n";
			out << "prefer_account=" << v.PreferAccount << "\n";
			out << "share_from_leaders=" << v.ShareFromLeaders << "\n";
			out << "share_from_anyone=" << v.ShareFromAnyone << "\n";
			out << "answer_requests=" << v.AnswerRequests << "\n";
			out << "banner_on_leave=" << v.BannerOnLeave << "\n";
			out << "banner_on_swap=" << v.BannerOnSwap << "\n";
			out << "banner_on_share=" << v.BannerOnShare << "\n";
			out << "banner_on_ask=" << v.BannerOnAsk << "\n";
			out << "up_banner=" << v.UpBanner << "\n";
			out << "backup_banner=" << v.BackupBanner << "\n";
			out << "up_sound=" << v.UpSound << "\n";
			out << "backup_sound=" << v.BackupSound << "\n";
			out << "sound_volume=" << v.SoundVolume << "\n";
			out << "sound_file=" << v.SoundFile << "\n";
			out << "up_flash=" << v.UpFlash << "\n";
			out << "backup_flash=" << v.BackupFlash << "\n";
			out << "flash_strength=" << v.FlashStrength << "\n";
			out << "up_flash_color=" << WriteColor(v.UpFlashColor) << "\n";
			out << "backup_flash_color=" << WriteColor(v.BackupFlashColor) << "\n";
			out << "last_seen_version=" << v.LastSeenVersion << "\n";
			out << "editor_all_professions=" << v.EditorAllProfessions << "\n";
			out << "overlay_messages=" << v.OverlayMessages << "\n";
			out << "banner_style=" << v.BannerStyle << "\n";
			out << "banner_info_size=" << v.BannerInfoSize << "\n";
			out << "banner_info_x=" << v.BannerInfoX << "\n";
			out << "banner_info_y=" << v.BannerInfoY << "\n";
			out << "banner_alert_size=" << v.BannerAlertSize << "\n";
			out << "banner_alert_x=" << v.BannerAlertX << "\n";
			out << "banner_alert_y=" << v.BannerAlertY << "\n";
			out << "banner_info_color=" << WriteColor(v.BannerInfoColor) << "\n";
			out << "banner_up_color=" << WriteColor(v.BannerUpColor) << "\n";
			out << "banner_backup_color=" << WriteColor(v.BannerBackupColor) << "\n";
			out << "banner_illusion_color=" << WriteColor(v.BannerIllusionColor) << "\n";
			out << "banner_seconds=" << v.BannerSeconds << "\n";
			out << "illusion_countdown=" << v.IllusionCountdown << "\n";
			out << "illusion_warn_seconds=" << v.IllusionWarnSeconds << "\n";
			out << "illusion_sound=" << v.IllusionSound << "\n";
			out << "illusion_flash=" << v.IllusionFlash << "\n";
			out << "illusion_flash_color=" << WriteColor(v.IllusionFlashColor) << "\n";
			out << "illusion_number_below=" << v.IllusionNumberBelow << "\n";
			out << "record_field_logs=" << v.RecordFieldLogs << "\n";
		}
		std::filesystem::rename(temp, s_File, ec);
	}
}
