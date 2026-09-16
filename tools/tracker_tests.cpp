// Unit tests for Rezz::Tracker and Rezz::Session. Run: build/release/rezz_tracker_tests.exe (exit code 0 = all passed).

#include <cstdio>
#include <string>

#include "Arc.h"
#include "Session.h"
#include "Tracker.h"

namespace
{
	int s_Failures = 0;

	#define CHECK(expr) \
		do { if (!(expr)) { std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #expr); s_Failures++; } } while (0)

	constexpr uint32_t BS  = 14419; // Battle Standard, 2000 ms cast, 120 s
	constexpr uint32_t IOL = 10244; // Illusion of Life, 1250 ms cast, 90 s
	constexpr uint32_t SON = 12569; // Spirit of Nature, 1500 ms cast, 120 s
	constexpr uint32_t SLAM = 12601;
	constexpr uint32_t FIRE = 5762, EARTH = 5761; // Glyph of Renewal variants, shared cooldown
	constexpr uint8_t  FULL = ArcDps::ANIMSTOP_RETURN_CONTROL;
	constexpr uint8_t  CMD  = ArcDps::ANIMSTOP_COMMAND;

	const std::string A = ":a.1", B = ":b.2", C = ":c.3";

	// Start a cast at aStart and stop it as a full cast.
	void Use(Rezz::Tracker& aTracker, uint64_t aStart, const std::string& aAccount, uint32_t aSkill, int32_t aBaseMs = 2500)
	{
		aTracker.OnCastStart(aStart, aAccount, aSkill);
		aTracker.OnCastStop(aStart + aBaseMs, aAccount, aSkill, FULL, aBaseMs);
	}

	std::string Up(const Rezz::Tracker& aTracker, uint64_t aNow)
	{
		Rezz::TurnView view = aTracker.GetTurn(aNow);
		return view.UpIndex < 0 ? std::string("-") : view.Rows[view.UpIndex].Account;
	}

	void TestInitialTurnIsFirstInOrder()
	{
		Rezz::Tracker t;
		t.SetOrder({ A, B, C });
		CHECK(Up(t, 1000) == A);
	}

	void TestUseAdvancesAndCooldown()
	{
		Rezz::Tracker t;
		t.SetOrder({ A, B, C });
		Use(t, 1000, A, BS);
		CHECK(Up(t, 5000) == B);
		Rezz::TurnView view = t.GetTurn(5000);
		CHECK(view.Rows[0].Status == Rezz::Eligibility::Cooldown);
		CHECK(view.Rows[0].IsLastUser);
		CHECK(view.Rows[0].ReadyInMs == 3500 + 120000 - 5000);
		const Rezz::PlayerStatus* a = t.FindPlayer(A);
		CHECK(a && a->Skills.size() == 1 && a->Skills[0].Uses == 1);
	}

	void TestCancelDoesNotAdvance()
	{
		Rezz::Tracker t;
		t.SetOrder({ A, B });
		t.OnCastStart(1000, A, BS);
		CHECK(t.GetTurn(1500).Rows[0].Status == Rezz::Eligibility::Casting);
		CHECK(Up(t, 1500) == A);
		t.OnCastStop(2800, A, BS, CMD, 1800); // cancelled late
		CHECK(Up(t, 3000) == A);
		CHECK(t.FindPlayer(A)->Skills[0].Cancels == 1);
		CHECK(t.FindPlayer(A)->Skills[0].Uses == 0);
	}

	void TestUsedWithoutFullStopReason()
	{
		// Dodged after the skill went off (base >= cast time) still counts as used.
		Rezz::Tracker t;
		t.SetOrder({ A, B });
		t.OnCastStart(1000, A, BS);
		t.OnCastStop(3100, A, BS, ArcDps::ANIMSTOP_MOVEDODGE, 2069);
		CHECK(Up(t, 4000) == B);
	}

	void TestOutOfTurnContinuesAfterUser()
	{
		Rezz::Tracker t;
		t.SetOrder({ A, B, C });
		Use(t, 1000, B, IOL, 1300); // A was up, B went instead
		CHECK(Up(t, 5000) == C);
		Use(t, 6000, C, BS);
		CHECK(Up(t, 10000) == A); // wraps around; B is on cooldown anyway
	}

	void TestSkipCooldownAndDowned()
	{
		Rezz::Tracker t;
		t.SetOrder({ A, B, C });
		Use(t, 1000, A, BS);
		t.OnLifeState(4000, B, Rezz::LifeState::Downed);
		CHECK(Up(t, 5000) == C);
		t.OnLifeState(6000, B, Rezz::LifeState::Alive);
		CHECK(Up(t, 7000) == B);
		t.OnLifeState(8000, B, Rezz::LifeState::Dead);
		CHECK(Up(t, 9000) == C);
	}

	void TestNobodyReadyReportsNextReady()
	{
		Rezz::Tracker t;
		t.SetOrder({ A, B });
		Use(t, 1000, A, IOL, 1300);  // ready at 2300 + 90 000
		Use(t, 10000, B, BS, 2500);  // ready at 12 500 + 120 000
		Rezz::TurnView view = t.GetTurn(20000);
		CHECK(view.UpIndex == -1);
		CHECK(view.NextReadyIndex == 0);
		CHECK(Up(t, 92300) == A); // A's cooldown over
	}

	void TestGlyphVariantsShareCooldown()
	{
		Rezz::Tracker t;
		t.SetOrder({ A, B });
		Use(t, 1000, A, FIRE);
		const Rezz::PlayerStatus* a = t.FindPlayer(A);
		t.OnCastStart(50000, A, EARTH); // impossible in game before 90 s: flagged as early recast
		CHECK(a->Skills.size() == 1);
		CHECK(a->Skills[0].EarlyRecasts == 1);
	}

	void TestSpiritCastAndSlamAttribution()
	{
		Rezz::Tracker t;
		t.SetOrder({ A, B });
		t.OnLifeState(1000, B, Rezz::LifeState::Downed);
		t.OnCastStart(2000, A, SON);
		t.OnCastStop(3700, A, SON, FULL, 1700);            // the spirit cast is the use
		CHECK(t.LastOrderedUser() == A);
		CHECK(Up(t, 3800) == "-");                         // A on cooldown, B downed
		t.OnLifeState(4450, B, Rezz::LifeState::Alive);    // got up ~115 ms before the slam's stop event
		CHECK(Up(t, 4500) == B);
		t.OnOwnedEffect(4565, A, SLAM);
		const Rezz::SkillStatus& spirit = t.FindPlayer(A)->Skills[0];
		CHECK(spirit.Uses == 1);
		CHECK(spirit.Revived == 1);
		CHECK(spirit.LastRevived == 1);
	}

	void TestAttributionRespectsTargetLimit()
	{
		Rezz::Tracker t;
		t.SetOrder({ A });
		for (const std::string& ally : { B, C, std::string(":d.4") }) { t.OnLifeState(1000, ally, Rezz::LifeState::Downed); }
		for (const std::string& ally : { B, C, std::string(":d.4") }) { t.OnLifeState(5000, ally, Rezz::LifeState::Alive); }
		t.OnOwnedEffect(5115, A, SLAM); // WvW limit 2: the third ally was revived some other way
		CHECK(t.FindPlayer(A)->Skills[0].Revived == 2);
	}

	void TestIllusionAttributionAtStop()
	{
		Rezz::Tracker t;
		t.SetOrder({ A });
		t.OnLifeState(1000, B, Rezz::LifeState::Downed);
		t.OnCastStart(2000, A, IOL);
		t.OnCastStop(3300, A, IOL, FULL, 1300);
		t.OnLifeState(3300, B, Rezz::LifeState::Alive);
		CHECK(t.FindPlayer(A)->Skills[0].Revived == 1);
		t.OnLifeState(20000, C, Rezz::LifeState::Downed);
		t.OnLifeState(21000, C, Rezz::LifeState::Alive); // no cast nearby: manual revive
		CHECK(t.FindPlayer(A)->Skills[0].Revived == 1);
	}

	void TestOrderChangeRestartsRotation()
	{
		Rezz::Tracker t;
		t.SetOrder({ A, B, C });
		Use(t, 1000, B, BS);
		t.SetOrder({ A, B, C }); // same order posted again: rotation kept
		CHECK(Up(t, 5000) == C);
		t.SetOrder({ C, B, A }); // new order: starts at the top (field test 2026-09-16)
		CHECK(t.LastOrderedUser().empty());
		CHECK(Up(t, 5000) == C);
		t.SetOrder({ B, A, C }); // B is first but on cooldown: skipped
		CHECK(Up(t, 5000) == A);
	}

	void TestStaleDownedAndStuckCast()
	{
		Rezz::Tracker t;
		t.SetOrder({ A, B });
		t.OnLifeState(1000, A, Rezz::LifeState::Downed);
		CHECK(Up(t, 2000) == B);
		CHECK(Up(t, 1000 + Rezz::Tracker::kStaleDownedMs + 1) == A);

		Rezz::Tracker s;
		s.SetOrder({ A });
		s.OnCastStart(1000, A, BS); // stop event never arrives
		CHECK(s.GetTurn(2000).Rows[0].Status == Rezz::Eligibility::Casting);
		CHECK(s.GetTurn(20000).Rows[0].Status == Rezz::Eligibility::Ready);
	}

	void TestPlayersNotInOrderDontMoveTurn()
	{
		Rezz::Tracker t;
		t.SetOrder({ A, B });
		Use(t, 1000, A, BS);
		Use(t, 2000, C, IOL, 1300); // not in the order
		CHECK(t.LastOrderedUser() == A);
		CHECK(Up(t, 5000) == B);
	}
	void TestBackupIsNextEligible()
	{
		Rezz::Tracker t;
		t.SetOrder({ A, B, C });
		Rezz::TurnView view = t.GetTurn(1000);
		CHECK(view.UpIndex == 0 && view.BackupIndex == 1);
		t.OnLifeState(2000, B, Rezz::LifeState::Downed);
		view = t.GetTurn(3000);
		CHECK(view.UpIndex == 0 && view.BackupIndex == 2);
		Use(t, 4000, A, BS);
		view = t.GetTurn(8000);
		CHECK(view.UpIndex == 2 && view.BackupIndex == -1); // A on cooldown, B downed
	}

	void TestAwayAndActivity()
	{
		Rezz::Tracker t;
		t.SetOrder({ A, B });
		t.SetAway(1000, A, true);
		CHECK(t.GetTurn(1500).Rows[0].Status == Rezz::Eligibility::Away);
		CHECK(Up(t, 1500) == B);
		t.OnLifeState(2000, B, Rezz::LifeState::Dead);
		CHECK(Up(t, 2500) == "-");
		t.OnActivity(3000, B); // cast something: not dead any more
		CHECK(Up(t, 3500) == B);
		t.OnLifeState(4000, A, Rezz::LifeState::Dead);
		t.SetAway(5000, A, false); // back on the map: alive
		CHECK(Up(t, 5500) == A);
	}

	ArcDps::EvAgentUpdate Agent(const std::string& aAccount, uint64_t aId, uint16_t aInstance, uint32_t aProfession, bool aAdded, bool aSelf = false)
	{
		ArcDps::EvAgentUpdate update{};
		strncpy_s(update.Account, aAccount.c_str(), _TRUNCATE);
		strncpy_s(update.Character, "Diamond Legend", _TRUNCATE);
		update.Id = aId;
		update.InstanceId = aInstance;
		update.Profession = aProfession;
		update.Added = aAdded;
		update.Self = aSelf;
		return update;
	}

	void Event(Rezz::Session& aSession, uint64_t aTime, uint8_t aStatechange, uint64_t aSrc, uint16_t aSrcInst, uint32_t aSkill,
		uint8_t aResult = 0, int32_t aBaseMs = 0, uint16_t aMasterInst = 0)
	{
		ArcDps::CombatEvent ev{};
		ev.Time = aTime;
		ev.IsStatechange = aStatechange;
		ev.SrcAgent = aSrc;
		ev.SrcInstId = aSrcInst;
		ev.SrcMasterInstId = aMasterInst;
		ev.SkillId = aSkill;
		ev.Result = aResult;
		ev.BuffDmg = aBaseMs;
		ArcDps::Agent src{ "Diamond Legend", aSrc, 0, 0, 0, 0 };
		ArcDps::EvCombatData data{ &ev, &src, nullptr, "", 0, 1 };
		aSession.OnCombat(data, aTime + 2650);
	}

	void TestSessionResolvesAccounts()
	{
		Rezz::Session s;
		s.OnAgentUpdate(Agent(A, 100, 7, 2, true), 0);
		s.OnAgentUpdate(Agent(B, 200, 8, 4, true), 0);
		s.SetOrder({ A, B });

		Event(s, 1000, ArcDps::CBTS_ANIMATIONSTART, 100, 7, BS);
		Event(s, 3500, ArcDps::CBTS_ANIMATIONSTOP, 100, 7, BS, FULL, 2500);
		CHECK(s.GetView(4000).Turn.UpIndex == 1);

		// B's agent id changed (not in the live feed): found by the instance id.
		Event(s, 5000, ArcDps::CBTS_CHANGEDOWN, 999, 8, 0);
		CHECK(s.GetView(5500).Turn.UpIndex == -1);
		Event(s, 6000, ArcDps::CBTS_CHANGEUP, 999, 8, 0);

		// A spirit slam from B's spirit (master instance 8) revives C and is attributed to B.
		s.OnAgentUpdate(Agent(C, 300, 9, 1, true), 0);
		Event(s, 7000, ArcDps::CBTS_CHANGEDOWN, 300, 9, 0);
		Event(s, 8000, ArcDps::CBTS_ANIMATIONSTART, 999, 8, SON);
		Event(s, 9700, ArcDps::CBTS_ANIMATIONSTOP, 999, 8, SON, FULL, 1700);
		Event(s, 10450, ArcDps::CBTS_CHANGEUP, 300, 9, 0);
		Event(s, 10565, ArcDps::CBTS_ANIMATIONSTOP, 5555, 40, SLAM, FULL, 750, 8);
		const Rezz::PlayerStatus* b = s.GetTracker().FindPlayer(B);
		CHECK(b && b->Skills.size() == 1 && b->Skills[0].Revived == 1);

		Rezz::SessionView view = s.GetView(11000);
		CHECK(view.Roster.size() == 3);
		for (const Rezz::RosterMember& member : view.Roster)
		{
			if (member.Account == A) { CHECK(member.SeenGroups == 1u << static_cast<int>(ReviveGroup::BattleStandard)); }
			if (member.Account == C) { CHECK(member.SeenGroups == 0); }
		}
	}

	void TestSessionBackupIsNextReady()
	{
		Rezz::Session s;
		s.OnAgentUpdate(Agent(B, 200, 8, 4, true), 0);
		s.SetOrder({ A, B, C });
		Rezz::SessionView view = s.GetView(1000);
		CHECK(view.Turn.UpIndex == 0 && view.BackupIndex == 1);
		Event(s, 1500, ArcDps::CBTS_CHANGEDOWN, 200, 8, 0); // B is down: the next player backs up
		CHECK(s.GetView(2000).BackupIndex == 2);
	}

	void TestCharacterNames()
	{
		CHECK(Rezz::IsUsableCharacterName("Sleeplxss"));
		CHECK(Rezz::IsUsableCharacterName("No Black No Bear"));
		CHECK(!Rezz::IsUsableCharacterName(""));
		CHECK(!Rezz::IsUsableCharacterName("ag1458"));       // arcdps placeholder
		CHECK(!Rezz::IsUsableCharacterName("Diamond Legend")); // WvW rank shown in Edge of the Mists
		CHECK(!Rezz::IsUsableCharacterName("Platinum Scout"));
		CHECK(Rezz::IsUsableCharacterName("Diamond Dave"));
	}

	size_t CountNotices(const std::vector<Rezz::Notice>& aNotices, Rezz::NoticeKind aKind)
	{
		size_t n = 0;
		for (const Rezz::Notice& notice : aNotices) { if (notice.Kind == aKind) { n++; } }
		return n;
	}

	void TestSessionLeaveNotices()
	{
		Rezz::Session s;
		s.OnAgentUpdate(Agent(":me.1", 1, 1, 7, true, true), 0);
		s.OnAgentUpdate(Agent(A, 100, 7, 2, true), 0);
		s.OnAgentUpdate(Agent(B, 200, 8, 4, true), 0);
		s.OnAgentUpdate(Agent(C, 300, 9, 1, true), 0);
		s.SetOrder({ A, B });

		// A leaves the map and comes back within the grace time: nothing.
		s.OnAgentUpdate(Agent(A, 100, 7, 0, false), 10000);
		s.Tick(12000);
		s.OnAgentUpdate(Agent(A, 101, 17, 2, true), 14000);
		s.Tick(30000);
		CHECK(s.TakeNotices().empty());

		// A leaves the map for longer: one notice, then "is back".
		s.OnAgentUpdate(Agent(A, 101, 17, 0, false), 40000);
		s.Tick(45000);
		CHECK(s.TakeNotices().empty());
		CHECK(s.GetView(45000).Turn.Rows[0].Status == Rezz::Eligibility::Away);
		s.Tick(48000);
		std::vector<Rezz::Notice> notices = s.TakeNotices();
		CHECK(notices.size() == 1 && notices[0].Kind == Rezz::NoticeKind::LeftMap && notices[0].Account == A);
		s.OnAgentUpdate(Agent(A, 102, 18, 2, true), 80000);
		notices = s.TakeNotices();
		CHECK(notices.size() == 1 && notices[0].Kind == Rezz::NoticeKind::Returned);

		// Our own map change: everyone leaves just before our self leave. No notices.
		s.OnAgentUpdate(Agent(A, 102, 18, 0, false), 100000);
		s.OnAgentUpdate(Agent(B, 200, 8, 0, false), 100000);
		s.OnAgentUpdate(Agent(C, 300, 9, 0, false), 100000);
		s.OnAgentUpdate(Agent(":me.1", 1, 1, 0, false, true), 101000);
		s.Tick(130000);
		CHECK(s.TakeNotices().empty());
		s.OnAgentUpdate(Agent(":me.1", 1, 1, 7, true, true), 140000);
		s.OnAgentUpdate(Agent(A, 103, 19, 2, true), 141000);
		s.OnAgentUpdate(Agent(B, 201, 20, 5, true), 141000); // came back as a thief
		notices = s.TakeNotices();
		CHECK(notices.size() == 1 && notices[0].Kind == Rezz::NoticeKind::ChangedProfession && notices[0].Account == B);

		// Leaving the squad is reported at once; the arcdps leave that follows adds nothing.
		s.OnRole(A, Rezz::SquadRole::Member, 1, 150000);
		s.OnRole(C, Rezz::SquadRole::Invited, 1, 150000); // not in the order, and not a leave anyway
		CHECK(s.TakeNotices().empty());
		s.OnRole(A, Rezz::SquadRole::None, 0, 160000);
		notices = s.TakeNotices();
		CHECK(notices.size() == 1 && notices[0].Kind == Rezz::NoticeKind::LeftSquad);
		s.OnAgentUpdate(Agent(A, 103, 19, 0, false), 162700);
		s.Tick(200000);
		CHECK(CountNotices(s.TakeNotices(), Rezz::NoticeKind::LeftMap) == 0);
	}

	void TestReadyIsUnconfirmedUntilWatched()
	{
		Rezz::Tracker t;
		t.SetOrder({ A, B });
		t.MarkSeenSince(1000, A); // we just joined their squad
		CHECK(t.GetTurn(2000).Rows[0].Status == Rezz::Eligibility::Ready);
		CHECK(t.GetTurn(2000).Rows[0].Unconfirmed);
		CHECK(!t.GetTurn(1000 + Rezz::Tracker::kConfirmAfterMs + 1).Rows[0].Unconfirmed);

		// A signet passive says the signet is ready right now.
		t.MarkSeenSince(1000, B);
		CHECK(t.GetTurn(2000).Rows[1].Unconfirmed);
		t.MarkSkillReady(2000, B, ReviveGroup::SignetOfMercy);
		CHECK(!t.GetTurn(2500).Rows[1].Unconfirmed);
		CHECK(t.GetTurn(2500).Rows[1].Status == Rezz::Eligibility::Ready);

		// Leaving the map and coming back makes it a guess again.
		Use(t, 3000, A, BS);
		CHECK(!t.GetTurn(5000).Rows[0].Unconfirmed);
		t.SetAway(6000, A, true);
		t.SetAway(300000, A, false);
		t.MarkSeenSince(300000, A);
		CHECK(t.GetTurn(301000).Rows[0].Unconfirmed);
	}

	void TestLeavingDuringOurLoadingScreen()
	{
		Rezz::Session s;
		s.OnAgentUpdate(Agent(":me.1", 1, 1, 7, true, true), 0);
		s.OnAgentUpdate(Agent(A, 100, 7, 2, true), 0);
		s.OnAgentUpdate(Agent(B, 200, 8, 4, true), 0);
		s.SetOrder({ A, B });

		// Our own map change: everyone is removed, then only B comes back with us.
		s.OnAgentUpdate(Agent(A, 100, 7, 0, false), 10000);
		s.OnAgentUpdate(Agent(B, 200, 8, 0, false), 10000);
		s.OnAgentUpdate(Agent(":me.1", 1, 1, 0, false, true), 11000);
		s.OnAgentUpdate(Agent(":me.1", 1, 1, 7, true, true), 30000);
		s.OnAgentUpdate(Agent(B, 201, 9, 4, true), 31000);
		s.Tick(40000);
		CHECK(s.TakeNotices().empty()); // still within the resync time

		s.Tick(30000 + Rezz::Session::kResyncAfterMs + 1); // no Unofficial Extras here: the fallback applies
		std::vector<Rezz::Notice> notices = s.TakeNotices();
		CHECK(notices.size() == 1 && notices[0].Account == A && notices[0].Kind == Rezz::NoticeKind::LeftMap);
		CHECK(s.GetView(70000).Turn.Rows[0].Status == Rezz::Eligibility::Away);
	}

	void TestOutOfRangeIsVisible()
	{
		Rezz::Session s;
		s.OnAgentUpdate(Agent(A, 100, 7, 2, true), 1000);
		s.SetOrder({ A });
		Event(s, 2000, ArcDps::CBTS_SQCOMBATSTART, 0, 0, 0);
		Rezz::SessionView view = s.GetView(3000);
		CHECK(view.SquadInCombat);
		CHECK(view.Roster.size() == 1 && view.NowMs == 3000);
		CHECK(view.Roster[0].LastEventMs == 1000); // only their join so far
		Event(s, 9000, ArcDps::CBTS_CHANGEUP, 100, 7, 0); // an event of theirs: still in range
		CHECK(s.GetView(20000).Roster[0].LastEventMs == 9000 + 2650);
	}

	void TestSelfLeavingSquadClearsOrder()
	{
		Rezz::Session s;
		s.OnAgentUpdate(Agent(":me.1", 1, 1, 7, true, true), 0);
		s.SetOrder({ A, B });
		s.OnRole(":me.1", Rezz::SquadRole::Member, 1, 1000);
		CHECK(s.TakeNotices().empty() && s.Order().size() == 2);
		s.OnRole(":me.1", Rezz::SquadRole::None, 0, 2000);
		std::vector<Rezz::Notice> notices = s.TakeNotices();
		CHECK(notices.size() == 1 && notices[0].Kind == Rezz::NoticeKind::OrderCleared);
		CHECK(s.Order().empty());

		// Role reported before arcdps told us our account (startup outside a squad).
		Rezz::Session t;
		t.SetOrder({ A });
		t.OnRole(":me.1", Rezz::SquadRole::None, 0, 1000);
		CHECK(t.Order().size() == 1);
		t.OnAgentUpdate(Agent(":me.1", 1, 1, 7, true, true), 2000);
		CHECK(t.Order().empty());
	}
}

int main()
{
	const struct { const char* Name; void (*Fn)(); } tests[] = {
		{ "initial turn is first in order", TestInitialTurnIsFirstInOrder },
		{ "use advances turn and starts cooldown", TestUseAdvancesAndCooldown },
		{ "cancel does not advance", TestCancelDoesNotAdvance },
		{ "used without full stop reason", TestUsedWithoutFullStopReason },
		{ "out of turn continues after user", TestOutOfTurnContinuesAfterUser },
		{ "skip cooldown and downed/dead", TestSkipCooldownAndDowned },
		{ "nobody ready reports next ready", TestNobodyReadyReportsNextReady },
		{ "glyph variants share cooldown", TestGlyphVariantsShareCooldown },
		{ "spirit cast and slam attribution", TestSpiritCastAndSlamAttribution },
		{ "attribution respects target limit", TestAttributionRespectsTargetLimit },
		{ "illusion attribution at stop", TestIllusionAttributionAtStop },
		{ "order change restarts rotation", TestOrderChangeRestartsRotation },
		{ "stale downed and stuck cast", TestStaleDownedAndStuckCast },
		{ "players not in order don't move turn", TestPlayersNotInOrderDontMoveTurn },
		{ "backup is next eligible", TestBackupIsNextEligible },
		{ "away and activity", TestAwayAndActivity },
		{ "session resolves accounts", TestSessionResolvesAccounts },
		{ "session backup is next ready", TestSessionBackupIsNextReady },
		{ "character names", TestCharacterNames },
		{ "session leave notices", TestSessionLeaveNotices },
		{ "self leaving squad clears order", TestSelfLeavingSquadClearsOrder },
		{ "ready is unconfirmed until watched", TestReadyIsUnconfirmedUntilWatched },
		{ "leaving during our loading screen", TestLeavingDuringOurLoadingScreen },
		{ "out of range is visible", TestOutOfRangeIsVisible },
	};

	for (const auto& test : tests)
	{
		int before = s_Failures;
		test.Fn();
		std::printf("%s %s\n", s_Failures == before ? "ok  " : "FAIL", test.Name);
	}
	std::printf("%s (%d failed checks)\n", s_Failures == 0 ? "ALL PASSED" : "FAILURES", s_Failures);
	return s_Failures == 0 ? 0 : 1;
}
