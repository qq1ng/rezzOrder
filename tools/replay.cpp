// Replays a recorder CSV through the capture code outside the game, to check the field recorder's
// filtering, buff tracking and periodic rows, and to measure per-event cost.
//
//   rezz_replay <input.csv> <output-dir> [--repeat N]
//   rezz_replay --selftest <output-dir>
//   rezz_replay --tracker <input.csv> [account1,account2,...]
//   rezz_replay --session <input.csv> [account1,account2,...]
//
// --tracker feeds a recorder log's squad events into the revive order tracker and prints each spent revive
// with who was up at that moment, plus per-player totals. Without an explicit order, everyone who cast a
// revive skill is put in the order by first use.
//
// --session feeds squad joins/leaves, roles and events through the live session (as the addon does) and prints
// the leave/return/swap notices it raises and the turn every 5 minutes.
//
// ARC, AGENT and MAP rows are replayed; the map is forced to WvW so recording is active. Ticks use the
// input's arrive_ms clock. With --repeat, all ARC rows are additionally replayed N times for timing.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include <map>
#include <sstream>

#include <cstdlib>

#include "Capture.h"
#include "Share.h"
#include "Session.h"
#include "ReviveSkills.h"
#include "Tracker.h"

namespace
{
	std::vector<std::string> ParseCsvLine(const std::string& aLine)
	{
		std::vector<std::string> fields(1);
		bool quoted = false;
		for (size_t i = 0; i < aLine.size(); i++)
		{
			char c = aLine[i];
			if (quoted)
			{
				if (c == '"' && i + 1 < aLine.size() && aLine[i + 1] == '"') { fields.back().push_back('"'); i++; }
				else if (c == '"') { quoted = false; }
				else { fields.back().push_back(c); }
			}
			else if (c == '"') { quoted = true; }
			else if (c == ',') { fields.emplace_back(); }
			else if (c != '\r') { fields.back().push_back(c); }
		}
		return fields;
	}

	struct Row
	{
		std::vector<std::string> Fields;
		const std::unordered_map<std::string, size_t>* Index;

		const std::string& operator[](const char* aColumn) const { return Fields[Index->at(aColumn)]; }
		uint64_t U(const char* aColumn) const { const std::string& s = (*this)[aColumn]; return s.empty() ? 0 : std::stoull(s); }
		int64_t I(const char* aColumn) const { const std::string& s = (*this)[aColumn]; return s.empty() ? 0 : std::stoll(s); }
	};

	void ReplayArc(const Row& aRow, uint32_t aTimeShift)
	{
		ArcDps::CombatEvent ev{};
		ev.Time = aRow.U("event_ms") + aTimeShift;
		ev.SrcAgent = aRow.U("src_id");
		ev.DstAgent = aRow.U("dst_id");
		ev.Value = static_cast<int32_t>(aRow.I("value"));
		ev.BuffDmg = static_cast<int32_t>(aRow.I("buff_dmg"));
		ev.OverstackValue = static_cast<uint32_t>(aRow.U("overstack"));
		ev.SkillId = static_cast<uint32_t>(aRow.U("skill_id"));
		ev.SrcInstId = static_cast<uint16_t>(aRow.U("src_inst"));
		ev.DstInstId = static_cast<uint16_t>(aRow.U("dst_inst"));
		ev.SrcMasterInstId = static_cast<uint16_t>(aRow.U("src_master_inst"));
		ev.Iff = static_cast<uint8_t>(aRow.U("iff"));
		ev.Result = static_cast<uint8_t>(aRow.U("result"));
		ev.IsActivation = static_cast<uint8_t>(aRow.U("activation"));
		ev.IsBuffRemove = static_cast<uint8_t>(aRow.U("buff_remove"));
		ev.IsStatechange = static_cast<uint8_t>(aRow.U("statechange"));

		ArcDps::Agent src{ aRow["src_name"].c_str(), ev.SrcAgent, static_cast<uint32_t>(aRow.U("src_prof")),
			static_cast<uint32_t>(aRow.U("src_elite")), static_cast<uint32_t>(aRow.U("src_self")), 0 };
		ArcDps::Agent dst{ aRow["dst_name"].c_str(), ev.DstAgent, 0, 0, 0, 0 };
		ArcDps::EvCombatData data{ &ev, &src, &dst, aRow["skill_name"].c_str(), aRow.U("arc_id"), 1 };
		Capture::OnCombat(aRow["channel"] == "LOCAL" ? Capture::CH_LOCAL : Capture::CH_SQUAD, &data);
	}

	void ReplayAgent(const Row& aRow)
	{
		ArcDps::EvAgentUpdate update{};
		strncpy_s(update.Account, aRow["dst_name"].c_str(), _TRUNCATE);
		strncpy_s(update.Character, aRow["src_name"].c_str(), _TRUNCATE);
		update.Id = aRow.U("src_id");
		update.InstanceId = aRow.U("dst_inst");
		const std::string& detail = aRow["detail"];
		update.Added = detail.find("JOIN") != std::string::npos;
		update.Self = static_cast<uint32_t>(aRow.U("src_self"));
		update.Profession = static_cast<uint32_t>(aRow.U("src_prof"));
		update.Elite = static_cast<uint32_t>(aRow.U("src_elite"));
		Capture::OnAgentUpdate(detail.substr(0, detail.find(' ')).c_str(), &update);
	}
}

namespace
{
	// Known sequence for the buff model: quickness applied, partly removed, cleared; chilled on a revive caster.
	int SelfTest(const char* aOutDir)
	{
		if (!Capture::Start(aOutDir, "selftest")) { return 1; }
		Capture::OnMapChange(38, 9, true);

		constexpr uint64_t caster = 1111;
		ArcDps::EvAgentUpdate join{};
		strncpy_s(join.Account, ":caster.1234", _TRUNCATE);
		strncpy_s(join.Character, "Caster", _TRUNCATE);
		join.Id = caster;
		join.InstanceId = 77;
		join.Added = 1;
		join.Profession = 2;
		Capture::OnAgentUpdate("SQUAD_JOIN", &join);

		auto send = [](uint64_t aTime, uint8_t aStatechange, uint32_t aSkill, uint64_t aSrc, uint64_t aDst,
			int32_t aValue, int32_t aBuffDmg, uint8_t aResult, uint8_t aBuffRemove, const char* aName)
		{
			ArcDps::CombatEvent ev{};
			ev.Time = aTime;
			ev.IsStatechange = aStatechange;
			ev.SkillId = aSkill;
			ev.SrcAgent = aSrc;
			ev.DstAgent = aDst;
			ev.Value = aValue;
			ev.BuffDmg = aBuffDmg;
			ev.Result = aResult;
			ev.IsBuffRemove = aBuffRemove;
			ev.SrcInstId = (aSrc == caster || aSrc == 9999) ? 77 : 0; // 9999: the caster after an id change
			ev.DstInstId = aDst == caster ? 77 : 0;
			ArcDps::Agent src{ "Diamond Legend", aSrc, 2, 0, 0, 0 };
			ArcDps::Agent dst{ "Diamond Legend", aDst, 2, 0, 0, 0 };
			ArcDps::EvCombatData data{ &ev, &src, &dst, aName, 0, 1 };
			Capture::OnCombat(Capture::CH_SQUAD, &data);
		};

		// Expected detail on the rows (see comments); checked by eye or script.
		send(1000, ArcDps::CBTS_BUFFAPPLY, 1187, 9, caster, 5000, 0, 0, 0, "Quickness");                // quick expiry 6000
		send(2000, ArcDps::CBTS_ANIMATIONSTART, 14419, caster, 0, 2000, 2400, 1, 0, "Battle Standard"); // quick=4000
		send(2500, ArcDps::CBTS_BUFFREMOVE_SINGLE, 1187, caster, 0, 1000, 0, 0, 2, "Quickness");         // expiry 5000
		send(2500, ArcDps::CBTS_BUFFREMOVE_SINGLE, 1187, caster, 0, 1000, 0, 0, 3, "Quickness");         // manual: ignored
		send(3000, ArcDps::CBTS_ANIMATIONSTOP, 14419, caster, 0, 1600, 2400, 22, 0, "Battle Standard"); // quick=2000, USED
		send(3500, ArcDps::CBTS_BUFFREMOVE_ALL, 1187, caster, 0, 1500, 0, 0, 1, "Quickness");            // expiry 3500
		send(4000, ArcDps::CBTS_BUFFAPPLY, 722, 9, caster, 2000, 0, 0, 0, "Chilled");                   // written: caster
		send(4100, ArcDps::CBTS_BUFFAPPLY, 722, 9, 2222, 2000, 0, 0, 0, "Chilled");                     // not written
		send(4500, ArcDps::CBTS_ANIMATIONSTART, 14419, caster, 0, 2000, 2400, 1, 0, "Battle Standard"); // quick=0 chill=1500
		send(5000, ArcDps::CBTS_ANIMATIONSTOP, 14419, caster, 0, 1000, 1500, 12, 0, "Battle Standard"); // CANCELLED
		send(5500, ArcDps::CBTS_ANIMATIONSTART, 14419, 9999, 0, 2000, 2400, 1, 0, "Battle Standard");   // alias: account kept, chill=500

		Capture::TickInfo tick;
		tick.NowMs = 6000;
		Capture::Tick(tick); // writes the pending alias info row
		Capture::Status status = Capture::GetStatus();
		Capture::Stop();
		std::printf("selftest wrote %s\n", status.LogPath.c_str());
		return 0;
	}
}

namespace
{
	std::vector<Row> ReadRows(const char* aPath, std::unordered_map<std::string, size_t>& aIndex)
	{
		std::vector<Row> rows;
		std::ifstream in(aPath, std::ios::binary);
		if (!in) { return rows; }
		std::string line;
		std::getline(in, line);
		std::vector<std::string> header = ParseCsvLine(line);
		for (size_t i = 0; i < header.size(); i++) { aIndex[header[i]] = i; }
		while (std::getline(in, line))
		{
			Row row{ ParseCsvLine(line), &aIndex };
			if (row.Fields.size() == aIndex.size()) { rows.push_back(std::move(row)); }
		}
		return rows;
	}

	const char* EligibilityName(Rezz::Eligibility aStatus)
	{
		switch (aStatus)
		{
			case Rezz::Eligibility::Ready:    return "ready";
			case Rezz::Eligibility::Casting:  return "casting";
			case Rezz::Eligibility::Cooldown: return "cooldown";
			case Rezz::Eligibility::Downed:   return "downed";
			case Rezz::Eligibility::Dead:     return "dead";
			default:                          return "away";
		}
	}

	int RunTracker(const char* aPath, const char* aOrder)
	{
		std::unordered_map<std::string, size_t> index;
		std::vector<Row> rows = ReadRows(aPath, index);
		if (rows.empty()) { std::cerr << "no rows in " << aPath << "\n"; return 1; }

		auto isSquadArc = [](const Row& aRow) { return aRow["kind"] == "ARC" && aRow["channel"] == "SQUAD"; };

		std::vector<std::string> order;
		if (aOrder)
		{
			std::stringstream list(aOrder);
			for (std::string account; std::getline(list, account, ',');) { order.push_back(account); }
		}
		else
		{
			for (const Row& row : rows)
			{
				const ReviveSkill* skill = FindReviveSkill(static_cast<uint32_t>(row.U("skill_id")));
				if (!isSquadArc(row) || row["statechange_name"] != "ANIMATIONSTOP" || !skill || !skill->IsPlayerCast) { continue; }
				const std::string& account = row["src_account"];
				if (!account.empty() && std::find(order.begin(), order.end(), account) == order.end()) { order.push_back(account); }
			}
		}

		Rezz::Tracker tracker;
		tracker.SetOrder(order);
		std::printf("order:");
		for (const std::string& account : order) { std::printf(" %s", account.c_str()); }
		std::printf("\n\n");

		std::unordered_map<uint64_t, std::string> instanceToAccount; // for the spirit's slams (owner by instance id)
		std::map<std::string, std::map<uint32_t, int>> revivedPerUse;  // group name -> allies revived -> uses
		size_t used = 0, inTurn = 0, nobodyUp = 0;

		auto lastRevived = [&](const std::string& aAccount, ReviveGroup aGroup) -> const Rezz::SkillStatus*
		{
			const Rezz::PlayerStatus* player = tracker.FindPlayer(aAccount);
			if (!player) { return nullptr; }
			for (const Rezz::SkillStatus& skill : player->Skills) { if (skill.Group == aGroup && skill.Uses > 0) { return &skill; } }
			return nullptr;
		};

		for (const Row& row : rows)
		{
			if (row["kind"] == "AGENT")
			{
				bool joined = row["detail"].find("JOIN") != std::string::npos;
				if (joined) { instanceToAccount[row.U("dst_inst")] = row["src_account"]; }
				continue;
			}
			if (!isSquadArc(row)) { continue; }

			const std::string& statechange = row["statechange_name"];
			const std::string& account = row["src_account"];
			uint64_t time = row.U("event_ms");
			uint32_t skillId = static_cast<uint32_t>(row.U("skill_id"));
			const ReviveSkill* skill = FindReviveSkill(skillId);

			if (statechange == "CHANGEDOWN") { tracker.OnLifeState(time, account, Rezz::LifeState::Downed); }
			else if (statechange == "CHANGEUP") { tracker.OnLifeState(time, account, Rezz::LifeState::Alive); }
			else if (statechange == "CHANGEDEAD") { tracker.OnLifeState(time, account, Rezz::LifeState::Dead); }
			else if (skill && statechange == "ANIMATIONSTART" && skill->IsPlayerCast) { tracker.OnCastStart(time, account, skillId); }
			else if (skill && statechange == "ANIMATIONSTOP" && !skill->IsPlayerCast)
			{
				auto owner = instanceToAccount.find(row.U("src_master_inst"));
				if (owner != instanceToAccount.end()) { tracker.OnOwnedEffect(time, owner->second, skillId); }
			}
			else if (skill && statechange == "ANIMATIONSTOP")
			{
				bool spent = CountsAsUsed(*skill, static_cast<uint8_t>(row.U("result")), static_cast<int32_t>(row.I("buff_dmg")));
				Rezz::TurnView before = tracker.GetTurn(time);
				const Rezz::SkillStatus* previous = spent ? lastRevived(account, skill->Group) : nullptr;
				if (previous) { revivedPerUse[GetGroupInfo(skill->Group).Name][previous->LastRevived]++; }

				tracker.OnCastStop(time, account, skillId, static_cast<uint8_t>(row.U("result")), static_cast<int32_t>(row.I("buff_dmg")));
				if (!spent) { continue; }

				used++;
				std::string up = before.UpIndex >= 0 ? before.Rows[before.UpIndex].Account : "-";
				if (up == account) { inTurn++; }
				if (before.UpIndex < 0) { nobodyUp++; }
				std::printf("%10llu  %-24s %-18s up was %-24s", time, account.c_str(), GetGroupInfo(skill->Group).Name, up.c_str());
				for (const Rezz::OrderRow& orderRow : before.Rows)
				{
					std::printf(" %s", orderRow.Status == Rezz::Eligibility::Cooldown
						? (std::to_string(orderRow.ReadyInMs / 1000) + "s").c_str()
						: EligibilityName(orderRow.Status));
				}
				std::printf("\n");
			}
		}

		// Last use of every skill hasn't been counted in the distribution yet.
		for (const std::string& account : order)
		{
			if (const Rezz::PlayerStatus* player = tracker.FindPlayer(account))
			{
				for (const Rezz::SkillStatus& skill : player->Skills)
				{
					if (skill.Uses > 0) { revivedPerUse[GetGroupInfo(skill.Group).Name][skill.LastRevived]++; }
				}
			}
		}

		std::printf("\n%zu spent revives; %zu by the player who was up, %zu while nobody in the order was up\n", used, inTurn, nobodyUp);
		std::printf("\n%-24s %-18s %5s %7s %7s %12s\n", "player", "skill", "uses", "cancels", "revived", "early recast");
		for (const std::string& account : order)
		{
			const Rezz::PlayerStatus* player = tracker.FindPlayer(account);
			if (!player) { continue; }
			for (const Rezz::SkillStatus& skill : player->Skills)
			{
				std::printf("%-24s %-18s %5u %7u %7u %12u\n", account.c_str(), GetGroupInfo(skill.Group).Name,
					skill.Uses, skill.Cancels, skill.Revived, skill.EarlyRecasts);
			}
		}
		std::printf("\nallies revived per use:\n");
		for (const auto& [group, counts] : revivedPerUse)
		{
			std::printf("  %-18s", group.c_str());
			for (const auto& [revived, uses] : counts) { std::printf(" %u:%d", revived, uses); }
			std::printf("\n");
		}
		return 0;
	}
}

namespace
{
	Rezz::SquadRole ParseRole(const std::string& aDetail)
	{
		static const std::pair<const char*, Rezz::SquadRole> roles[] = {
			{ "role=SquadLeader", Rezz::SquadRole::Leader }, { "role=Lieutenant", Rezz::SquadRole::Lieutenant },
			{ "role=Member", Rezz::SquadRole::Member }, { "role=Invited", Rezz::SquadRole::Invited },
			{ "role=Applied", Rezz::SquadRole::Applied }, { "role=None", Rezz::SquadRole::None },
		};
		for (const auto& [prefix, role] : roles) { if (aDetail.rfind(prefix, 0) == 0) { return role; } }
		return Rezz::SquadRole::Unknown;
	}

	int RunSession(const char* aPath, const char* aOrder)
	{
		std::unordered_map<std::string, size_t> index;
		std::vector<Row> rows = ReadRows(aPath, index);
		if (rows.empty()) { std::cerr << "no rows in " << aPath << "\n"; return 1; }

		std::vector<std::string> order;
		if (aOrder)
		{
			std::stringstream list(aOrder);
			for (std::string account; std::getline(list, account, ',');) { order.push_back(account); }
		}
		else
		{
			for (const Row& row : rows)
			{
				const ReviveSkill* skill = FindReviveSkill(static_cast<uint32_t>(row.U("skill_id")));
				if (row["kind"] != "ARC" || row["statechange_name"] != "ANIMATIONSTOP" || !skill || !skill->IsPlayerCast) { continue; }
				const std::string& account = row["src_account"];
				if (!account.empty() && std::find(order.begin(), order.end(), account) == order.end()) { order.push_back(account); }
			}
		}

		Rezz::Session session;
		session.SetOrder(order);
		// The bench setting normally arrives with a shared order; a replay sets it from REZZ_BENCH.
		if (const char* bench = std::getenv("REZZ_BENCH")) { session.SetBench(Rezz::Share::BenchFromName(bench)); }

		// A recording started after the squad formed has no join rows for its members, only the periodic SEEN rows
		// the recorder writes for everyone it knows. Those are enough to put them in the roster up front.
		std::unordered_map<uint64_t, bool> seeded;
		for (const Row& row : rows)
		{
			if (row["kind"] != "SEEN" || row["dst_name"].empty() || seeded.count(row.U("src_id"))) { continue; }
			seeded[row.U("src_id")] = true;
			ArcDps::EvAgentUpdate update{};
			strncpy_s(update.Account, row["dst_name"].c_str(), _TRUNCATE);
			strncpy_s(update.Character, row["src_name"].c_str(), _TRUNCATE);
			update.Id = row.U("src_id");
			update.Added = 1;
			update.Self = static_cast<uint32_t>(row.U("src_self"));
			update.Profession = static_cast<uint32_t>(row.U("src_prof"));
			update.Elite = static_cast<uint32_t>(row.U("src_elite"));
			session.OnAgentUpdate(update, 0);
		}

		uint64_t lastPrint = 0;
		size_t notices = 0;
		std::string lastIllusions;
		std::string lastTurn;
		for (const Row& row : rows)
		{
			const std::string& kind = row["kind"];
			uint64_t now = row.U("arrive_ms");
			if (kind == "AGENT")
			{
				ArcDps::EvAgentUpdate update{};
				strncpy_s(update.Account, row["dst_name"].c_str(), _TRUNCATE);
				strncpy_s(update.Character, row["src_name"].c_str(), _TRUNCATE);
				update.Id = row.U("src_id");
				update.InstanceId = row.U("dst_inst");
				update.Added = row["detail"].find("JOIN") != std::string::npos;
				update.Self = static_cast<uint32_t>(row.U("src_self"));
				update.Profession = static_cast<uint32_t>(row.U("src_prof"));
				update.Elite = static_cast<uint32_t>(row.U("src_elite"));
				session.OnAgentUpdate(update, now);
			}
			else if (kind == "UE_SQUAD")
			{
				const std::string& detail = row["detail"];
				size_t at = detail.find("subgroup=");
				uint16_t subgroup = at == std::string::npos ? 0 : static_cast<uint16_t>(std::atoi(detail.c_str() + at + 9));
				session.OnRole(row["src_name"], ParseRole(detail), subgroup, now);
			}
			else if (kind == "ARC" && row["channel"] == "SQUAD")
			{
				ArcDps::CombatEvent ev{};
				ev.Time = row.U("event_ms");
				ev.SrcAgent = row.U("src_id");
				ev.DstAgent = row.U("dst_id");
				ev.Value = static_cast<int32_t>(row.I("value"));
				ev.BuffDmg = static_cast<int32_t>(row.I("buff_dmg"));
				ev.SkillId = static_cast<uint32_t>(row.U("skill_id"));
				ev.SrcInstId = static_cast<uint16_t>(row.U("src_inst"));
				ev.DstInstId = static_cast<uint16_t>(row.U("dst_inst"));
				ev.SrcMasterInstId = static_cast<uint16_t>(row.U("src_master_inst"));
				ev.Result = static_cast<uint8_t>(row.U("result"));
				ev.IsStatechange = static_cast<uint8_t>(row.U("statechange"));
				ArcDps::Agent src{ row["src_name"].c_str(), ev.SrcAgent, 0, 0, 0, 0 };
				ArcDps::Agent dst{ row["dst_name"].c_str(), ev.DstAgent, 0, 0, 0, 0 };
				ArcDps::EvCombatData data{ &ev, &src, &dst, row["skill_name"].c_str(), 0, 1 };
				session.OnCombat(data, now);
			}
			if (now == 0) { continue; }

			session.Tick(now);
			for (const Rezz::Notice& notice : session.TakeNotices())
			{
				notices++;
				std::printf("%10llu  NOTICE  %s\n", now, notice.Text.c_str());
			}

			// Who is under Illusion of Life, and whether this client would show the countdown for them: printed
			// whenever that changes.
			Rezz::SessionView illusions = session.GetView(now);
			std::string state;
			for (const Rezz::IllusionTarget& target : illusions.Illusions)
			{
				bool shown = illusions.SelfCanRevive || target.OurCast;
				state += " " + Rezz::DisplayAccount(target.Account) + (target.OurCast ? "(our cast)" : "") +
					(shown ? "" : "(not shown)");
			}
			if (state != lastIllusions)
			{
				std::printf("%10llu  ILLUSION%s\n", now, state.empty() ? " none" : state.c_str());
				for (const Rezz::IllusionTarget& target : illusions.Illusions)
				{
					std::printf("            %s runs out in %llu ms\n", Rezz::DisplayAccount(target.Account).c_str(),
						static_cast<unsigned long long>(target.EndsInMs));
				}
				lastIllusions = state;
			}
			// Every change of the turn, with each player's state: for tracing a turn that stayed where it was.
			{
				Rezz::SessionView view = session.GetView(now);
				std::string turn;
				for (int i = 0; i < static_cast<int>(view.Turn.Rows.size()); i++)
				{
					const Rezz::OrderRow& orderRow = view.Turn.Rows[i];
					turn += " " + std::string(i == view.Turn.UpIndex ? ">" : (i == view.BackupIndex ? "b" : "")) +
						Rezz::DisplayAccount(orderRow.Account).substr(0, 8) + "=" +
						(orderRow.Status == Rezz::Eligibility::Cooldown ? "cd" : EligibilityName(orderRow.Status));
				}
				if (turn != lastTurn)
				{
					std::printf("%10llu  TURN   %s\n", now, turn.c_str());
					lastTurn = turn;
				}
			}
			if (now >= lastPrint + 5 * 60 * 1000)
			{
				lastPrint = now;
				Rezz::SessionView view = session.GetView(now);
				std::printf("%10llu  turn   ", now);
				for (int i = 0; i < static_cast<int>(view.Turn.Rows.size()); i++)
				{
					const Rezz::OrderRow& orderRow = view.Turn.Rows[i];
					std::printf(" %s%s=%s", i == view.Turn.UpIndex ? ">" : (i == view.BackupIndex ? "b" : ""),
						Rezz::DisplayAccount(orderRow.Account).substr(0, 8).c_str(),
						orderRow.Status == Rezz::Eligibility::Cooldown ? (std::to_string(orderRow.ReadyInMs / 1000) + "s").c_str()
							: EligibilityName(orderRow.Status));
				}
				std::printf("\n");
			}
		}
		std::printf("\n%zu notices\n", notices);
		return 0;
	}
}

int main(int argc, char** argv)
{
	if (argc >= 3 && std::string(argv[1]) == "--selftest") { return SelfTest(argv[2]); }
	if (argc >= 3 && std::string(argv[1]) == "--tracker") { return RunTracker(argv[2], argc >= 4 ? argv[3] : nullptr); }
	if (argc >= 3 && std::string(argv[1]) == "--session") { return RunSession(argv[2], argc >= 4 ? argv[3] : nullptr); }
	if (argc < 3)
	{
		std::cerr << "usage: rezz_replay <input.csv> <output-dir> [--repeat N]\n";
		return 2;
	}
	int repeat = (argc >= 5 && std::string(argv[3]) == "--repeat") ? std::stoi(argv[4]) : 0;

	std::ifstream in(argv[1], std::ios::binary);
	if (!in) { std::cerr << "cannot open " << argv[1] << "\n"; return 1; }

	std::string line;
	std::getline(in, line);
	std::unordered_map<std::string, size_t> index;
	{
		std::vector<std::string> header = ParseCsvLine(line);
		for (size_t i = 0; i < header.size(); i++) { index[header[i]] = i; }
	}

	std::vector<Row> rows;
	while (std::getline(in, line))
	{
		Row row{ ParseCsvLine(line), &index };
		if (row.Fields.size() == index.size()) { rows.push_back(std::move(row)); }
	}

	if (!Capture::Start(argv[2], "replay")) { std::cerr << "cannot start recorder\n"; return 1; }
	Capture::OnMapChange(38, 9, true); // Eternal Battlegrounds: recording active

	size_t arcRows = 0;
	for (const Row& row : rows)
	{
		const std::string& kind = row["kind"];
		if (kind == "ARC") { ReplayArc(row, 0); arcRows++; }
		else if (kind == "AGENT") { ReplayAgent(row); }

		Capture::TickInfo tick;
		tick.NowMs = static_cast<uint32_t>(row.U("arrive_ms"));
		tick.HasPosition = true;
		Capture::Tick(tick);
	}

	if (repeat > 0)
	{
		auto begin = std::chrono::steady_clock::now();
		for (int r = 0; r < repeat; r++)
		{
			for (const Row& row : rows)
			{
				if (row["kind"] == "ARC") { ReplayArc(row, static_cast<uint32_t>((r + 1) * 3600000)); }
			}
		}
		double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
		std::printf("timing: %zu events in %.3f s = %.1f us/event\n", arcRows * repeat, seconds,
			seconds * 1e6 / static_cast<double>(arcRows * repeat));
	}

	Capture::Status status = Capture::GetStatus();
	Capture::Stop();
	std::printf("replayed %zu rows (%zu ARC); wrote %llu rows, %.2f MB to %s\n", rows.size(), arcRows,
		status.Rows, status.Bytes / (1024.0 * 1024.0), status.LogPath.c_str());
	return 0;
}
