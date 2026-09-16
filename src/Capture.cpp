#include "Capture.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iterator>
#include <map>
#include <mutex>
#include <unordered_map>

#include <mmsystem.h>

#include "Recorder.h"
#include "ReviveSkills.h"
#include "unofficial_extras/Definitions.h"

namespace Capture
{
	std::atomic<bool> LogAllEvents{false};
	std::atomic<bool> LogKeys{false};

	namespace
	{
		constexpr size_t   kFeedLimit      = 60;
		constexpr size_t   kCommandLimit   = 20;
		constexpr int64_t  kMaxSaneDelayMs = 10 * 60 * 1000;
		constexpr uint32_t kPositionEveryMs = 1000;
		constexpr uint32_t kSeenEveryMs     = 10 * 1000;
		constexpr uint32_t kClockEveryMs    = 60 * 1000;

		constexpr uint8_t CBTS_SQCOMBATSTART = 9;
		constexpr uint8_t CBTS_SQCOMBATEND   = 10;
		constexpr uint8_t CBTS_BUFFINITIAL   = 18;
		constexpr uint8_t CBTB_MANUAL        = 3;

		// Conditions/boons that change cast speed (quickness, slow) or recharge speed (chilled, alacrity).
		enum TrackedBuff { TB_QUICKNESS, TB_SLOW, TB_CHILLED, TB_ALACRITY, TB_COUNT };
		constexpr uint32_t    kTrackedBuffIds[TB_COUNT]   = { 1187, 26766, 722, 30328 };
		constexpr const char* kTrackedBuffNames[TB_COUNT] = { "quick", "slow", "chill", "alac" };

		// Buffs recorded for everyone: revive effects, and signet passives whose return marks a recharged signet.
		constexpr uint32_t kReviveBuffIds[] = {
			9162,  // Signet of Mercy passive
			10610, // Signet of Undeath passive
			5764,  // Renewal of Fire self-revive
			5765,  // Renewal of Air
			848,   // Resurrection (after getting up)
			10346, // Illusion of Life on a revived ally: 15 s, then down again unless they rally. The apply was
			       // already logged by name; this adds the removal, which is what tells a rally from a timeout.
		};

		// Players are identified by account name. Character names are unreliable: in Edge of the Mists players
		// from other worlds show as localized rank names ("Diamond Legend"), which repeat. The arcdps agent id
		// is the key into this table, with the map instance id as fallback because player ids can change
		// mid-map (arcdps' iid change isn't sent to the live feed).
		struct AgentState
		{
			std::string Character;
			std::string Account;
			uint16_t    InstanceId     = 0;
			bool        IsSelf         = false;
			bool        IsReviveCaster = false;
			uint32_t    EventsInWindow = 0;
			uint64_t    LastEventMs    = 0;
			uint64_t    BuffExpiry[TB_COUNT] = {};
		};

		Recorder                           s_Recorder;
		std::mutex                         s_Mutex; // guards everything below
		DelayStat                          s_Delay[CH_COUNT][WHO_COUNT][SCOPE_COUNT];
		DelayStat                          s_WindowDelay[WHO_COUNT]; // squad feed, reset every clock row
		std::deque<FeedRow>                s_ReviveFeed;
		std::deque<FeedRow>                s_SelfFeed;
		std::map<std::string, Member>      s_Squad; // by account name (with leading ':')
		std::unordered_map<uint64_t, AgentState> s_Agents; // squad players by arcdps agent id (from join events)
		std::unordered_map<uint64_t, uint64_t>   s_IdAliases; // agent id seen in events -> id in s_Agents
		std::vector<std::string>                 s_PendingInfo; // info rows produced under s_Mutex, written after
		std::deque<ChatCommand>            s_Commands;
		std::string                        s_SelfAccount;
		std::string                        s_SelfCharacter;
		uint32_t                           s_MarkCount = 0;

		std::atomic<uint8_t>  s_MapType{0};
		std::atomic<uint32_t> s_MapId{0};
		std::atomic<uint64_t> s_ArcEvents{0};
		std::atomic<uint64_t> s_UeEvents{0};

		// Render thread only.
		uint32_t s_LastPositionMs = 0;
		uint32_t s_LastSeenMs     = 0;
		uint32_t s_LastClockMs    = 0;

		const char* ChannelName(Channel aChannel) { return aChannel == CH_SQUAD ? "SQUAD" : "LOCAL"; }

		bool IsWvwMap(uint8_t aMapType)
		{
			// MumbleLink map types 9-15: Eternal Battlegrounds, the three borderlands, Fortune's Vale,
			// Obsidian Sanctum, Edge of the Mists. The lobby (18) is excluded.
			return aMapType >= 9 && aMapType <= 15;
		}

		int FindTrackedBuff(uint32_t aSkillId)
		{
			for (int i = 0; i < TB_COUNT; i++)
			{
				if (kTrackedBuffIds[i] == aSkillId) { return i; }
			}
			return -1;
		}

		bool IsReviveBuff(uint32_t aSkillId)
		{
			return std::find(std::begin(kReviveBuffIds), std::end(kReviveBuffIds), aSkillId) != std::end(kReviveBuffIds);
		}

		std::string WallClock()
		{
			auto now = std::chrono::system_clock::now();
			std::time_t t = std::chrono::system_clock::to_time_t(now);
			int ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
			std::tm local{};
			localtime_s(&local, &t);
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
				local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec, ms);
			return buf;
		}

		void PushFeed(std::deque<FeedRow>& aFeed, const FeedRow& aRow)
		{
			aFeed.push_back(aRow);
			while (aFeed.size() > kFeedLimit) { aFeed.pop_front(); }
		}

		std::string Str(const char* aText) { return aText ? aText : ""; }

		// Squad player for an event's agent, or null. Falls back to the map instance id when the agent id is
		// unknown (player id changed); the new id is then remembered as an alias. Caller holds s_Mutex.
		AgentState* FindAgent(uint64_t aId, uint16_t aInstanceId)
		{
			if (aId == 0) { return nullptr; }
			auto it = s_Agents.find(aId);
			if (it != s_Agents.end()) { return &it->second; }

			auto alias = s_IdAliases.find(aId);
			if (alias != s_IdAliases.end())
			{
				auto aliased = s_Agents.find(alias->second);
				if (aliased != s_Agents.end()) { return &aliased->second; }
			}

			if (aInstanceId == 0) { return nullptr; }
			for (auto& [id, agent] : s_Agents)
			{
				if (agent.InstanceId == aInstanceId)
				{
					s_IdAliases[aId] = id;
					s_PendingInfo.push_back("agent id alias new_id=" + std::to_string(aId) + " known_id=" + std::to_string(id) +
						" instance=" + std::to_string(aInstanceId) + " account=" + agent.Account);
					return &agent;
				}
			}
			return nullptr;
		}

		// Duration-stacking model of a tracked buff, in arcdps event time. Buffs are applied to dst and
		// removed from src. Caller holds s_Mutex.
		void UpdateTrackedBuff(const ArcDps::CombatEvent& aEv, AgentState* aSrc, AgentState* aDst, int aBuff)
		{
			switch (aEv.IsStatechange)
			{
				case ArcDps::CBTS_BUFFAPPLY:
				case CBTS_BUFFINITIAL:
				{
					if (aDst == nullptr) { return; }
					uint64_t& expiry = aDst->BuffExpiry[aBuff];
					expiry = std::max(expiry, aEv.Time) + static_cast<uint64_t>(std::max(aEv.Value, 0));
					return;
				}
				case ArcDps::CBTS_BUFFREMOVE_ALL:
				{
					if (aSrc) { aSrc->BuffExpiry[aBuff] = aEv.Time; }
					return;
				}
				case ArcDps::CBTS_BUFFREMOVE_SINGLE:
				{
					if (aSrc == nullptr || aEv.IsBuffRemove == CBTB_MANUAL) { return; } // manual: synthesized alongside REMOVE_ALL
					uint64_t& expiry = aSrc->BuffExpiry[aBuff];
					uint64_t removed = static_cast<uint64_t>(std::max(aEv.Value, 0));
					expiry = expiry > aEv.Time + removed ? expiry - removed : aEv.Time;
					return;
				}
				default:
					return;
			}
		}

		// "quick=1200 slow=0 chill=0 alac=5300": remaining ms of each tracked buff. Caller holds s_Mutex.
		std::string BuffSnapshot(const AgentState* aAgent, uint64_t aTime)
		{
			if (aAgent == nullptr) { return ""; }
			std::string out;
			for (int i = 0; i < TB_COUNT; i++)
			{
				uint64_t expiry = aAgent->BuffExpiry[i];
				if (!out.empty()) { out.push_back(' '); }
				out += kTrackedBuffNames[i];
				out.push_back('=');
				out += std::to_string(expiry > aTime ? expiry - aTime : 0);
			}
			return out;
		}

		void WriteInfoRow(const char* aKind, const std::string& aDetail)
		{
			CsvRow row;
			row[COL_KIND] = aKind;
			row[COL_ARRIVE_MS] = std::to_string(timeGetTime());
			row[COL_DETAIL] = aDetail;
			s_Recorder.Write(row);
		}
	}

	void DelayStat::Add(uint32_t aMs)
	{
		Count++;
		Sum += aMs;
		Last = aMs;
		if (aMs < Min) { Min = aMs; }
		if (aMs > Max) { Max = aMs; }
	}

	const char* RoleName(uint8_t aRole)
	{
		switch (static_cast<UserRole>(aRole))
		{
			case UserRole::SquadLeader: return "SquadLeader";
			case UserRole::Lieutenant:  return "Lieutenant";
			case UserRole::Member:      return "Member";
			case UserRole::Invited:     return "Invited";
			case UserRole::Applied:     return "Applied";
			case UserRole::None:        return "None";
			default:                    return "Invalid";
		}
	}

	bool Start(const std::filesystem::path& aLogDirectory, const char* aAddonVersion)
	{
		auto now = std::chrono::system_clock::now();
		std::time_t t = std::chrono::system_clock::to_time_t(now);
		std::tm local{};
		localtime_s(&local, &t);
		char name[64];
		std::snprintf(name, sizeof(name), "rezz_%04d%02d%02d_%02d%02d%02d.csv",
			local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);

		if (!s_Recorder.Start(aLogDirectory / name)) { return false; }

		WriteInfoRow("INFO", "session start wall=" + WallClock() + " addon=" + Str(aAddonVersion));
		return true;
	}

	void Stop()
	{
		WriteInfoRow("INFO", "session end wall=" + WallClock() +
			" arc_events=" + std::to_string(s_ArcEvents.load()) + " ue_events=" + std::to_string(s_UeEvents.load()));
		s_Recorder.Stop();
	}

	Snapshot GetSnapshot()
	{
		Snapshot snap;
		snap.LogPath = s_Recorder.Path();
		snap.Rows = s_Recorder.RowCount();
		snap.NowMs = timeGetTime();

		std::scoped_lock lock(s_Mutex);
		for (int c = 0; c < CH_COUNT; c++)
			for (int w = 0; w < WHO_COUNT; w++)
				for (int s = 0; s < SCOPE_COUNT; s++)
					snap.Delay[c][w][s] = s_Delay[c][w][s];
		snap.ReviveFeed = s_ReviveFeed;
		snap.SelfFeed = s_SelfFeed;
		for (const auto& [account, member] : s_Squad) { snap.Squad.push_back(member); }
		snap.Commands = s_Commands;
		snap.SelfAccount = s_SelfAccount;
		snap.SelfCharacter = s_SelfCharacter;
		snap.MarkCount = s_MarkCount;
		return snap;
	}

	Status GetStatus()
	{
		Status status;
		status.ArcEvents = s_ArcEvents;
		status.UeEvents = s_UeEvents;
		status.Rows = s_Recorder.RowCount();
		status.Bytes = s_Recorder.BytesWritten();
		status.InWvw = IsWvwMap(s_MapType);
		status.LogPath = s_Recorder.Path();
		return status;
	}

	void OnCombat(Channel aChannel, const ArcDps::EvCombatData* aData)
	{
		uint32_t arrive = timeGetTime();
		if (aData == nullptr || aData->Ev == nullptr) { return; } // agent notifications come via OnAgentUpdate
		s_ArcEvents++;

		const ArcDps::CombatEvent& ev = *aData->Ev;
		const ArcDps::Agent* src = aData->Src;
		const ArcDps::Agent* dst = aData->Dst;
		uint64_t srcId = src ? src->Id : ev.SrcAgent;
		uint64_t dstId = dst ? dst->Id : ev.DstAgent;
		uint8_t sc = ev.IsStatechange;

		int64_t delay = static_cast<int64_t>(arrive) - static_cast<int64_t>(static_cast<uint32_t>(ev.Time));
		bool delayValid = ev.Time != 0 && delay >= 0 && delay < kMaxSaneDelayMs;
		bool isSelf = src != nullptr && src->IsSelf != 0;

		// Classify cheaply first; most events (damage, boons) are dropped without any string work.
		bool isAnimation = sc == ArcDps::CBTS_ANIMATIONSTART || sc == ArcDps::CBTS_ANIMATIONSTOP;
		bool isLifeState = sc == ArcDps::CBTS_CHANGEDOWN || sc == ArcDps::CBTS_CHANGEUP || sc == ArcDps::CBTS_CHANGEDEAD;
		bool isBuffEvent = sc == ArcDps::CBTS_BUFFAPPLY || sc == ArcDps::CBTS_BUFFREMOVE_SINGLE ||
			sc == ArcDps::CBTS_BUFFREMOVE_ALL || sc == CBTS_BUFFINITIAL;
		bool isFightFlow = sc == ArcDps::CBTS_ENTERCOMBAT || sc == ArcDps::CBTS_EXITCOMBAT ||
			sc == CBTS_SQCOMBATSTART || sc == CBTS_SQCOMBATEND || sc == ArcDps::CBTS_MAPCHANGE;
		const ReviveSkill* revive = (isAnimation || sc == ArcDps::CBTS_COMBAT) ? FindReviveSkill(ev.SkillId) : nullptr;
		int trackedBuff = isBuffEvent ? FindTrackedBuff(ev.SkillId) : -1;
		bool reviveBuff = isBuffEvent && IsReviveBuff(ev.SkillId);
		bool reviveByName = revive == nullptr && !reviveBuff && trackedBuff < 0 &&
			(isAnimation || sc == ArcDps::CBTS_BUFFAPPLY) && IsReviveRelatedName(aData->SkillName);

		// Revive skills use the Phase 0 rule; arcdps' own flag misreads several of them.
		const char* verdict = "";
		if (sc == ArcDps::CBTS_ANIMATIONSTOP)
		{
			verdict = revive
				? (CountsAsUsed(*revive, ev.Result, ev.BuffDmg) ? "USED" : "CANCELLED")
				: ArcDps::CastVerdict(ev.IsActivation);
		}

		bool writeTrackedBuff = false;
		std::string buffSnapshot;
		std::string srcAccount;
		std::string dstAccount;
		{
			std::scoped_lock lock(s_Mutex);
			if (delayValid)
			{
				Who w = isSelf ? WHO_SELF : WHO_OTHER;
				s_Delay[aChannel][w][SCOPE_ALL].Add(static_cast<uint32_t>(delay));
				if (isAnimation) { s_Delay[aChannel][w][SCOPE_ANIMATION].Add(static_cast<uint32_t>(delay)); }
				if (aChannel == CH_SQUAD) { s_WindowDelay[w].Add(static_cast<uint32_t>(delay)); }
			}

			if (aChannel == CH_SQUAD)
			{
				AgentState* srcAgent = FindAgent(srcId, ev.SrcInstId);
				AgentState* dstAgent = FindAgent(dstId, ev.DstInstId);
				if (srcAgent)
				{
					srcAccount = srcAgent->Account;
					srcAgent->EventsInWindow++;
					srcAgent->LastEventMs = arrive;
					if (revive && sc == ArcDps::CBTS_ANIMATIONSTART) { srcAgent->IsReviveCaster = true; }
				}
				if (dstAgent) { dstAccount = dstAgent->Account; }

				if (trackedBuff >= 0)
				{
					UpdateTrackedBuff(ev, srcAgent, dstAgent, trackedBuff);
					// Chilled/slow/alacrity matter for revive casters' cast and recharge times; quickness only
					// appears in the snapshot, it is applied too often to write every event.
					AgentState* affected = sc == ArcDps::CBTS_BUFFAPPLY || sc == CBTS_BUFFINITIAL ? dstAgent : srcAgent;
					writeTrackedBuff = trackedBuff != TB_QUICKNESS && affected && affected->IsReviveCaster;
				}

				if (isAnimation && (revive || reviveByName)) { buffSnapshot = BuffSnapshot(srcAgent, ev.Time); }
			}

			if (isAnimation || isLifeState)
			{
				FeedRow feed;
				feed.ArriveMs = arrive;
				feed.DelayMs = delayValid ? delay : -1;
				feed.Channel = ChannelName(aChannel);
				feed.Who = src && src->Name ? src->Name : "";
				feed.IsSelf = isSelf;
				feed.SkillId = ev.SkillId;
				feed.Skill = Str(aData->SkillName);
				if (feed.Skill.empty() && revive) { feed.Skill = revive->Name; }
				if (sc == ArcDps::CBTS_ANIMATIONSTART)
				{
					char buf[96];
					std::snprintf(buf, sizeof(buf), "START (trigger in %d ms, control back in %d ms)", ev.Value, ev.BuffDmg);
					feed.Event = buf;
				}
				else if (sc == ArcDps::CBTS_ANIMATIONSTOP)
				{
					char buf[128];
					std::snprintf(buf, sizeof(buf), "STOP after %d ms (base %d ms) %s, arc=%s", ev.Value, ev.BuffDmg,
						ArcDps::AnimationStopName(ev.Result), ArcDps::AnimationName(ev.IsActivation));
					feed.Event = buf;
					feed.Verdict = verdict;
				}
				else
				{
					feed.Event = ArcDps::StateChangeName(sc);
					feed.Skill.clear();
				}

				if (isAnimation && isSelf) { PushFeed(s_SelfFeed, feed); }
				if ((isAnimation && (revive || reviveByName)) || isLifeState) { PushFeed(s_ReviveFeed, feed); }
			}
		}

		bool relevant = (isAnimation && (revive || reviveByName)) || isLifeState || isFightFlow ||
			(sc == ArcDps::CBTS_COMBAT && revive) || reviveBuff || (isBuffEvent && reviveByName) || writeTrackedBuff;
		bool record = LogAllEvents || (relevant && aChannel == CH_SQUAD && IsWvwMap(s_MapType));
		if (!record) { return; }

		CsvRow row;
		row[COL_KIND] = "ARC";
		row[COL_CHANNEL] = ChannelName(aChannel);
		row[COL_ARRIVE_MS] = std::to_string(arrive);
		row[COL_EVENT_MS] = std::to_string(ev.Time);
		row[COL_DELAY_MS] = delayValid ? std::to_string(delay) : "";
		row[COL_ARC_ID] = std::to_string(aData->Id);
		row[COL_STATECHANGE] = std::to_string(sc);
		row[COL_STATECHANGE_NAME] = ArcDps::StateChangeName(sc);
		row[COL_ACTIVATION] = std::to_string(ev.IsActivation);
		if (sc == ArcDps::CBTS_ANIMATIONSTOP)
		{
			row[COL_ACTIVATION_NAME] = ArcDps::AnimationName(ev.IsActivation);
			row[COL_VERDICT] = verdict;
			row[COL_DETAIL] = std::string("stop_reason=") + ArcDps::AnimationStopName(ev.Result);
		}
		if (!buffSnapshot.empty())
		{
			if (!row[COL_DETAIL].empty()) { row[COL_DETAIL].push_back(' '); }
			row[COL_DETAIL] += buffSnapshot;
		}
		row[COL_SKILL_ID] = std::to_string(ev.SkillId);
		row[COL_SKILL_NAME] = Str(aData->SkillName);
		row[COL_REVIVE_SKILL] = revive ? revive->Name : (reviveByName ? "name-match" : "");
		row[COL_VALUE] = std::to_string(ev.Value);
		row[COL_BUFF_DMG] = std::to_string(ev.BuffDmg);
		row[COL_OVERSTACK] = std::to_string(ev.OverstackValue);
		row[COL_RESULT] = std::to_string(ev.Result);
		row[COL_IFF] = std::to_string(ev.Iff);
		row[COL_BUFF_REMOVE] = std::to_string(ev.IsBuffRemove);
		row[COL_SRC_ID] = std::to_string(srcId);
		if (src)
		{
			row[COL_SRC_NAME] = Str(src->Name);
			row[COL_SRC_PROF] = std::to_string(src->Profession);
			row[COL_SRC_ELITE] = std::to_string(src->Elite);
			row[COL_SRC_SELF] = std::to_string(src->IsSelf);
		}
		row[COL_SRC_ACCOUNT] = srcAccount;
		row[COL_SRC_INST] = std::to_string(ev.SrcInstId);
		row[COL_SRC_MASTER_INST] = std::to_string(ev.SrcMasterInstId);
		row[COL_DST_ID] = std::to_string(dstId);
		if (dst) { row[COL_DST_NAME] = Str(dst->Name); }
		row[COL_DST_ACCOUNT] = dstAccount;
		row[COL_DST_INST] = std::to_string(ev.DstInstId);
		s_Recorder.Write(row);
	}

	void OnAgentUpdate(const char* aEventName, const ArcDps::EvAgentUpdate* aUpdate)
	{
		if (aUpdate == nullptr) { return; }

		CsvRow row;
		row[COL_KIND] = "AGENT";
		row[COL_ARRIVE_MS] = std::to_string(timeGetTime());
		row[COL_SRC_ID] = std::to_string(aUpdate->Id);
		row[COL_SRC_NAME] = aUpdate->Character;
		row[COL_SRC_PROF] = std::to_string(aUpdate->Profession);
		row[COL_SRC_ELITE] = std::to_string(aUpdate->Elite);
		row[COL_SRC_SELF] = std::to_string(aUpdate->Self);
		row[COL_SRC_ACCOUNT] = aUpdate->Account;
		row[COL_DST_NAME] = aUpdate->Account;
		row[COL_DST_INST] = std::to_string(aUpdate->InstanceId);
		row[COL_DETAIL] = std::string(aEventName) + " subgroup=" + std::to_string(aUpdate->Subgroup) +
			" team=" + std::to_string(aUpdate->Team) + " prof=" + ArcDps::ProfessionName(aUpdate->Profession);
		s_Recorder.Write(row);

		std::scoped_lock lock(s_Mutex);
		// Aliases may point at an entry that is being replaced or removed; rebuild them from new events.
		s_IdAliases.clear();
		if (aUpdate->Added)
		{
			AgentState& agent = s_Agents[aUpdate->Id];
			agent.Character = aUpdate->Character;
			agent.Account = aUpdate->Account;
			agent.InstanceId = static_cast<uint16_t>(aUpdate->InstanceId);
			agent.IsSelf = aUpdate->Self != 0;
			if (agent.IsSelf)
			{
				s_SelfAccount = aUpdate->Account;
				s_SelfCharacter = aUpdate->Character;
			}
		}
		else
		{
			s_Agents.erase(aUpdate->Id);
		}
	}

	void OnSquadUpdate(const UserInfo* aUsers, uint64_t aCount)
	{
		if (aUsers == nullptr) { return; }
		s_UeEvents++;
		uint32_t arrive = timeGetTime();

		for (uint64_t i = 0; i < aCount; i++)
		{
			const UserInfo& user = aUsers[i];
			std::string account = Str(user.AccountName);

			CsvRow row;
			row[COL_KIND] = "UE_SQUAD";
			row[COL_ARRIVE_MS] = std::to_string(arrive);
			row[COL_SRC_NAME] = account;
			row[COL_DETAIL] = std::string("role=") + RoleName(static_cast<uint8_t>(user.Role)) +
				" subgroup=" + std::to_string(user.Subgroup) +
				" ready=" + std::to_string(user.ReadyStatus) +
				" type=" + std::to_string(static_cast<int>(user.GroupType)) +
				" join=" + std::to_string(user.JoinTime);
			s_Recorder.Write(row);

			std::scoped_lock lock(s_Mutex);
			if (user.Role == UserRole::None || user.Role == UserRole::Invalid)
			{
				s_Squad.erase(account);
			}
			else
			{
				s_Squad[account] = Member{ account, static_cast<uint8_t>(user.Role), user.Subgroup };
			}
		}
	}

	void OnChatMessage(const SquadMessageInfo* aMessage)
	{
		if (aMessage == nullptr) { return; }
		s_UeEvents++;
		uint32_t arrive = timeGetTime();

		std::string account = Str(aMessage->AccountName);
		std::string text = aMessage->Text ? std::string(aMessage->Text, aMessage->TextLength) : "";
		bool isCommand = !text.empty() && text[0] == '!';

		uint8_t role = static_cast<uint8_t>(UserRole::Invalid);
		{
			std::scoped_lock lock(s_Mutex);
			auto it = s_Squad.find(account);
			if (it != s_Squad.end()) { role = it->second.Role; }

			if (isCommand)
			{
				ChatCommand cmd;
				cmd.ArriveMs = arrive;
				cmd.Account = account;
				cmd.Role = RoleName(role);
				cmd.Text = text;
				cmd.WouldAccept = static_cast<UserRole>(role) == UserRole::SquadLeader;
				s_Commands.push_back(cmd);
				while (s_Commands.size() > kCommandLimit) { s_Commands.pop_front(); }
			}
		}

		CsvRow row;
		row[COL_KIND] = "UE_CHAT";
		row[COL_ARRIVE_MS] = std::to_string(arrive);
		row[COL_SRC_NAME] = account;
		row[COL_DST_NAME] = Str(aMessage->CharacterName);
		row[COL_DETAIL] = std::string("role=") + RoleName(role) +
			" channel_id=" + std::to_string(aMessage->ChannelId) +
			" type=" + std::to_string(static_cast<int>(aMessage->Type)) +
			" subgroup=" + std::to_string(aMessage->Subgroup) +
			" broadcast=" + std::to_string(aMessage->IsBroadcast & 1) +
			" server_time=" + Str(aMessage->Timestamp);
		// Only "!" commands are stored verbatim; normal chat is not written to disk.
		row[COL_TEXT] = isCommand ? text : "<" + std::to_string(text.size()) + " chars not recorded>";
		s_Recorder.Write(row);
	}

	void OnKey(UINT aMsg, WPARAM aWParam, LPARAM aLParam, bool aTextboxFocused)
	{
		if (!LogKeys || aTextboxFocused) { return; }

		bool isKey = aMsg == WM_KEYDOWN || aMsg == WM_SYSKEYDOWN;
		if (isKey && (aLParam & (1 << 30))) { return; } // auto-repeat

		std::string name;
		if (isKey)
		{
			// Key names are localized (e.g. German "Ö"); convert to UTF-8 so the CSV stays valid.
			wchar_t wide[64] = {};
			if (GetKeyNameTextW(static_cast<LONG>(aLParam), wide, static_cast<int>(std::size(wide))) > 0)
			{
				char utf8[256] = {};
				if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, sizeof(utf8), nullptr, nullptr) > 0) { name = utf8; }
			}
		}
		else if (aMsg == WM_XBUTTONDOWN)
		{
			name = GET_XBUTTON_WPARAM(aWParam) == XBUTTON1 ? "Mouse4" : "Mouse5";
		}
		else if (aMsg == WM_MBUTTONDOWN)
		{
			name = "Mouse3";
		}
		else
		{
			return;
		}

		char detail[96];
		std::snprintf(detail, sizeof(detail), "vk=0x%02X %s", isKey ? static_cast<unsigned>(aWParam) : 0u, name.c_str());
		WriteInfoRow("KEY", detail);
	}

	void OnMapChange(uint32_t aMapId, uint8_t aMapType, bool aIsCompetitive)
	{
		s_MapId = aMapId;
		s_MapType = aMapType;
		WriteInfoRow("MAP", "map_id=" + std::to_string(aMapId) + " map_type=" + std::to_string(aMapType) +
			" competitive=" + std::to_string(aIsCompetitive) + " wvw=" + std::to_string(IsWvwMap(aMapType)));
	}

	void Mark(const std::string& aLabel)
	{
		uint32_t arrive = timeGetTime();
		uint32_t number;
		{
			std::scoped_lock lock(s_Mutex);
			number = ++s_MarkCount;

			FeedRow feed;
			feed.ArriveMs = arrive;
			feed.IsMark = true;
			feed.Event = "#" + std::to_string(number) + " " + aLabel;
			PushFeed(s_ReviveFeed, feed);
			PushFeed(s_SelfFeed, feed);
		}

		CsvRow row;
		row[COL_KIND] = "MARK";
		row[COL_ARRIVE_MS] = std::to_string(arrive);
		row[COL_DETAIL] = "#" + std::to_string(number) + " wall=" + WallClock();
		row[COL_TEXT] = aLabel;
		s_Recorder.Write(row);
	}

	void LogInfo(const std::string& aDetail)
	{
		WriteInfoRow("INFO", aDetail);
	}

	void Tick(const TickInfo& aTick)
	{
		bool wvw = IsWvwMap(s_MapType);

		std::vector<std::string> info;
		{
			std::scoped_lock lock(s_Mutex);
			info.swap(s_PendingInfo);
		}
		for (const std::string& detail : info) { WriteInfoRow("INFO", detail); }

		if (wvw && aTick.HasPosition && aTick.NowMs - s_LastPositionMs >= kPositionEveryMs)
		{
			s_LastPositionMs = aTick.NowMs;
			char detail[96];
			std::snprintf(detail, sizeof(detail), "x=%.2f y=%.2f z=%.2f map_id=%u",
				aTick.Position[0], aTick.Position[1], aTick.Position[2], s_MapId.load());
			CsvRow row;
			row[COL_KIND] = "POS";
			row[COL_ARRIVE_MS] = std::to_string(aTick.NowMs);
			row[COL_DETAIL] = detail;
			s_Recorder.Write(row);
		}

		if (aTick.NowMs - s_LastSeenMs >= kSeenEveryMs)
		{
			s_LastSeenMs = aTick.NowMs;
			std::vector<CsvRow> rows;
			{
				std::scoped_lock lock(s_Mutex);
				for (auto& [id, agent] : s_Agents)
				{
					if (wvw)
					{
						// How many squad-feed events each squad member produced in the last window; a drop to
						// zero while they are fighting shows they left our visibility range.
						CsvRow& row = rows.emplace_back();
						row[COL_KIND] = "SEEN";
						row[COL_ARRIVE_MS] = std::to_string(aTick.NowMs);
						row[COL_VALUE] = std::to_string(agent.EventsInWindow);
						row[COL_SRC_ID] = std::to_string(id);
						row[COL_SRC_NAME] = agent.Character;
						row[COL_SRC_SELF] = std::to_string(agent.IsSelf);
						row[COL_DST_NAME] = agent.Account;
						uint64_t ago = agent.LastEventMs > aTick.NowMs ? 0 : aTick.NowMs - agent.LastEventMs;
						row[COL_DETAIL] = "last_event_ms_ago=" +
							(agent.LastEventMs ? std::to_string(ago) : std::string("never")) +
							" revive_caster=" + std::to_string(agent.IsReviveCaster);
					}
					agent.EventsInWindow = 0;
				}
			}
			for (const CsvRow& row : rows) { s_Recorder.Write(row); }
		}

		if (aTick.NowMs - s_LastClockMs >= kClockEveryMs)
		{
			s_LastClockMs = aTick.NowMs;
			std::string delays;
			{
				std::scoped_lock lock(s_Mutex);
				static constexpr const char* who[] = { "self", "other" };
				for (int w = 0; w < WHO_COUNT; w++)
				{
					DelayStat& stat = s_WindowDelay[w];
					delays += std::string(" ") + who[w] + "_n=" + std::to_string(stat.Count);
					if (stat.Count)
					{
						delays += " " + std::string(who[w]) + "_min=" + std::to_string(stat.Min) +
							" " + who[w] + "_avg=" + std::to_string(stat.Sum / stat.Count) +
							" " + who[w] + "_max=" + std::to_string(stat.Max);
					}
					stat = DelayStat{};
				}
			}
			// Wall clock next to timeGetTime lets two players' logs be lined up afterwards.
			WriteInfoRow("CLOCK", "wall=" + WallClock() + " tgt=" + std::to_string(aTick.NowMs) +
				" map_id=" + std::to_string(s_MapId.load()) + " wvw=" + std::to_string(wvw) +
				" arc_events=" + std::to_string(s_ArcEvents.load()) + " ue_events=" + std::to_string(s_UeEvents.load()) +
				delays);
		}
	}
}
