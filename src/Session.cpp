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

		// Same ordering as comparing two Lower() copies, without building them. The roster is sorted on every
		// frame the overlay draws, so a comparator that allocates twice per comparison is a few hundred
		// allocations a frame, and it holds the session lock while the arcdps thread is waiting for it.
		bool LessNoCase(const std::string& aLeft, const std::string& aRight)
		{
			size_t shared = std::min(aLeft.size(), aRight.size());
			for (size_t i = 0; i < shared; i++)
			{
				unsigned char left = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(aLeft[i])));
				unsigned char right = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(aRight[i])));
				if (left != right) { return left < right; }
			}
			return aLeft.size() < aRight.size();
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
			case ArcDps::CBTS_SQCOMBATSTART: m_CombatSinceMs = aNowMs; m_OutOfCombatSinceMs = 0; return;
			case ArcDps::CBTS_SQCOMBATEND:   m_CombatSinceMs = 0; m_OutOfCombatSinceMs = aNowMs; return;
			case ArcDps::CBTS_ENTERCOMBAT:
			{
				// Fallback when the squad-wide events don't arrive. Somebody fighting again also means the break
				// that would have reset the turn is over.
				if (m_CombatSinceMs == 0) { m_CombatSinceMs = aNowMs; m_OutOfCombatSinceMs = 0; }
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
					TakeAttributions();
					return;
				}
				std::string account = ResolveAccount(srcId, ev.SrcInstId);
				if (account.empty()) { return; }

				// Worked out before the tracker marks the skill spent, which would take them out of the running.
				bool used = CountsAsUsed(*skill, ev.Result, ev.BuffDmg);
				TurnView turn = m_Tracker.GetTurn(ev.Time);
				bool inTurn = turn.UpIndex >= 0 && turn.Rows[turn.UpIndex].Account == account;
				const PlayerStatus* caster = m_Tracker.FindPlayer(account);
				CastEnd ending = CastEnd::Full;
				if (!used)
				{
					switch (ev.Result)
					{
						case ArcDps::ANIMSTOP_COMMAND:   ending = CastEnd::ByHand; break;
						case ArcDps::ANIMSTOP_INTERRUPT: ending = CastEnd::Interrupted; break;
						case ArcDps::ANIMSTOP_MOVEDODGE: ending = CastEnd::Movement; break;
						default: ending = CastEnd::ByHand; break;
					}
					// Going down mid-cast reads as a cancel too, and is worth telling apart.
					if (caster && caster->Life != LifeState::Alive) { ending = CastEnd::WentDown; }
				}
				m_Stats.OnCast(ev.Time, account, used, ending, inTurn);

				m_Tracker.OnCastStop(ev.Time, account, ev.SkillId, ev.Result, ev.BuffDmg);
				TakeAttributions();
				if (!m_IllusionApplySeen && skill->Group == ReviveGroup::IllusionOfLife && CountsAsUsed(*skill, ev.Result, ev.BuffDmg))
				{
					MatchIllusion(ev.Time, account, true);
				}
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
				TakeAttributions();
				if (state == LifeState::Downed) { m_Stats.OnDown(ev.Time, account, ChanceNow(ev.Time)); }
				else if (state == LifeState::Dead) { m_Stats.OnDead(ev.Time, account); }
				else { m_PendingUps.push_back(PendingUp{ account, ev.Time, aNowMs }); }
				// Down or dead under Illusion of Life: either it ran out, or they went down before it did. Both
				// leave nothing to warn about.
				if (state != LifeState::Alive) { m_Illusions.erase(account); }
				if (sc == ArcDps::CBTS_CHANGEUP && !m_IllusionApplySeen) { MatchIllusion(ev.Time, account, false); }
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

				// Illusion of Life on them. The effect itself is the only sure sign of who a cast revived: another
				// revive landing at the same moment, or a rally off an enemy dying, gets a player up without it
				// (field log 2026-09-15: four allies up at the instant of a cast that can only take three).
				if (ev.SkillId == kIllusionOfLifeEffect)
				{
					m_IllusionApplySeen = true;
					uint64_t length = ev.Value > 0 ? static_cast<uint64_t>(ev.Value) : 15000;
					std::string caster = ResolveAccount(srcId, ev.SrcInstId);
					m_Illusions[account] = IllusionState{ ev.Time + length, caster };
					// The effect landed, so the skill was spent even if its cast was reported as cancelled.
					m_Tracker.MarkUsed(ev.Time, caster, ReviveGroup::IllusionOfLife);
					return;
				}

				// A signet passive shows the signet is slotted, and that it is off cooldown right now.
				ReviveGroup group = ev.SkillId == kSignetOfMercyPassive ? ReviveGroup::SignetOfMercy
					: ev.SkillId == kSignetOfUndeathPassive ? ReviveGroup::SignetOfUndeath : ReviveGroup::Count;
				if (group == ReviveGroup::Count) { return; }
				GetMember(account).SeenGroups |= GroupBit(group);
				m_Tracker.MarkSkillReady(ev.Time, account, group);
				return;
			}
			case ArcDps::CBTS_BUFFREMOVE_ALL:
			case ArcDps::CBTS_BUFFREMOVE_SINGLE:
			{
				// Illusion of Life ending early: they killed something and rallied, so there is nothing left to
				// count down (field test 2026-09-16: removed with 4962 ms left at the moment the enemy died).
				if (ev.SkillId != kIllusionOfLifeEffect) { return; }
				std::string account = ResolveAccount(srcId, ev.SrcInstId); // on a removal, src is who loses it
				if (!account.empty()) { m_Illusions.erase(account); }
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
			// Whether a new map also swallows the first Illusion of Life apply isn't known yet, so the stand-in is
			// ready again after every one. Once an apply comes through it steps aside.
			if (added) { m_IllusionApplySeen = false; }
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
		std::string previousCharacter = member.Character;
		member.Character = aUpdate.Character;
		// While a player loads, arcdps reports profession 0 and elite 0: keep what was known.
		if (aUpdate.Profession != 0)
		{
			member.Profession = aUpdate.Profession;
			member.Elite = aUpdate.Elite;
		}
		// arcdps' subgroup is 1 for everybody in the field logs; Unofficial Extras' is the real one.
		if (!m_Subgroups.count(account)) { member.Subgroup = aUpdate.Subgroup; }
		member.OnMap = true;
		m_Tracker.SetAway(aNowMs, account, false);
		std::erase_if(m_PendingLeaves, [&](const PendingLeave& aLeave) { return aLeave.Account == account; });

		bool swappedProfession = previousProfession != 0 && aUpdate.Profession != 0 && previousProfession != aUpdate.Profession;
		// Another character of the same profession is still another skill bar. Placeholder names are never
		// compared: Edge of the Mists reports WvW ranks instead of character names, and everybody on the map
		// would look like they had just swapped.
		bool swappedCharacter = !swappedProfession && previousCharacter != member.Character &&
			IsUsableCharacterName(previousCharacter) && IsUsableCharacterName(member.Character);
		bool changed = swappedProfession || swappedCharacter;
		if (changed) { m_Tracker.ForgetSkills(account); member.SeenGroups = 0; }
		if (m_Substitutes && !m_Vacancies.empty())
		{
			// Swapped out to something without a revive skill and back again; or a substitute who moved in on
			// the wrong character and is now on the right one.
			if (!isSelf && !InOrder(account) && SureReviver(member)) { ReclaimPlace(account, aNowMs, SubgroupOf(account)); }
			MatchSubstitutes(aNowMs);
		}
		if (isSelf || !InOrder(account)) { m_Reported.erase(account); return; }

		if (changed)
		{
			int standing = SelfStanding(aNowMs);
			std::string text = Named(account) + (swappedProfession
				? " swapped to " + std::string(ProfessionShortName(member.Profession))
				: " swapped character");
			if (m_Substitutes && SureReviver(member))
			{
				// Druids and troubadours carry their revive skill on any character. Its state starts again unknown.
				text += ", keeps their place";
			}
			else
			{
				// A different skill bar, and no telling whether it carries a revive skill. The place stays open for
				// a substitute, or for them swapping back.
				if (!IsReviveProfession(member.Profession)) { text += " (no revive skill)"; }
				DropFromOrder(account, SubgroupOf(account), aNowMs);
				text += ", out of the order";
			}
			Notify(NoticeKind::ChangedProfession, account, text + StandingChange(standing, SelfStanding(aNowMs)));
			MatchSubstitutes(aNowMs);
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
		if (aSubgroup != 0) { member.Subgroup = aSubgroup; } // 0 only while loading or not in the squad

		if (member.IsSelf && aRole == SquadRole::None) { OnSelfLeftSquad(); }

		bool inSquad = aRole == SquadRole::Leader || aRole == SquadRole::Lieutenant || aRole == SquadRole::Member;
		uint16_t previousGroup = SubgroupOf(aAccount);
		if (inSquad && aSubgroup != 0 && aSubgroup != previousGroup)
		{
			bool toBench = previousGroup != 0 && IsBench(aSubgroup, aAccount);
			m_Subgroups[aAccount] = aSubgroup;
			std::erase_if(m_Arrivals, [&](const Arrival& aArrival) { return aArrival.Account == aAccount; });
			if (m_Substitutes)
			{
				if (previousGroup == 0)
				{
					// No subgroup before: they just joined the squad, or Unofficial Extras is listing the squad for
					// the first time (our own start or reload). Neither is moving in, and treating it as one would
					// let whoever already stood in a subgroup take a benched player's place on this client alone.
					ReclaimPlace(aAccount, aNowMs, 0);
				}
				else if (InOrder(aAccount))
				{
					if (toBench)
					{
						m_LastBenched = aSubgroup;
						int standing = SelfStanding(aNowMs);
						std::string name = Named(aAccount);
						DropFromOrder(aAccount, previousGroup, aNowMs);
						Notify(NoticeKind::Substituted, aAccount, name + " went to the bench, out of the order" +
							StandingChange(standing, SelfStanding(aNowMs)));
					}
				}
				else if (!ReclaimPlace(aAccount, aNowMs, aSubgroup))
				{
					m_Arrivals.push_back(Arrival{ aAccount, aSubgroup, aNowMs, IsBench(previousGroup, aAccount) });
				}
				MatchSubstitutes(aNowMs);
			}
		}

		if (inSquad == member.InSquad) { return; }
		member.InSquad = inSquad;
		if (inSquad || member.IsSelf || aRole != SquadRole::None) { return; } // invited/applied: not a leave

		// Left the squad: known at once, ~2.7 s before arcdps removes them.
		int standing = SelfStanding(aNowMs);
		m_Tracker.SetAway(aNowMs, aAccount, true);
		std::erase_if(m_PendingLeaves, [&](const PendingLeave& aLeave) { return aLeave.Account == aAccount; });
		std::erase_if(m_Arrivals, [&](const Arrival& aArrival) { return aArrival.Account == aAccount; });
		m_Subgroups.erase(aAccount);
		if (InOrder(aAccount))
		{
			// Their place is part of the message, so it is read off before they lose it.
			std::string name = Named(aAccount);
			bool report = !m_Reported.count(aAccount);
			DropFromOrder(aAccount, previousGroup, aNowMs);
			// Somebody ahead of us dropping out is how the turn reaches us without anyone casting.
			if (report)
			{
				Notify(NoticeKind::LeftSquad, aAccount, name + " left the squad, out of the order" +
					StandingChange(standing, SelfStanding(aNowMs)));
				m_Reported[aAccount] = true;
			}
			MatchSubstitutes(aNowMs);
		}
	}

	uint16_t Session::SubgroupOf(const std::string& aAccount) const
	{
		auto it = m_Subgroups.find(aAccount);
		return it == m_Subgroups.end() ? 0 : it->second;
	}

	bool Session::SureReviver(const RosterMember& aMember) const
	{
		constexpr uint32_t kDruid = 5, kTroubadour = 73;
		if (aMember.Profession == 0) { return false; }
		return aMember.Elite == kDruid || aMember.Elite == kTroubadour || aMember.SeenGroups != 0;
	}

	void Session::SetBench(uint16_t aBench)
	{
		uint16_t bench = Share::BenchFromName(Share::BenchName(aBench));
		if (bench != m_BenchGroup) { m_LastBenched = 0; }
		m_BenchGroup = bench;
	}

	bool Session::IsBench(uint16_t aGroup, const std::string& aMover) const
	{
		if (m_BenchGroup == 0 || aGroup == 0) { return false; }
		if (m_BenchGroup == Share::kBenchLast)
		{
			// The highest subgroup anybody is in, the mover counted where they still are. An empty subgroup past
			// it is a new group being built, unless it is the bench that the last swap emptied.
			uint16_t last = 0;
			for (const auto& [account, group] : m_Subgroups) { last = std::max(last, group); }
			bool emptiedBench = aGroup > last && aGroup == m_LastBenched;
			if (aGroup != last && !emptiedBench) { return false; }
		}
		else if (aGroup != m_BenchGroup) { return false; }

		for (const std::string& other : m_Tracker.Order())
		{
			if (other != aMover && SubgroupOf(other) == aGroup) { return false; }
		}
		return true;
	}

	void Session::SetSubstitutes(bool aEnabled)
	{
		m_Substitutes = aEnabled;
		if (!aEnabled)
		{
			m_Vacancies.clear();
			m_Arrivals.clear();
		}
	}

	void Session::DropFromOrder(const std::string& aAccount, uint16_t aOpenFor, uint64_t aNowMs)
	{
		const std::vector<std::string>& order = m_Tracker.Order();
		auto it = std::find(order.begin(), order.end(), aAccount);
		if (it == order.end()) { return; }
		size_t place = static_cast<size_t>(it - order.begin());
		bool precast = std::find(m_Precast.begin(), m_Precast.end(), aAccount) != m_Precast.end();
		bool open = m_Substitutes && aOpenFor != 0;

		// Open places are kept as "in front of whoever is at Index". The ones in front of this player stay; the
		// ones right behind now wait in front of the player after them, behind this place.
		uint32_t ahead = 0;
		for (const Vacancy& vacancy : m_Vacancies) { if (vacancy.Index == place) { ahead++; } }
		for (Vacancy& vacancy : m_Vacancies)
		{
			if (vacancy.Index <= place) { continue; }
			if (vacancy.Index == place + 1) { vacancy.Tie += ahead + (open ? 1 : 0); }
			vacancy.Index--;
		}

		// Not SetOrder: that treats any change as a new order and starts the rotation again, which would move
		// everyone's turn because one person walked away.
		m_Tracker.RemoveFromOrder(aAccount);
		std::erase(m_Precast, aAccount);
		std::erase_if(m_Vacancies, [&](const Vacancy& aVacancy) { return aVacancy.Account == aAccount; });
		if (open)
		{
			auto member = m_Roster.find(aAccount);
			uint32_t profession = member == m_Roster.end() ? 0 : member->second.Profession;
			m_Vacancies.push_back(Vacancy{ aAccount, aOpenFor, profession, aNowMs, place, ahead, precast });
		}
	}

	void Session::FillPlace(size_t aIndex, const std::string& aAccount)
	{
		Vacancy filled = m_Vacancies[aIndex];
		m_Vacancies.erase(m_Vacancies.begin() + static_cast<std::ptrdiff_t>(aIndex));
		m_Tracker.InsertIntoOrder(filled.Index, aAccount);
		if (filled.Precast && std::find(m_Precast.begin(), m_Precast.end(), aAccount) == m_Precast.end())
		{
			m_Precast.push_back(aAccount);
		}
		for (Vacancy& vacancy : m_Vacancies)
		{
			if (vacancy.Index > filled.Index) { vacancy.Index++; }
			else if (vacancy.Index == filled.Index && vacancy.Tie > filled.Tie)
			{
				vacancy.Index++;
				vacancy.Tie -= filled.Tie + 1;
			}
		}
		std::erase_if(m_Arrivals, [&](const Arrival& aArrival) { return aArrival.Account == aAccount; });
		m_Reported.erase(aAccount);
	}

	bool Session::ReclaimPlace(const std::string& aAccount, uint64_t aNowMs, uint16_t aSubgroup)
	{
		if (InOrder(aAccount)) { return false; }
		for (size_t i = 0; i < m_Vacancies.size(); i++)
		{
			const Vacancy& vacancy = m_Vacancies[i];
			if (vacancy.Account != aAccount || vacancy.TimeMs + kSubstituteWindowMs < aNowMs) { continue; }
			if (aSubgroup != 0 && vacancy.Subgroup != aSubgroup) { continue; }
			int standing = SelfStanding(aNowMs);
			FillPlace(i, aAccount);
			Notify(NoticeKind::Returned, aAccount, Named(aAccount) + " is back in their place" +
				StandingChange(standing, SelfStanding(aNowMs)));
			return true;
		}
		return false;
	}

	void Session::MatchSubstitutes(uint64_t aNowMs)
	{
		auto stale = [aNowMs](uint64_t aTimeMs) { return aTimeMs + kSubstituteWindowMs < aNowMs; };
		std::erase_if(m_Vacancies, [&](const Vacancy& aVacancy) { return stale(aVacancy.TimeMs) || InOrder(aVacancy.Account); });
		std::erase_if(m_Arrivals, [&](const Arrival& aArrival) { return stale(aArrival.TimeMs); });

		auto professionOf = [this](const std::string& aAccount)
		{
			auto it = m_Roster.find(aAccount);
			return it == m_Roster.end() ? 0u : it->second.Profession;
		};

		for (size_t i = 0; i < m_Vacancies.size(); )
		{
			const Vacancy vacancy = m_Vacancies[i];

			auto sure = [this](const std::string& aAccount)
			{
				auto member = m_Roster.find(aAccount);
				// On the wrong character, or not on our map yet: waits, and counts once that changes.
				return member != m_Roster.end() && SureReviver(member->second);
			};
			std::vector<std::string> candidates;
			for (const Arrival& arrival : m_Arrivals)
			{
				if (arrival.Subgroup != vacancy.Subgroup || SubgroupOf(arrival.Account) != vacancy.Subgroup ||
					InOrder(arrival.Account) || !sure(arrival.Account)) { continue; }
				candidates.push_back(arrival.Account);
			}
			// Nobody moved into the subgroup this place was left from: in a rotation of three or more the
			// substitute lands somewhere else entirely, and all that marks them is coming off the bench.
			bool offBench = candidates.empty();
			if (offBench)
			{
				for (const Arrival& arrival : m_Arrivals)
				{
					if (!arrival.FromBench || InOrder(arrival.Account) || !sure(arrival.Account)) { continue; }
					candidates.push_back(arrival.Account);
				}
			}
			if (candidates.empty()) { i++; continue; }

			// Places waiting for the same candidates: the ones left from this subgroup, or every open place
			// when the match is by coming off the bench.
			int holes = 0, sameHoles = 0;
			for (const Vacancy& other : m_Vacancies)
			{
				if (!offBench && other.Subgroup != vacancy.Subgroup) { continue; }
				holes++;
				if (other.Profession == vacancy.Profession) { sameHoles++; }
			}

			std::string pick;
			if (candidates.size() == 1 && holes == 1) { pick = candidates[0]; }
			else
			{
				// Several at once: a druid replaces a druid. Only when that leaves no doubt either way.
				std::vector<std::string> same;
				for (const std::string& candidate : candidates)
				{
					if (professionOf(candidate) == vacancy.Profession) { same.push_back(candidate); }
				}
				if (same.size() == 1 && sameHoles == 1) { pick = same[0]; }
				else if (static_cast<int>(candidates.size()) >= holes)
				{
					// Everybody has moved and it still can't be told apart: say so once and leave it to them.
					uint16_t group = vacancy.Subgroup;
					std::string names;
					for (const Vacancy& other : m_Vacancies)
					{
						if (offBench || other.Subgroup == group) { names += (names.empty() ? "" : ", ") + DisplayAccount(other.Account); }
					}
					Notify(NoticeKind::Substituted, vacancy.Account, "Can't tell who replaces " + names +
						", add them by hand");
					bool all = offBench;
					std::erase_if(m_Vacancies, [group, all](const Vacancy& aOther) { return all || aOther.Subgroup == group; });
					std::erase_if(m_Arrivals, [group, all](const Arrival& aOther) { return all || aOther.Subgroup == group; });
					i = 0;
					continue;
				}
				else { i++; continue; } // more moves are on their way
			}

			int standing = SelfStanding(aNowMs);
			FillPlace(i, pick);
			Notify(NoticeKind::Substituted, pick, Named(pick) + " took the place of " + DisplayAccount(vacancy.Account) +
				StandingChange(standing, SelfStanding(aNowMs)));
			i = 0;
		}
	}

	FightStats::Chance Session::ChanceNow(uint64_t aTimeMs) const
	{
		FightStats::Chance chance;
		TurnView turn = m_Tracker.GetTurn(aTimeMs);
		if (turn.UpIndex >= 0) { chance.UpAccount = turn.Rows[turn.UpIndex].Account; }
		for (const OrderRow& row : turn.Rows)
		{
			if (row.Status != Eligibility::Ready) { continue; }
			// How many allies that player could pick up with what they have: the skill they have been seen
			// using, or the one their profession carries.
			ReviveGroup group = ReviveGroup::Count;
			if (row.Player != nullptr && !row.Player->Skills.empty()) { group = row.Player->Skills.front().Group; }
			uint8_t targets = group == ReviveGroup::Count ? 0 : GroupTargets(group);
			chance.Capacity += targets > 0 ? targets : 1; // not seen casting yet: assume they can pick up one
		}
		return chance;
	}

	void Session::TakeAttributions()
	{
		for (Attribution& attribution : m_Tracker.TakeAttributions())
		{
			m_Stats.OnRevived(attribution.TimeMs, attribution.Reviver);
			m_Attributions.push_back(std::move(attribution));
		}
		constexpr size_t kKeep = 32;
		if (m_Attributions.size() > kKeep) { m_Attributions.erase(m_Attributions.begin(), m_Attributions.end() - kKeep); }
	}

	void Session::SettleUps(uint64_t aNowMs)
	{
		// An ally getting up is matched with the cast that did it a moment later, so each up waits before it is
		// counted as revived or as a rally.
		constexpr uint64_t kSameUpMs = 1500;
		for (size_t i = 0; i < m_PendingUps.size(); )
		{
			const PendingUp& up = m_PendingUps[i];
			if (aNowMs < up.ArrivedMs + kUpSettleMs) { i++; continue; }
			std::string reviver;
			for (const Attribution& attribution : m_Attributions)
			{
				uint64_t apart = attribution.TimeMs > up.TimeMs ? attribution.TimeMs - up.TimeMs : up.TimeMs - attribution.TimeMs;
				if (attribution.Revived == up.Account && apart <= kSameUpMs) { reviver = attribution.Reviver; break; }
			}
			m_Stats.OnUp(up.TimeMs, up.Account, reviver);
			m_PendingUps.erase(m_PendingUps.begin() + static_cast<std::ptrdiff_t>(i));
		}
	}

	void Session::MatchIllusion(uint64_t aTimeMs, const std::string& aAccount, bool aIsCast)
	{
		// In the field logs the ally gets up at exactly the millisecond the cast completes, and the two events
		// arrive in either order. Anyone getting up at any other moment was revived some other way.
		constexpr uint64_t kSameInstantMs = 50;
		constexpr uint64_t kKeepMs        = 2000;
		constexpr uint64_t kLengthMs      = 15000;
		constexpr int      kTargets       = 3;
		auto stale = [aTimeMs](const RecentEvent& aEvent) { return aEvent.TimeMs + kKeepMs < aTimeMs; };
		std::erase_if(m_RecentIllusionCasts, stale);
		std::erase_if(m_RecentGotUp, stale);
		auto sameInstant = [aTimeMs](uint64_t aOther) { return (aOther > aTimeMs ? aOther - aTimeMs : aTimeMs - aOther) <= kSameInstantMs; };

		if (aIsCast)
		{
			int targets = 0;
			for (const RecentEvent& up : m_RecentGotUp)
			{
				if (targets == kTargets || !sameInstant(up.TimeMs) || up.Account == aAccount) { continue; }
				m_Illusions[up.Account] = IllusionState{ aTimeMs + kLengthMs, aAccount };
				targets++;
			}
			m_RecentIllusionCasts.push_back(RecentEvent{ aTimeMs, aAccount });
			return;
		}

		for (const RecentEvent& cast : m_RecentIllusionCasts)
		{
			if (!sameInstant(cast.TimeMs) || cast.Account == aAccount) { continue; }
			m_Illusions[aAccount] = IllusionState{ cast.TimeMs + kLengthMs, cast.Account };
			break;
		}
		m_RecentGotUp.push_back(RecentEvent{ aTimeMs, aAccount });
	}

	// Whether we could revive somebody right now: a revive profession, on our feet, on this map, and a revive
	// skill that is ready. Not having seen our skill used yet counts as ready, as it does in the order.
	bool Session::SelfCanRevive(uint64_t aNowMs) const
	{
		if (m_SelfAccount.empty()) { return false; }
		auto member = m_Roster.find(m_SelfAccount);
		if (member == m_Roster.end() || !IsReviveProfession(member->second.Profession)) { return false; }
		const PlayerStatus* self = m_Tracker.FindPlayer(m_SelfAccount);
		if (self == nullptr) { return true; }
		if (self->Away || self->Life != LifeState::Alive) { return false; }
		if (self->Skills.empty()) { return true; }
		for (const SkillStatus& skill : self->Skills)
		{
			if (skill.State == SkillState::Ready || (skill.State == SkillState::Cooldown && skill.ReadyAtMs <= aNowMs)) { return true; }
		}
		return false;
	}

	void Session::OnSelfLeftSquad()
	{
		// Map changes never report role None (field log); it only comes when we really left, or at startup
		// when we aren't in a squad.
		if (m_Tracker.Order().empty()) { return; }
		m_Tracker.SetOrder({});
		m_PendingLeaves.clear();
		m_Reported.clear();
		m_Vacancies.clear();
		m_Arrivals.clear();
		m_Subgroups.clear();
		m_BenchGroup = 0;
		m_LastBenched = 0;
		ClearRequest();
		DismissShare();
		Notify(NoticeKind::OrderCleared, m_SelfAccount, "You left the squad: revive order cleared");
	}

	void Session::Tick(uint64_t aNowMs)
	{
		std::erase_if(m_Illusions, [aNowMs](const auto& aEntry) { return aEntry.second.EndsAt <= aNowMs; });
		// A profession arcdps only now reported can settle a swap that was waiting on it.
		if (!m_Vacancies.empty()) { MatchSubstitutes(aNowMs); }

		// Out of a fight the turn is always at the top of the order (decided with the squad, 2026-09-16).
		// Field test: whoever cast last was remembered into the next fight, so the turn started somewhere in
		// the middle of the list with every skill ready, and reordering anyone was the only way back. A break
		// shorter than kRotationResetMs is the same fight carrying on, and the rotation carries on with it.
		// It holds for as long as the squad stays out of combat, so a revive used between fights (or before the
		// first one) doesn't carry into the next fight either.
		SettleUps(aNowMs);
		if (m_CombatSinceMs == 0)
		{
			if (m_OutOfCombatSinceMs == 0) { m_OutOfCombatSinceMs = aNowMs; }
			if (aNowMs >= m_OutOfCombatSinceMs + kRotationResetMs)
			{
				m_Tracker.ResetRotation();
				// The engagement is over, so its stats are final. Same break as the rotation reset: arcdps ends a
				// fight at every lull, and a squad would call that one fight.
				if (m_Stats.Active())
				{
					FightStat fight;
					if (m_Stats.Close(aNowMs, m_FightNumber + 1, fight) && fight.Downs > 0)
					{
						m_FightNumber++;
						m_Fights.push_back(fight);
						if (m_Fights.size() > kKeepFights) { m_Fights.erase(m_Fights.begin()); }
						Notify(NoticeKind::FightSummary, {}, "Fight " + std::to_string(fight.Number) + ": " + fight.Summary());
					}
				}
			}
		}

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
		// Open places are positions in the order they were left from: a new order makes them meaningless.
		if (aAccounts != m_Tracker.Order()) { m_Vacancies.clear(); }
		m_Tracker.SetOrder(std::move(aAccounts));
		m_OrderFrom.clear();
	}

	void Session::SetPrecast(std::vector<std::string> aAccounts)
	{
		m_Precast = std::move(aAccounts);
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
		// Our own paste: normally the order it names is already ours, but it may have been typed by hand or
		// pasted from somewhere else, and then this is how we get it (field test 2026-09-17).
		bool ours = !aAccount.empty() && aAccount == m_SelfAccount;

		std::string name = DisplayAccount(aAccount);
		if (message.What == Share::Kind::Remove)
		{
			// Taking yourself out needs nobody's approval, so every client does it and the order stays the same
			// everywhere. Somebody who swapped off a revive build is the usual reason.
			std::erase_if(m_Requests, [&](const JoinRequest& aRequest) { return aRequest.Account == aAccount; });
			if (!InOrder(aAccount)) { return; }
			int standing = SelfStanding(aNowMs);
			std::string named = Named(aAccount);
			DropFromOrder(aAccount);
			Notify(NoticeKind::LeftSquad, aAccount, named + " asked to be taken out of the order" +
				StandingChange(standing, SelfStanding(aNowMs)));
			return;
		}
		if (message.What == Share::Kind::Add)
		{
			if (ours || !AnswersRequests()) { return; }
			auto member = m_Roster.find(aAccount);
			if (member == m_Roster.end() || !member->second.InSquad) { return; }
			if (InOrder(aAccount)) { return; } // already in it: nothing to decide

			auto waiting = std::find_if(m_Requests.begin(), m_Requests.end(),
				[&](const JoinRequest& aRequest) { return aRequest.Account == aAccount; });
			if (waiting != m_Requests.end())
			{
				waiting->Place = message.Place;
				waiting->TimeMs = aNowMs;
			}
			else
			{
				if (m_Requests.size() >= kMaxRequests) { m_Requests.pop_front(); }
				m_Requests.push_back(JoinRequest{ aAccount, message.Place, aNowMs });
			}

			uint64_t& announced = m_AskedNoticeMs[aAccount];
			if (aNowMs >= announced + kAskAgainMs || announced == 0)
			{
				announced = aNowMs;
				std::string where = message.Place > 0 ? " at " + std::to_string(message.Place) + "." : "";
				Notify(NoticeKind::ShareRequested, aAccount, name + " asked to be in the revive order" + where);
			}
			return;
		}

		std::vector<RosterMember> roster;
		roster.reserve(m_Roster.size());
		for (const auto& [account, member] : m_Roster) { roster.push_back(member); }
		Share::Resolved resolved = Share::Resolve(message.Entries, roster);
		if (resolved.Accounts.empty()) { return; }

		if (ours)
		{
			if (resolved.Accounts == m_Tracker.Order() && resolved.Precast == m_Precast && message.Bench == m_BenchGroup) { return; }
			int before = SelfPlace();
			SetOrder(resolved.Accounts);
			SetPrecast(resolved.Precast);
			SetBench(message.Bench);
			Notify(NoticeKind::ShareApplied, m_SelfAccount, "using the revive order you pasted (" +
				std::to_string(resolved.Accounts.size()) + " players)" + PlaceChange(before));
			return;
		}

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
			SetPrecast(resolved.Precast);
			SetBench(message.Bench);
			m_OrderFrom = aAccount;
			m_HasShare = false;
			Notify(NoticeKind::ShareApplied, aAccount, what + PlaceChange(before));
			return;
		}

		m_Share = SharedOrder{ aAccount, role, resolved.Accounts, resolved.Precast, resolved.Unknown, SelfPlace(), 0, aNowMs,
			message.Bench };
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
		SetPrecast(m_Share.Precast);
		SetBench(m_Share.Bench);
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

	void Session::ClearRequest(const std::string& aAccount)
	{
		if (aAccount.empty()) { m_Requests.clear(); return; }
		std::erase_if(m_Requests, [&](const JoinRequest& aRequest) { return aRequest.Account == aAccount; });
	}

	bool Session::AnswersRequests() const
	{
		if (m_AnswerRule == AnswerRule::Never) { return false; }
		if (m_AnswerRule == AnswerRule::Always) { return true; }
		if (!m_Tracker.Order().empty()) { return OrderIsOurs(); }

		// No order yet, so nobody owns one: the commander is asked to start it. If no commander is running the
		// addon, their lieutenants are, which is as close to one client as the roster can tell us.
		auto self = m_Roster.find(m_SelfAccount);
		if (self == m_Roster.end()) { return false; }
		if (self->second.Role == SquadRole::Leader) { return true; }
		if (self->second.Role != SquadRole::Lieutenant) { return false; }
		return std::none_of(m_Roster.begin(), m_Roster.end(), [](const auto& aEntry)
		{
			return aEntry.second.Role == SquadRole::Leader && aEntry.second.InSquad;
		});
	}

	SessionView Session::GetView(uint64_t aNowMs) const
	{
		SessionView view;
		view.HasShare = m_HasShare;
		view.Share = m_Share;
		// Unanswered requests stop asking after a while: by then the squad has moved on. Only one client is
		// asked to answer, so one player asking doesn't open a window on every screen in the squad.
		if (AnswersRequests())
		{
			for (const JoinRequest& request : m_Requests)
			{
				if (aNowMs < request.TimeMs + kRequestShowMs && !InOrder(request.Account)) { view.Requests.push_back(request); }
			}
		}
		view.Turn = m_Tracker.GetTurn(aNowMs);
		view.BackupIndex = view.Turn.BackupIndex;
		view.Order = m_Tracker.Order();
		view.Precast = m_Precast;
		view.Bench = m_BenchGroup;
		view.SelfAccount = m_SelfAccount;
		view.SquadInCombat = m_CombatSinceMs != 0;
		view.NowMs = aNowMs;
		view.SelfCanRevive = SelfCanRevive(aNowMs);
		for (const auto& [account, illusion] : m_Illusions)
		{
			// Our own Illusion of Life is no use to warn us about: we can't revive ourselves.
			if (account == m_SelfAccount || illusion.EndsAt <= aNowMs) { continue; }
			bool ourCast = !m_SelfAccount.empty() && illusion.Caster == m_SelfAccount;
			view.Illusions.push_back(IllusionTarget{ account, illusion.EndsAt - aNowMs, ourCast });
		}
		std::sort(view.Illusions.begin(), view.Illusions.end(),
			[](const IllusionTarget& a, const IllusionTarget& b) { return a.EndsInMs < b.EndsInMs; });

		view.Roster.reserve(m_Roster.size());
		for (const auto& [account, member] : m_Roster) { view.Roster.push_back(member); }
		std::sort(view.Roster.begin(), view.Roster.end(), [](const RosterMember& a, const RosterMember& b)
		{
			bool aRevive = IsReviveProfession(a.Profession), bRevive = IsReviveProfession(b.Profession);
			if (aRevive != bRevive) { return aRevive; }
			return LessNoCase(a.Account, b.Account);
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
