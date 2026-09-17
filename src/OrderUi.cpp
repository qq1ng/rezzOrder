#include "OrderUi.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <unordered_map>

#include "imgui/imgui.h"

#include "ArcStyle.h"
#include "Banner.h"
#include "Demo.h"
#include "Fonts.h"
#include "Icons.h"
#include "Live.h"
#include "Notify.h"
#include "Settings.h"
#include "Timing.h"
#include "Share.h"

namespace OrderUi
{
	bool              ShowEditor = false;
	FrameInfo         LastFrame;
	FrameInfo         LastEditorFrame;
	FrameInfo         LastShareFrame;
	FrameInfo         LastRequestFrame;
	int               ShotMenu = -1;
	std::atomic<bool> TextInputActive{false};
	std::atomic<bool> EditorToggleRequested{false};
	std::atomic<bool> LockToggleRequested{false};
	std::atomic<bool> CopyOrderRequested{false};

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
		// Downed and dead players: they cannot revive anyone and they are not coming back into the rotation
		// on their own, so they sink into the background instead of shouting in red.
		const ImVec4 kDim     { 0.45f, 0.45f, 0.48f, 1.0f };
		// Somebody else's turn is the same idea in a quieter shade: green at full strength always means the
		// order has come round to you.
		const ImVec4 kGreenOther { 0.52f, 0.72f, 0.54f, 1.0f };

		ImU32 UpFill(bool aMine)  { return aMine ? IM_COL32(60, 185, 60, 130) : IM_COL32(58, 105, 68, 85); }
		ImU32 UpEdge(bool aMine)  { return aMine ? IM_COL32(120, 240, 120, 245) : IM_COL32(95, 150, 105, 185); }

		struct TimedNotice
		{
			std::string Text;
			unsigned    TimeMs;
		};

		std::deque<TimedNotice> s_Notices;
		unsigned s_LastNowMs = 0;
		bool s_MenuOpen = false; // one of our popups was open last frame: keep taking input while it is
		bool s_GripHeld = false; // the corner grip is being dragged
		bool s_GripHot  = false; // ... or hovered: the window must not move out from under it

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
			if (limit <= 0) { return aName; }
			// Counted in characters, not bytes, so an accented name is not cut through the middle of a letter.
			int characters = 0;
			for (size_t i = 0; i < aName.size(); i++)
			{
				if ((static_cast<unsigned char>(aName[i]) & 0xC0) == 0x80) { continue; }
				if (characters == limit) { aName.resize(i); break; }
				characters++;
			}
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
		// Faded along with the rest of the row for a player who is out of the rotation.
		ImVec4 IconTint(uint32_t aProfession, bool aDim)
		{
			ImVec4 color = ProfessionColor(aProfession);
			if (aDim) { color.x *= 0.55f; color.y *= 0.55f; color.z *= 0.55f; color.w = 0.65f; }
			return color;
		}

		void ProfessionIcon(const std::string& aAccount, const Rezz::RosterMember* aMember, bool aDim = false)
		{
			uint32_t profession = aMember ? aMember->Profession : 0;
			float size = ImGui::GetTextLineHeight();
			if (void* icon = Icons::Get(profession, aMember ? aMember->Elite : 0))
			{
				ImGui::Image(icon, ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1), IconTint(profession, aDim));
			}
			else if (profession != 0)
			{
				ImGui::TextColored(IconTint(profession, aDim), "%s", Rezz::ProfessionShortName(profession));
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

		// Toggling it keeps the player where they are in the order.
		void TogglePrecast(const Rezz::SessionView& aView, const std::string& aAccount)
		{
			if (Rezz::Demo::Running())
			{
				AddNotice("demo squad: stop the demo to change the real order", s_LastNowMs);
				return;
			}
			std::vector<std::string>& precast = Settings::Current.Precast;
			auto it = std::find(precast.begin(), precast.end(), aAccount);
			if (it == precast.end()) { precast.push_back(aAccount); }
			else { precast.erase(it); }
			Live::SetPrecast(precast);
			Settings::MarkDirty();
			(void)aView;
		}

		void ApplyOrder(std::vector<std::string> aOrder)
		{
			// The demo squad is on screen with its own made-up players. Editing what is shown would write
			// those names into the real order, so the demo is look-only.
			if (Rezz::Demo::Running())
			{
				AddNotice("demo squad: stop the demo to change the real order", s_LastNowMs);
				return;
			}
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

		// Squad members with a revive profession who aren't in the order yet, in subgroup order (unknown last).
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
			// Stable: within a subgroup the roster's own order (revive professions first, then by name) stays.
			auto group = [](const Rezz::RosterMember* aMember) { return aMember->Subgroup == 0 ? 1000 : static_cast<int>(aMember->Subgroup); };
			std::stable_sort(candidates.begin(), candidates.end(),
				[&](const Rezz::RosterMember* a, const Rezz::RosterMember* b) { return group(a) < group(b); });
			return candidates;
		}

		// The elite specialization's name, or the profession's for a core build. Ids from the GW2 API.
		const char* SpecName(uint32_t aProfession, uint32_t aElite)
		{
			switch (aElite)
			{
				case 5:  return "Druid";         case 55: return "Soulbeast";    case 72: return "Untamed";      case 78: return "Galeshot";
				case 18: return "Berserker";     case 61: return "Spellbreaker"; case 68: return "Bladesworn";   case 74: return "Paragon";
				case 27: return "Dragonhunter";  case 62: return "Firebrand";    case 65: return "Willbender";   case 81: return "Luminary";
				case 40: return "Chronomancer";  case 59: return "Mirage";       case 66: return "Virtuoso";     case 73: return "Troubadour";
				case 48: return "Tempest";       case 56: return "Weaver";       case 67: return "Catalyst";     case 80: return "Evoker";
				case 34: return "Reaper";        case 60: return "Scourge";      case 64: return "Harbinger";    case 76: return "Ritualist";
				case 43: return "Scrapper";      case 57: return "Holosmith";    case 70: return "Mechanist";    case 75: return "Amalgam";
				case 7:  return "Daredevil";     case 58: return "Deadeye";      case 71: return "Specter";      case 77: return "Antiquary";
				case 52: return "Herald";        case 63: return "Renegade";     case 69: return "Vindicator";   case 79: return "Conduit";
				default: break;
			}
			static constexpr const char* kCore[] = { "Unknown", "Guardian", "Warrior", "Engineer", "Ranger", "Thief",
				"Elementalist", "Mesmer", "Necromancer", "Revenant" };
			return aProfession < std::size(kCore) ? kCore[aProfession] : "Unknown";
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

		// Named orders: the same ten people run together every night, so the order is worth keeping.
		void SavePreset(const std::string& aName, const std::vector<std::string>& aOrder)
		{
			if (aName.empty() || aOrder.empty()) { return; }
			if (Rezz::Demo::Running())
			{
				AddNotice("demo squad: stop the demo before saving an order", s_LastNowMs);
				return;
			}
			Settings::Current.Presets[aName] = aOrder;
			Settings::MarkDirty();
		}

		// Type a name, press enter, and the order is kept under it.
		void SavePresetField(const Rezz::SessionView& aView, const char* aId)
		{
			char buffer[48] = {};
			ImGui::SetNextItemWidth(140);
			ImGui::PushID(aId);
			bool entered = ImGui::InputTextWithHint("##preset_name", "name...", buffer, sizeof(buffer),
				ImGuiInputTextFlags_EnterReturnsTrue);
			TextInputActive = TextInputActive || ImGui::IsItemActive();
			ImGui::PopID();
			if (entered) { SavePreset(buffer, aView.Order); }
		}

		void PresetMenu(const Rezz::SessionView& aView)
		{
			Settings::Values& s = Settings::Current;
			if (!ImGui::BeginMenu("Saved orders")) { return; }
			if (s.Presets.empty()) { ImGui::TextColored(kGrey, "none saved yet"); }
			for (const auto& [name, accounts] : s.Presets)
			{
				ImGui::PushID(name.c_str());
				if (ImGui::MenuItem(name.c_str(), std::to_string(accounts.size()).c_str())) { ApplyOrder(accounts); }
				if (ImGui::BeginPopupContextItem("preset_menu"))
				{
					if (ImGui::MenuItem("Delete")) { s.Presets.erase(name); Settings::MarkDirty(); ImGui::EndPopup(); ImGui::PopID(); break; }
					ImGui::EndPopup();
				}
				ImGui::PopID();
			}
			if (!aView.Order.empty())
			{
				ImGui::Separator();
				ImGui::TextColored(kGrey, "save this order as:");
				SavePresetField(aView, "menu");
			}
			ImGui::EndMenu();
		}

		// Addons cannot write in chat, so the line goes to the clipboard and the player pastes it.
		// Takes the order explicitly, so the copy can include a player who is being added in the same click
		// (the view still holds the order from before this frame).
		void CopyOrderLine(const std::vector<std::string>& aOrder, const std::vector<Rezz::RosterMember>& aRoster)
		{
			if (aOrder.empty())
			{
				AddNotice("no order to share yet", s_LastNowMs);
				return;
			}
			std::string line = Rezz::Share::Encode(aOrder, Settings::Current.Precast, aRoster,
				static_cast<uint16_t>(Settings::Current.Bench));
			ImGui::SetClipboardText(line.c_str());
			AddNotice("copied: " + line + " - paste it in squad chat", s_LastNowMs);
			Live::ClearRequest();
		}

		void CopyOrder(const Rezz::SessionView& aView)
		{
			CopyOrderLine(aView.Order, aView.Roster);
		}

		void CopyOrderItem(const Rezz::SessionView& aView)
		{
			if (aView.Order.empty()) { return; }
			if (ImGui::MenuItem("Copy order for squad chat", "Ctrl+Shift+K")) { CopyOrder(aView); }
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

		// Whether this player still has a part to play: a recharging skill comes back and their turn comes
		// round again, while a downed, dead or absent player is simply skipped.
		bool IsRelevant(Rezz::Eligibility aStatus)
		{
			return IsAvailable(aStatus) || aStatus == Rezz::Eligibility::Cooldown;
		}

		std::string SecondsText(uint64_t aMs)
		{
			return std::to_string((aMs + 999) / 1000) + "s";
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

		// The three shapes the turn window can take, in the order they were drawn as mockups.
		void LayoutOptions()
		{
			Settings::Values& s = Settings::Current;
			static const char* kNames[] = { "compact list", "big bars", "focus card", "horizontal strip", "next up" };
			int layout = static_cast<int>(s.Layout);
			ImGui::SetNextItemWidth(120);
			if (ImGui::Combo("layout", &layout, kNames, IM_ARRAYSIZE(kNames)))
			{
				s.Layout = static_cast<Settings::OverlayLayout>(layout);
				Settings::MarkDirty();
			}
			Toggle("recharge bar", &s.CooldownBar);
			Toggle("recharge seconds", &s.CooldownSeconds);
		}

		// Every setting of the turn window, in one place. The right-click menu and the Nexus options page
		// both show this, so there is one control per thing and no two of them can disagree.
		void WindowOptions()
		{
			Settings::Values& s = Settings::Current;
			LayoutOptions();
			ImGui::Separator();
			Toggle("show the window", &s.OverlayVisible);
			Toggle("locked (click-through)", &s.OverlayLocked);
			Toggle("only in WvW", &s.OverlayOnlyWvw);
			Toggle("title bar", &s.OverlayTitleBar);
			Toggle("background", &s.OverlayBackground);
			ImGui::SetNextItemWidth(120);
			if (ImGui::SliderFloat("background alpha", &s.OverlayBgAlpha, 0.0f, 1.0f, "%.2f")) { Settings::MarkDirty(); }
			// One size control: with no width set this scales the whole window, which is what dragging the
			// corner grip does as well.
			ImGui::SetNextItemWidth(120);
			if (ImGui::SliderFloat("size", &s.OverlayScale, 0.7f, 2.5f, "%.1f")) { Settings::MarkDirty(); }
			ImGui::Separator();
			// A width is a floor, not a cage: the window still grows when the text needs the room, so nothing
			// is ever cut off at a bigger size.
			ValueFloat("least width (0: fit)", &s.OverlayWidth, 0, 2000);
			ValueInt("max name length", &s.OverlayMaxNameLength, 0, 40);
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", "How much room a name gets. The window keeps its size, so a longer name is cut to fit.");
			}
			ValueInt("max displayed", &s.OverlayMaxRows, 0, 50);
			static const char* kMessageSides[] = { "below the window", "above the window", "don't show" };
			ImGui::SetNextItemWidth(140);
			if (ImGui::Combo("messages", &s.OverlayMessages, kMessageSides, IM_ARRAYSIZE(kMessageSides))) { Settings::MarkDirty(); }
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", "Where short-lived messages appear, like somebody leaving the squad. They get a strip of "
					"their own, so the window itself never changes size: put it on the side that has room.");
			}
		}

		// Like an arcdps window: right-click > style > the options.
		void StyleMenu()
		{
			if (!ImGui::BeginMenu("style")) { return; }
			WindowOptions();
			ImGui::EndMenu();
		}

		// In game a menu opens under the cursor. The harness has no cursor, so for a screenshot it is put
		// beside the window instead of on top of the rows it is meant to illustrate.
		void PlaceMenuForShot()
		{
			if (ShotMenu == -1) { return; }
			ImVec2 pos = ImGui::GetWindowPos();
			ImGui::SetNextWindowPos(ImVec2(pos.x + ImGui::GetWindowSize().x + 8.0f, pos.y));
		}

		// Where an open menu landed, so a screenshot can take in the window and its menu together.
		void NoteMenuRect()
		{
			LastFrame.MenuX = ImGui::GetWindowPos().x;
			LastFrame.MenuY = ImGui::GetWindowPos().y;
			LastFrame.MenuWidth = ImGui::GetWindowSize().x;
			LastFrame.MenuHeight = ImGui::GetWindowSize().y;
		}

		bool OverlayContextMenu(const Rezz::SessionView& aView)
		{
			if (ShotMenu == -2) { ImGui::OpenPopup("overlay_menu"); PlaceMenuForShot(); }
			// NoOpenOverItems, or right-clicking a player would open this as well as their own menu, and this
			// one is drawn last so it would be the one that wins. Rows are items, so they keep their menu and
			// the empty space around them opens this one.
			if (!ImGui::BeginPopupContextWindow("overlay_menu",
				ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
			{
				return false;
			}
			NoteMenuRect();
			AddMeItem(aView);
			AddPlayerMenu(aView);
			PresetMenu(aView);
			CopyOrderItem(aView);
			if (ImGui::MenuItem("Open order editor")) { ShowEditor = true; }
			if (ImGui::MenuItem("Hide overlay")) { Settings::Current.OverlayVisible = false; Settings::MarkDirty(); }
			ImGui::Separator();
			StyleMenu();
			ImGui::EndPopup();
			return true;
		}

		// ------------------------------------------------------------------ shared row drawing
		//
		// The three layouts (compact list, big bars, focus card) differ only in how a row is drawn. Names,
		// icons, markers, the drag handle and the row menu are shared, so a change to any of them shows up
		// in all three.

		// A font at a size other than the overlay's own. Fonts::Pop() resets the window font scale, so the
		// overlay font has to be pushed again afterwards.
		struct FontRef
		{
			ImFont* Font = nullptr;
			float   Size = 0.0f;

			ImVec2 Measure(const char* aText) const { return Font->CalcTextSizeA(Size, FLT_MAX, 0.0f, aText); }
		};

		FontRef PickFont(float aScale, bool aBig = false)
		{
			float base = Settings::Current.OverlayScale;
			Fonts::Pop();
			Fonts::Push(base * aScale, aBig);
			FontRef ref{ ImGui::GetFont(), ImGui::GetFontSize() };
			Fonts::Pop();
			Fonts::Push(base);
			return ref;
		}

		void DrawText(const FontRef& aFont, ImVec2 aPos, const ImVec4& aColor, const char* aText)
		{
			ImGui::GetWindowDrawList()->AddText(aFont.Font, aFont.Size, aPos, ImGui::GetColorU32(aColor), aText);
		}

		FontRef CurrentFont() { return FontRef{ ImGui::GetFont(), ImGui::GetFontSize() }; }

		// ------------------------------------------------------------------ fixed widths
		//
		// The turn window is sized from the settings and never from what it shows right now. Players join and
		// leave, get nicknames and take turns, and each of those used to change the width; the window sits
		// packed in beside other windows at a screen edge, so any change pushed it into them or off the screen
		// (field test 2026-09-16). A name that doesn't fit its column is cut to fit instead.

		constexpr int kDefaultNameChars = 20; // "full names": about as long as a character name gets

		// Takes the last character off, a whole UTF-8 sequence at a time: names can carry accents, and
		// cutting through the middle of one would show a replacement glyph.
		void PopCharacter(std::string& aText)
		{
			while (!aText.empty())
			{
				unsigned char last = static_cast<unsigned char>(aText.back());
				aText.pop_back();
				if ((last & 0xC0) != 0x80) { return; }
			}
		}

		// Room for one name in aFont: the configured name length, in letters of average width.
		float NameColumn(const FontRef& aFont)
		{
			int chars = Settings::Current.OverlayMaxNameLength > 0 ? Settings::Current.OverlayMaxNameLength : kDefaultNameChars;
			static constexpr const char kSample[] = "Sereth Vale Kalden Roth Brisa Thornwood Orrin Kade";
			float perChar = aFont.Measure(kSample).x / static_cast<float>(sizeof(kSample) - 1);
			return std::ceil(perChar * static_cast<float>(chars));
		}

		// aText cut down to aWidth, ending in a dot when anything was taken off. A precast star survives the
		// cut, since it means something.
		std::string Fit(std::string aText, float aWidth, const FontRef& aFont)
		{
			if (aFont.Measure(aText.c_str()).x <= aWidth) { return aText; }
			bool precast = !aText.empty() && aText.back() == Rezz::Share::kPrecastMark;
			if (precast) { aText.pop_back(); }
			const char* tail = precast ? ".*" : ".";
			while (!aText.empty() && aFont.Measure((aText + tail).c_str()).x > aWidth) { PopCharacter(aText); }
			// A cut that lands between two words would leave "always ." with the dot floating.
			while (!aText.empty() && aText.back() == ' ') { aText.pop_back(); }
			return aText + tail;
		}

		// Everything the layouts need about the frame being drawn.
		struct Frame
		{
			const Rezz::SessionView* View        = nullptr;
			const RosterIndex*       Roster      = nullptr;
			std::vector<std::string> Names;                  // display names, cut to the layout's name column
			std::vector<std::string> FullNames;              // the same before cutting, for a card or header
			int                      SelfIndex   = -1;
			bool                     Interactive = false;
			bool                     EditMode    = false;
			bool                     MenuOpen    = false;     // a row menu is open: keep taking input
			float                    Line        = 0.0f;      // text line height at the overlay's font
			float                    Space       = 0.0f;      // gap between columns
		};

		const Rezz::OrderRow& Row(const Frame& aFrame, int aIndex) { return aFrame.View->Turn.Rows[aIndex]; }

		// Cuts every row's name to the layout's name column. Each layout calls it once with its own font.
		void FitNames(Frame& aFrame, const FontRef& aFont, float aWidth)
		{
			for (size_t i = 0; i < aFrame.Names.size(); i++) { aFrame.Names[i] = Fit(aFrame.FullNames[i], aWidth, aFont); }
		}

		const Rezz::RosterMember* Member(const Frame& aFrame, int aIndex)
		{
			return Find(*aFrame.Roster, Row(aFrame, aIndex).Account);
		}

		bool IsUp(const Frame& aFrame, int aIndex)     { return aIndex == aFrame.View->Turn.UpIndex; }

		// Who the card at the top stands for: the player who is up, or with nobody up the one who will be ready
		// first. Layouts leave that player out of the rows under the card, so the row count doesn't change
		// with whether anybody is ready. -1 when there is nobody for it at all.
		int CardIndex(const Frame& aFrame)
		{
			const Rezz::TurnView& turn = aFrame.View->Turn;
			return turn.UpIndex >= 0 ? turn.UpIndex : turn.NextReadyIndex;
		}

		// A precast player may spend their revive before their turn comes, on their own call. They are in the
		// order like everybody else; this only says so.
		bool IsPrecast(const Frame& aFrame, int aIndex)
		{
			const std::vector<std::string>& precast = aFrame.View->Precast;
			return std::find(precast.begin(), precast.end(), Row(aFrame, aIndex).Account) != precast.end();
		}
		bool IsBackup(const Frame& aFrame, int aIndex) { return aIndex == aFrame.View->BackupIndex; }

		ImVec4 NameColor(const Frame& aFrame, int aIndex)
		{
			const Rezz::OrderRow& row = Row(aFrame, aIndex);
			if (row.Account == aFrame.View->SelfAccount) { return kYellow; }
			return IsRelevant(row.Status) ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : kDim;
		}

		// "ready", "ready?" (nothing of theirs seen yet), "casting", "63s", "DOWN", "DEAD", "away".
		// Empty while a cooldown is only shown as a bar.
		std::string StatusLabel(const Rezz::OrderRow& aRow)
		{
			switch (aRow.Status)
			{
				case Rezz::Eligibility::Ready:    return aRow.Unconfirmed ? "ready?" : "ready";
				case Rezz::Eligibility::Casting:  return "casting";
				case Rezz::Eligibility::Cooldown: return Settings::Current.CooldownSeconds ? SecondsText(aRow.ReadyInMs) : std::string();
				case Rezz::Eligibility::Downed:   return "DOWN";
				case Rezz::Eligibility::Dead:     return "DEAD";
				case Rezz::Eligibility::Away:     return "away";
			}
			return {};
		}

		ImVec4 StatusColor(const Rezz::OrderRow& aRow)
		{
			switch (aRow.Status)
			{
				case Rezz::Eligibility::Ready:    return kGreen;
				case Rezz::Eligibility::Casting:  return kYellow;
				case Rezz::Eligibility::Cooldown: return kRed;   // counts down back into the rotation
				default: return kDim;                            // downed, dead, away: out of it
			}
		}

		std::string MarkerLabel(const Frame& aFrame, int aIndex)
		{
			if (IsUp(aFrame, aIndex)) { return "UP"; }
			if (IsBackup(aFrame, aIndex)) { return "BK"; }
			return std::to_string(aIndex + 1);
		}

		ImVec4 MarkerColor(const Frame& aFrame, int aIndex)
		{
			if (IsUp(aFrame, aIndex)) { return aIndex == aFrame.SelfIndex ? kGreen : kGreenOther; }
			if (IsBackup(aFrame, aIndex)) { return kOrange; }
			return kGrey;
		}

		// How much of the recharge is left, 0 (ready) to 1 (just used).
		float CooldownLeft(const Rezz::OrderRow& aRow)
		{
			if (aRow.Status != Rezz::Eligibility::Cooldown || aRow.RechargeMs == 0) { return 0.0f; }
			float left = static_cast<float>(aRow.ReadyInMs) / static_cast<float>(aRow.RechargeMs);
			return left < 0.0f ? 0.0f : (left > 1.0f ? 1.0f : left);
		}

		// The recharge as a bar that fills up as the skill comes back. Blue, so it is neither the green of
		// "ready" nor the orange of the backup.
		void CooldownFill(const Rezz::OrderRow& aRow, const ImVec2& aMin, const ImVec2& aMax, float aRounding)
		{
			if (!Settings::Current.CooldownBar) { return; }
			float left = CooldownLeft(aRow);
			if (left <= 0.0f) { return; }
			ImDrawList* draw = ImGui::GetWindowDrawList();
			draw->AddRectFilled(aMin, aMax, IM_COL32(255, 255, 255, 18), aRounding);
			float width = (aMax.x - aMin.x) * (1.0f - left);
			if (width < 1.0f) { return; }
			draw->AddRectFilled(aMin, ImVec2(aMin.x + width, aMax.y), IM_COL32(64, 110, 160, 210), aRounding);
		}

		// Up and backup differ in fill, in the thickness of the left edge and in their label, so they can be
		// told apart without seeing colour. Our own row is marked on the right.
		void HighlightRow(const Frame& aFrame, int aIndex, const ImVec2& aMin, const ImVec2& aMax, float aRounding)
		{
			ImDrawList* draw = ImGui::GetWindowDrawList();
			bool up = IsUp(aFrame, aIndex);
			bool backup = IsBackup(aFrame, aIndex);
			bool mine = aIndex == aFrame.SelfIndex;
			if (up || backup)
			{
				draw->AddRectFilled(aMin, aMax, up ? UpFill(mine) : IM_COL32(200, 130, 30, mine ? 95 : 65), aRounding);
				float edge = up ? 3.0f : 1.0f;
				draw->AddRectFilled(ImVec2(aMin.x - 2, aMin.y), ImVec2(aMin.x - 2 + edge, aMax.y),
					up ? UpEdge(mine) : IM_COL32(240, 170, 60, mine ? 220 : 160));
			}
			if (mine)
			{
				draw->AddRectFilled(ImVec2(aMax.x - 2, aMin.y), ImVec2(aMax.x, aMax.y), IM_COL32(255, 230, 77, 200));
			}
		}

		// Silent through a whole squad fight: they are out of the range arcdps reports on, so their state
		// may be out of date.
		bool OutOfRange(const Frame& aFrame, int aIndex)
		{
			const Rezz::RosterMember* member = Member(aFrame, aIndex);
			return aFrame.View->SquadInCombat && member &&
				aFrame.View->NowMs > member->LastEventMs + Rezz::Session::kOutOfRangeMs;
		}

		void RowMenu(Frame& aFrame, int aIndex)
		{
			if (!ImGui::BeginPopupContextItem("row_menu")) { return; }
			NoteMenuRect();
			aFrame.MenuOpen = true;
			const Rezz::SessionView& view = *aFrame.View;
			ImGui::TextColored(kGrey, "%s", aFrame.Names[aIndex].c_str());
			ImGui::Separator();
			if (ImGui::MenuItem("Remove player")) { RemoveFromOrder(view.Order, aIndex); }
			if (ImGui::MenuItem("May cast early (precast)", nullptr, IsPrecast(aFrame, aIndex)))
			{
				TogglePrecast(view, Row(aFrame, aIndex).Account);
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("They spend their revive when they see it coming, instead of waiting for their turn.");
			}
			NicknameMenu(Row(aFrame, aIndex).Account);
			AddMeItem(view);
			if (ImGui::MenuItem("Move up", nullptr, false, aIndex > 0)) { MoveInOrder(view.Order, aIndex, aIndex - 1); }
			if (ImGui::MenuItem("Move down", nullptr, false, aIndex + 1 < static_cast<int>(view.Turn.Rows.size())))
			{
				MoveInOrder(view.Order, aIndex, aIndex + 1);
			}
			AddPlayerMenu(view);
			// Also here, not only on the window menu: the rows can cover the whole window, leaving nowhere to
			// right-click for it.
			PresetMenu(view);
			CopyOrderItem(view);
			if (ImGui::MenuItem("Open order editor")) { ShowEditor = true; }
			ImGui::Separator();
			StyleMenu();
			ImGui::EndPopup();
		}

		// The whole row is the drag handle and opens the row menu. The visuals are drawn on top afterwards,
		// so the cursor is left where the row started.
		void RowInteraction(Frame& aFrame, int aIndex, const ImVec2& aSize)
		{
			if (!aFrame.Interactive) { return; }
			if (ShotMenu == aIndex) { ImGui::OpenPopup("row_menu"); PlaceMenuForShot(); }
			ImVec2 rowPos = ImGui::GetCursorPos();
			ImGui::InvisibleButton("row", aSize);
			if (aFrame.EditMode && ImGui::BeginDragDropSource())
			{
				ImGui::SetDragDropPayload("REZZ_ORDER_ROW", &aIndex, sizeof(aIndex));
				ImGui::TextUnformatted(aFrame.Names[aIndex].c_str());
				ImGui::EndDragDropSource();
			}
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("REZZ_ORDER_ROW"))
				{
					MoveInOrder(aFrame.View->Order, *static_cast<const int*>(payload->Data), aIndex);
				}
				ImGui::EndDragDropTarget();
			}
			if (aFrame.EditMode && ImGui::IsItemHovered())
			{
				ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 255, 255, 90));
			}
			AccountTooltip(Row(aFrame, aIndex).Account, Member(aFrame, aIndex));
			RowMenu(aFrame, aIndex);
			ImGui::SetCursorPos(rowPos);
		}

		void NoteRow(const Frame& aFrame, int aIndex)
		{
			const Rezz::OrderRow& row = Row(aFrame, aIndex);
			std::string status = StatusLabel(row);
			if (status.empty()) { status = SecondsText(row.ReadyInMs); } // the report always names the state
			LastFrame.Rows.push_back(std::to_string(aIndex + 1) + ". " + MarkerLabel(aFrame, aIndex) + " " +
				aFrame.Names[aIndex] + " " + status);
		}

		// "max displayed" shows a window of the order starting at whoever is up, so the rows that matter are
		// the visible ones. The numbers keep each player's real place in the order.
		std::vector<int> VisibleRows(const Frame& aFrame)
		{
			std::vector<int> rows;
			int count = static_cast<int>(aFrame.View->Turn.Rows.size());
			if (count == 0) { return rows; }
			int max = Settings::Current.OverlayMaxRows;
			int shown = max > 0 ? std::min(count, max) : count;
			int first = max > 0 ? std::max(CardIndex(aFrame), 0) : 0;
			for (int offset = 0; offset < shown; offset++) { rows.push_back((first + offset) % count); }
			return rows;
		}

		// The line above the rows: our own state in big letters when it is ours to act on, otherwise who is up.
		void CompactBanner(const Frame& aFrame, float aBannerHeight)
		{
			const Rezz::TurnView& turn = aFrame.View->Turn;
			bool mine = aFrame.SelfIndex >= 0 && (IsUp(aFrame, aFrame.SelfIndex) || IsBackup(aFrame, aFrame.SelfIndex));
			if (mine)
			{
				Fonts::Pop();
				Fonts::Push(Settings::Current.OverlayScale, true);
				bool up = IsUp(aFrame, aFrame.SelfIndex);
				LastFrame.Banner = up ? "YOUR TURN" : "BACKUP";
				ImGui::TextColored(up ? kGreen : kOrange, "%s", LastFrame.Banner.c_str());
				Fonts::Pop();
				Fonts::Push(Settings::Current.OverlayScale);
				return;
			}

			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (aBannerHeight - aFrame.Line) * 0.5f);
			if (turn.UpIndex >= 0)
			{
				LastFrame.Banner = "Up: " + aFrame.Names[turn.UpIndex];
				ImGui::TextColored(kGrey, "Up:");
				ImGui::SameLine();
				ImGui::TextUnformatted(aFrame.Names[turn.UpIndex].c_str());
			}
			else
			{
				LastFrame.Banner = "Nobody ready";
				ImGui::TextColored(kRed, "Nobody ready");
				if (turn.NextReadyIndex >= 0)
				{
					std::string wait = SecondsText(turn.Rows[turn.NextReadyIndex].ReadyInMs);
					LastFrame.Banner += " - " + aFrame.Names[turn.NextReadyIndex] + " in " + wait;
					ImGui::SameLine();
					ImGui::TextColored(kGrey, "- %s in %s", aFrame.Names[turn.NextReadyIndex].c_str(), wait.c_str());
				}
			}
		}

		// ------------------------------------------------------------------ layout 1: compact list
		//
		// One text line per player: marker, icon, name, state. Every column is as wide as its widest possible
		// value, so the window keeps one size while the states change.
		float LayoutCompact(Frame& aFrame)
		{
			LastFrame.Layout = "compact";
			const Rezz::TurnView& turn = aFrame.View->Turn;
			float x0 = ImGui::GetCursorPosX();
			float line = aFrame.Line;
			float spacing = aFrame.Space;

			float nameWidth = NameColumn(CurrentFont());
			FitNames(aFrame, CurrentFont(), nameWidth);
			float markerWidth = 0.0f;
			for (const char* text : { "UP", "BK", "99" }) { markerWidth = std::max(markerWidth, ImGui::CalcTextSize(text).x); }
			float statusWidth = 0.0f;
			for (const char* text : { "ready?", "casting", "120s", "DOWN", "DEAD", "away" })
			{
				statusWidth = std::max(statusWidth, ImGui::CalcTextSize(text).x);
			}
			float iconX = x0 + markerWidth + spacing;
			float nameX = iconX + line + spacing;
			float statusX = nameX + nameWidth + spacing;

			float nobodyWidth = ImGui::CalcTextSize("Nobody ready - ").x + nameWidth + ImGui::CalcTextSize(" in 120s").x;
			float contentWidth = std::max({ statusX + statusWidth - x0, nobodyWidth, ImGui::CalcTextSize("YOUR TURN").x * 1.5f });

			// The banner line is always the height of the big font, so the window doesn't jump.
			float bannerHeight = std::round(line * 1.5f);
			ImVec2 bannerPos = ImGui::GetCursorPos();
			CompactBanner(aFrame, bannerHeight);
			ImGui::SetCursorPos(ImVec2(bannerPos.x, bannerPos.y + bannerHeight + ImGui::GetStyle().ItemSpacing.y));

			for (int i : VisibleRows(aFrame))
			{
				const Rezz::OrderRow& row = turn.Rows[i];
				ImGui::PushID(i);
				ImVec2 rowPos = ImGui::GetCursorPos();
				RowInteraction(aFrame, i, ImVec2(contentWidth, line));

				ImVec2 min = ImGui::GetCursorScreenPos();
				ImVec2 max(min.x + contentWidth, min.y + line);
				CooldownFill(row, ImVec2(min.x, max.y - 2), max, 0.0f);
				HighlightRow(aFrame, i, ImVec2(min.x - 1, min.y - 1), ImVec2(max.x + 3, max.y + 1), 2.0f);

				ImGui::TextColored(MarkerColor(aFrame, i), "%s", MarkerLabel(aFrame, i).c_str());
				if (OutOfRange(aFrame, i))
				{
					ImGui::SameLine(0, 2);
					ImGui::TextColored(kGrey, "?");
					if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlapped))
					{
						ImGui::SetTooltip("no events from this player during the fight: out of range");
					}
				}

				ImGui::SameLine(iconX);
				ProfessionIcon(row.Account, Member(aFrame, i), !IsRelevant(row.Status));
				ImGui::SameLine(nameX);
				PlayerNameText(row.Account, aFrame.Names[i], NameColor(aFrame, i), Member(aFrame, i));
				std::string status = StatusLabel(row);
				if (!status.empty())
				{
					ImGui::SameLine(statusX);
					ImGui::TextColored(StatusColor(row), "%s", status.c_str());
				}
				NoteRow(aFrame, i);

				ImGui::SetCursorPos(ImVec2(rowPos.x, rowPos.y + line + ImGui::GetStyle().ItemSpacing.y));
				ImGui::PopID();
			}
			return contentWidth;
		}

		// ------------------------------------------------------------------ layout 2: big bars
		//
		// A tall bar per player, like the native squad window: big icon and name, the recharge filling the
		// bar from the left. Made to be read without looking straight at it.
		float LayoutBars(Frame& aFrame)
		{
			LastFrame.Layout = "bars";
			const Rezz::TurnView& turn = aFrame.View->Turn;
			ImDrawList* draw = ImGui::GetWindowDrawList();
			FontRef nameFont = PickFont(1.25f);
			FontRef bigFont = PickFont(1.0f, true);

			float line = aFrame.Line;
			float pad = std::round(line * 0.5f);
			float iconSize = std::round(line * 1.4f);
			float rowHeight = std::max(std::round(line * 2.1f), iconSize + pad);
			float gap = std::max(2.0f, std::round(line * 0.2f));

			float nameWidth = NameColumn(nameFont);
			FitNames(aFrame, nameFont, nameWidth);
			float markerWidth = 0.0f;
			for (const char* text : { "UP", "BK", "99" }) { markerWidth = std::max(markerWidth, ImGui::CalcTextSize(text).x); }
			float statusWidth = 0.0f;
			for (const char* text : { "ready?", "casting", "120s", "DOWN", "DEAD", "away" })
			{
				statusWidth = std::max(statusWidth, ImGui::CalcTextSize(text).x);
			}

			float needed = pad + iconSize + pad + nameWidth + aFrame.Space * 2 + markerWidth + aFrame.Space + statusWidth + pad;

			// The header is the part that gets read first, so the window is made wide enough for a name column
			// at header size, whoever is up, plus the corner text.
			float headerNameWidth = NameColumn(bigFont);
			float cornerWidth = ImGui::CalcTextSize("you: 99.").x + aFrame.Space;
			float headerNeeded = pad + nameFont.Measure("UP").x + aFrame.Space + bigFont.Size + aFrame.Space +
				headerNameWidth + cornerWidth + pad;
			float barWidth = std::max({ needed, headerNeeded, bigFont.Measure("YOUR TURN").x + pad * 2 + cornerWidth,
				bigFont.Measure("Nobody ready").x + pad * 2 + cornerWidth });

			// Header, built like the focus card's: it carries the colour of what this means for us, so the
			// top of the window answers "is this mine?" before any name is read.
			ImVec2 bannerPos = ImGui::GetCursorScreenPos();
			float bannerHeight = std::round(bigFont.Size * 1.35f);
			bool mine = aFrame.SelfIndex >= 0 && IsUp(aFrame, aFrame.SelfIndex);
			bool ours = aFrame.SelfIndex >= 0 && IsBackup(aFrame, aFrame.SelfIndex);
			ImVec2 headerMax(bannerPos.x + barWidth, bannerPos.y + bannerHeight);
			draw->AddRectFilled(bannerPos, headerMax, mine ? UpFill(true)
				: ours ? IM_COL32(150, 95, 25, 110) : IM_COL32(40, 46, 52, 150), 3.0f);
			draw->AddRect(bannerPos, headerMax, mine ? UpEdge(true)
				: ours ? IM_COL32(240, 170, 60, 230) : IM_COL32(70, 76, 82, 190), 3.0f,
				ImDrawCornerFlags_All, mine || ours ? 2.0f : 1.0f);

			// Our own place sits in the corner and keeps its room, so a long name never runs into it.
			std::string mineText;
			if (aFrame.SelfIndex >= 0 && !mine)
			{
				mineText = "you: " + MarkerLabel(aFrame, aFrame.SelfIndex);
				if (!ours) { mineText += "."; }
			}
			float mineWidth = mineText.empty() ? 0.0f : ImGui::CalcTextSize(mineText.c_str()).x + aFrame.Space;

			float left = bannerPos.x + pad;
			float room = barWidth - pad * 2 - mineWidth;
			if (mine)
			{
				LastFrame.Banner = "YOUR TURN";
				DrawText(bigFont, ImVec2(left, bannerPos.y + (bannerHeight - bigFont.Size) * 0.5f), kGreen, "YOUR TURN");
			}
			else if (turn.UpIndex >= 0)
			{
				LastFrame.Banner = "Up: " + aFrame.Names[turn.UpIndex];
				const Rezz::RosterMember* upMember = Member(aFrame, turn.UpIndex);
				DrawText(nameFont, ImVec2(left, bannerPos.y + (bannerHeight - nameFont.Size) * 0.5f), kGreenOther, "UP");
				left += nameFont.Measure("UP").x + aFrame.Space;
				if (void* icon = Icons::Get(upMember ? upMember->Profession : 0, upMember ? upMember->Elite : 0))
				{
					float size = bigFont.Size;
					draw->AddImage(icon, ImVec2(left, bannerPos.y + (bannerHeight - size) * 0.5f),
						ImVec2(left + size, bannerPos.y + (bannerHeight + size) * 0.5f), ImVec2(0, 0), ImVec2(1, 1),
						ImGui::GetColorU32(IconTint(upMember ? upMember->Profession : 0, false)));
					left += size + aFrame.Space;
				}
				// Whatever room is left after the icon and the corner text belongs to the name.
				float available = bannerPos.x + pad + room - left;
				std::string name = Fit(aFrame.FullNames[turn.UpIndex], available, bigFont);
				DrawText(bigFont, ImVec2(left, bannerPos.y + (bannerHeight - bigFont.Size) * 0.5f),
					ImGui::GetStyleColorVec4(ImGuiCol_Text), name.c_str());
			}
			else
			{
				LastFrame.Banner = "Nobody ready";
				DrawText(bigFont, ImVec2(left, bannerPos.y + (bannerHeight - bigFont.Size) * 0.5f), kRed, "Nobody ready");
			}
			if (!mineText.empty())
			{
				DrawText(FontRef{ ImGui::GetFont(), ImGui::GetFontSize() },
					ImVec2(headerMax.x - pad - ImGui::CalcTextSize(mineText.c_str()).x,
						bannerPos.y + bannerHeight - aFrame.Line - pad * 0.4f),
					ours ? kOrange : kYellow, mineText.c_str());
			}
			ImGui::Dummy(ImVec2(barWidth, bannerHeight));

			for (int i : VisibleRows(aFrame))
			{
				const Rezz::OrderRow& row = turn.Rows[i];
				const Rezz::RosterMember* member = Member(aFrame, i);
				ImGui::PushID(i);
				ImVec2 rowPos = ImGui::GetCursorPos();
				RowInteraction(aFrame, i, ImVec2(barWidth, rowHeight));

				ImVec2 min = ImGui::GetCursorScreenPos();
				ImVec2 max(min.x + barWidth, min.y + rowHeight);
				draw->AddRectFilled(min, max, IM_COL32(38, 42, 46, 190), 3.0f);
				CooldownFill(row, min, max, 3.0f);
				HighlightRow(aFrame, i, min, max, 3.0f);
				if (IsUp(aFrame, i)) { draw->AddRect(min, max, UpEdge(i == aFrame.SelfIndex), 3.0f, ImDrawCornerFlags_All, 2.0f); }
				else { draw->AddRect(min, max, IM_COL32(70, 76, 82, 200), 3.0f); }

				float x = min.x + pad;
				if (void* icon = Icons::Get(member ? member->Profession : 0, member ? member->Elite : 0))
				{
					draw->AddImage(icon, ImVec2(x, min.y + (rowHeight - iconSize) * 0.5f),
						ImVec2(x + iconSize, min.y + (rowHeight + iconSize) * 0.5f), ImVec2(0, 0), ImVec2(1, 1),
						ImGui::GetColorU32(IconTint(member ? member->Profession : 0, !IsRelevant(row.Status))));
				}
				x += iconSize + pad;
				DrawText(nameFont, ImVec2(x, min.y + (rowHeight - nameFont.Size) * 0.5f), NameColor(aFrame, i),
					aFrame.Names[i].c_str());

				std::string status = StatusLabel(row);
				float right = max.x - pad;
				if (!status.empty())
				{
					ImVec2 size = ImGui::CalcTextSize(status.c_str());
					DrawText(FontRef{ ImGui::GetFont(), ImGui::GetFontSize() },
						ImVec2(right - size.x, min.y + (rowHeight - size.y) * 0.5f), StatusColor(row), status.c_str());
					right -= statusWidth + aFrame.Space;
				}
				std::string marker = MarkerLabel(aFrame, i);
				if (OutOfRange(aFrame, i)) { marker += "?"; }
				ImVec2 markerSize = ImGui::CalcTextSize(marker.c_str());
				DrawText(FontRef{ ImGui::GetFont(), ImGui::GetFontSize() },
					ImVec2(right - markerSize.x, min.y + (rowHeight - markerSize.y) * 0.5f), MarkerColor(aFrame, i), marker.c_str());
				NoteRow(aFrame, i);

				ImGui::SetCursorPos(ImVec2(rowPos.x, rowPos.y + rowHeight + gap));
				ImGui::PopID();
			}
			return barWidth;
		}

		// The card at the top of the focus layouts: who is up, in the biggest font the window has, coloured by
		// what it means for us. Leaves the cursor under itself.
		float DrawUpCard(Frame& aFrame, float aCardWidth, const FontRef& aHugeFont, bool aBackupLine = true)
		{
			const Rezz::TurnView& turn = aFrame.View->Turn;
			ImDrawList* draw = ImGui::GetWindowDrawList();
			FontRef smallFont{ ImGui::GetFont(), ImGui::GetFontSize() };
			float line = aFrame.Line;
			float pad = std::round(line * 0.6f);
			float cardHeight = std::round(aHugeFont.Size * 1.35f + line * 1.6f);
			float cardWidth = aCardWidth;
			FontRef hugeFont = aHugeFont;
			ImVec2 cardPos = ImGui::GetCursorPos();

			int upIndex = turn.UpIndex;
			if (upIndex >= 0) { ImGui::PushID(upIndex); RowInteraction(aFrame, upIndex, ImVec2(cardWidth, cardHeight)); ImGui::PopID(); }
			ImVec2 min = ImGui::GetCursorScreenPos();
			ImVec2 max(min.x + cardWidth, min.y + cardHeight);
			bool mine = aFrame.SelfIndex >= 0 && upIndex == aFrame.SelfIndex;
			// Our turn colours the card green; being the backup colours it orange, so the card alone says
			// whether this is about to be our problem.
			bool ours = aFrame.SelfIndex >= 0 && IsBackup(aFrame, aFrame.SelfIndex);
			ImU32 fill = mine ? IM_COL32(50, 150, 50, 110) : ours ? IM_COL32(150, 95, 25, 110) : IM_COL32(40, 46, 52, 190);
			ImU32 edge = mine ? IM_COL32(120, 230, 120, 255) : ours ? IM_COL32(240, 170, 60, 230) : IM_COL32(90, 96, 102, 220);
			draw->AddRectFilled(min, max, fill, 3.0f);
			if (upIndex >= 0) { CooldownFill(turn.Rows[upIndex], min, max, 3.0f); }
			draw->AddRect(min, max, edge, 3.0f, ImDrawCornerFlags_All, mine || ours ? 2.0f : 1.0f);

			float textY = min.y + pad * 0.6f;
			if (mine)
			{
				LastFrame.Banner = "YOUR TURN";
				DrawText(hugeFont, ImVec2(min.x + pad, textY), kGreen, "YOUR TURN");
			}
			else if (upIndex >= 0)
			{
				LastFrame.Banner = "Up: " + aFrame.Names[upIndex];
				float x = min.x + pad;
				DrawText(smallFont, ImVec2(x, textY + (hugeFont.Size - smallFont.Size) * 0.5f), kGreen, "UP");
				x += smallFont.Measure("UP").x + aFrame.Space;
				const Rezz::RosterMember* member = Member(aFrame, upIndex);
				if (void* icon = Icons::Get(member ? member->Profession : 0, member ? member->Elite : 0))
				{
					float size = std::round(hugeFont.Size * 0.85f);
					draw->AddImage(icon, ImVec2(x, textY + (hugeFont.Size - size) * 0.5f), ImVec2(x + size, textY + (hugeFont.Size + size) * 0.5f),
						ImVec2(0, 0), ImVec2(1, 1), ImGui::GetColorU32(ProfessionColor(member ? member->Profession : 0)));
					x += size + aFrame.Space;
				}
				std::string name = Fit(aFrame.FullNames[upIndex], min.x + cardWidth - pad - x, hugeFont);
				DrawText(hugeFont, ImVec2(x, textY), ImGui::GetStyleColorVec4(ImGuiCol_Text), name.c_str());
			}
			else
			{
				LastFrame.Banner = "Nobody ready";
				DrawText(hugeFont, ImVec2(min.x + pad, textY), kRed, "Nobody ready");
			}

			// Second line of the card: who takes over, and where we stand.
			float secondY = min.y + cardHeight - line - pad * 0.5f;
			std::string backup = aFrame.View->BackupIndex >= 0
				? "backup: " + aFrame.Names[aFrame.View->BackupIndex]
				: (turn.NextReadyIndex >= 0 ? "next: " + aFrame.Names[turn.NextReadyIndex] + " in " +
					SecondsText(turn.Rows[turn.NextReadyIndex].ReadyInMs) : std::string("no backup ready"));
			if (ours) { backup = "backup: you"; }
			// A layout that shows the backup as the next row, in orange, has no need to say it twice.
			if (!aBackupLine && aFrame.View->BackupIndex >= 0) { backup.clear(); }

			// Where we stand keeps its corner, and the backup line gives way rather than running into it.
			std::string mineText;
			if (aFrame.SelfIndex >= 0 && !mine)
			{
				mineText = "you: " + MarkerLabel(aFrame, aFrame.SelfIndex) + " " +
					StatusLabel(turn.Rows[aFrame.SelfIndex]);
			}
			float mineWidth = mineText.empty() ? 0.0f : smallFont.Measure(mineText.c_str()).x + aFrame.Space;
			float backupRoom = cardWidth - pad * 2 - mineWidth;
			while (backup.size() > 4 && smallFont.Measure(backup.c_str()).x > backupRoom) { backup.pop_back(); }
			DrawText(smallFont, ImVec2(min.x + pad, secondY),
				mine ? ImVec4(0.78f, 0.90f, 0.78f, 1.0f) : ours ? kOrange : kGrey, backup.c_str());
			if (!mineText.empty())
			{
				DrawText(smallFont, ImVec2(max.x - pad - smallFont.Measure(mineText.c_str()).x, secondY),
					kYellow, mineText.c_str());
			}
			// The card stands for the player who is up, or with nobody up the one who will be first again.
			if (CardIndex(aFrame) >= 0) { NoteRow(aFrame, CardIndex(aFrame)); }
			ImGui::SetCursorPos(ImVec2(cardPos.x, cardPos.y + cardHeight + ImGui::GetStyle().ItemSpacing.y));
			return cardHeight;
		}

		// ------------------------------------------------------------------ layout 4: focus card
		//
		// Whoever is up fills a card at the top; everyone else is a queue underneath it, in reading order
		// starting after the card. Made for a glance: one name, large.
		float LayoutFocus(Frame& aFrame)
		{
			LastFrame.Layout = "focus";
			const Rezz::TurnView& turn = aFrame.View->Turn;
			FontRef hugeFont = PickFont(1.6f, true);

			float line = aFrame.Line;
			float pad = std::round(line * 0.6f);

			// The queue wraps into up to three columns, so a big squad grows downwards slowly. Its size comes
			// from how many rows are shown, not from who is up: the card takes one of them either way, so the
			// window doesn't change shape when nobody is ready.
			std::vector<int> visible = VisibleRows(aFrame);
			std::vector<int> queue;
			int card = CardIndex(aFrame);
			for (int i : visible) { if (i != card) { queue.push_back(i); } }
			int slots = std::max(static_cast<int>(visible.size()) - 1, static_cast<int>(queue.size()));
			int columns = slots > 4 ? 3 : 1;
			// Every cell is laid out the same way, so the states line up in a column instead of following
			// names of different lengths.
			float queueNameWidth = NameColumn(CurrentFont());
			FitNames(aFrame, CurrentFont(), queueNameWidth);
			float queueMarkerWidth = ImGui::CalcTextSize("99.").x;
			float queueStatusX = queueMarkerWidth + aFrame.Space + line + aFrame.Space + queueNameWidth + aFrame.Space;
			float cellWidth = queue.empty() ? 0.0f : queueStatusX + ImGui::CalcTextSize("ready?").x + aFrame.Space * 2;
			float secondLineWidth = ImGui::CalcTextSize("backup: ").x + queueNameWidth + aFrame.Space +
				ImGui::CalcTextSize("you: 99 casting").x;
			float cardWidth = std::max({ cellWidth * columns, hugeFont.Measure("YOUR TURN").x + pad * 2,
				hugeFont.Measure("Nobody ready").x + pad * 2, secondLineWidth + pad * 2 });

			DrawUpCard(aFrame, cardWidth, hugeFont);

			// The queue.
			ImGui::TextColored(kGrey, "queue");
			ImVec2 queuePos = ImGui::GetCursorPos();
			for (size_t position = 0; position < queue.size(); position++)
			{
				int i = queue[position];
				int column = static_cast<int>(position) % columns;
				int row = static_cast<int>(position) / columns;
				ImVec2 cellPos(queuePos.x + column * cellWidth, queuePos.y + row * (line + 2));
				ImGui::SetCursorPos(cellPos);
				ImGui::PushID(i);
				RowInteraction(aFrame, i, ImVec2(cellWidth, line));
				ImVec2 cellMin = ImGui::GetCursorScreenPos();
				// The recharge line starts after the position number, so it doesn't read as an underline.
				float barLeft = cellMin.x + queueMarkerWidth + aFrame.Space;
				CooldownFill(turn.Rows[i], ImVec2(barLeft, cellMin.y + line - 2),
					ImVec2(cellMin.x + cellWidth - aFrame.Space * 2, cellMin.y + line), 0.0f);
				HighlightRow(aFrame, i, cellMin, ImVec2(cellMin.x + cellWidth - 4, cellMin.y + line), 2.0f);

				float cellX = cellPos.x - queuePos.x;
				ImGui::TextColored(MarkerColor(aFrame, i), "%s", (MarkerLabel(aFrame, i) + ".").c_str());
				ImGui::SameLine(cellX + queueMarkerWidth + aFrame.Space);
				ProfessionIcon(Row(aFrame, i).Account, Member(aFrame, i), !IsRelevant(turn.Rows[i].Status));
				ImGui::SameLine(cellX + queueMarkerWidth + aFrame.Space + line + aFrame.Space);
				PlayerNameText(Row(aFrame, i).Account, aFrame.Names[i], NameColor(aFrame, i), Member(aFrame, i));
				std::string status = StatusLabel(turn.Rows[i]);
				if (!status.empty())
				{
					ImGui::SameLine(cellX + queueStatusX);
					ImGui::TextColored(StatusColor(turn.Rows[i]), "%s", status.c_str());
				}
				NoteRow(aFrame, i);
				ImGui::PopID();
			}
			int lines = slots == 0 ? 0 : (slots + columns - 1) / columns;
			ImGui::SetCursorPos(ImVec2(queuePos.x, queuePos.y + lines * (line + 2)));
			return cardWidth;
		}

		// ------------------------------------------------------------------ layout 6: next up
		//
		// Only what has to be acted on: the player who is up on the card, the backup directly under it, and
		// however many of the following players "max displayed" asks for. The list rolls with the turn, so
		// the top row is always the backup and the positions never mean "place in the order" here - they are
		// simply the order things will happen in.
		float LayoutNextUp(Frame& aFrame)
		{
			LastFrame.Layout = "nextup";
			const Rezz::TurnView& turn = aFrame.View->Turn;
			FontRef hugeFont = PickFont(1.6f, true);

			float line = aFrame.Line;
			float pad = std::round(line * 0.6f);
			float spacing = aFrame.Space;

			// The backup always sits directly under the card, even when somebody between them in the order is
			// down or recharging: the next person who can act is what this layout is for. The rest follow in
			// the order the turn will reach them.
			std::vector<int> rolled;
			int count = static_cast<int>(turn.Rows.size());
			int card = CardIndex(aFrame);
			int backup = aFrame.View->BackupIndex;
			if (backup >= 0 && backup != card) { rolled.push_back(backup); }
			for (int step = 1; step <= count; step++)
			{
				int index = (std::max(card, 0) + step) % count;
				if (index == card || index == backup) { continue; }
				rolled.push_back(index);
			}
			// A fixed number of rows, so the window keeps its height; the backup is almost always the first.
			int shown = Settings::Current.OverlayMaxRows;
			if (shown > 0 && static_cast<int>(rolled.size()) > shown) { rolled.resize(static_cast<size_t>(shown)); }

			float nameWidth = NameColumn(CurrentFont());
			FitNames(aFrame, CurrentFont(), nameWidth);
			float markerWidth = 0.0f;
			for (const char* text : { "BK", "99" }) { markerWidth = std::max(markerWidth, ImGui::CalcTextSize(text).x); }
			float statusWidth = 0.0f;
			for (const char* text : { "ready?", "casting", "120s", "DOWN", "DEAD", "away" })
			{
				statusWidth = std::max(statusWidth, ImGui::CalcTextSize(text).x);
			}
			float rowWidth = markerWidth + spacing + line + spacing + nameWidth + spacing + statusWidth;
			// The card's second line carries "next: <name> in 120s" when nobody is ready, and "you: N <state>".
			float secondLineWidth = ImGui::CalcTextSize("next: ").x + nameWidth + ImGui::CalcTextSize(" in 120s").x +
				spacing + ImGui::CalcTextSize("you: 99 casting").x;
			float cardWidth = std::max({ rowWidth + pad * 2, hugeFont.Measure("YOUR TURN").x + pad * 2,
				hugeFont.Measure("Nobody ready").x + pad * 2, secondLineWidth + pad * 2 });

			DrawUpCard(aFrame, cardWidth, hugeFont, false);

			float x0 = ImGui::GetCursorPosX();
			float iconX = x0 + markerWidth + spacing;
			float nameX = iconX + line + spacing;
			float statusX = nameX + nameWidth + spacing;
			for (size_t position = 0; position < rolled.size(); position++)
			{
				int i = rolled[position];
				const Rezz::OrderRow& row = turn.Rows[i];
				ImGui::PushID(i);
				ImVec2 rowPos = ImGui::GetCursorPos();
				RowInteraction(aFrame, i, ImVec2(cardWidth, line));

				ImVec2 min = ImGui::GetCursorScreenPos();
				ImVec2 max(min.x + cardWidth, min.y + line);
				CooldownFill(row, ImVec2(min.x, max.y - 2), max, 0.0f);
				HighlightRow(aFrame, i, ImVec2(min.x - 1, min.y - 1), ImVec2(max.x - 1, max.y + 1), 2.0f);

				// The backup is marked; everyone else shows their place in the order, the same number the editor
				// gives them. Field test 2026-09-16: these used to count places in this list, so they stopped
				// matching the editor as soon as the list rolled.
				if (IsBackup(aFrame, i)) { ImGui::TextColored(kOrange, "BK"); }
				else { ImGui::TextColored(kGrey, "%d", i + 1); }

				ImGui::SameLine(iconX);
				ProfessionIcon(row.Account, Member(aFrame, i), !IsRelevant(row.Status));
				ImGui::SameLine(nameX);
				PlayerNameText(row.Account, aFrame.Names[i], NameColor(aFrame, i), Member(aFrame, i));
				std::string status = StatusLabel(row);
				if (!status.empty())
				{
					ImGui::SameLine(statusX);
					ImGui::TextColored(StatusColor(row), "%s", status.c_str());
				}
				NoteRow(aFrame, i);

				ImGui::SetCursorPos(ImVec2(rowPos.x, rowPos.y + line + ImGui::GetStyle().ItemSpacing.y));
				ImGui::PopID();
			}
			return cardWidth;
		}

		// ------------------------------------------------------------------ layout 5: horizontal strip
		//
		// The order left to right in one line of cells, for a thin bar along the top or bottom of the screen.
		// There is no banner: whoever is up is the green cell, and it says so when it is us.
		float LayoutStrip(Frame& aFrame)
		{
			LastFrame.Layout = "strip";
			const Rezz::TurnView& turn = aFrame.View->Turn;
			ImDrawList* draw = ImGui::GetWindowDrawList();
			FontRef nameFont = PickFont(1.15f);

			float line = aFrame.Line;
			float pad = std::round(line * 0.4f);
			float iconSize = std::round(line * 1.3f);
			float cellHeight = std::max(std::round(line * 2.3f), iconSize + pad * 2);

			float nameWidth = NameColumn(nameFont);
			FitNames(aFrame, nameFont, nameWidth);
			float statusWidth = 0.0f;
			for (const char* text : { "ready?", "casting", "120s", "DOWN", "DEAD", "away", "YOUR TURN" })
			{
				statusWidth = std::max(statusWidth, ImGui::CalcTextSize(text).x);
			}
			float markerWidth = ImGui::CalcTextSize("99").x + aFrame.Space;
			float cellWidth = pad + iconSize + pad + std::max(nameWidth, markerWidth + statusWidth) + pad;

			std::vector<int> visible = VisibleRows(aFrame);
			// With a width set by hand the strip wraps into further lines instead of running off the screen.
			int perLine = std::max(1, static_cast<int>(visible.size()));
			if (Settings::Current.OverlayWidth > 0.0f)
			{
				perLine = std::max(1, static_cast<int>(Settings::Current.OverlayWidth / cellWidth));
			}
			float stripWidth = cellWidth * std::min(perLine, std::max(1, static_cast<int>(visible.size())));

			// The one state the cells can't show on their own. Its line is kept while somebody is up as well,
			// so the strip doesn't change height, and drawn rather than laid out so it can't widen it.
			ImVec2 noticePos = ImGui::GetCursorScreenPos();
			if (turn.UpIndex < 0)
			{
				LastFrame.Banner = "Nobody ready";
				FontRef small = CurrentFont();
				DrawText(small, noticePos, kRed, "Nobody ready");
				if (turn.NextReadyIndex >= 0)
				{
					std::string wait = SecondsText(turn.Rows[turn.NextReadyIndex].ReadyInMs);
					LastFrame.Banner += " - " + aFrame.FullNames[turn.NextReadyIndex] + " in " + wait;
					float left = small.Measure("Nobody ready ").x;
					std::string rest = Fit("- " + aFrame.FullNames[turn.NextReadyIndex] + " in " + wait, stripWidth - left, small);
					DrawText(small, ImVec2(noticePos.x + left, noticePos.y), kGrey, rest.c_str());
				}
			}
			else
			{
				bool mine = aFrame.SelfIndex >= 0 && IsUp(aFrame, aFrame.SelfIndex);
				LastFrame.Banner = mine ? "YOUR TURN" : "Up: " + aFrame.Names[turn.UpIndex];
			}
			ImGui::Dummy(ImVec2(0.0f, line));
			ImVec2 stripPos = ImGui::GetCursorPos();
			for (size_t position = 0; position < visible.size(); position++)
			{
				int i = visible[position];
				int cellColumn = static_cast<int>(position) % perLine;
				int cellLine = static_cast<int>(position) / perLine;
				const Rezz::OrderRow& row = turn.Rows[i];
				const Rezz::RosterMember* member = Member(aFrame, i);
				ImGui::SetCursorPos(ImVec2(stripPos.x + cellColumn * cellWidth, stripPos.y + cellLine * (cellHeight + 2)));
				ImGui::PushID(i);
				RowInteraction(aFrame, i, ImVec2(cellWidth, cellHeight));

				ImVec2 min = ImGui::GetCursorScreenPos();
				ImVec2 max(min.x + cellWidth - 2, min.y + cellHeight);
				draw->AddRectFilled(min, max, IM_COL32(38, 42, 46, 190), 3.0f);
				CooldownFill(row, min, max, 3.0f);
				HighlightRow(aFrame, i, min, max, 3.0f);
				draw->AddRect(min, max, IsUp(aFrame, i) ? UpEdge(i == aFrame.SelfIndex) : IM_COL32(70, 76, 82, 200),
					3.0f, ImDrawCornerFlags_All, IsUp(aFrame, i) ? 2.0f : 1.0f);

				float x = min.x + pad;
				if (void* icon = Icons::Get(member ? member->Profession : 0, member ? member->Elite : 0))
				{
					draw->AddImage(icon, ImVec2(x, min.y + (cellHeight - iconSize) * 0.5f),
						ImVec2(x + iconSize, min.y + (cellHeight + iconSize) * 0.5f), ImVec2(0, 0), ImVec2(1, 1),
						ImGui::GetColorU32(IconTint(member ? member->Profession : 0, !IsRelevant(row.Status))));
				}
				x += iconSize + pad;
				DrawText(nameFont, ImVec2(x, min.y + pad * 0.5f), NameColor(aFrame, i), aFrame.Names[i].c_str());

				// Second line of the cell: place in the order, then the state (or "YOUR TURN" on our own cell).
				FontRef small{ ImGui::GetFont(), ImGui::GetFontSize() };
				float lowerY = min.y + cellHeight - line - pad * 0.5f;
				std::string marker = MarkerLabel(aFrame, i);
				if (OutOfRange(aFrame, i)) { marker += "?"; }
				DrawText(small, ImVec2(x, lowerY), MarkerColor(aFrame, i), marker.c_str());
				bool shout = i == aFrame.SelfIndex && IsUp(aFrame, i);
				std::string status = shout ? "YOUR TURN" : StatusLabel(row);
				if (!status.empty())
				{
					DrawText(small, ImVec2(x + markerWidth, lowerY), shout ? kGreen : StatusColor(row), status.c_str());
				}
				NoteRow(aFrame, i);
				ImGui::PopID();
			}
			int lines = (static_cast<int>(visible.size()) + perLine - 1) / perLine;
			ImGui::SetCursorPos(ImVec2(stripPos.x, stripPos.y + lines * (cellHeight + 2)));
			return cellWidth * std::min(perLine, static_cast<int>(visible.size()));
		}

		constexpr int kMessagesBelow = 0;
		constexpr int kMessagesAbove = 1;
		constexpr int kMessagesHidden = 2;

		// Messages that come and go, like somebody leaving the squad, sit in a strip of their own against the turn window's top or bottom edge. Drawn inside the window they made it
		// taller for as long as they showed, which pushed it into the windows packed around it or off the
		// screen (field test 2026-09-16). The strip is as wide as the window, lets clicks through to the game,
		// and goes on whichever side the player has room.
		void RenderMessages(const Context& aContext)
		{
			const Settings::Values& s = Settings::Current;
			while (!s_Notices.empty() && aContext.NowMs - s_Notices.front().TimeMs > kNoticeShowMs) { s_Notices.pop_front(); }
			if (s.OverlayMessages == kMessagesHidden || !LastFrame.Drawn || s_Notices.empty()) { return; }

			bool above = s.OverlayMessages == kMessagesAbove;
			const float gap = 2.0f;
			const ImVec2 padding(6.0f, 4.0f);
			ImVec2 anchor(LastFrame.X, above ? LastFrame.Y - gap : LastFrame.Y + LastFrame.Height + gap);
			ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, ImVec2(0.0f, above ? 1.0f : 0.0f));
			ImGui::SetNextWindowSizeConstraints(ImVec2(LastFrame.Width, 0.0f), ImVec2(LastFrame.Width, FLT_MAX));
			ImGui::SetNextWindowBgAlpha(s.OverlayBgAlpha);
			ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
				ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
				ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
			if (!s.OverlayBackground) { flags |= ImGuiWindowFlags_NoBackground; }
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			bool open = ImGui::Begin("###rezzorder_messages", nullptr, flags);
			ImGui::PopStyleVar(2);
			if (open)
			{
				Fonts::Push(s.OverlayScale);
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + LastFrame.Width - padding.x * 2);
				for (const TimedNotice& notice : s_Notices) { ImGui::TextColored(kOrange, "%s", notice.Text.c_str()); }
				ImGui::PopTextWrapPos();
				Fonts::Pop();
				LastFrame.MessagesX = ImGui::GetWindowPos().x;
				LastFrame.MessagesY = ImGui::GetWindowPos().y;
				LastFrame.MessagesWidth = ImGui::GetWindowSize().x;
				LastFrame.MessagesHeight = ImGui::GetWindowSize().y;
			}
			ImGui::End();
		}

		// The overlay keeps one size while the order stays the same: every column is as wide as its widest
		// possible value, and the banner line is always reserved.
		void RenderOverlay(const Rezz::SessionView& aView, const RosterIndex& aRoster, const Context& aContext)
		{
			Settings::Values& s = Settings::Current;
			LastFrame = FrameInfo{};
			if (!s.OverlayVisible || !aContext.IsGameplay || aContext.IsMapOpen) { return; }
			if (s.OverlayOnlyWvw && !aContext.InWvw) { return; }

			ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing |
				ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar;
			// Holding Ctrl+Shift makes the overlay editable (and movable) even while it is locked.
			bool editMode = ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift;
			// A menu stays usable after the keys are released; it closes by clicking elsewhere.
			bool interactive = editMode || !s.OverlayLocked || s_MenuOpen;
			if (!s.OverlayTitleBar) { flags |= ImGuiWindowFlags_NoTitleBar; }
			if (!s.OverlayBackground) { flags |= ImGuiWindowFlags_NoBackground; }
			if (s.OverlayLocked && !interactive) { flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs; }
			// Dragging the corner sizes the window; it must not drag the window along with it.
			if (s_GripHot) { flags |= ImGuiWindowFlags_NoMove; }

			ArcStyle::Push();
			ImGui::SetNextWindowPos(ImVec2(s.OverlayX, s.OverlayY), ImGuiCond_Appearing);
			// The width the player set is the narrowest the window may be, never the widest: text that needs more
			// room at a bigger size pushes it wider instead of being cut off.
			ImVec2 least(s.OverlayWidth > 0 ? s.OverlayWidth : 0.0f, 0.0f);
			bool empty = aView.Order.empty();
			if (empty)
			{
				// With no order the window keeps the size it last had with one. Players fit it in among their other
				// windows while it is full (the demo squad is how most do it); shrunk to its one line of text, it
				// left an awkward gap there whenever there was no order (field test 2026-09-16).
				least.x = std::max(least.x, s.OverlayFilledWidth);
				least.y = s.OverlayFilledHeight;
			}
			if (least.x > 0.0f || least.y > 0.0f)
			{
				ImGui::SetNextWindowSizeConstraints(least, ImVec2(FLT_MAX, FLT_MAX));
			}
			ImGui::SetNextWindowBgAlpha(s.OverlayBgAlpha);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, s.OverlayLocked && !interactive ? 0.0f : 1.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 5));
			bool open = ImGui::Begin(s.OverlayTitleBar ? "Rezz Order###rezzorder_overlay" : "###rezzorder_overlay", nullptr, flags);
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
			float contentWidth = ImGui::CalcTextSize("Rezz Order: no players yet.").x;

			Frame frame;
			frame.View = &aView;
			frame.Roster = &aRoster;
			frame.Interactive = interactive;
			frame.EditMode = editMode;
			frame.Line = ImGui::GetTextLineHeight();
			frame.Space = ImGui::GetStyle().ItemSpacing.x * 1.5f;

			if (aView.Order.empty())
			{
				LastFrame.Layout = "empty";
				const char* first = "Rezz Order: no players yet.";
				const char* second = s.OverlayLocked ? "Ctrl+Shift + right-click to add players" : "Right-click to add players";
				// Centred in the room the order usually takes.
				ImVec2 room = ImGui::GetContentRegionAvail();
				ImVec2 block(std::max(ImGui::CalcTextSize(first).x, ImGui::CalcTextSize(second).x),
					ImGui::GetTextLineHeightWithSpacing() + ImGui::GetTextLineHeight());
				ImVec2 start = ImGui::GetCursorPos();
				float offsetX = std::max(0.0f, std::floor((room.x - block.x) * 0.5f));
				ImGui::SetCursorPos(ImVec2(start.x + offsetX, start.y + std::max(0.0f, std::floor((room.y - block.y) * 0.5f))));
				ImGui::TextColored(kGrey, "%s", first);
				ImGui::SetCursorPosX(start.x + offsetX);
				ImGui::TextColored(kGrey, "%s", second);
			}
			else
			{
				for (int i = 0; i < static_cast<int>(aView.Turn.Rows.size()); i++)
				{
					const Rezz::OrderRow& row = aView.Turn.Rows[i];
					std::string name = ShortName(PlayerName(row.Account, Find(aRoster, row.Account)));
					// The same mark the shared chat line uses, so the two read the same way.
					if (std::find(aView.Precast.begin(), aView.Precast.end(), row.Account) != aView.Precast.end())
					{
						name += Rezz::Share::kPrecastMark;
					}
					frame.Names.push_back(name);
					frame.FullNames.push_back(name);
					if (row.Account == aView.SelfAccount) { frame.SelfIndex = i; }
				}
				switch (s.Layout)
				{
					case Settings::OverlayLayout::Bars:  contentWidth = std::max(contentWidth, LayoutBars(frame)); break;
					case Settings::OverlayLayout::Focus: contentWidth = std::max(contentWidth, LayoutFocus(frame)); break;
					case Settings::OverlayLayout::Strip: contentWidth = std::max(contentWidth, LayoutStrip(frame)); break;
					case Settings::OverlayLayout::NextUp: contentWidth = std::max(contentWidth, LayoutNextUp(frame)); break;
					default:                             contentWidth = std::max(contentWidth, LayoutCompact(frame)); break;
				}
			}

			// Nothing that comes and goes is drawn in here: see RenderMessages.
			// Keeps the width constant while the statuses change.
			ImGui::Dummy(ImVec2(contentWidth, 0.0f));
			Fonts::Pop();

			// A grip in the corner, like any window has, except that it scales the whole thing: with the width
			// left to fit the rows there is nothing else a corner could mean here. It is drawn and hit-tested
			// by hand: a real widget down there would count as content and push the corner away from itself.
			if (interactive)
			{
				float grip = std::max(10.0f, ImGui::GetTextLineHeight() * 0.7f);
				ImVec2 corner(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
					ImGui::GetWindowPos().y + ImGui::GetWindowSize().y);
				ImVec2 gripMin(corner.x - grip, corner.y - grip);
				bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
					ImGui::IsMouseHoveringRect(gripMin, corner, false);
				if (hovered && ImGui::IsMouseClicked(0)) { s_GripHeld = true; }
				if (!ImGui::IsMouseDown(0)) { s_GripHeld = false; }
				if (s_GripHeld)
				{
					// Sideways drag reads as "bigger"; the height follows the rows by itself.
					float step = ImGui::GetIO().MouseDelta.x / std::max(contentWidth, 1.0f);
					float scale = s.OverlayScale * (1.0f + step);
					s.OverlayScale = scale < 0.7f ? 0.7f : (scale > 2.5f ? 2.5f : scale);
					Settings::MarkDirty();
				}
				// Always there to be found while the window is unlocked, the way any resizable window shows
				// its corner; brighter once the mouse is on it.
				if (s_GripHeld || hovered) { ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE); }
				ImU32 color = s_GripHeld ? IM_COL32(255, 230, 120, 235)
					: hovered ? IM_COL32(220, 220, 220, 180) : IM_COL32(190, 190, 190, 80);
				ImDrawList* draw = ImGui::GetWindowDrawList();
				for (float offset = 3.0f; offset < grip; offset += 3.0f)
				{
					draw->AddLine(ImVec2(corner.x - offset, corner.y - 2), ImVec2(corner.x - 2, corner.y - offset), color, 1.0f);
				}
				s_GripHot = s_GripHeld || hovered;
			}
			else { s_GripHot = false; }

			if (interactive) { frame.MenuOpen = OverlayContextMenu(aView) || frame.MenuOpen; }
			s_MenuOpen = frame.MenuOpen;

			LastFrame.Drawn = true;
			LastFrame.X = ImGui::GetWindowPos().x;
			LastFrame.Y = ImGui::GetWindowPos().y;
			LastFrame.Width = ImGui::GetWindowSize().x;
			LastFrame.Height = ImGui::GetWindowSize().y;
			if (!empty && (std::fabs(LastFrame.Width - s.OverlayFilledWidth) > 0.5f || std::fabs(LastFrame.Height - s.OverlayFilledHeight) > 0.5f))
			{
				s.OverlayFilledWidth = LastFrame.Width;
				s.OverlayFilledHeight = LastFrame.Height;
				Settings::MarkDirty();
			}
			ImGui::End();
			RenderMessages(aContext);
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

			// One spec at a time, in subgroup order: the troubadour in subgroup 2 goes before the one in 4.
			struct Spec { uint32_t Profession; uint32_t Elite; std::string Name; int Count; };
			std::vector<Spec> specs;
			for (const Rezz::RosterMember* member : candidates)
			{
				if (member->Profession == 0) { continue; } // not on our map: nothing known to group them by
				auto it = std::find_if(specs.begin(), specs.end(),
					[&](const Spec& aSpec) { return aSpec.Profession == member->Profession && aSpec.Elite == member->Elite; });
				if (it != specs.end()) { it->Count++; continue; }
				specs.push_back(Spec{ member->Profession, member->Elite, SpecName(member->Profession, member->Elite), 1 });
			}
			std::sort(specs.begin(), specs.end(), [](const Spec& a, const Spec& b) { return a.Name < b.Name; });
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::CalcTextSize("Add in subgroup order").x + ImGui::GetFrameHeight() * 2);
			if (ImGui::BeginCombo("##add_spec", "Add in subgroup order", ImGuiComboFlags_HeightLarge))
			{
				if (specs.empty()) { ImGui::TextColored(kGrey, "Nobody left to add"); }
				for (const Spec& spec : specs)
				{
					std::string label = spec.Name + " (" + std::to_string(spec.Count) + ")##" +
						std::to_string(spec.Profession) + "_" + std::to_string(spec.Elite);
					if (ImGui::Selectable(label.c_str()))
					{
						std::vector<std::string> order = aView.Order;
						for (const Rezz::RosterMember* member : candidates)
						{
							if (member->Profession == spec.Profession && member->Elite == spec.Elite) { order.push_back(member->Account); }
						}
						ApplyOrder(order);
					}
				}
				ImGui::EndCombo();
			}

			ImGui::BeginChild("squad_list", ImVec2(0, 0), true);
			if (candidates.empty())
			{
				ImGui::TextColored(kGrey, "Nobody to add. Squad members on your map");
				ImGui::TextColored(kGrey, "appear here (needs ArcDPS).");
			}
			uint16_t shownGroup = 0xFFFF;
			for (const Rezz::RosterMember* member : candidates)
			{
				if (member->Subgroup != shownGroup)
				{
					shownGroup = member->Subgroup;
					if (shownGroup == 0) { ImGui::TextColored(kGrey, "No subgroup"); }
					else { ImGui::TextColored(kGrey, "Subgroup %u", static_cast<unsigned>(shownGroup)); }
				}
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

			// Part of the order, and shared with it, so every client agrees on which subgroup is the bench.
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Bench subgroup");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::CalcTextSize("none").x + ImGui::GetFrameHeight() * 2);
			std::string benchLabel = Rezz::Share::BenchName(aView.Bench);
			if (ImGui::BeginCombo("##bench", benchLabel.c_str()))
			{
				// "last" follows the squad as it fills up; 1 is the first subgroup.
				static constexpr int kChoices[] = { 0, Rezz::Share::kBenchLast, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
				for (int group : kChoices)
				{
					std::string label = Rezz::Share::BenchName(static_cast<uint16_t>(group));
					if (ImGui::Selectable(label.c_str(), group == aView.Bench))
					{
						if (Rezz::Demo::Running()) { AddNotice("demo squad: stop the demo to change the real order", s_LastNowMs); }
						else
						{
							Settings::Current.Bench = group;
							Live::SetBench(group);
							Settings::MarkDirty();
						}
					}
				}
				ImGui::EndCombo();
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Moving a player from the order into this subgroup takes them out, and whoever moves into\n"
					"the subgroup they left takes their place. \"last\" is the highest subgroup in use, and follows the\n"
					"squad as it fills up. A subgroup with anybody from the order in it is never the bench.\n"
					"Copied into squad chat with the order.");
			}

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

				// Room for the five small buttons and the gaps between them, measured rather than guessed so
				// the row still lines up if the font size changes.
				const ImGuiStyle& style = ImGui::GetStyle();
				float button = ImGui::CalcTextSize("N").x + style.FramePadding.x * 2;
				float right = ImGui::GetWindowContentRegionMax().x - (button * 5 + style.ItemSpacing.x * 4);
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
				// Lit up while the mark is on, so the order can be read down the column at a glance.
				bool precast = std::find(aView.Precast.begin(), aView.Precast.end(), account) != aView.Precast.end();
				if (precast) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.58f, 0.46f, 0.10f, 1.0f)); }
				if (ImGui::SmallButton("*")) { TogglePrecast(aView, account); }
				if (precast) { ImGui::PopStyleColor(); }
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", "May cast early (precast). They spend their revive when they see it "
						"coming, instead of waiting for their turn.");
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

			if (ImGui::Button("Save as...")) { ImGui::OpenPopup("save_preset"); }
			if (ImGui::BeginPopup("save_preset"))
			{
				ImGui::TextColored(kGrey, "Keep this order under a name:");
				SavePresetField(aView, "editor");
				ImGui::EndPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Load...")) { ImGui::OpenPopup("load_preset"); }
			if (ImGui::BeginPopup("load_preset"))
			{
				Settings::Values& s = Settings::Current;
				if (s.Presets.empty()) { ImGui::TextColored(kGrey, "no saved orders yet"); }
				for (auto it = s.Presets.begin(); it != s.Presets.end(); )
				{
					ImGui::PushID(it->first.c_str());
					if (ImGui::Button("Load")) { ApplyOrder(it->second); ImGui::CloseCurrentPopup(); }
					ImGui::SameLine();
					if (ImGui::SmallButton("x"))
					{
						it = s.Presets.erase(it);
						Settings::MarkDirty();
						ImGui::PopID();
						continue;
					}
					ImGui::SameLine();
					ImGui::Text("%s (%zu)", it->first.c_str(), it->second.size());
					ImGui::PopID();
					++it;
				}
				ImGui::EndPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Copy for squad chat")) { CopyOrder(aView); }
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", Rezz::Share::Encode(aView.Order, aView.Precast, aView.Roster, aView.Bench).c_str());
			}
			ImGui::SameLine();
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

		// Somebody who isn't the commander shared an order. It is never applied behind the player's back, so
		// this asks, in its own small window that takes clicks even while the overlay is locked.
		void RenderShare(const Rezz::SessionView& aView, const RosterIndex& aRoster, const Context& aContext)
		{
			LastShareFrame = FrameInfo{};
			if (!aView.HasShare || !aContext.IsGameplay) { return; }

			ArcStyle::Push();
			ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);
			// Near the middle of the screen: it is a question, and it must not appear off-screen because the
			// turn window happens to sit in a corner.
			ImVec2 screen = ImGui::GetIO().DisplaySize;
			ImGui::SetNextWindowPos(ImVec2(screen.x * 0.5f, screen.y * 0.35f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			if (ImGui::Begin("Revive order shared###rezzorder_share", nullptr, ImGuiWindowFlags_AlwaysAutoResize |
				ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
			{
				const Rezz::RosterMember* sender = Find(aRoster, aView.Share.From);
				ImGui::TextColored(kYellow, "%s", PlayerName(aView.Share.From, sender).c_str());
				ImGui::SameLine();
				ImGui::TextUnformatted("shared a revive order:");

				for (size_t i = 0; i < aView.Share.Accounts.size(); i++)
				{
					const std::string& account = aView.Share.Accounts[i];
					const Rezz::RosterMember* member = Find(aRoster, account);
					bool isSelf = account == aView.SelfAccount;
					ImGui::TextColored(isSelf ? kYellow : kGrey, "%zu.", i + 1);
					ImGui::SameLine();
					ProfessionIcon(account, member);
					ImGui::SameLine();
					PlayerNameText(account, PlayerName(account, member),
						isSelf ? kYellow : ImGui::GetStyleColorVec4(ImGuiCol_Text), member);
				}

				// Where this would put us is the part of somebody else's order that is ours to care about.
				int now = aView.Share.OurPlaceNow;
				int then = aView.Share.OurPlaceThen;
				if (then != now)
				{
					if (then == 0) { ImGui::TextColored(kOrange, "you would not be in the order any more"); }
					else if (now == 0) { ImGui::TextColored(kYellow, "you would be %d.", then); }
					else { ImGui::TextColored(kYellow, "you would be %d. instead of %d.", then, now); }
				}
				else if (then != 0)
				{
					ImGui::TextColored(kGrey, "your place stays %d.", then);
				}
				if (!aView.Share.Unknown.empty())
				{
					ImGui::TextColored(kOrange, "%zu name(s) not in the squad were left out", aView.Share.Unknown.size());
				}

				if (ImGui::Button("Use this order")) { Live::AcceptShare(); }
				ImGui::SameLine();
				if (ImGui::Button("Ignore")) { Live::DismissShare(); }
				ImGui::SameLine();
				ImGui::TextColored(kGrey, "(your own order stays until you accept)");

				LastShareFrame.Drawn = true;
				LastShareFrame.Layout = "share";
				LastShareFrame.X = ImGui::GetWindowPos().x;
				LastShareFrame.Y = ImGui::GetWindowPos().y;
				LastShareFrame.Width = ImGui::GetWindowSize().x;
				LastShareFrame.Height = ImGui::GetWindowSize().y;
			}
			ImGui::End();
			ArcStyle::Pop();
		}

		// Somebody typed "!rezz?" in squad chat. Addons cannot answer by themselves, so this asks whether to
		// put the order on the clipboard; pasting it is still the player's keystroke.
		void RenderRequest(const Rezz::SessionView& aView, const RosterIndex& aRoster, const Context& aContext)
		{
			LastRequestFrame = FrameInfo{};
			if (!aView.HasRequest || !aContext.IsGameplay) { return; }

			ArcStyle::Push();
			ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);
			ImVec2 screen = ImGui::GetIO().DisplaySize;
			ImGui::SetNextWindowPos(ImVec2(screen.x * 0.5f, screen.y * 0.35f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			if (ImGui::Begin("Revive order asked for###rezzorder_request", nullptr, ImGuiWindowFlags_AlwaysAutoResize |
				ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
			{
				const Rezz::RosterMember* asker = Find(aRoster, aView.RequestFrom);
				std::string askerName = PlayerName(aView.RequestFrom, asker);
				ImGui::TextColored(kYellow, "%s", askerName.c_str());
				ImGui::SameLine();
				ImGui::TextUnformatted("asked for the revive order.");

				// Somebody asking for the order has usually just joined, so the answer is normally to put
				// them in it first. The line below is what the left-hand button copies.
				bool inOrder = std::find(aView.Order.begin(), aView.Order.end(), aView.RequestFrom) != aView.Order.end();
				std::vector<std::string> withThem = aView.Order;
				if (!inOrder) { withThem.push_back(aView.RequestFrom); }
				if (!inOrder)
				{
					ProfessionIcon(aView.RequestFrom, asker);
					ImGui::SameLine();
					ImGui::TextColored(kOrange, "%s is not in the order.", askerName.c_str());
					if (asker && asker->Profession != 0 && !Rezz::IsReviveProfession(asker->Profession))
					{
						ImGui::SameLine();
						ImGui::TextColored(kRed, "(%s has no revive skill)", Rezz::ProfessionShortName(asker->Profession));
					}
				}
				ImGui::TextColored(kGrey, "%s", Rezz::Share::Encode(withThem, aView.Precast, aView.Roster, aView.Bench).c_str());

				if (inOrder)
				{
					if (ImGui::Button("Copy it for squad chat")) { CopyOrder(aView); }
				}
				else
				{
					if (ImGui::Button(("Add " + askerName + " and copy").c_str()))
					{
						ApplyOrder(withThem);
						CopyOrderLine(withThem, aView.Roster);
					}
					ImGui::SameLine();
					if (ImGui::Button("Copy without adding")) { CopyOrder(aView); }
				}
				ImGui::SameLine();
				if (ImGui::Button("Not now")) { Live::ClearRequest(); }
				ImGui::SameLine();
				ImGui::TextColored(kGrey, "(then paste it in squad chat)");

				LastRequestFrame.Drawn = true;
				LastRequestFrame.Layout = "request";
				LastRequestFrame.X = ImGui::GetWindowPos().x;
				LastRequestFrame.Y = ImGui::GetWindowPos().y;
				LastRequestFrame.Width = ImGui::GetWindowSize().x;
				LastRequestFrame.Height = ImGui::GetWindowSize().y;
			}
			ImGui::End();
			ArcStyle::Pop();
		}

		void RenderEditor(const Rezz::SessionView& aView, const RosterIndex& aRoster, const Context& aContext)
		{
			LastEditorFrame = FrameInfo{};
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
				LastEditorFrame.Drawn = true;
				LastEditorFrame.Layout = "editor";
				LastEditorFrame.X = ImGui::GetWindowPos().x;
				LastEditorFrame.Y = ImGui::GetWindowPos().y;
				LastEditorFrame.Width = ImGui::GetWindowSize().x;
				LastEditorFrame.Height = ImGui::GetWindowSize().y;
			}
			ImGui::End();
			ArcStyle::Pop();
		}
	}

	void Init()
	{
		Live::SetOrder(Settings::Current.Order);
		Live::SetPrecast(Settings::Current.Precast);
		Live::SetBench(Settings::Current.Bench);
		Live::SetShareRules(Settings::Current.ShareFromLeaders, Settings::Current.ShareFromAnyone);
		Live::SetAnswerRule(Settings::Current.AnswerRequests);
		Live::SetSubstitutes(Settings::Current.Substitutes);
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

	namespace
	{
		Notify::Sound SoundOf(int aValue)
		{
			return aValue > 0 && aValue < static_cast<int>(Notify::Sound::Count) ? static_cast<Notify::Sound>(aValue) : Notify::Sound::None;
		}

		std::vector<unsigned> s_IllusionPreviews; // when each "try" from the options page runs out
		std::vector<unsigned> s_IllusionAlerted;  // when the countdowns that got their sound and flash run out
		// Who a countdown is on screen for. A player who rallies drops out of it at once, so without saying so
		// the sound and flash of a countdown that lasted a moment have nothing to show for them (field test
		// 2026-09-17: rallied 0.8 s after their countdown started).
		std::vector<std::pair<std::string, unsigned>> s_IllusionShown;

		// Somebody revived by Illusion of Life goes down again when it runs out, unless they kill something first.
		// In its last seconds a banner names them, for whoever could revive them again: only a player whose own
		// revive is ready sees it. The players of one cast run out together, so they share the one line.
		void IllusionCountdown(const Rezz::SessionView& aView, const RosterIndex& aRoster, unsigned aNowMs, bool aSignals)
		{
			const Settings::Values& s = Settings::Current;
			constexpr unsigned kSameCastMs = 500; // players whose Illusion runs out this close together share one cast
			constexpr size_t   kMostShown  = 3;
			std::erase_if(s_IllusionPreviews, [aNowMs](unsigned aEnd) { return aEnd <= aNowMs; });
			// Gone from the countdown before it ran out: they got up, which is the good ending.
			std::erase_if(s_IllusionShown, [&](const std::pair<std::string, unsigned>& aShown)
			{
				bool stillUnderIt = std::any_of(aView.Illusions.begin(), aView.Illusions.end(),
					[&](const Rezz::IllusionTarget& aTarget) { return aTarget.Account == aShown.first; });
				if (stillUnderIt) { return false; }
				// Within a moment of the end it simply ran out, and they went down: nothing to say.
				bool ranOut = aShown.second <= aNowMs + 300;
				// Somebody else from the same cast is still on the clock, so the countdown carries on with their
				// name on it. That is the answer to the sound already, and a rally message would only cover it.
				bool castGoesOn = std::any_of(aView.Illusions.begin(), aView.Illusions.end(), [&](const Rezz::IllusionTarget& aTarget)
				{
					unsigned ends = aNowMs + static_cast<unsigned>(aTarget.EndsInMs);
					return (ends > aShown.second ? ends - aShown.second : aShown.second - ends) <= kSameCastMs;
				});
				if (!ranOut && !castGoesOn) { AddNotice("Rally", aNowMs); }
				return true;
			});
			std::erase_if(s_IllusionAlerted, [aNowMs](unsigned aEnd) { return aEnd + 1000 <= aNowMs; });

			struct Pending
			{
				uint64_t    EndsInMs;
				std::string Name;
			};
			std::vector<Pending> pending;
			bool preview = !s_IllusionPreviews.empty();
			static constexpr const char* kPreviewNames[] = { "PlayerXYZ", "PlayerABC", "PlayerQRS" };
			for (size_t i = 0; i < s_IllusionPreviews.size(); i++)
			{
				pending.push_back(Pending{ s_IllusionPreviews[i] - aNowMs, kPreviewNames[i % std::size(kPreviewNames)] });
			}
			if (!preview && s.IllusionCountdown)
			{
				for (const Rezz::IllusionTarget& target : aView.Illusions)
				{
					// For whoever could revive them again, and always for the mesmer who cast it: they are the one
					// watching that player, even with their own revive spent on them.
					if (!aView.SelfCanRevive && !target.OurCast) { continue; }
					pending.push_back(Pending{ target.EndsInMs, PlayerName(target.Account, Find(aRoster, target.Account)) });
				}
			}
			if (pending.empty()) { return; }
			std::sort(pending.begin(), pending.end(), [](const Pending& a, const Pending& b) { return a.EndsInMs < b.EndsInMs; });

			uint64_t warnMs = static_cast<uint64_t>(std::clamp(s.IllusionWarnSeconds, 1, 14)) * 1000;
			size_t shown = 0;
			for (size_t i = 0; i < pending.size() && shown < kMostShown; )
			{
				// One countdown per cast: everyone whose Illusion runs out with the soonest one.
				uint64_t soonest = pending[i].EndsInMs;
				std::string names;
				for (; i < pending.size() && pending[i].EndsInMs <= soonest + kSameCastMs; i++)
				{
					names += (names.empty() ? "" : ", ") + pending[i].Name;
				}
				if (soonest > warnMs) { break; } // sorted: every later cast is further off still

				// The moment a countdown starts, once for it: its sound, and a flash in its own colour, so it is
				// noticed without looking for it. A second cast running out later gets its own.
				unsigned endsAt = aNowMs + static_cast<unsigned>(soonest);
				bool raised = std::any_of(s_IllusionAlerted.begin(), s_IllusionAlerted.end(), [endsAt](unsigned aEnd)
				{
					return (aEnd > endsAt ? aEnd - endsAt : endsAt - aEnd) <= kSameCastMs;
				});
				if (!raised)
				{
					s_IllusionAlerted.push_back(endsAt);
					if (aSignals || preview) { Notify::Alert(SoundOf(s.IllusionSound), s.IllusionFlashColor, s.IllusionFlash, aNowMs); }
				}
				Banner::Countdown(static_cast<unsigned>((soonest + 999) / 1000), names);
				shown++;
				for (const Rezz::IllusionTarget& target : aView.Illusions)
				{
					if (target.EndsInMs > soonest + kSameCastMs) { continue; }
					auto known = std::find_if(s_IllusionShown.begin(), s_IllusionShown.end(),
						[&](const std::pair<std::string, unsigned>& aShown) { return aShown.first == target.Account; });
					if (known == s_IllusionShown.end()) { s_IllusionShown.emplace_back(target.Account, endsAt); }
					else { known->second = endsAt; }
				}
			}
		}
	}

	void ClearNotices()
	{
		s_Notices.clear();
		s_IllusionShown.clear();
	}

	void Render(const Context& aContext)
	{
		if (EditorToggleRequested.exchange(false)) { ShowEditor = !ShowEditor; }
		if (LockToggleRequested.exchange(false))
		{
			Settings::Current.OverlayLocked = !Settings::Current.OverlayLocked;
			Settings::MarkDirty();
		}

		s_LastNowMs = aContext.NowMs;
		{ Timing::Step step("arcdps style"); ArcStyle::Update(aContext.NowMs); }
		{ Timing::Step step("fonts"); Fonts::Update(Settings::Current.OverlayScale, aContext.NowMs); }
		TextInputActive = false;
		Rezz::SessionView live;
		{ Timing::Step step("session view"); live = Live::GetView(); }
		// Demo mode shows a squad that isn't there; the real order must not be touched while it runs.
		Rezz::SessionView view = Rezz::Demo::Running() ? Rezz::Demo::View(aContext.NowMs) : live;
		if (!Rezz::Demo::Running() && (view.Order != Settings::Current.Order || view.Precast != Settings::Current.Precast ||
			view.Bench != Settings::Current.Bench))
		{
			Settings::Current.Order = view.Order;
			Settings::Current.Precast = view.Precast;
			Settings::Current.Bench = view.Bench;
			Settings::MarkDirty();
		}
		if (CopyOrderRequested.exchange(false)) { CopyOrder(view); }
		CountCharacterNames(view);

		// The three signals for "it is on you now". The turn window shows it too, but only if you are
		// looking at it.
		Notify::Standing standing = Notify::Standing::None;
		if (!view.SelfAccount.empty())
		{
			const Rezz::TurnView& turn = view.Turn;
			if (turn.UpIndex >= 0 && turn.Rows[turn.UpIndex].Account == view.SelfAccount) { standing = Notify::Standing::Up; }
			else if (view.BackupIndex >= 0 && turn.Rows[view.BackupIndex].Account == view.SelfAccount) { standing = Notify::Standing::Backup; }
		}
		// The demo squad is there to set these up, so it raises them wherever the player is standing.
		bool signalsHere = aContext.InWvw || Rezz::Demo::Running();
		{
			// Includes starting a sound, which opens the audio device.
			Timing::Step step("turn signals");
			std::string banner = Notify::OnStanding(standing, aContext.NowMs, signalsHere, aContext.IsGameplay,
				Rezz::Demo::Running());
			if (!banner.empty())
			{
				Banner::Show(standing == Notify::Standing::Up ? Banner::Kind::Up : Banner::Kind::Backup, banner, aContext.NowMs);
			}
			Notify::Render(aContext.NowMs, aContext.IsGameplay && !aContext.IsMapOpen);
		}

		RosterIndex roster;
		{ Timing::Step step("roster index"); roster = IndexRoster(view); }
		{
			Timing::Step step("banners");
			IllusionCountdown(view, roster, aContext.NowMs, signalsHere);
			Banner::Render(aContext.NowMs);
		}
		{ Timing::Step step("turn window"); RenderOverlay(view, roster, aContext); }
		{ Timing::Step step("share windows"); RenderShare(view, roster, aContext); RenderRequest(view, roster, aContext); }
		{ Timing::Step step("order editor"); RenderEditor(view, roster, aContext); }
		{ Timing::Step step("settings file"); Settings::Flush(aContext.NowMs); }
	}

	namespace
	{
		// One banner switch, with the message it would show on hover.
		void BannerToggle(const char* aLabel, bool* aValue, const char* aExample)
		{
			if (ImGui::Checkbox(aLabel, aValue)) { Settings::MarkDirty(); }
			if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", aExample); }
		}

		// Banner, sound and flash for one standing, on one line each.
		// A sound picker: every sound, with "file..." last.
		void SoundCombo(const char* aId, int* aValue)
		{
			Notify::Sound current = *aValue >= 0 && *aValue < static_cast<int>(Notify::Sound::Count)
				? static_cast<Notify::Sound>(*aValue) : Notify::Sound::None;
			if (!ImGui::BeginCombo(aId, Notify::SoundName(current))) { return; }
			for (Notify::Sound sound : Notify::MenuOrder())
			{
				bool selected = sound == current;
				if (ImGui::Selectable(Notify::SoundName(sound), selected))
				{
					*aValue = static_cast<int>(sound);
					Settings::MarkDirty();
				}
				if (selected) { ImGui::SetItemDefaultFocus(); }
			}
			ImGui::EndCombo();
		}

		// Size and place of one group of banners, on one line.
		void BannerGroupOptions(const char* aWhat, const char* aHint, float* aSize, float* aX, float* aY)
		{
			ImGui::PushID(aWhat);
			ImGui::TextColored(kYellow, "%s", aWhat);
			if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", aHint); }
			ImGui::SameLine(110);
			ImGui::SetNextItemWidth(90);
			if (ImGui::SliderFloat("size", aSize, 0.8f, 4.0f, "%.1f")) { Settings::MarkDirty(); }
			ImGui::SameLine();
			ImGui::SetNextItemWidth(90);
			if (ImGui::SliderFloat("across", aX, 0.0f, 100.0f, "%.0f%%")) { Settings::MarkDirty(); }
			ImGui::SameLine();
			ImGui::SetNextItemWidth(90);
			if (ImGui::SliderFloat("down", aY, 0.0f, 95.0f, "%.0f%%")) { Settings::MarkDirty(); }
			ImGui::PopID();
		}

		void StandingSignals(const char* aWhat, bool* aBanner, int* aSound, bool* aFlash, float aColor[3])
		{
			ImGui::PushID(aWhat);
			ImGui::TextColored(kYellow, "%s", aWhat);
			ImGui::SameLine(110);
			if (ImGui::Checkbox("banner", aBanner)) { Settings::MarkDirty(); }

			ImGui::SameLine();
			ImGui::SetNextItemWidth(130);
			SoundCombo("##sound", aSound);
			ImGui::SameLine();
			// Listening to them all is the only way to pick one.
			if (ImGui::Button("play")) { Notify::Play(SoundOf(*aSound)); }

			ImGui::SameLine();
			if (ImGui::Checkbox("flash", aFlash)) { Settings::MarkDirty(); }
			ImGui::SameLine();
			ImGui::SetNextItemWidth(160);
			if (ImGui::ColorEdit3("##colour", aColor, ImGuiColorEditFlags_NoInputs)) { Settings::MarkDirty(); }
			ImGui::PopID();
		}
	}

	void Options()
	{
		Settings::Values& s = Settings::Current;
		ImGui::TextUnformatted("Revive order");
		if (ImGui::Button("Open order editor (Ctrl+Shift+O)")) { ShowEditor = true; }
		ImGui::SameLine();
		if (Rezz::Demo::Running())
		{
			if (ImGui::Button("Stop demo")) { Rezz::Demo::Stop(); }
			ImGui::SameLine();
			ImGui::TextColored(kOrange, "demo squad: not real players");
		}
		else if (ImGui::Button("Demo squad"))
		{
			Rezz::Demo::Start(Live::GetView().SelfAccount, s_LastNowMs);
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("A made-up squad fighting on a loop, to place and size the window.");
		}
		ImGui::Separator();
		ImGui::TextUnformatted("Turn window");
		ImGui::TextColored(kGrey, "The same settings are on the window itself: right-click it > style.");
		ImGui::TextColored(kGrey, "Drag the corner grip to size it, Ctrl+Shift+L locks it (Ctrl+Shift edits it while locked).");
		WindowOptions();

		ImGui::Separator();
		ImGui::TextUnformatted("Look and names");
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

		ImGui::Separator();
		ImGui::TextUnformatted("Notifications");
		ImGui::TextColored(kGrey, "Every message is shown next to the turn window. These pick the ones that also get a");
		ImGui::TextColored(kGrey, "banner across the top of the screen.");
		BannerToggle("a player in the order leaves or is replaced", &s.BannerOnLeave,
			"\"3. PlayerXYZ left the squad - you are up now\"");
		BannerToggle("a player swaps profession", &s.BannerOnSwap,
			"\"3. PlayerXYZ swapped to Ranger (no revive skill)\"");
		BannerToggle("somebody shares an order", &s.BannerOnShare,
			"\"PlayerXYZ shared a revive order (6 players) - you are now 2. (was 4.)\"");
		BannerToggle("somebody asks for the order", &s.BannerOnAsk,
			"\"PlayerXYZ asked for the revive order\"");

		// The banners are drawn by the addon rather than sent to Nexus: Nexus' own alert has no size, place or time
		// on screen to set. The top middle of the screen is where the game shows the selected target, so both
		// groups can be moved clear of it.
		ImGui::Spacing();
		static const char* kBannerStyles[static_cast<int>(Banner::Style::Count)] = {
			"text", "plate", "window", "callout" };
		ImGui::SetNextItemWidth(120);
		if (ImGui::Combo("banner style", &s.BannerStyle, kBannerStyles, IM_ARRAYSIZE(kBannerStyles))) { Settings::MarkDirty(); }
		ImGui::SameLine();
		ImGui::SetNextItemWidth(90);
		if (ImGui::InputFloat("seconds on screen", &s.BannerSeconds, 0.5f, 1.0f, "%.1f"))
		{
			s.BannerSeconds = std::clamp(s.BannerSeconds, 1.0f, 60.0f);
			Settings::MarkDirty();
		}
		TextInputActive = TextInputActive || ImGui::IsItemActive();

		BannerGroupOptions("messages", "Somebody left the squad, an order was shared, a problem with the addon's setup.",
			&s.BannerInfoSize, &s.BannerInfoX, &s.BannerInfoY);
		ImGui::SameLine();
		if (ImGui::ColorEdit3("##messagecolour", s.BannerInfoColor, ImGuiColorEditFlags_NoInputs)) { Settings::MarkDirty(); }
		ImGui::SameLine();
		if (ImGui::Button("Try##messages"))
		{
			Banner::Show(Banner::Kind::Info, "3. PlayerXYZ left the squad, out of the order - you are up now", s_LastNowMs);
		}

		BannerGroupOptions("alerts", "Your turn, backup, and the Illusion of Life countdown.",
			&s.BannerAlertSize, &s.BannerAlertX, &s.BannerAlertY);
		ImGui::SameLine();
		if (ImGui::Button("Try##alerts"))
		{
			Banner::Show(Banner::Kind::Backup, "Backup", s_LastNowMs);
			Banner::Show(Banner::Kind::Up, "You're up", s_LastNowMs);
		}
		ImGui::TextColored(kGrey, "alert text colours");
		ImGui::SameLine(110);
		if (ImGui::ColorEdit3("your turn##bannercolour", s.BannerUpColor, ImGuiColorEditFlags_NoInputs)) { Settings::MarkDirty(); }
		ImGui::SameLine();
		if (ImGui::ColorEdit3("backup##bannercolour", s.BannerBackupColor, ImGuiColorEditFlags_NoInputs)) { Settings::MarkDirty(); }
		ImGui::SameLine();
		if (ImGui::ColorEdit3("Illusion of Life##bannercolour", s.BannerIllusionColor, ImGuiColorEditFlags_NoInputs)) { Settings::MarkDirty(); }

		ImGui::Spacing();
		ImGui::TextUnformatted("When the turn reaches you");
		ImGui::TextColored(kGrey, "Only on WvW maps. The demo squad raises them too, so they can be tried out.");
		StandingSignals("your turn", &s.UpBanner, &s.UpSound, &s.UpFlash, s.UpFlashColor);
		StandingSignals("backup", &s.BackupBanner, &s.BackupSound, &s.BackupFlash, s.BackupFlashColor);

		ImGui::SetNextItemWidth(160);
		if (ImGui::SliderInt("sound volume", &s.SoundVolume, 0, 100, "%d%%")) { Settings::MarkDirty(); }
		if (s.UpSound == static_cast<int>(Notify::Sound::File) || s.BackupSound == static_cast<int>(Notify::Sound::File))
		{
			char path[260] = {};
			strncpy_s(path, s.SoundFile.c_str(), _TRUNCATE);
			ImGui::SetNextItemWidth(400);
			if (ImGui::InputText("WAV file", path, sizeof(path))) { s.SoundFile = path; Settings::MarkDirty(); }
			TextInputActive = TextInputActive || ImGui::IsItemActive();
		}
		ImGui::SetNextItemWidth(160);
		if (ImGui::SliderFloat("flash strength", &s.FlashStrength, 0.05f, 1.0f, "%.2f")) { Settings::MarkDirty(); }
		// Straight to the signal, past the guard that stops a real turn from announcing itself twice in a row:
		// trying colours and strengths means clicking these over and over.
		if (ImGui::Button("Try: your turn"))
		{
			Notify::Preview(Notify::Standing::Up, s_LastNowMs);
			Banner::Show(Banner::Kind::Up, "You're up", s_LastNowMs);
		}
		ImGui::SameLine();
		if (ImGui::Button("Try: backup"))
		{
			Notify::Preview(Notify::Standing::Backup, s_LastNowMs);
			Banner::Show(Banner::Kind::Backup, "Backup", s_LastNowMs);
		}
		ImGui::SameLine();
		ImGui::TextColored(kGrey, "(its banner, its sound, and a pulse along the screen edges)");

		ImGui::Spacing();
		ImGui::TextUnformatted("Illusion of Life");
		if (ImGui::Checkbox("count down before a revived player goes down again", &s.IllusionCountdown)) { Settings::MarkDirty(); }
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("%s", "A player revived by Illusion of Life goes down again after 15 seconds unless they kill "
				"something. Everyone running the addon whose own revive skill is ready gets a countdown of the last seconds. "
				"The first Illusion of Life of a session may not be seen.");
		}
		ImGui::SetNextItemWidth(90);
		if (ImGui::InputInt("seconds before", &s.IllusionWarnSeconds))
		{
			s.IllusionWarnSeconds = std::clamp(s.IllusionWarnSeconds, 1, 14);
			Settings::MarkDirty();
		}
		TextInputActive = TextInputActive || ImGui::IsItemActive();
		ImGui::SameLine();
		if (ImGui::Checkbox("figures under the names", &s.IllusionNumberBelow)) { Settings::MarkDirty(); }
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("%s", "Under the names puts the seconds nearer the middle of the screen, and further from the "
				"target's name and effects at the top.");
		}
		ImGui::PushID("illusion");
		ImGui::TextColored(kYellow, "%s", "when it starts");
		ImGui::SameLine(110);
		ImGui::SetNextItemWidth(130);
		SoundCombo("##sound", &s.IllusionSound);
		ImGui::SameLine();
		if (ImGui::Button("play")) { Notify::Play(SoundOf(s.IllusionSound)); }
		ImGui::SameLine();
		if (ImGui::Checkbox("flash", &s.IllusionFlash)) { Settings::MarkDirty(); }
		ImGui::SameLine();
		ImGui::SetNextItemWidth(160);
		if (ImGui::ColorEdit3("##colour", s.IllusionFlashColor, ImGuiColorEditFlags_NoInputs)) { Settings::MarkDirty(); }
		ImGui::PopID();
		// Each press is another cast, so pressing twice shows two countdowns running at once.
		if (ImGui::Button("Try: Illusion countdown") && s_IllusionPreviews.size() < 3)
		{
			s_IllusionPreviews.push_back(s_LastNowMs + static_cast<unsigned>(std::clamp(s.IllusionWarnSeconds, 1, 14)) * 1000);
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Bench swaps");
		if (ImGui::Checkbox("Somebody moved into a benched player's subgroup takes their place", &s.Substitutes))
		{
			Settings::MarkDirty();
			Live::SetSubstitutes(s.Substitutes);
		}
		ImGui::TextColored(kGrey, "Set the bench subgroup in the order editor; it is shared with the order. Moving a player from");
		ImGui::TextColored(kGrey, "the order into it takes them out, and a druid, troubadour or anyone seen reviving who moves into");
		ImGui::TextColored(kGrey, "the subgroup they left within 2 minutes takes their place. Needs Unofficial Extras.");

		ImGui::Separator();
		ImGui::TextUnformatted("Sharing the order in squad chat");
		ImGui::TextColored(kGrey, "Addons cannot write in chat. \"Copy order for squad chat\" (right-click the turn window)");
		ImGui::TextColored(kGrey, "puts a line like \"!rezzorder Kalden > Orrin*\" on the clipboard; paste it in squad chat and");
		ImGui::TextColored(kGrey, "everyone running this addon reads it. \"?rezzorder\" asks the squad for the order.");
		if (ImGui::Checkbox("Take over an order shared by the commander or a lieutenant", &s.ShareFromLeaders))
		{
			Settings::MarkDirty();
			Live::SetShareRules(s.ShareFromLeaders, s.ShareFromAnyone);
		}
		if (ImGui::Checkbox("...by anyone in the squad, without asking", &s.ShareFromAnyone))
		{
			Settings::MarkDirty();
			Live::SetShareRules(s.ShareFromLeaders, s.ShareFromAnyone);
		}
		ImGui::TextColored(kGrey, "Anyone else's order waits in a window for you to accept it.");
		ImGui::TextColored(kGrey, "Ctrl+Shift+K copies the order without opening anything.");

		// Without this, one "!rezz?" would open a window on every screen in the squad at once.
		static const char* kAnswerNames[] = { "nobody: only a line in the turn window",
			"me, when the order is mine", "me, always" };
		int answer = s.AnswerRequests;
		ImGui::SetNextItemWidth(260);
		if (ImGui::Combo("Who answers a request for the order", &answer, kAnswerNames, IM_ARRAYSIZE(kAnswerNames)))
		{
			s.AnswerRequests = answer;
			Settings::MarkDirty();
			Live::SetAnswerRule(answer);
		}
		ImGui::TextColored(kGrey, "An order you took over from somebody else is theirs to answer for, so by default");
		ImGui::TextColored(kGrey, "only the client that built the order is asked. The window closes by itself as soon");
		ImGui::TextColored(kGrey, "as an order appears in squad chat.");
		ImGui::TextColored(kGrey, "Hold Ctrl+Shift over the overlay to reorder it directly, even while it is locked.");
		ImGui::TextColored(kGrey, "Squad events reach addons ~2.6 s late (ArcDPS design): states change a moment after they happen in game.");
	}
}
