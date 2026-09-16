#include "OrderUi.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <unordered_map>

#include "imgui/imgui.h"

#include "ArcStyle.h"
#include "Fonts.h"
#include "Icons.h"
#include "Live.h"
#include "Settings.h"

namespace OrderUi
{
	bool              ShowEditor = false;
	std::atomic<bool> TextInputActive{false};
	std::atomic<bool> EditorToggleRequested{false};
	std::atomic<bool> LockToggleRequested{false};

	namespace
	{
		constexpr unsigned kNoticeShowMs = 20 * 1000;
		constexpr size_t   kNoticeLimit  = 4;

		const ImVec4 kGreen   { 0.40f, 0.90f, 0.40f, 1.0f };
		const ImVec4 kYellow  { 1.00f, 0.90f, 0.30f, 1.0f };
		const ImVec4 kOrange  { 1.00f, 0.65f, 0.20f, 1.0f };
		const ImVec4 kRed     { 1.00f, 0.35f, 0.35f, 1.0f };
		const ImVec4 kDarkRed { 0.75f, 0.20f, 0.20f, 1.0f };
		const ImVec4 kGrey    { 0.62f, 0.62f, 0.62f, 1.0f };

		struct TimedNotice
		{
			std::string Text;
			unsigned    TimeMs;
		};

		std::deque<TimedNotice> s_Notices;
		bool s_MenuOpen = false; // one of our popups was open last frame: keep taking input while it is

		ImVec4 ProfessionColor(uint32_t aProfession)
		{
			switch (aProfession)
			{
				case 1: return { 0.45f, 0.76f, 0.85f, 1.0f }; // Guardian
				case 2: return { 1.00f, 0.82f, 0.40f, 1.0f }; // Warrior
				case 3: return { 0.82f, 0.61f, 0.35f, 1.0f }; // Engineer
				case 4: return { 0.55f, 0.86f, 0.51f, 1.0f }; // Ranger
				case 5: return { 0.75f, 0.56f, 0.58f, 1.0f }; // Thief
				case 6: return { 0.96f, 0.54f, 0.53f, 1.0f }; // Elementalist
				case 7: return { 0.71f, 0.47f, 0.84f, 1.0f }; // Mesmer
				case 8: return { 0.32f, 0.65f, 0.44f, 1.0f }; // Necromancer
				case 9: return { 0.82f, 0.43f, 0.35f, 1.0f }; // Revenant
				default: return kGrey;
			}
		}

		using RosterIndex = std::unordered_map<std::string, const Rezz::RosterMember*>;

		RosterIndex IndexRoster(const Rezz::SessionView& aView)
		{
			RosterIndex index;
			for (const Rezz::RosterMember& member : aView.Roster) { index[member.Account] = &member; }
			return index;
		}

		const Rezz::RosterMember* Find(const RosterIndex& aIndex, const std::string& aAccount)
		{
			auto it = aIndex.find(aAccount);
			return it == aIndex.end() ? nullptr : it->second;
		}

		// Character names repeat between players when they are WvW ranks, so names seen more than once in the
		// squad fall back to the account name as well.
		std::unordered_map<std::string, int> s_CharacterNameCount;

		void CountCharacterNames(const Rezz::SessionView& aView)
		{
			s_CharacterNameCount.clear();
			for (const Rezz::RosterMember& member : aView.Roster)
			{
				if (Rezz::IsUsableCharacterName(member.Character)) { s_CharacterNameCount[member.Character]++; }
			}
		}

		const std::string* Nickname(const std::string& aAccount)
		{
			auto it = Settings::Current.Nicknames.find(aAccount);
			return it == Settings::Current.Nicknames.end() || it->second.empty() ? nullptr : &it->second;
		}

		void SetNickname(const std::string& aAccount, const char* aText)
		{
			std::string nickname = aText ? aText : "";
			if (nickname.empty()) { Settings::Current.Nicknames.erase(aAccount); }
			else { Settings::Current.Nicknames[aAccount] = nickname; }
			Settings::MarkDirty();
		}

		// A nickname is shown instead of the character or account name, and lets players be recognised in
		// Edge of the Mists where other worlds' players have no usable character name.
		void NicknameMenu(const std::string& aAccount)
		{
			if (!ImGui::BeginMenu("nickname")) { return; }
			char buffer[64] = {};
			if (const std::string* nickname = Nickname(aAccount)) { strncpy_s(buffer, nickname->c_str(), _TRUNCATE); }
			ImGui::SetNextItemWidth(160);
			if (ImGui::InputText("##nickname", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
			{
				SetNickname(aAccount, buffer);
				ImGui::CloseCurrentPopup();
			}
			TextInputActive = TextInputActive || ImGui::IsItemActive();
			ImGui::TextColored(kGrey, "%s", Rezz::DisplayAccount(aAccount).c_str());
			if (Nickname(aAccount) && ImGui::MenuItem("clear")) { SetNickname(aAccount, ""); }
			ImGui::EndMenu();
		}

		std::string PlayerName(const std::string& aAccount, const Rezz::RosterMember* aMember)
		{
			if (const std::string* nickname = Nickname(aAccount)) { return *nickname; }
			if (!Settings::Current.PreferAccount && aMember && Rezz::IsUsableCharacterName(aMember->Character))
			{
				auto it = s_CharacterNameCount.find(aMember->Character);
				if (it == s_CharacterNameCount.end() || it->second == 1) { return aMember->Character; }
			}
			return Rezz::DisplayAccount(aAccount);
		}

		std::string ShortName(std::string aName)
		{
			int limit = Settings::Current.OverlayMaxNameLength;
			if (limit > 0 && static_cast<int>(aName.size()) > limit) { aName.resize(static_cast<size_t>(limit)); }
			return aName;
		}

		// Full account name ("Gorath.5076") when the last item is hovered, also inside a selectable row.
		void AccountTooltip(const std::string& aAccount, const Rezz::RosterMember* aMember = nullptr)
		{
			if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlapped)) { return; }
			std::string text = Rezz::DisplayAccount(aAccount);
			if (aMember && Rezz::IsUsableCharacterName(aMember->Character)) { text = aMember->Character + " (" + text + ")"; }
			ImGui::SetTooltip("%s", text.c_str());
		}

		// Elite specialization icon tinted in the profession colour, like arcdps. Text while the icon loads.
		void ProfessionIcon(const std::string& aAccount, const Rezz::RosterMember* aMember)
		{
			uint32_t profession = aMember ? aMember->Profession : 0;
			float size = ImGui::GetTextLineHeight();
			if (void* icon = Icons::Get(profession, aMember ? aMember->Elite : 0))
			{
				ImGui::Image(icon, ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1), ProfessionColor(profession));
			}
			else if (profession != 0)
			{
				ImGui::TextColored(ProfessionColor(profession), "%s", Rezz::ProfessionShortName(profession));
			}
			else
			{
				ImGui::Dummy(ImVec2(size, size));
			}
			AccountTooltip(aAccount, aMember);
		}

		void PlayerNameText(const std::string& aAccount, const std::string& aName, const ImVec4& aColor,
			const Rezz::RosterMember* aMember = nullptr)
		{
			ImGui::TextColored(aColor, "%s", aName.c_str());
			AccountTooltip(aAccount, aMember);
		}

		std::string SeenSkills(uint32_t aSeenGroups)
		{
			std::string text;
			for (int g = 0; g < static_cast<int>(ReviveGroup::Count); g++)
			{
				if (!(aSeenGroups & (1u << g))) { continue; }
				if (!text.empty()) { text += ", "; }
				text += kReviveGroups[g].Name;
			}
			return text;
		}

		void ApplyOrder(std::vector<std::string> aOrder)
		{
			Settings::Values& s = Settings::Current;
			s.Order = std::move(aOrder);
			Live::SetOrder(s.Order);
			Settings::MarkDirty();
		}

		void MoveInOrder(std::vector<std::string> aOrder, int aFrom, int aTo)
		{
			if (aFrom < 0 || aTo < 0 || aFrom >= static_cast<int>(aOrder.size()) || aTo >= static_cast<int>(aOrder.size())) { return; }
			std::string moved = aOrder[aFrom];
			aOrder.erase(aOrder.begin() + aFrom);
			aOrder.insert(aOrder.begin() + aTo, moved);
			ApplyOrder(std::move(aOrder));
		}

		void RemoveFromOrder(std::vector<std::string> aOrder, int aIndex)
		{
			if (aIndex < 0 || aIndex >= static_cast<int>(aOrder.size())) { return; }
			aOrder.erase(aOrder.begin() + aIndex);
			ApplyOrder(std::move(aOrder));
		}

		// Squad members with a revive profession who aren't in the order yet.
		std::vector<const Rezz::RosterMember*> Candidates(const Rezz::SessionView& aView, bool aAllProfessions)
		{
			std::vector<const Rezz::RosterMember*> candidates;
			for (const Rezz::RosterMember& member : aView.Roster)
			{
				if (!member.InSquad && !member.OnMap) { continue; }
				if (std::find(aView.Order.begin(), aView.Order.end(), member.Account) != aView.Order.end()) { continue; }
				bool revive = Rezz::IsReviveProfession(member.Profession) || member.Profession == 0;
				if (!revive && !aAllProfessions) { continue; }
				candidates.push_back(&member);
			}
			return candidates;
		}

		// A menu entry with the profession icon drawn into the space the label leaves free.
		bool MenuItemWithIcon(const Rezz::RosterMember& aMember, const std::string& aLabel)
		{
			float size = ImGui::GetTextLineHeight();
			float indent = size + ImGui::GetStyle().ItemInnerSpacing.x;
			std::string label(static_cast<size_t>(indent / ImGui::CalcTextSize(" ").x) + 1, ' ');
			label += aLabel;
			bool clicked = ImGui::MenuItem(label.c_str());
			void* icon = Icons::Get(aMember.Profession, aMember.Elite);
			ImVec2 min = ImGui::GetItemRectMin();
			ImVec2 iconMin(min.x + ImGui::GetStyle().ItemInnerSpacing.x, min.y + (ImGui::GetItemRectSize().y - size) * 0.5f);
			if (icon)
			{
				ImGui::GetWindowDrawList()->AddImage(icon, iconMin, ImVec2(iconMin.x + size, iconMin.y + size),
					ImVec2(0, 0), ImVec2(1, 1), ImGui::GetColorU32(ProfessionColor(aMember.Profession)));
			}
			return clicked;
		}

		void AddMeItem(const Rezz::SessionView& aView)
		{
			if (aView.SelfAccount.empty()) { return; }
			if (std::find(aView.Order.begin(), aView.Order.end(), aView.SelfAccount) != aView.Order.end()) { return; }
			if (!ImGui::MenuItem("Add me")) { return; }
			std::vector<std::string> order = aView.Order;
			order.push_back(aView.SelfAccount);
			ApplyOrder(order);
		}

		void AddPlayerMenu(const Rezz::SessionView& aView)
		{
			if (!ImGui::BeginMenu("Add player")) { return; }
			std::vector<const Rezz::RosterMember*> candidates = Candidates(aView, false);
			if (candidates.empty()) { ImGui::TextColored(kGrey, "nobody left to add"); }
			for (const Rezz::RosterMember* member : candidates)
			{
				if (MenuItemWithIcon(*member, PlayerName(member->Account, member)))
				{
					std::vector<std::string> order = aView.Order;
					order.push_back(member->Account);
					ApplyOrder(order);
				}
			}
			ImGui::EndMenu();
		}

		bool IsAvailable(Rezz::Eligibility aStatus)
		{
			return aStatus == Rezz::Eligibility::Ready || aStatus == Rezz::Eligibility::Casting;
		}

		std::string SecondsText(uint64_t aMs)
		{
			return std::to_string((aMs + 999) / 1000) + "s";
		}

		void StatusText(const Rezz::OrderRow& aRow)
		{
			switch (aRow.Status)
			{
				// "ready?": nothing of theirs has been seen yet, so the skill could have been used just
				// before we could watch them (we joined, or they came back).
				case Rezz::Eligibility::Ready:    ImGui::TextColored(kGreen, aRow.Unconfirmed ? "ready?" : "ready"); break;
				case Rezz::Eligibility::Casting:  ImGui::TextColored(kYellow, "casting"); break;
				case Rezz::Eligibility::Cooldown: ImGui::TextColored(kGrey, "%s", SecondsText(aRow.ReadyInMs).c_str()); break;
				case Rezz::Eligibility::Downed:   ImGui::TextColored(kRed, "DOWN"); break;
				case Rezz::Eligibility::Dead:     ImGui::TextColored(kDarkRed, "DEAD"); break;
				case Rezz::Eligibility::Away:     ImGui::TextColored(kGrey, "away"); break;
			}
		}

		// Laid out like the options menu of an arcdps window: checkboxes, then a value box with its label.
		void Toggle(const char* aLabel, bool* aValue)
		{
			if (ImGui::Checkbox(aLabel, aValue)) { Settings::MarkDirty(); }
		}

		void ValueInt(const char* aLabel, int* aValue, int aMin, int aMax)
		{
			ImGui::SetNextItemWidth(60);
			ImGui::PushID(aLabel);
			if (ImGui::InputInt("##value", aValue, 0, 0))
			{
				*aValue = *aValue < aMin ? aMin : (*aValue > aMax ? aMax : *aValue);
				Settings::MarkDirty();
			}
			ImGui::PopID();
			ImGui::SameLine();
			ImGui::TextUnformatted(aLabel);
		}

		void ValueFloat(const char* aLabel, float* aValue, float aMin, float aMax)
		{
			int value = static_cast<int>(*aValue);
			ValueInt(aLabel, &value, static_cast<int>(aMin), static_cast<int>(aMax));
			*aValue = static_cast<float>(value);
		}

		void WindowOptions()
		{
			Settings::Values& s = Settings::Current;
			Toggle("title bar", &s.OverlayTitleBar);
			Toggle("scroll bar", &s.OverlayScrollBar);
			Toggle("background", &s.OverlayBackground);
			Toggle("locked (click-through)", &s.OverlayLocked);
			Toggle("only in WvW", &s.OverlayOnlyWvw);
			ValueFloat("window width", &s.OverlayWidth, 0, 2000);
			if (s.OverlayScrollBar) { ValueFloat("window height", &s.OverlayHeight, 40, 2000); }
			ValueInt("max name length", &s.OverlayMaxNameLength, 0, 40);
			ValueInt("max displayed", &s.OverlayMaxRows, 0, 50);
			ImGui::SetNextItemWidth(120);
			if (ImGui::SliderFloat("background alpha", &s.OverlayBgAlpha, 0.0f, 1.0f, "%.2f")) { Settings::MarkDirty(); }
			ImGui::SetNextItemWidth(120);
			if (ImGui::SliderFloat("text size", &s.OverlayScale, 0.7f, 2.5f, "%.1f")) { Settings::MarkDirty(); }
		}

		// Like an arcdps window: right-click > style > the options.
		void StyleMenu()
		{
			if (!ImGui::BeginMenu("style")) { return; }
			WindowOptions();
			ImGui::EndMenu();
		}

		bool OverlayContextMenu(const Rezz::SessionView& aView)
		{
			if (!ImGui::BeginPopupContextWindow("overlay_menu")) { return false; }
			AddMeItem(aView);
			AddPlayerMenu(aView);
			if (ImGui::MenuItem("Open order editor")) { ShowEditor = true; }
			if (ImGui::MenuItem("Hide overlay")) { Settings::Current.OverlayVisible = false; Settings::MarkDirty(); }
			ImGui::Separator();
			StyleMenu();
			ImGui::EndPopup();
			return true;
		}

		// The overlay keeps one size while the order stays the same: every column is as wide as its widest
		// possible value, and the banner line is always reserved.
		void RenderOverlay(const Rezz::SessionView& aView, const RosterIndex& aRoster, const Context& aContext)
		{
			Settings::Values& s = Settings::Current;
			if (!s.OverlayVisible || !aContext.IsGameplay || aContext.IsMapOpen) { return; }
			if (s.OverlayOnlyWvw && !aContext.InWvw) { return; }


			ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing |
				ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;
			if (s.OverlayScrollBar) { flags |= ImGuiWindowFlags_NoResize; }
			else { flags |= ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar; }
			// Holding Ctrl+Shift makes the overlay editable (and movable) even while it is locked.
			bool editMode = ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift;
			// A menu stays usable after the keys are released; it closes by clicking elsewhere.
			bool interactive = editMode || !s.OverlayLocked || s_MenuOpen;
			bool menuOpen = false;
			if (!s.OverlayTitleBar) { flags |= ImGuiWindowFlags_NoTitleBar; }
			if (!s.OverlayBackground) { flags |= ImGuiWindowFlags_NoBackground; }
			if (s.OverlayLocked && !interactive) { flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs; }

			ArcStyle::Push();
			ImGui::SetNextWindowPos(ImVec2(s.OverlayX, s.OverlayY), ImGuiCond_Appearing);
			if (s.OverlayScrollBar)
			{
				ImGui::SetNextWindowSize(ImVec2(s.OverlayWidth > 0 ? s.OverlayWidth : 240.0f, s.OverlayHeight), ImGuiCond_Always);
			}
			else if (s.OverlayWidth > 0)
			{
				// Fixed width, height still fits the rows.
				ImGui::SetNextWindowSizeConstraints(ImVec2(s.OverlayWidth, 0), ImVec2(s.OverlayWidth, FLT_MAX));
			}
			ImGui::SetNextWindowBgAlpha(s.OverlayBgAlpha);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, s.OverlayLocked && !interactive ? 0.0f : 1.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 5));
			bool open = ImGui::Begin(s.OverlayTitleBar ? "Rezz Order###overlay" : "###overlay", nullptr, flags);
			ImGui::PopStyleVar(2);
			if (!open) { ImGui::End(); ArcStyle::Pop(); return; }

			ImVec2 pos = ImGui::GetWindowPos();
			if (std::fabs(pos.x - s.OverlayX) > 0.5f || std::fabs(pos.y - s.OverlayY) > 0.5f)
			{
				s.OverlayX = pos.x;
				s.OverlayY = pos.y;
				Settings::MarkDirty();
			}

			Fonts::Push(s.OverlayScale);
			const Rezz::TurnView& turn = aView.Turn;
			float x0 = ImGui::GetCursorPosX();
			float spacing = ImGui::GetStyle().ItemSpacing.x * 1.5f;
			float lineHeight = ImGui::GetTextLineHeight();
			float contentWidth = ImGui::CalcTextSize("Rezz Order: no order yet.").x;

			if (aView.Order.empty())
			{
				ImGui::TextColored(kGrey, "Rezz Order: no players yet.");
				ImGui::TextColored(kGrey, s.OverlayLocked ? "Ctrl+Shift + right-click to add players"
					: "Right-click to add players");
			}
			else
			{
				std::vector<std::string> names;
				float nameWidth = 0.0f;
				int selfIndex = -1;
				for (int i = 0; i < static_cast<int>(turn.Rows.size()); i++)
				{
					const Rezz::OrderRow& row = turn.Rows[i];
					names.push_back(ShortName(PlayerName(row.Account, Find(aRoster, row.Account))));
					nameWidth = std::max(nameWidth, ImGui::CalcTextSize(names.back().c_str()).x);
					if (row.Account == aView.SelfAccount) { selfIndex = i; }
				}
				float markerWidth = std::max(ImGui::CalcTextSize("UP").x, ImGui::CalcTextSize("99").x);
				markerWidth = std::max(markerWidth, ImGui::CalcTextSize("BK").x);
				float statusWidth = 0.0f;
				for (const char* text : { "ready?", "casting", "120s", "DOWN", "DEAD", "away" })
				{
					statusWidth = std::max(statusWidth, ImGui::CalcTextSize(text).x);
				}
				float iconX = x0 + markerWidth + spacing;
				float nameX = iconX + lineHeight + spacing;
				float statusX = nameX + nameWidth + spacing;
				float rowWidth = statusX + statusWidth - x0;
				std::string longestName;
				for (const std::string& name : names) { if (name.size() > longestName.size()) { longestName = name; } }
				float nobodyWidth = ImGui::CalcTextSize(("Nobody ready - " + longestName + " in 120s").c_str()).x;
				contentWidth = std::max({ contentWidth, rowWidth, nobodyWidth, ImGui::CalcTextSize("YOUR TURN").x * 1.5f });

				// Banner line, always the height of the big font.
				float bannerHeight = std::round(lineHeight * 1.5f);
				ImVec2 bannerPos = ImGui::GetCursorPos();
				bool bigBanner = selfIndex >= 0 && (selfIndex == turn.UpIndex || selfIndex == aView.BackupIndex);
				if (bigBanner)
				{
					Fonts::Pop();
					Fonts::Push(s.OverlayScale, true);
					if (selfIndex == turn.UpIndex) { ImGui::TextColored(kGreen, "YOUR TURN"); }
					else { ImGui::TextColored(kOrange, "BACKUP"); }
					Fonts::Pop();
					Fonts::Push(s.OverlayScale);
				}
				else
				{
					ImGui::SetCursorPosY(bannerPos.y + (bannerHeight - lineHeight) * 0.5f);
					if (turn.UpIndex >= 0)
					{
						ImGui::TextColored(kGrey, "Up:");
						ImGui::SameLine();
						ImGui::TextUnformatted(names[turn.UpIndex].c_str());
					}
					else
					{
						ImGui::TextColored(kRed, "Nobody ready");
						if (turn.NextReadyIndex >= 0)
						{
							ImGui::SameLine();
							ImGui::TextColored(kGrey, "- %s in %s", names[turn.NextReadyIndex].c_str(),
								SecondsText(turn.Rows[turn.NextReadyIndex].ReadyInMs).c_str());
						}
					}
				}
				ImGui::SetCursorPos(ImVec2(bannerPos.x, bannerPos.y + bannerHeight + ImGui::GetStyle().ItemSpacing.y));

				// "max displayed" shows a window of the order starting at whoever is up, so the rows that
				// matter are always the visible ones. Numbers keep the player's real place in the order.
				int rowCount = static_cast<int>(turn.Rows.size());
				int shown = s.OverlayMaxRows > 0 ? std::min(rowCount, s.OverlayMaxRows) : rowCount;
				int first = s.OverlayMaxRows > 0 && turn.UpIndex >= 0 ? turn.UpIndex : 0;
				for (int offset = 0; offset < shown; offset++)
				{
					int i = (first + offset) % rowCount;
					const Rezz::OrderRow& row = turn.Rows[i];
					const Rezz::RosterMember* member = Find(aRoster, row.Account);
					bool isUp = i == turn.UpIndex;
					bool isBackup = i == aView.BackupIndex;
					ImGui::PushID(i);
					ImVec2 rowPos = ImGui::GetCursorPos();

					if (interactive)
					{
						// The whole row is a drag handle; the texts are drawn on top of it afterwards.
						ImGui::InvisibleButton("row", ImVec2(contentWidth, lineHeight));
						if (editMode && ImGui::BeginDragDropSource())
						{
							ImGui::SetDragDropPayload("REZZ_ORDER_ROW", &i, sizeof(i));
							ImGui::TextUnformatted(names[i].c_str());
							ImGui::EndDragDropSource();
						}
						if (ImGui::BeginDragDropTarget())
						{
							if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("REZZ_ORDER_ROW"))
							{
								MoveInOrder(aView.Order, *static_cast<const int*>(payload->Data), i);
							}
							ImGui::EndDragDropTarget();
						}
						if (editMode && ImGui::IsItemHovered())
						{
							ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 255, 255, 90));
						}
						if (ImGui::BeginPopupContextItem("row_menu"))
						{
							menuOpen = true;
							ImGui::TextColored(kGrey, "%s", names[i].c_str());
							ImGui::Separator();
							if (ImGui::MenuItem("Remove player")) { RemoveFromOrder(aView.Order, i); }
							NicknameMenu(row.Account);
							AddMeItem(aView);
							if (ImGui::MenuItem("Move up", nullptr, false, i > 0)) { MoveInOrder(aView.Order, i, i - 1); }
							if (ImGui::MenuItem("Move down", nullptr, false, i + 1 < static_cast<int>(turn.Rows.size()))) { MoveInOrder(aView.Order, i, i + 1); }
							AddPlayerMenu(aView);
							if (ImGui::MenuItem("Open order editor")) { ShowEditor = true; }
							ImGui::Separator();
							StyleMenu();
							ImGui::EndPopup();
						}
						ImGui::SetCursorPos(rowPos);
					}

					// Up and backup differ in fill, in the thickness of the edge bar and in their label, so
					// they can be told apart without seeing colour.
					if (isUp || isBackup)
					{
						ImVec2 min = ImGui::GetCursorScreenPos();
						ImVec2 max(min.x + contentWidth, min.y + lineHeight);
						ImU32 color = isUp ? IM_COL32(60, 170, 60, 110) : IM_COL32(200, 130, 30, 80);
						ImDrawList* draw = ImGui::GetWindowDrawList();
						draw->AddRectFilled(ImVec2(min.x - 3, min.y - 1), ImVec2(max.x + 3, max.y + 1), color, 2.0f);
						float edge = isUp ? 3.0f : 1.0f;
						ImU32 edgeColor = isUp ? IM_COL32(120, 230, 120, 230) : IM_COL32(240, 170, 60, 200);
						draw->AddRectFilled(ImVec2(min.x - 5, min.y - 1), ImVec2(min.x - 5 + edge, max.y + 1), edgeColor);
					}

					if (isUp) { ImGui::TextColored(kGreen, "UP"); }
					else if (isBackup) { ImGui::TextColored(kOrange, "BK"); }
					else { ImGui::TextColored(kGrey, "%d", i + 1); }

					// Silent through a whole squad fight: they are out of the range arcdps reports on, so
					// their state may be out of date.
					bool outOfRange = aView.SquadInCombat && member &&
						aView.NowMs > member->LastEventMs + Rezz::Session::kOutOfRangeMs;
					if (outOfRange)
					{
						ImGui::SameLine(0, 2);
						ImGui::TextColored(kGrey, "?");
						if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlapped))
						{
							ImGui::SetTooltip("no events from this player during the fight: out of range");
						}
					}

					ImGui::SameLine(iconX);
					ProfessionIcon(row.Account, member);

					ImGui::SameLine(nameX);
					const ImVec4& color = row.Account == aView.SelfAccount ? kYellow
						: (IsAvailable(row.Status) ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : kGrey);
					PlayerNameText(row.Account, names[i], color, member);

					ImGui::SameLine(statusX);
					StatusText(row);
					if (interactive) { ImGui::SetCursorPos(ImVec2(rowPos.x, rowPos.y + lineHeight + ImGui::GetStyle().ItemSpacing.y)); }
					ImGui::PopID();
				}
			}

			// Roster notices fade out after a while; long ones wrap to the overlay's width.
			while (!s_Notices.empty() && aContext.NowMs - s_Notices.front().TimeMs > kNoticeShowMs) { s_Notices.pop_front(); }
			ImGui::PushTextWrapPos(x0 + contentWidth);
			for (const TimedNotice& notice : s_Notices)
			{
				ImGui::TextColored(kOrange, "%s", notice.Text.c_str());
			}
			if (editMode)
			{
				ImGui::TextColored(kYellow, "edit: drag rows to reorder, right-click a row for more");
			}
			else if (!s.OverlayLocked)
			{
				ImGui::TextColored(kGrey, "drag to move, Ctrl+Shift to edit, right-click for options");
			}
			ImGui::PopTextWrapPos();
			// Keeps the width constant while the statuses change; not needed when the width is set by hand.
			if (s.OverlayWidth <= 0 && !s.OverlayScrollBar) { ImGui::Dummy(ImVec2(contentWidth, 0.0f)); }
			Fonts::Pop();

			if (interactive) { menuOpen = OverlayContextMenu(aView) || menuOpen; }
			s_MenuOpen = menuOpen;
			ImGui::End();
			ArcStyle::Pop();
		}

		void SquadColumn(const Rezz::SessionView& aView)
		{
			Settings::Values& s = Settings::Current;
			ImGui::TextUnformatted("Squad");
			ImGui::SameLine();
			if (ImGui::Checkbox("all professions", &s.EditorAllProfessions)) { Settings::MarkDirty(); }

			std::vector<const Rezz::RosterMember*> candidates = Candidates(aView, s.EditorAllProfessions);

			if (ImGui::Button("Add all with a revive skill seen"))
			{
				std::vector<std::string> order = aView.Order;
				for (const Rezz::RosterMember* member : candidates) { if (member->SeenGroups) { order.push_back(member->Account); } }
				ApplyOrder(order);
			}

			ImGui::BeginChild("squad_list", ImVec2(0, 0), true);
			if (candidates.empty())
			{
				ImGui::TextColored(kGrey, "Nobody to add. Squad members on your map");
				ImGui::TextColored(kGrey, "appear here (needs ArcDPS).");
			}
			for (const Rezz::RosterMember* member : candidates)
			{
				ImGui::PushID(member->Account.c_str());
				if (ImGui::SmallButton("+"))
				{
					std::vector<std::string> order = aView.Order;
					order.push_back(member->Account);
					ApplyOrder(order);
				}
				ImGui::SameLine();
				ProfessionIcon(member->Account, member);
				ImGui::SameLine();
				PlayerNameText(member->Account, PlayerName(member->Account, member),
					member->IsSelf ? kYellow : ImGui::GetStyleColorVec4(ImGuiCol_Text), member);
				if (member->Role == Rezz::SquadRole::Leader) { ImGui::SameLine(); ImGui::TextColored(kYellow, "[Lead]"); }
				else if (member->Role == Rezz::SquadRole::Lieutenant) { ImGui::SameLine(); ImGui::TextColored(kYellow, "[Lt]"); }
				if (!member->OnMap) { ImGui::SameLine(); ImGui::TextColored(kGrey, "(other map)"); }
				if (member->SeenGroups)
				{
					ImGui::SameLine();
					ImGui::TextColored(kGreen, "seen");
					if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", SeenSkills(member->SeenGroups).c_str()); }
				}
				ImGui::PopID();
			}
			ImGui::EndChild();
		}

		void OrderColumn(const Rezz::SessionView& aView, const RosterIndex& aRoster)
		{
			ImGui::TextUnformatted("Revive order");
			ImGui::SameLine();
			ImGui::TextColored(kGrey, "(drag a row to reorder)");

			ImGui::TextColored(kGrey, "The backup is whoever is ready next after the player who is up.");

			std::vector<std::string> order = aView.Order;
			bool changed = false;
			ImGui::BeginChild("order_list", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true);
			if (order.empty()) { ImGui::TextColored(kGrey, "Add players from the squad list."); }
			float rowHeight = ImGui::GetFrameHeight();
			float lineHeight = ImGui::GetTextLineHeight();
			for (int i = 0; i < static_cast<int>(order.size()); i++)
			{
				const std::string account = order[i];
				const Rezz::RosterMember* member = Find(aRoster, account);
				ImGui::PushID(account.c_str());

				// The whole row is the drag handle; the buttons on it still take clicks.
				ImVec2 rowPos = ImGui::GetCursorPos();
				ImGui::Selectable("##row", false, ImGuiSelectableFlags_AllowItemOverlap, ImVec2(0, rowHeight));
				ImGui::SetItemAllowOverlap();
				if (ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload("REZZ_ORDER_ROW", &i, sizeof(i));
					ImGui::Text("%s", PlayerName(account, member).c_str());
					ImGui::EndDragDropSource();
				}
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("REZZ_ORDER_ROW"))
					{
						int from = *static_cast<const int*>(payload->Data);
						if (from != i && from >= 0 && from < static_cast<int>(order.size()))
						{
							std::string moved = order[from];
							order.erase(order.begin() + from);
							order.insert(order.begin() + i, moved);
							changed = true;
						}
					}
					ImGui::EndDragDropTarget();
				}

				ImGui::SetCursorPos(ImVec2(rowPos.x + 4, rowPos.y + (rowHeight - lineHeight) * 0.5f));
				ImGui::Text("%2d.", i + 1);
				ImGui::SameLine();
				ProfessionIcon(account, member);
				ImGui::SameLine();
				PlayerNameText(account, PlayerName(account, member),
					member && member->IsSelf ? kYellow : ImGui::GetStyleColorVec4(ImGuiCol_Text), member);
				if (member == nullptr || !member->OnMap) { ImGui::SameLine(); ImGui::TextColored(kGrey, "(not on map)"); }
				else if (member->Profession != 0 && !Rezz::IsReviveProfession(member->Profession))
				{
					ImGui::SameLine();
					ImGui::TextColored(kRed, "(no revive skill)");
				}

				float right = ImGui::GetWindowContentRegionMax().x - 86;
				ImGui::SameLine(right);
				if (ImGui::SmallButton("N")) { ImGui::OpenPopup("nickname"); }
				if (ImGui::IsItemHovered()) { ImGui::SetTooltip("nickname"); }
				if (ImGui::BeginPopup("nickname"))
				{
					char buffer[64] = {};
					if (const std::string* nickname = Nickname(account)) { strncpy_s(buffer, nickname->c_str(), _TRUNCATE); }
					ImGui::TextColored(kGrey, "Show %s as:", Rezz::DisplayAccount(account).c_str());
					ImGui::SetNextItemWidth(160);
					if (ImGui::InputText("##nickname", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
					{
						SetNickname(account, buffer);
						ImGui::CloseCurrentPopup();
					}
					TextInputActive = TextInputActive || ImGui::IsItemActive();
					if (ImGui::Button("Clear")) { SetNickname(account, ""); ImGui::CloseCurrentPopup(); }
					ImGui::EndPopup();
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("^") && i > 0) { std::swap(order[i], order[i - 1]); changed = true; }
				ImGui::SameLine();
				if (ImGui::SmallButton("v") && i + 1 < static_cast<int>(order.size())) { std::swap(order[i], order[i + 1]); changed = true; }
				ImGui::SameLine();
				if (ImGui::SmallButton("x")) { order.erase(order.begin() + i); changed = true; }
				ImGui::SetCursorPos(ImVec2(rowPos.x, rowPos.y + rowHeight + ImGui::GetStyle().ItemSpacing.y));

				ImGui::PopID();
				if (changed) { break; }
			}
			ImGui::EndChild();
			if (changed) { ApplyOrder(order); }

			if (ImGui::Button("Clear order")) { ImGui::OpenPopup("clear_order"); }
			if (ImGui::BeginPopup("clear_order"))
			{
				ImGui::TextUnformatted("Remove everyone from the order?");
				if (ImGui::Button("Clear")) { ApplyOrder({}); ImGui::CloseCurrentPopup(); }
				ImGui::SameLine();
				if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); }
				ImGui::EndPopup();
			}
		}

		void RenderEditor(const Rezz::SessionView& aView, const RosterIndex& aRoster, const Context& aContext)
		{
			if (!ShowEditor || !aContext.IsGameplay) { return; }

			ArcStyle::Push();
			ImGui::SetNextWindowSize(ImVec2(720, 440), ImGuiCond_FirstUseEver);
			if (ImGui::Begin(kEditorName, &ShowEditor))
			{
				size_t onMap = 0, revive = 0;
				for (const Rezz::RosterMember& member : aView.Roster)
				{
					if (!member.OnMap) { continue; }
					onMap++;
					if (Rezz::IsReviveProfession(member.Profession)) { revive++; }
				}
				ImGui::TextColored(kGrey, "You: %s | squad on your map: %zu, %zu on a profession with a revive skill",
					aView.SelfAccount.empty() ? "(unknown yet)" : Rezz::DisplayAccount(aView.SelfAccount).c_str(), onMap, revive);

				if (ImGui::BeginTable("editor_columns", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV,
					ImGui::GetContentRegionAvail()))
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					SquadColumn(aView);
					ImGui::TableNextColumn();
					OrderColumn(aView, aRoster);
					ImGui::EndTable();
				}
			}
			ImGui::End();
			ArcStyle::Pop();
		}
	}

	void Init()
	{
		Live::SetOrder(Settings::Current.Order);
	}

	void AddNotice(const std::string& aText, unsigned aNowMs)
	{
		s_Notices.push_back(TimedNotice{ aText, aNowMs });
		while (s_Notices.size() > kNoticeLimit) { s_Notices.pop_front(); }
	}

	void OnOrderCleared()
	{
		Settings::Current.Order.clear();
		Settings::MarkDirty();
	}

	void Render(const Context& aContext)
	{
		if (EditorToggleRequested.exchange(false)) { ShowEditor = !ShowEditor; }
		if (LockToggleRequested.exchange(false))
		{
			Settings::Current.OverlayLocked = !Settings::Current.OverlayLocked;
			Settings::MarkDirty();
		}

		ArcStyle::Update(aContext.NowMs);
		Fonts::Update(Settings::Current.OverlayScale, aContext.NowMs);
		TextInputActive = false;
		Rezz::SessionView view = Live::GetView();
		if (view.Order != Settings::Current.Order)
		{
			Settings::Current.Order = view.Order;
			Settings::MarkDirty();
		}
		CountCharacterNames(view);
		RosterIndex roster = IndexRoster(view);
		RenderOverlay(view, roster, aContext);
		RenderEditor(view, roster, aContext);
		Settings::Flush(aContext.NowMs);
	}

	void Options()
	{
		Settings::Values& s = Settings::Current;
		ImGui::TextUnformatted("Revive order");
		if (ImGui::Button("Open order editor (Ctrl+Shift+O)")) { ShowEditor = true; }
		if (ImGui::Checkbox("Show overlay", &s.OverlayVisible)) { Settings::MarkDirty(); }
		if (ImGui::Checkbox("Lock overlay: can't be moved, clicks pass through (Ctrl+Shift+L)", &s.OverlayLocked)) { Settings::MarkDirty(); }
		if (ImGui::Checkbox("Only show on WvW maps", &s.OverlayOnlyWvw)) { Settings::MarkDirty(); }
		if (ImGui::Checkbox("Title bar", &s.OverlayTitleBar)) { Settings::MarkDirty(); }
		ImGui::SetNextItemWidth(200);
		if (ImGui::SliderFloat("Overlay background", &s.OverlayBgAlpha, 0.0f, 1.0f, "%.2f")) { Settings::MarkDirty(); }
		ImGui::SetNextItemWidth(200);
		if (ImGui::SliderFloat("Overlay size", &s.OverlayScale, 0.7f, 2.5f, "%.1f")) { Settings::MarkDirty(); }
		if (ImGui::Checkbox("Match ArcDPS appearance (colours, padding, font size)", &s.MatchArcDps)) { Settings::MarkDirty(); }
		if (s.MatchArcDps)
		{
			const ArcStyle::Values& arc = ArcStyle::Get();
			if (arc.Ok) { ImGui::TextColored(kGrey, "Read from %s", ArcStyle::SourcePath()); }
			else { ImGui::TextColored(kGrey, "arcdps.ini not found yet: using the Nexus look."); }
		}

		static const char* kFontNames[] = { "Nexus font", "ArcDPS font", "Font file..." };
		int font = static_cast<int>(s.Font);
		ImGui::SetNextItemWidth(200);
		if (ImGui::Combo("Overlay font", &font, kFontNames, IM_ARRAYSIZE(kFontNames)))
		{
			s.Font = static_cast<Settings::FontSource>(font);
			Settings::MarkDirty();
		}
		if (s.Font == Settings::FontSource::File)
		{
			char path[260] = {};
			strncpy_s(path, s.FontFile.c_str(), _TRUNCATE);
			ImGui::SetNextItemWidth(400);
			if (ImGui::InputText("TTF file", path, sizeof(path))) { s.FontFile = path; Settings::MarkDirty(); }
		}
		else if (s.Font == Settings::FontSource::ArcDps)
		{
			ImGui::TextColored(kGrey, "Uses arcdps_font.ttf from the game folder, or the font ArcDPS shows by default.");
		}
		if (ImGui::Checkbox("Always use account names", &s.PreferAccount)) { Settings::MarkDirty(); }
		ImGui::TextColored(kGrey, "Otherwise character names are used, except in Edge of the Mists where players from other worlds show a WvW rank.");
		if (ImGui::Checkbox("Alert when a player in the order leaves or swaps profession", &s.AlertOnChanges)) { Settings::MarkDirty(); }
		ImGui::TextColored(kGrey, "Hold Ctrl+Shift over the overlay to reorder it directly, even while it is locked.");
		ImGui::TextColored(kGrey, "Squad events reach addons ~2.6 s late (ArcDPS design): states change a moment after they happen in game.");
	}
}
