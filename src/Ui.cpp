#include "Ui.h"

#include <cstring>

#include <Windows.h>
#include <shellapi.h>

#include "imgui/imgui.h"

#include "Capture.h"

namespace Ui
{
	bool                      ShowWindow = false;
	Diagnostics::Dependencies Deps;
	std::atomic<bool> ToggleRequested{false};
	std::atomic<bool> MarkRequested{false};
	std::atomic<bool> TextInputActive{false};

	namespace
	{
		// Labels for the solo test checklist in docs/phase0-test-plan.md. Picking one fills the mark label.
		// Entries starting with "---" are dividers and can't be picked.
		constexpr const char* kTests[] = {
			"(custom label)",
			"--- Group: basics (A = you, B = helper) ---",
			"G1 Same spot: B casts a few normal skills",
			"G2 Same spot: B goes down (fall damage)",
			"--- Group: range ---",
			"G3 B ~1200 away: casts, then goes down",
			"G4 B ~3000 away: casts, then goes down",
			"G5 B out of sight: casts, then goes down",
			"G6 B on another part of the map: casts, then goes down",
			"--- Group: A revives downed B ---",
			"G7 Signet of Mercy on downed B",
			"G8 Renewal of Water on downed B",
			"G9 Renewal of Air on downed B (cast from range)",
			"G10 Renewal of Earth on downed B",
			"G11 Renewal of Fire on downed B",
			"G12 Illusion of Life on downed B - no kill",
			"G13 Illusion of Life on downed B - B kills an NPC within 15s",
			"G14 Signet of Undeath on downed B",
			"G15 Battle Standard on downed B",
			"G16 Spirit of Nature on downed B",
			"G17 Revive skill aimed next to downed B (miss)",
			"--- Group: B casts, A watches ---",
			"G18 B revive skill - full cast, nobody downed",
			"G19 B revive skill - cancel early",
			"G20 B revive skill - cancel late",
			"G21 B revives downed A",
			"--- Group: chat and roles ---",
			"G22 B (member) posts !rezz test in squad chat",
			"G23 B (lieutenant) posts !rezz test in squad chat",
			"G24 B in another subgroup posts !rezz test in party chat",
			"G25 A posts !rezz test in party chat, B in another subgroup",
			"G26 B leaves the squad and rejoins",
			"--- Session 2: cancel late (just before cast bar ends) ---",
			"Illusion of Life - cancel late",
			"Spirit of Nature - cancel late",
			"Renewal of Fire - cancel late",
			"Renewal of Water - cancel late",
			"Renewal of Air - cancel late",
			"Renewal of Earth - cancel late",
			"Signet of Undeath - cancel late",
			"Battle Standard - cancel late",
			"Signet of Mercy - cancel late (repeat)",
			"--- Session 2: with quickness ---",
			"Illusion of Life - quickness, full cast",
			"Illusion of Life - quickness, cancel late",
			"Spirit of Nature - quickness, full cast",
			"Spirit of Nature - quickness, cancel late",
			"Glyph of Renewal - quickness, full cast",
			"Glyph of Renewal - quickness, cancel late",
			"Signet of Undeath - quickness, full cast",
			"Signet of Undeath - quickness, cancel late",
			"Battle Standard - quickness, full cast",
			"Battle Standard - quickness, cancel late",
			"Signet of Mercy - quickness, full cast",
			"Signet of Mercy - quickness, cancel late",
			"--- Session 2: other ---",
			"Cast interrupted by CC (add skill name)",
			"Battle Standard - self-revive: go down while banner is in the air",
			"--- Session 1 (done) ---",
			"Signet of Mercy - full cast, nobody downed",
			"Signet of Mercy - cancel early (dodge)",
			"Signet of Mercy - cancel late (just before cast bar ends)",
			"Renewal of Fire - full cast",
			"Renewal of Fire - cancel early",
			"Renewal of Fire - self-revive: cast, then go down within 15s",
			"Renewal of Water - full cast",
			"Renewal of Water - cancel early",
			"Renewal of Air - full cast",
			"Renewal of Air - cancel early",
			"Renewal of Earth - full cast",
			"Renewal of Earth - cancel early",
			"Glyph of Renewal - swap attunement mid-cast",
			"Illusion of Life - full cast",
			"Illusion of Life - cancel early",
			"Signet of Undeath - full cast",
			"Signet of Undeath - cancel early",
			"Battle Standard - full cast",
			"Battle Standard - cancel early",
			"Battle Standard - self-revive: cast, then go down",
			"Spirit of Nature - full cast",
			"Spirit of Nature - cancel early",
			"Spirit of Nature - self-revive: cast, then go down",
			"Go down (fall damage)",
			"Downed - bleed out / defeated",
			"Downed - rally or downed skill self-revive",
			"Cast interrupted by CC",
			"Squad chat as commander: !rezz test",
			"Party chat: !rezz test",
		};

		int  s_TestIndex = 0;
		char s_Label[128] = "";

		const ImVec4 kGreen  { 0.40f, 0.90f, 0.40f, 1.0f };
		const ImVec4 kOrange { 1.00f, 0.65f, 0.20f, 1.0f };
		const ImVec4 kRed    { 1.00f, 0.35f, 0.35f, 1.0f };
		const ImVec4 kYellow { 1.00f, 0.90f, 0.30f, 1.0f };
		const ImVec4 kGrey   { 0.60f, 0.60f, 0.60f, 1.0f };

		void DoMark()
		{
			Capture::Mark(s_Label[0] != '\0' ? s_Label : "(no label)");
		}

		void TextColoredIf(bool aCondition, const ImVec4& aColor, const char* aText)
		{
			if (aCondition) { ImGui::TextColored(aColor, "%s", aText); }
			else            { ImGui::TextUnformatted(aText); }
		}

		void FeedTable(const char* aId, const std::deque<Capture::FeedRow>& aFeed, uint32_t aNowMs)
		{
			ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
				ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
			if (!ImGui::BeginTable(aId, 7, flags, ImVec2(0, ImGui::GetContentRegionAvail().y))) { return; }

			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Ago");
			ImGui::TableSetupColumn("Delay");
			ImGui::TableSetupColumn("Ch");
			ImGui::TableSetupColumn("Who");
			ImGui::TableSetupColumn("Skill");
			ImGui::TableSetupColumn("Event", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Verdict");
			ImGui::TableHeadersRow();

			for (auto it = aFeed.rbegin(); it != aFeed.rend(); ++it)
			{
				const Capture::FeedRow& row = *it;
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				ImGui::Text("%.1fs", (aNowMs - row.ArriveMs) / 1000.0f);

				if (row.IsMark)
				{
					ImGui::TableSetColumnIndex(5);
					ImGui::TextColored(kYellow, "MARK %s", row.Event.c_str());
					continue;
				}

				ImGui::TableNextColumn();
				if (row.DelayMs >= 0) { ImGui::Text("%lld ms", row.DelayMs); } else { ImGui::TextColored(kGrey, "?"); }

				ImGui::TableNextColumn();
				ImGui::TextUnformatted(row.Channel);

				ImGui::TableNextColumn();
				TextColoredIf(row.IsSelf, kYellow, row.Who.c_str());

				ImGui::TableNextColumn();
				if (row.SkillId != 0) { ImGui::Text("%s (%u)", row.Skill.c_str(), row.SkillId); }

				ImGui::TableNextColumn();
				bool isDown = row.Event == "CHANGEDOWN" || row.Event == "CHANGEDEAD";
				bool isUp = row.Event == "CHANGEUP";
				ImGui::TextColored(isDown ? kRed : (isUp ? kGreen : ImGui::GetStyleColorVec4(ImGuiCol_Text)), "%s", row.Event.c_str());

				ImGui::TableNextColumn();
				if (!row.Verdict.empty())
				{
					bool used = row.Verdict.rfind("USED", 0) == 0;
					ImGui::TextColored(used ? kGreen : kOrange, "%s", row.Verdict.c_str());
				}
			}
			ImGui::EndTable();
		}

		void DelayTable(const Capture::Snapshot& aSnap)
		{
			ImGui::TextWrapped("Delay = time the event reached this addon minus the time arcdps registered it. "
				"arcdps documents ~2-3 s for the squad feed. \"Animation\" rows only count cast start/stop events.");

			if (!ImGui::BeginTable("delay", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) { return; }
			for (const char* header : { "Channel", "Who", "Events", "Count", "Last", "Min", "Avg", "Max" })
			{
				ImGui::TableSetupColumn(header);
			}
			ImGui::TableHeadersRow();

			static constexpr const char* channels[] = { "SQUAD", "LOCAL" };
			static constexpr const char* who[] = { "self", "others" };
			static constexpr const char* scopes[] = { "all", "animation" };
			for (int c = 0; c < Capture::CH_COUNT; c++)
				for (int w = 0; w < Capture::WHO_COUNT; w++)
					for (int s = 0; s < Capture::SCOPE_COUNT; s++)
					{
						const Capture::DelayStat& stat = aSnap.Delay[c][w][s];
						ImGui::TableNextRow();
						ImGui::TableNextColumn(); ImGui::TextUnformatted(channels[c]);
						ImGui::TableNextColumn(); ImGui::TextUnformatted(who[w]);
						ImGui::TableNextColumn(); ImGui::TextUnformatted(scopes[s]);
						ImGui::TableNextColumn(); ImGui::Text("%llu", stat.Count);
						if (stat.Count == 0) { continue; }
						ImGui::TableNextColumn(); ImGui::Text("%u ms", stat.Last);
						ImGui::TableNextColumn(); ImGui::Text("%u ms", stat.Min);
						ImGui::TableNextColumn(); ImGui::Text("%llu ms", stat.Sum / stat.Count);
						ImGui::TableNextColumn(); ImGui::Text("%u ms", stat.Max);
					}
			ImGui::EndTable();
		}

		void SquadTab(const Capture::Snapshot& aSnap)
		{
			ImGui::Text("You: %s  %s", aSnap.SelfAccount.empty() ? "(unknown yet)" : aSnap.SelfAccount.c_str(),
				aSnap.SelfCharacter.c_str());
			ImGui::Separator();

			ImGui::Text("Squad roster (Unofficial Extras): %zu", aSnap.Squad.size());
			if (aSnap.Squad.empty())
			{
				ImGui::TextColored(kGrey, "Empty. Join or open a squad/party; if it stays empty, Unofficial Extras isn't reaching Nexus.");
			}
			else if (ImGui::BeginTable("squad", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY, ImVec2(0, 160)))
			{
				ImGui::TableSetupColumn("Account");
				ImGui::TableSetupColumn("Role");
				ImGui::TableSetupColumn("Subgroup");
				ImGui::TableHeadersRow();
				for (const Capture::Member& member : aSnap.Squad)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn(); ImGui::TextUnformatted(member.Account.c_str());
					ImGui::TableNextColumn(); ImGui::TextUnformatted(Capture::RoleName(member.Role));
					ImGui::TableNextColumn(); ImGui::Text("%u", member.Subgroup);
				}
				ImGui::EndTable();
			}

			ImGui::Separator();
			ImGui::Text("Chat commands starting with \"!\" (only these are recorded):");
			if (ImGui::BeginTable("commands", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY, ImVec2(0, ImGui::GetContentRegionAvail().y)))
			{
				ImGui::TableSetupColumn("Ago");
				ImGui::TableSetupColumn("Sender");
				ImGui::TableSetupColumn("Role");
				ImGui::TableSetupColumn("Accepted?");
				ImGui::TableSetupColumn("Text", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();
				for (auto it = aSnap.Commands.rbegin(); it != aSnap.Commands.rend(); ++it)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn(); ImGui::Text("%.1fs", (aSnap.NowMs - it->ArriveMs) / 1000.0f);
					ImGui::TableNextColumn(); ImGui::TextUnformatted(it->Account.c_str());
					ImGui::TableNextColumn(); ImGui::TextUnformatted(it->Role.c_str());
					ImGui::TableNextColumn(); ImGui::TextColored(it->WouldAccept ? kGreen : kOrange, it->WouldAccept ? "auto-apply" : "ask user");
					ImGui::TableNextColumn(); ImGui::TextUnformatted(it->Text.c_str());
				}
				ImGui::EndTable();
			}
		}

		void OpenLogFolder(const std::string& aLogPath)
		{
			std::string folder = aLogPath.substr(0, aLogPath.find_last_of("\\/"));
			ShellExecuteA(nullptr, "open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		}
	}

	void Render(bool aIsGameplay)
	{
		if (ToggleRequested.exchange(false)) { ShowWindow = !ShowWindow; }
		if (MarkRequested.exchange(false)) { DoMark(); }

		if (!ShowWindow || !aIsGameplay)
		{
			TextInputActive = false;
			return;
		}

		Capture::Snapshot snap = Capture::GetSnapshot();

		ImGui::SetNextWindowSize(ImVec2(900, 560), ImGuiCond_FirstUseEver);
		if (ImGui::Begin(kWindowName, &ShowWindow))
		{
			ImGui::TextColored(kGrey, "Log: %s  (%llu rows)", snap.LogPath.c_str(), snap.Rows);
			ImGui::SameLine();
			if (ImGui::SmallButton("Open folder")) { OpenLogFolder(snap.LogPath); }

			bool logAll = Capture::LogAllEvents;
			if (ImGui::Checkbox("Log all arcdps events", &logAll)) { Capture::LogAllEvents = logAll; }
			ImGui::SameLine();
			bool logKeys = Capture::LogKeys;
			if (ImGui::Checkbox("Log key presses", &logKeys)) { Capture::LogKeys = logKeys; }

			ImGui::Separator();
			ImGui::SetNextItemWidth(420);
			int previousTest = s_TestIndex;
			if (ImGui::Combo("Test", &s_TestIndex, kTests, IM_ARRAYSIZE(kTests)) && s_TestIndex > 0)
			{
				if (std::strncmp(kTests[s_TestIndex], "---", 3) == 0) { s_TestIndex = previousTest; }
				else { strncpy_s(s_Label, kTests[s_TestIndex], _TRUNCATE); }
			}
			ImGui::SetNextItemWidth(420);
			if (ImGui::InputText("Label", s_Label, sizeof(s_Label), ImGuiInputTextFlags_EnterReturnsTrue)) { DoMark(); }
			TextInputActive = ImGui::IsItemActive();
			ImGui::SameLine();
			if (ImGui::Button("Mark")) { DoMark(); }
			ImGui::SameLine();
			ImGui::TextColored(kGrey, "%u marks | press Mark before each test (hotkey: Ctrl+Shift+M)", snap.MarkCount);

			if (ImGui::BeginTabBar("tabs"))
			{
				if (ImGui::BeginTabItem("Revive feed"))
				{
					FeedTable("revive", snap.ReviveFeed, snap.NowMs);
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("My casts (all skills)"))
				{
					FeedTable("self", snap.SelfFeed, snap.NowMs);
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Delay"))
				{
					DelayTable(snap);
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Squad & chat"))
				{
					SquadTab(snap);
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
		}
		ImGui::End();
	}

	void Options()
	{
		Capture::Status status = Capture::GetStatus();

		ImGui::Text("Recorder status");
		auto check = [](const char* aLabel, bool aOk, const char* aHint)
		{
			ImGui::TextColored(aOk ? kGreen : kRed, "%s %s", aOk ? "[ok]" : "[missing]", aLabel);
			if (!aOk) { ImGui::SameLine(); ImGui::TextColored(kGrey, "%s", aHint); }
		};
		if (Deps.Checked)
		{
			check("ArcDPS", Deps.ArcDps, "install ArcDPS from the Nexus library");
			check("Arcdps Integration", Deps.Integration, "restart the game after installing ArcDPS");
			check("Unofficial Extras", Deps.UnofficialExtras, "install Unofficial Extras from the Nexus library");
		}
		else
		{
			ImGui::TextColored(kGrey, "Checking dependencies shortly after game start...");
		}
		ImGui::Text("On a WvW map: %s", status.InWvw ? "yes (recording)" : "no (not recording)");
		ImGui::Text("ArcDPS events received: %llu | squad/chat updates: %llu", status.ArcEvents, status.UeEvents);
		ImGui::Text("Log: %s (%.1f MB, %llu rows)", status.LogPath.c_str(), status.Bytes / (1024.0 * 1024.0), status.Rows);
		if (ImGui::Button("Open log folder")) { OpenLogFolder(status.LogPath); }

		ImGui::Separator();
		ImGui::Checkbox("Show Phase 0 recorder window (Ctrl+Shift+R)", &ShowWindow);
		bool logAll = Capture::LogAllEvents;
		if (ImGui::Checkbox("Log all arcdps events (large files)", &logAll)) { Capture::LogAllEvents = logAll; }
		bool logKeys = Capture::LogKeys;
		if (ImGui::Checkbox("Log key presses (not while typing in chat)", &logKeys)) { Capture::LogKeys = logKeys; }
	}
}
