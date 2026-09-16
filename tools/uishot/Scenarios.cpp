#include "Scenarios.h"

#include <cstring>

#include "Arc.h"
#include "Demo.h"
#include "ReviveSkills.h"

namespace Shots
{
	namespace
	{
		// The squad the mockups used, so the drawings and the real renders can be compared side by side.
		// account, character, profession, elite specialization
		// Invented names, capitalised the way the game requires and within its 19-character limit. Nothing
		// here is a real player: these screenshots end up in the README.
		const Player kRoster[] = {
			{ ":Sereth.4127",   "Sereth Vale",     7, 73 }, // Mesmer / Troubadour
			{ ":Kalden.5076",   "Kalden Roth",     6, 80 }, // Elementalist / Catalyst
			{ ":Maryth.2081",   "Maryth Solane",   6, 56 }, // Elementalist / Tempest
			{ ":Orrin.9143",    "Orrin Kade",      7, 59 }, // Mesmer / Chronomancer
			{ ":Brisa.3388",    "Brisa Thornwood", 4, 55 }, // Ranger / Druid
			{ ":Halvor.6610",   "Halvor Stane",    2, 61 }, // Warrior / Berserker
			{ ":Nessa.1174",    "Nessa Grimm",     8, 34 }, // Necromancer / Reaper
			{ ":Edric.7752",    "Edric Lumen",     1, 62 }, // Guardian / Firebrand
			{ ":Tamsin.5519",   "Tamsin Ward",     4, 72 }, // Ranger / Soulbeast
		};

		constexpr size_t kSelfIndex = 3; // we play Orrin Kade, 4th in the order

		std::vector<std::string> Accounts(size_t aCount)
		{
			std::vector<std::string> accounts;
			for (size_t i = 0; i < aCount && i < std::size(kRoster); i++) { accounts.push_back(kRoster[i].Account); }
			return accounts;
		}

		// A squad of aCount players, us in the middle of it, everyone in the order.
		Squad Base(size_t aCount = 6)
		{
			Squad squad;
			for (size_t i = 0; i < aCount && i < std::size(kRoster); i++)
			{
				const Player& player = kRoster[i];
				squad.Add(player.Account, player.Character, player.Profession, player.Elite, i == kSelfIndex);
			}
			squad.Order(Accounts(aCount));
			return squad;
		}

		const std::string& Me() { return kRoster[kSelfIndex].Account; }
		const std::string& Account(size_t aIndex) { return kRoster[aIndex].Account; }
	}

	namespace
	{
		// A squad mid-fight: two skills spent, one player down, the rest ready. We are 4th.
		Rezz::SessionView Mixed()
		{
			Squad squad = Base();
			squad.Used(Account(2), 27000);  // Maryth: 90 s glyph, ~63 s left
			squad.Used(Account(4), 92000);  // Brisa: 120 s spirit, ~28 s left
			squad.Downed(Account(3));       // us
			return squad.View();
		}

		// The same squad with us ready and up: everyone before us has spent theirs.
		Rezz::SessionView OurTurn()
		{
			Squad squad = Base();
			squad.Used(Account(0), 10000);
			squad.Used(Account(1), 8000);
			squad.Used(Account(2), 6000);
			return squad.View();
		}

		Rezz::SessionView NobodyReady()
		{
			Squad squad = Base();
			for (size_t i = 0; i < 6; i++) { squad.Used(Account(i), 10000 + i * 3000); }
			return squad.View();
		}

		Rezz::SessionView NinePlayers()
		{
			Squad squad = Base(9);
			squad.Used(Account(2), 27000);
			squad.Used(Account(7), 20000);
			squad.Dead(Account(6));
			return squad.View();
		}

		Rezz::SessionView Empty()
		{
			Squad squad = Base();
			squad.Order({});
			return squad.View();
		}

		Rezz::SessionView OnlyUs()
		{
			Squad squad = Base();
			squad.Order({ Me() });
			return squad.View();
		}

		Rezz::SessionView OutOfRange()
		{
			Squad squad = Base();
			squad.InCombat(20000); // long enough for the silence to count
			squad.Seen(Account(0));
			squad.Seen(Account(3));
			squad.Used(Account(2), 27000);
			return squad.View();
		}

		// Someone who joined moments ago: their "ready" is a guess until we have watched them.
		Rezz::SessionView JustJoined()
		{
			Squad squad(600000);
			for (size_t i = 0; i < 4; i++)
			{
				squad.Add(kRoster[i].Account, kRoster[i].Character, kRoster[i].Profession, kRoster[i].Elite, i == kSelfIndex);
			}
			// A fifth player joins now, so nothing of theirs has been seen yet.
			ArcDps::EvAgentUpdate update{};
			strncpy_s(update.Account, kRoster[4].Account.c_str(), _TRUNCATE);
			strncpy_s(update.Character, kRoster[4].Character.c_str(), _TRUNCATE);
			update.Id = 900;
			update.InstanceId = 90;
			update.Profession = kRoster[4].Profession;
			update.Elite = kRoster[4].Elite;
			update.Added = 1;
			squad.Session().OnAgentUpdate(update, 600000);
			squad.Order(Accounts(5));
			squad.Used(Account(0), 20000);
			return squad.View();
		}

		// Somebody typed "?rezzorder" in squad chat and we are the one with an order.
		Rezz::SessionView OrderAsked()
		{
			Squad squad = Base();
			squad.Used(Account(2), 27000);
			squad.Session().OnChatMessage(Account(4), "?rezzorder", kNowMs);
			return squad.View();
		}

		// The usual case: whoever asks has just joined and is not in the order yet.
		Rezz::SessionView OrderAskedByOutsider()
		{
			Squad squad = Base();
			squad.Order({ Account(0), Account(1), Account(2), Account(3), Account(4) });
			squad.Used(Account(2), 27000);
			squad.Session().OnChatMessage(Account(5), "?rezzorder", kNowMs);
			return squad.View();
		}

		// A plain squad member pasted an order: it waits for us instead of being applied.
		Rezz::SessionView SharedOffer()
		{
			Squad squad = Base();
			squad.Role(Account(0), Rezz::SquadRole::Member);
			squad.Session().OnChatMessage(Account(0), "!rezzorder Halvor > Kalden > Orrin* > Brisa", 600000);
			return squad.View();
		}

		// Demo mode, this many seconds into its loop. The window is drawn from the demo session itself,
		// so these shots also check that the demo plays out the way it is written.
		Rezz::SessionView DemoAt(uint64_t aIntoS)
		{
			Rezz::Demo::Start(":Orrin.9143", kNowMs - aIntoS * 1000);
			return Rezz::Demo::View(kNowMs);
		}

		Rezz::SessionView Casting()
		{
			Squad squad = Base();
			squad.Used(Account(0), 30000);
			squad.Casting(Account(1));
			return squad.View();
		}

		void Layout(Settings::Values& aValues, Settings::OverlayLayout aLayout)
		{
			aValues.Layout = aLayout;
		}

		std::vector<Scenario> Build()
		{
			using L = Settings::OverlayLayout;
			std::vector<Scenario> list;

			// Every layout in the two states that matter most: our turn, and someone else's.
			struct LayoutCase { const char* Key; L Value; };
			const LayoutCase layouts[] = { { "compact", L::Compact }, { "bars", L::Bars }, { "focus", L::Focus },
				{ "strip", L::Strip } };
			for (const LayoutCase& layout : layouts)
			{
				L value = layout.Value;
				list.push_back({ std::string(layout.Key) + "-your-turn", "we are up, backup behind us",
					[value](Settings::Values& s) { Layout(s, value); }, OurTurn });
				list.push_back({ std::string(layout.Key) + "-someone-else", "someone else is up, we are down",
					[value](Settings::Values& s) { Layout(s, value); }, Mixed });
				list.push_back({ std::string(layout.Key) + "-nine", "nine players, two on cooldown, one dead",
					[value](Settings::Values& s) { Layout(s, value); }, NinePlayers });
				list.push_back({ std::string(layout.Key) + "-nobody-ready", "every skill spent",
					[value](Settings::Values& s) { Layout(s, value); }, NobodyReady });
			}

			// The header carries what the turn means for us, and it must survive a name that wants the
			// whole width.
			list.push_back({ "bars-backup", "big bars: we are the backup, so the header is orange",
				[](Settings::Values& s) { Layout(s, L::Bars); }, [] {
					Squad squad = Base();
					squad.Used(Account(0), 10000);
					squad.Used(Account(1), 8000); // the turn sits on Maryth and we are next
					return squad.View();
				} });
			// The longest a character name can be is 19 characters; Edge of the Mists rank names ("Diamond
			// Scout") are shorter. A nickname is the only way to go past that, so both are covered.
			list.push_back({ "bars-long-name", "big bars: the longest name the game allows (19 characters)",
				[](Settings::Values& s)
				{
					Layout(s, L::Bars);
					s.Nicknames[":Halvor.6610"] = "Bartholomew Quickfi"; // 19
					s.Nicknames[":Maryth.2081"] = "Diamond Scout";       // a WvW rank, as in Edge of the Mists
				}, Mixed });
			list.push_back({ "bars-long-nickname", "big bars: a nickname longer than any real name",
				[](Settings::Values& s)
				{
					Layout(s, L::Bars);
					s.Nicknames[":Halvor.6610"] = "the one who always runs in first";
				}, Mixed });
			// "next up": the card, the backup under it, and however many more were asked for.
			list.push_back({ "nextup-someone-else", "next up: the card, the backup, then the next few",
				[](Settings::Values& s) { Layout(s, L::NextUp); s.OverlayMaxRows = 3; }, Mixed });
			list.push_back({ "nextup-your-turn", "next up: our turn, with the backup right below",
				[](Settings::Values& s) { Layout(s, L::NextUp); s.OverlayMaxRows = 3; }, OurTurn });
			list.push_back({ "nextup-all", "next up with no limit: everybody, rolled to the turn",
				[](Settings::Values& s) { Layout(s, L::NextUp); }, NinePlayers });

			// Precast players are marked with the same star the chat line uses.
			list.push_back({ "compact-precast", "two players marked as free to cast early",
				[](Settings::Values& s) { Layout(s, L::Compact); }, [] {
					Squad squad = Base();
					squad.Used(Account(2), 27000);
					Rezz::SessionView view = squad.View();
					view.Precast = { Account(1), Account(4) };
					return view;
				} });
			// The menus, for the README: everything an order needs can be done from them.
			list.push_back({ "menu-window", "right-click the window: add, remove, saved orders, share, style",
				[](Settings::Values& s) { Layout(s, L::Compact); }, Mixed, Scenario::Window::Overlay, -2 });
			list.push_back({ "menu-player", "right-click a player for what applies to them",
				[](Settings::Values& s) { Layout(s, L::Compact); }, Mixed, Scenario::Window::Overlay, 1 });
			list.push_back({ "compact-empty", "no order yet: the window still shows",
				[](Settings::Values& s) { Layout(s, L::Compact); }, Empty });
			list.push_back({ "compact-only-us", "an order with just us in it",
				[](Settings::Values& s) { Layout(s, L::Compact); }, OnlyUs });
			list.push_back({ "compact-out-of-range", "two players silent through the fight",
				[](Settings::Values& s) { Layout(s, L::Compact); }, OutOfRange });
			list.push_back({ "compact-just-joined", "a player we have not watched yet: ready?",
				[](Settings::Values& s) { Layout(s, L::Compact); }, JustJoined });
			list.push_back({ "compact-casting", "someone is casting their revive",
				[](Settings::Values& s) { Layout(s, L::Compact); }, Casting });
			list.push_back({ "compact-max-rows", "max displayed = 4, starting at whoever is up",
				[](Settings::Values& s) { Layout(s, L::Compact); s.OverlayMaxRows = 4; }, NinePlayers });
			list.push_back({ "compact-locked", "locked: no hint line, no border",
				[](Settings::Values& s) { Layout(s, L::Compact); s.OverlayLocked = true; }, Mixed });
			list.push_back({ "compact-big", "text size 1.6",
				[](Settings::Values& s) { Layout(s, L::Compact); s.OverlayScale = 1.6f; }, Mixed });
			list.push_back({ "bars-no-cooldown-bar", "seconds only, no recharge bar",
				[](Settings::Values& s) { Layout(s, L::Bars); s.CooldownBar = false; }, Mixed });
			list.push_back({ "bars-no-seconds", "recharge bar only, no seconds",
				[](Settings::Values& s) { Layout(s, L::Bars); s.CooldownSeconds = false; }, Mixed });
			list.push_back({ "focus-backup", "focus card: we are the backup, so the card is orange",
				[](Settings::Values& s) { Layout(s, L::Focus); }, [] {
					Squad squad = Base();
					squad.Used(Account(0), 10000);
					squad.Used(Account(1), 8000); // the turn sits on Maryth and we are next after them
					return squad.View();
				} });
			list.push_back({ "focus-nobody-ready-wide", "focus card with a width set by hand",
				[](Settings::Values& s) { Layout(s, L::Focus); s.OverlayWidth = 420.0f; }, NobodyReady });
			list.push_back({ "strip-nine-wrapped", "a strip with a width set: cells wrap to a second line",
				[](Settings::Values& s) { Layout(s, L::Strip); s.OverlayWidth = 700.0f; }, NinePlayers });
			list.push_back({ "demo-early", "demo mode, 12 s in: one revive spent",
				[](Settings::Values& s) { Layout(s, L::Compact); }, [] { return DemoAt(12); } });
			list.push_back({ "demo-mid", "demo mode, 50 s in: two on cooldown, one down",
				[](Settings::Values& s) { Layout(s, L::Bars); }, [] { return DemoAt(50); } });
			list.push_back({ "demo-late", "demo mode, 75 s in: someone died, most skills spent",
				[](Settings::Values& s) { Layout(s, L::Focus); }, [] { return DemoAt(75); } });
			// The edge flash, at its default strength and at a loud one, over the whole screen.
			list.push_back({ "flash-your-turn", "the screen-edge flash when the turn reaches you",
				[](Settings::Values& s) { Layout(s, L::Compact); }, OurTurn, Scenario::Window::Screen });
			list.push_back({ "flash-backup-strong", "the backup flash at strength 0.6",
				[](Settings::Values& s) { Layout(s, L::Compact); s.FlashStrength = 0.6f; },
				[] {
					Squad squad = Base();
					squad.Used(Account(0), 10000);
					squad.Used(Account(1), 8000); // the turn sits on Maryth and we are next
					return squad.View();
				}, Scenario::Window::Screen });
			list.push_back({ "share-request", "someone asked the squad for the order",
				[](Settings::Values& s) { Layout(s, L::Compact); }, OrderAsked, Scenario::Window::Request });
			list.push_back({ "share-request-newcomer", "the player asking is not in the order yet",
				[](Settings::Values& s) { Layout(s, L::Compact); }, OrderAskedByOutsider, Scenario::Window::Request });
			list.push_back({ "share-offer", "a squad member shared an order: accept or ignore",
				[](Settings::Values& s) { Layout(s, L::Compact); }, SharedOffer, Scenario::Window::Share });
			list.push_back({ "editor", "the order editor", [](Settings::Values&) {}, NinePlayers,
				Scenario::Window::Editor });
			return list;
		}
	}

	const std::vector<Scenario>& All()
	{
		static const std::vector<Scenario> kAll = Build();
		return kAll;
	}
}
