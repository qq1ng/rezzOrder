#include "Session.h"

#include <algorithm>
#include <cctype>

#include "Share.h"

namespace Rezz
{
	namespace
	{
		constexpr uint32_t kSignetOfMercyPassive   = 9162;
		constexpr uint32_t kSignetOfUndeathPassive = 10610;
		// Our own self leave arrives ~1 s after arcdps removed every squad member for our map change.
		constexpr uint64_t kSelfLeaveSlackMs = 3000;
		// More notices than this in one tick are merged into one.
		constexpr size_t   kMaxSeparateNotices = 3;

		uint32_t GroupBit(ReviveGroup aGroup) { return 1u << static_cast<uint32_t>(aGroup); }

		std::string Lower(std::string aText)
		{
			std::transform(aText.begin(), aText.end(), aText.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return aText;
		}
	}

	bool IsReviveProfession(uint32_t aProfession)
	{
		switch (aProfession)
		{
			case 1: // Guardian: Signet of Mercy
			case 2: // Warrior: Battle Standard
			case 4: // Ranger: Spirit of Nature
			case 6: // Elementalist: Glyph of Renewal
			case 7: // Mesmer: Illusion of Life
			case 8: // Necromancer: Signet of Undeath
				return true;
			default:
				return false;
		}
	}

	const char* ProfessionShortName(uint32_t aProfession)
	{
		static constexpr const char* names[] = { "???", "Guard", "War", "Engi", "Ranger", "Thief", "Ele", "Mes", "Necro", "Rev" };
		return aProfession < std::size(names) ? names[aProfession] : "???";
	}

	bool IsUsableCharacterName(const std::string& aName)
	{
		if (aName.empty()) { return false; }
		// Character names never contain digits, so anything with one is a placeholder like "ag1458".
		for (unsigned char c : aName) { if (std::isdigit(c)) { return false; } }

		// WvW rank names are "<tier> <rank>" and repeat between players (English client).
		static const char* kTiers[] = { "Bronze", "Silver", "Gold", "Platinum", "Mithril", "Diamond" };
		static const char* kRanks[] = { "Invader", "Assaulter", "Raider", "Recruit", "Scout", "Soldier",
			"Squire", "Footman", "Knight", "Major", "Colonel", "General", "Veteran", "Champion", "Legend",
			"Dominator", "Conqueror", "Vanquisher", "Warlord", "Marshal" };
		size_t space = aName.find(' ');
		if (space == std::string::npos || aName.find(' ', space + 1) != std::string::npos) { return true; }
		std::string first = aName.substr(0, space), second = aName.substr(space + 1);
		bool tier = false, rank = false;
		for (const char* text : kTiers) { tier = tier || first == text; }
		for (const char* text : kRanks) { rank = rank || second == text; }
		return !(tier && rank);
	}

	std::string DisplayAccount(const std::string& aAccount)
	{
		std::string name = !aAccount.empty() && aAccount[0] == ':' ? aAccount.substr(1) : aAccount;
		size_t dot = name.rfind('.');
		bool numbered = dot != std::string::npos && dot + 1 < name.size() &&
			std::all_of(name.begin() + dot + 1, name.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
		return numbered ? name.substr(0, dot) : name;
	}

	RosterMember& Session::GetMember(const std::string& aAccount)
	{
		RosterMember& member = m_Roster[aAccount];
		if (member.Account.empty()) { member.Account = aAccount; }
		return member;
	}

	bool Session::InOrder(const std::string& aAccount) const
	{
		const std::vector<std::string>& order = m_Tracker.Order();
		return std::find(order.begin(), order.end(), aAccount) != order.end();
	}

	std::string Session::Named(const std::string& aAccount) const
	{
		const std::vector<std::string>& order = m_Tracker.Order();
		auto it = std::find(order.begin(), order.end(), aAccount);
		std::string name = DisplayAccount(aAccount);
		if (it == order.end()) { return name; }
		return std::to_string(it - order.begin() + 1) + ". " + name;
	}

	int Session::SelfPlace() const
	{
		if (m_SelfAccount.empty()) { return 0; }
		const std::vector<std::string>& order = m_Tracker.Order();
		auto it = std::find(order.begin(), order.end(), m_SelfAccount);
		return it == order.end() ? 0 : static_cast<int>(it - order.begin()) + 1;
	}

	// Taking over somebody else's order can move us in it, and where we stand is the one thing about the
	// order that has to reach us without looking.
	std::string Session::PlaceChange(int aPlaceBefore) const
	{
		int now = SelfPlace();
		if (now == aPlaceBefore) { return {}; }
		if (now == 0) { return " - you are not in it any more"; }
		if (aPlaceBefore == 0) { return " - you are now " + std::to_string(now) + "."; }
		return " - you are now " + std::to_string(now) + ". (was " + std::to_string(aPlaceBefore) + ".)";
	}

	int Session::SelfStanding(uint64_t aNowMs) const
	{
		if (m_SelfAccount.empty()) { return 0; }
		TurnView turn = m_Tracker.GetTurn(aNowMs);
		if (turn.UpIndex >= 0 && turn.Rows[turn.UpIndex].Account == m_SelfAccount) { return 1; }
		if (turn.BackupIndex >= 0 && turn.Rows[turn.BackupIndex].Account == m_SelfAccount) { return 2; }
		return 0;
	}

	std::string Session::StandingChange(int aBefore, int aAfter) const
	{
		if (aAfter == aBefore || aAfter == 0) { return {}; }
		return aAfter == 1 ? " - you are up now" : " - you are the backup now";
	}

	void Session::Notify(NoticeKind aKind, const std::string& aAccount, std::string aText)
	{
		m_Notices.push_back(Notice{ aKind, aAccount, std::move(aText) });
	}

	std::string Session::AccountByInstance(uint16_t aInstanceId) const
	{
		if (aInstanceId == 0) { return {}; }
		for (const auto& [id, agent] : m_Agents)
		{
			if (agent.InstanceId == aInstanceId) { return agent.Account; }
		}
		return {};
	}

	std::string Session::ResolveAccount(uint64_t aId, uint16_t aInstanceId)
	{
		if (aId == 0) { return {}; }
		auto it = m_Agents.find(aId);
		if (it != m_Agents.end()) { return it->second.Account; }

		auto alias = m_Aliases.find(aId);
		if (alias != m_Aliases.end())
		{
			auto aliased = m_Agents.find(alias->second);
			if (aliased != m_Agents.end()) { return aliased->second.Account; }
		}

		if (aInstanceId == 0) { return {}; }
		for (const auto& [id, agent] : m_Agents)
		{
			if (agent.InstanceId == aInstanceId)
			{
				m_Aliases[aId] = id;
				return agent.Account;
			}
		}
		return {};
	}

	void Session::OnCombat(const ArcDps::EvCombatData& aData, uint64_t aNowMs)
	{
		if (aData.Ev == nullptr) { return; }
		const ArcDps::CombatEvent& ev = *aData.Ev;
		uint8_t sc = ev.IsStatechange;
		uint64_t srcId = aData.Src ? aData.Src->Id : ev.SrcAgent;

		switch (sc)
		{
			case ArcDps::CBTS_SQCOMBATSTART: m_CombatSinceMs = aNowMs; return;
			case ArcDps::CBTS_SQCOMBATEND:   m_CombatSinceMs = 0; return;
			case ArcDps::CBTS_ENTERCOMBAT:
			{
				// Fallback when the squad-wide events don't arrive.
				if (m_CombatSinceMs == 0) { m_CombatSinceMs = aNowMs; }
				std::string account = ResolveAccount(srcId, ev.SrcInstId);
				if (!account.empty()) { GetMember(account).LastEventMs = aNowMs; }
				return;
			}
			case ArcDps::CBTS_ANIMATIONSTART:
			{
				std::string account = ResolveAccount(srcId, ev.SrcInstId);
				if (account.empty()) { return; }
				GetMember(account).LastEventMs = aNowMs;
				m_Tracker.OnActivity(ev.Time, account);
				const ReviveSkill* skill = FindReviveSkill(ev.SkillId);
				if (skill && skill->IsPlayerCast)
				{
					m_Tracker.OnCastStart(ev.Time, account, ev.SkillId);
					GetMember(account).SeenGroups |= GroupBit(skill->Group);
				}
				return;
			}
			case ArcDps::CBTS_ANIMATIONSTOP:
			{
				const ReviveSkill* skill = FindReviveSkill(ev.SkillId);
				if (skill == nullptr) { return; }
				if (!skill->IsPlayerCast)
				{
					// The spirit's slam: owner by master instance id.
					std::string owner = AccountByInstance(ev.SrcMasterInstId);
					if (!owner.empty()) { m_Tracker.OnOwnedEffect(ev.Time, owner, ev.SkillId); }
					return;
				}
				std::string account = ResolveAccount(srcId, ev.SrcInstId);
				if (!account.empty()) { m_Tracker.OnCastStop(ev.Time, account, ev.SkillId, ev.Result, ev.BuffDmg); }
				return;
			}
			case ArcDps::CBTS_CHANGEDOWN:
			case ArcDps::CBTS_CHANGEUP:
			case ArcDps::CBTS_CHANGEDEAD:
			{
				std::string account = ResolveAccount(srcId, ev.SrcInstId);
				if (account.empty()) { return; }
				GetMember(account).LastEventMs = aNowMs;
				LifeState state = sc == ArcDps::CBTS_CHANGEDOWN ? LifeState::Downed
					: sc == ArcDps::CBTS_CHANGEDEAD ? LifeState::Dead : LifeState::Alive;
				m_Tracker.OnLifeState(ev.Time, account, state);
				return;
			}
			case ArcDps::CBTS_BUFFAPPLY:
			case ArcDps::CBTS_BUFFINITIAL:
			{
				// Any buff on a squad member shows they are within range of us.
				uint64_t dstId = aData.Dst ? aData.Dst->Id : ev.DstAgent;
				std::string account = ResolveAccount(dstId, ev.DstInstId);
				if (account.empty()) { return; }
				GetMember(account).LastEventMs = aNowMs;

				// A signet passive shows the signet is slotted, and that it is off cooldown right now.
				ReviveGroup group = ev.SkillId == kSignetOfMercyPassive ? ReviveGroup::SignetOfMercy
					: ev.SkillId == kSignetOfUndeathPassive ? ReviveGroup::SignetOfUndeath : ReviveGroup::Count;
				if (group == ReviveGroup::Count) { return; }
				GetMember(account).SeenGroups |= GroupBit(group);
				m_Tracker.MarkSkillReady(ev.Time, account, group);
				return;
			}
			default:
				return;
		}
	}

	void Session::OnAgentUpdate(const ArcDps::EvAgentUpdate& aUpdate, uint64_t aNowMs)
	{
		std::string account = aUpdate.Account;
		if (account.empty()) { return; }
		bool isSelf = aUpdate.Self != 0;
		bool added = aUpdate.Added != 0;
		m_Aliases.clear(); // may point at an entry being replaced or removed

		RosterMember& member = GetMember(account);
		if (isSelf)
		{
			m_SelfAccount = account;
			// Unofficial Extras may report our role before arcdps tells us which account is ours.
			if (!member.IsSelf && member.Role == SquadRole::None) { OnSelfLeftSquad(); }
			member.IsSelf = true;
			// Back from a loading screen: check afterwards who really came back with us.
			if (added) { m_ResyncAtMs = aNowMs + kResyncAfterMs; }
			if (!added)
			{
				// Our own map change: the squad leaves just before this are not real leaves.
				m_LastSelfLeaveMs = aNowMs;
				m_ResyncAtMs = 0; // we are loading; the check restarts once we are back on a map
				std::erase_if(m_PendingLeaves, [&](const PendingLeave& aLeave) { return aLeave.TimeMs + kSelfLeaveSlackMs >= aNowMs; });
			}
		}

		if (!added)
		{
			m_Agents.erase(aUpdate.Id);
			member.OnMap = false;
			int standing = SelfStanding(aNowMs);
			m_Tracker.SetAway(aNowMs, account, true);
			if (!isSelf && member.InSquad && InOrder(account))
			{
				m_PendingLeaves.push_back(PendingLeave{ account, aNowMs, member.Profession, standing });
			}
			return;
		}

		m_Agents[aUpdate.Id] = AgentRef{ account, static_cast<uint16_t>(aUpdate.InstanceId) };
		if (!member.OnMap) { m_Tracker.MarkSeenSince(aNowMs, account); }
		member.LastEventMs = aNowMs;
		uint32_t previousProfession = member.Profession;
		member.Character = aUpdate.Character;
		if (aUpdate.Profession != 0) { member.Profession = aUpdate.Profession; }
		member.Elite = aUpdate.Elite;
		member.Subgroup = aUpdate.Subgroup;
		member.OnMap = true;
		m_Tracker.SetAway(aNowMs, account, false);
		std::erase_if(m_PendingLeaves, [&](const PendingLeave& aLeave) { return aLeave.Account == account; });

		bool changed = previousProfession != 0 && aUpdate.Profession != 0 && previousProfession != aUpdate.Profession;
		if (changed) { m_Tracker.ForgetSkills(account); member.SeenGroups = 0; }
		if (isSelf || !InOrder(account)) { m_Reported.erase(account); return; }

		if (changed)
		{
			// A different profession is a different skill bar: the old revive skill state no longer applies.
			std::string text = Named(account) + " swapped to " + ProfessionShortName(member.Profession);
			if (!IsReviveProfession(member.Profession)) { text += " (no revive skill)"; }
			Notify(NoticeKind::ChangedProfession, account, text);
		}
		else if (m_Reported.count(account))
		{
			Notify(NoticeKind::Returned, account, Named(account) + " is back (" + ProfessionShortName(member.Profession) + ")");
		}
		m_Reported.erase(account);
	}

	void Session::OnRole(const std::string& aAccount, SquadRole aRole, uint16_t aSubgroup, uint64_t aNowMs)
	{
		if (aAccount.empty() || aRole == SquadRole::Unknown) { return; }
		m_HasRoles = true;
		RosterMember& member = GetMember(aAccount);
		member.Role = aRole;
		member.Subgroup = aSubgroup;

		if (member.IsSelf && aRole == SquadRole::None) { OnSelfLeftSquad(); }

		bool inSquad = aRole == SquadRole::Leader || aRole == SquadRole::Lieutenant || aRole == SquadRole::Member;
		if (inSquad == member.InSquad) { return; }
		member.InSquad = inSquad;
		if (inSquad || member.IsSelf || aRole != SquadRole::None) { return; } // invited/applied: not a leave

		// Left the squad: known at once, ~2.7 s before arcdps removes them.
		int standing = SelfStanding(aNowMs);
		m_Tracker.SetAway(aNowMs, aAccount, true);
		std::erase_if(m_PendingLeaves, [&](const PendingLeave& aLeave) { return aLeave.Account == aAccount; });
		if (InOrder(aAccount) && !m_Reported.count(aAccount))
		{
			// Somebody ahead of us dropping out is how the turn reaches us without anyone casting.
			Notify(NoticeKind::LeftSquad, aAccount, Named(aAccount) + " left the squad" +
				StandingChange(standing, SelfStanding(aNowMs)));
			m_Reported[aAccount] = true;
		}
	}

	void Session::OnSelfLeftSquad()
	{
		// Map changes never report role None (field log); it only comes when we really left, or at startup
		// when we aren't in a squad.
		if (m_Tracker.Order().empty()) { return; }
		m_Tracker.SetOrder({});
		m_PendingLeaves.clear();
		m_Reported.clear();
		ClearRequest();
		DismissShare();
		Notify(NoticeKind::OrderCleared, m_SelfAccount, "You left the squad: revive order cleared");
	}

	void Session::Tick(uint64_t aNowMs)
	{
		// Anyone in the order who didn't come back after our own map change left while we were loading.
		// Unofficial Extras reports a squad leave at once, even while we are loading, so this fallback is
		// only needed when it isn't running.
		if (m_ResyncAtMs != 0 && aNowMs >= m_ResyncAtMs)
		{
			m_ResyncAtMs = 0;
			if (m_HasRoles) { return; }
			for (const std::string& account : m_Tracker.Order())
			{
				auto it = m_Roster.find(account);
				if (it == m_Roster.end() || it->second.OnMap || it->second.IsSelf || m_Reported.count(account)) { continue; }
				Notify(NoticeKind::LeftMap, account, Named(account) + " left while you were loading");
				m_Reported[account] = true;
			}
		}

		while (!m_PendingLeaves.empty() && m_PendingLeaves.front().TimeMs + kLeaveGraceMs <= aNowMs)
		{
			PendingLeave leave = m_PendingLeaves.front();
			m_PendingLeaves.pop_front();
			if (m_LastSelfLeaveMs + kSelfLeaveSlackMs >= leave.TimeMs && m_LastSelfLeaveMs <= leave.TimeMs + kLeaveGraceMs) { continue; }

			auto it = m_Roster.find(leave.Account);
			if (it == m_Roster.end() || it->second.OnMap || !InOrder(leave.Account) || m_Reported.count(leave.Account)) { continue; }
			Notify(NoticeKind::LeftMap, leave.Account, Named(leave.Account) + " left the map" +
				StandingChange(leave.OurStanding, SelfStanding(aNowMs)));
			m_Reported[leave.Account] = true;
		}
	}

	void Session::SetOrder(std::vector<std::string> aAccounts)
	{
		m_Tracker.SetOrder(std::move(aAccounts));
		m_OrderFrom.clear();
	}

	void Session::SetAnswerRule(AnswerRule aRule)
	{
		m_AnswerRule = aRule;
	}

	void Session::SetShareRules(bool aFromLeaders, bool aFromAnyone)
	{
		m_ShareFromLeaders = aFromLeaders;
		m_ShareFromAnyone = aFromAnyone;
	}

	void Session::OnChatMessage(const std::string& aAccount, const std::string& aText, uint64_t aNowMs)
	{
		Share::Message message = Share::Parse(aText);
		if (message.What == Share::Kind::None) { return; }
		if (!aAccount.empty() && aAccount == m_SelfAccount) { return; } // our own paste

		std::string name = DisplayAccount(aAccount);
		if (message.What == Share::Kind::Request)
		{
			// Someone joined late and asked. Only a client that has an order can answer.
			// Only a client that has an order can answer one.
			if (m_Tracker.Order().empty()) { return; }
			m_RequestFrom = aAccount;
			m_RequestAtMs = aNowMs;
			Notify(NoticeKind::ShareRequested, aAccount, name + " asked for the revive order");
			return;
		}

		std::vector<RosterMember> roster;
		roster.reserve(m_Roster.size());
		for (const auto& [account, member] : m_Roster) { roster.push_back(member); }
		Share::Resolved resolved = Share::Resolve(message.Names, roster);
		if (resolved.Accounts.empty()) { return; }

		SquadRole role = SquadRole::Unknown;
		if (auto it = m_Roster.find(aAccount); it != m_Roster.end()) { role = it->second.Role; }

		std::string what = name + " shared a revive order (" + std::to_string(resolved.Accounts.size()) + " players)";
		if (!resolved.Unknown.empty()) { what += ", " + std::to_string(resolved.Unknown.size()) + " not in the squad"; }

		// The squad's own leadership may set the order without being asked; anyone else has to be let in.
		bool trusted = m_ShareFromAnyone ||
			(m_ShareFromLeaders && (role == SquadRole::Leader || role == SquadRole::Lieutenant));
		// Somebody answered a question that was going round the squad: nobody else needs to.
		ClearRequest();

		if (trusted)
		{
			int before = SelfPlace();
			SetOrder(resolved.Accounts);
			m_OrderFrom = aAccount;
			m_HasShare = false;
			Notify(NoticeKind::ShareApplied, aAccount, what + PlaceChange(before));
			return;
		}

		m_Share = SharedOrder{ aAccount, role, resolved.Accounts, resolved.Unknown, SelfPlace(), 0, aNowMs };
		auto mine = std::find(resolved.Accounts.begin(), resolved.Accounts.end(), m_SelfAccount);
		m_Share.OurPlaceThen = mine == resolved.Accounts.end() ? 0
			: static_cast<int>(mine - resolved.Accounts.begin()) + 1;
		m_HasShare = true;
		Notify(NoticeKind::ShareOffered, aAccount, what);
	}

	void Session::AcceptShare()
	{
		if (!m_HasShare) { return; }
		int before = SelfPlace();
		SetOrder(m_Share.Accounts);
		m_OrderFrom = m_Share.From;
		m_HasShare = false;
		Notify(NoticeKind::ShareApplied, m_Share.From, "using " + DisplayAccount(m_Share.From) + "'s revive order (" +
			std::to_string(m_Share.Accounts.size()) + " players)" + PlaceChange(before));
	}

	void Session::DismissShare()
	{
		m_HasShare = false;
		m_Share = SharedOrder{};
	}

	void Session::ClearRequest()
	{
		m_RequestFrom.clear();
		m_RequestAtMs = 0;
	}

	SessionView Session::GetView(uint64_t aNowMs) const
	{
		SessionView view;
		view.HasShare = m_HasShare;
		view.Share = m_Share;
		// An unanswered request stops asking after a while: by then the squad has moved on.
		// Only the client the order belongs to is asked to answer, so one question doesn't open a window on
		// every screen in the squad.
		bool answers = m_AnswerRule == AnswerRule::Always ||
			(m_AnswerRule == AnswerRule::WhenOrderIsOurs && OrderIsOurs());
		view.HasRequest = answers && !m_RequestFrom.empty() && aNowMs < m_RequestAtMs + kRequestShowMs &&
			!m_Tracker.Order().empty();
		if (view.HasRequest) { view.RequestFrom = m_RequestFrom; }
		view.Turn = m_Tracker.GetTurn(aNowMs);
		view.BackupIndex = view.Turn.BackupIndex;
		view.Order = m_Tracker.Order();
		view.SelfAccount = m_SelfAccount;
		view.SquadInCombat = m_CombatSinceMs != 0;
		view.NowMs = aNowMs;

		view.Roster.reserve(m_Roster.size());
		for (const auto& [account, member] : m_Roster) { view.Roster.push_back(member); }
		std::sort(view.Roster.begin(), view.Roster.end(), [](const RosterMember& a, const RosterMember& b)
		{
			bool aRevive = IsReviveProfession(a.Profession), bRevive = IsReviveProfession(b.Profession);
			if (aRevive != bRevive) { return aRevive; }
			return Lower(a.Account) < Lower(b.Account);
		});
		return view;
	}

	std::vector<Notice> Session::TakeNotices()
	{
		std::vector<Notice> notices;
		notices.swap(m_Notices);
		if (notices.size() > kMaxSeparateNotices)
		{
			std::string text = std::to_string(notices.size()) + " players in the order changed:";
			for (const Notice& notice : notices) { text += " " + notice.Text + ";"; }
			text.pop_back();
			notices.assign(1, Notice{ notices.front().Kind, "", text });
		}
		return notices;
	}
}
