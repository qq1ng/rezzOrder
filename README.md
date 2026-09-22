# Rezz Order

> ## Discontinued
>
> **This addon is no longer developed, and I no longer recommend running it.**
>
> Rezz Order worked out whose revive skill was ready by adding up cast events from ArcDPS. In its
> 20.09.2026 build, ArcDPS stopped giving addons that data.
>
>> Since the 20.09.2026 ArcDPS build, the turn tracking no longer works at all, and I am not going to
> rebuild it another way. The older releases below stay downloadable so nothing breaks unannounced, but
> I would turn the addon off.
>
> The code stays, for anyone who wants to read it.
>22.09.2026


A Guild Wars 2 addon for WvW squads with a set revive tool order. It watches everyone's instant revive
skills and shows whose turn it is, so two people don't burn an Illu and Spirit on the
same downed player.

DM **exoqqen**
for questions.

![What it does](docs/images/banner.gif)


## Install

**[Download the latest RezzOrder.dll](https://github.com/qq1ng/rezzOrder/releases/latest)**, then:

1. **Nexus.** Get it from [raidcore.gg/Nexus](https://raidcore.gg/Nexus) and install it into your Guild Wars
   2 folder. Nexus is the addon loader everything else sits on.
2. **ArcDPS, plus the ArcDPS Integration addon.** ArcDPS comes from
   [deltaconnected.com/arcdps](https://www.deltaconnected.com/arcdps/). *ArcDPS Integration* is a separate
   addon you install from the Nexus addon library (Nexus menu, Addons, search for it). It is what passes
   ArcDPS combat events on to other addons, and without it this addon sees nothing at all.
3. **Unofficial Extras**, from
   [its releases page](https://github.com/Krappa322/arcdps_unofficial_extras_releases). Required: it provides
   squad roles, subgroups, instant notice when somebody leaves the squad, and the squad chat messages needed for
   order sharing. Without it the addon still counts revives, but sharing, bench swaps and leave detection don't work.
4. Put `RezzOrder.dll` into `<Guild Wars 2>\addons\`.
5. Start the game. You should get a message telling you everything was found, or naming what
   is missing.
## Using it

### Build an order

**Right-click the turn window.** That menu is all you need most times: *Add me*, *Add player* to pick
somebody out of the squad, *Remove player*, a nickname, and your
saved orders. Right-click a player for options specific to them.

![The right-click menus](docs/images/menus.png)

For setting a whole squad up at once more easily, press **Ctrl+Shift+O** for the order editor. The left column is the
squad, the right column is the order: `+` adds someone, dragging a player moves them in the order. Only professions with an
instant revive are listed unless you tick *all professions*.

![The order editor](docs/images/editor.png)

### Read the turn window

- `UP` is whose turn it is. `BK` is the backup, meaning the next person ready after them.
- During a fight the turn works down the list: someone whose rez comes back waits until the list gets back
  to them. Once the squad has been out of combat for 10 seconds, the turn resets to the top.
- Bright green means **you**. A quieter green means somebody else is up.
- A recharging player shows their CD timer in red, with a bar filling up.
- A downed or dead player fades to grey, they are skipped.
- `ready?` means that player joined too recently for the addon to know if their skill is up.
- `?` next to a number means nothing has been heard from them for the whole fight, so they are out of range
  and their state may be stale.
- A `*` after a name marks a precast player. See below.

Leaving the squad or swapping to another character takes that player out of the order by itself, and
everybody still in it keeps their turn. Moving to another map does not: they show as `away` and are skipped
until they are back.

### Customizable

Right-click the window and pick **style**:

- Five layouts: a compact list, big bars, a focus card, a horizontal strip for a screen edge, or next up
- **Next up** always shows the player who is up at the top, the backup directly under it, and as many of the
  following players as *max displayed* allows.
- Cooldown as a bar, in seconds, or both.
- Drag the **corner grip** to size the whole window. Drag the window itself to move it.
- The window keeps one size whoever is up and whatever the names are, so it can sit packed in beside other
  windows. With no order it keeps the size it last had with one. *max name length* sets how much room a name gets; a longer one is cut.
- *messages* puts short-lived messages, like somebody leaving the squad, in a strip below or above the window,
  or hides them. Pick the side that has room.
- **Ctrl+Shift+L** locks it: it stops moving and clicks pass through to the game. Hold **Ctrl+Shift** to
  edit it anyway.

![The layouts](docs/images/layouts.png)

### Try without a squad

Nexus options, **Demo squad**. A made-up squad fights on a loop so you can place the window, pick a
layout and tune the sound and flash.

### Notifications once it's your turn

In the Nexus options page, under *When the turn reaches you*, each of these can be set for "your turn" and
for "backup" separately:

- a **banner** on screen: its style, size, place and colour can be set, separately for alerts (your turn,
  backup, Illusion of Life) and for other messages, so either can be kept clear of the target's name at the top,
- a **sound**, twelve to choose from with a play button on each, or your own WAV file,
- a **flash** along the edges of the screen, in a colour you pick.


### Illusion of Life countdown

A player revived by Illusion of Life goes down again after 15 seconds unless they kill something. The mesmer who
cast it, and everyone whose own revive skill is ready, gets a countdown of their last seconds (5 by default): the seconds in large figures and
their names, with a sound and a pink flash along the screen edges when it starts. It stops the moment they rally.
Nexus options, under *Illusion of Life*.

### After a fight: stats

Once the squad has been out of combat for 10 seconds, the fight is counted and a line appears beside the turn
window: `Fight 3: 5 revived - 7 possible - 9 downs, 2 rallied, 2 died`. Click it for details, or open
it from the turn window's right-click menu, the Nexus menu, or the options page.

- **revived / possible / downs:** how many downs a revive skill picked up, how many happened while somebody's
  revive was ready (counting how many allies those skills could revive), and how many there were in total. *Possible*
  cannot know who was in range, so read it as the best case.
- **Per player:** allies saved, skills used (and how many were fired out of turn), turns missed while somebody lay
  there, casts that picked nobody up, casts cancelled (by hand, interrupted, moving, or going down mid-cast), and
  how long on average they took from an ally going down to their cast.
- **Copy** puts the table on the clipboard for Discord.
- Stats live in memory for the session only, and nothing is written to disk. They cover what your own client saw:
  players out of ArcDPS range are missing.

The summary line can be set to appear after every fight, only while Ctrl+Shift is held, or never, in the options
page under *Fight stats*.

### Share the order with the squad

Addons are not allowed to write in chat, so sharing is done via copy paste:

1. **Ctrl+Shift+K**, or right-click the window and pick *Copy order for squad chat*. Your clipboard now
   holds a line like `!rezzorder Kalden > Orrin* > Halvor`.
2. Paste it into squad chat.

Everyone else running the addon automatically reads it from chat. An order from the commander or a lieutenant is applied
instantly. An order from anybody else opens a window showing the order and where it would put you, with *Use this
order* and *Ignore*.

### Ask to be put in the order

Type **`!rezzorder add`** in squad chat, or **`!rezzorder add 3`** / **`add first`** if you want a particular place.
This is how you get into a squad's order without the commander having to search people manually.

Only one person gets a popup: the one whose order it is, so a squad of ten does not get ten popups. Before anybody
has an order there is no owner, so the commander is asked instead, or the lieutenants when no commander is running
the addon.

Their popup lists everyone waiting, each with the place they asked for already filled in, and a box to put them
somewhere else. *Add and copy* puts them in and copies the new order; it still has to be pasted in squad chat for
the rest of the squad to get it. Where somebody ends up is the owner's decision, not the asker's.

**`!rezzorder remove`** takes you out again, on everybody's screen at once and without asking anyone. Use it when
you swap to a build without a revive skill.

### Bench swaps

Squads that rotate players in and out don't have to touch the order. Pick the **bench subgroup** in the order
editor: *last* (the highest subgroup in use, which follows the squad as it fills up), or a fixed number such as 1.
It is part of the order and goes into squad chat with it (`... > ted* > bench:last`), so everyone uses the same
one. Then swap people in the squad window as usual:

1. Move the player going out to the bench. They leave the order at once.
2. Move the player coming in to the subgroup they left, within 2 minutes.

The newcomer takes their exact place, `*` mark and turn included, on every client. The two moves can come in
either order.

- Only a druid, a troubadour, or somebody the addon has seen use a revive skill can take a place: not every
  warrior carries Battle Standard. Moved in on the wrong character, they get it once they swap.
- Moving somebody between the fighting subgroups changes nothing, even into a subgroup with nobody from the order.
- A subgroup with anybody from the order in it is never the bench, so a night without a bench, where the last
  subgroup is a fighting group, is safe as long as somebody from the order is in it.
- With *last*, an empty subgroup past the last one in use is a new group being built, not the bench.
- Dragged to the bench by mistake? Move them back within 2 minutes and they have their place again.
- A player leaving the squad, or swapping to a character without a sure revive, leaves their place open the same
  way. Rejoining or swapping back within 2 minutes gives it back. A druid or troubadour swapping to another
  druid or troubadour keeps it.
- Several swaps in one subgroup are matched by profession. If that still can't tell them apart, the places stay
  empty and a message says to add them by hand.
- Without a bench subgroup set, nobody is benched.

Nexus options, *Bench swaps*. On by default.

### Precast players

A `*` after a name marks a **precast** player: somebody who uses their revivetool when they see a fight going
badly and calls it on voice, precasting instead of waiting for their turn. Right-click a player and pick *May cast
early* to mark them.

The mark shows in the turn window and is part of the chatcommand, so everyone sees who is a precast player.They are still part of the normal order in case they don't precast, but get skipped if they do.




## Building it

Visual Studio with the C++ desktop workload, CMake and Ninja.

```
git clone --recursive https://github.com/qq1ng/rezzOrder
cd rezzOrder
.\scripts\build.ps1           # build\release\RezzOrder.dll
.\scripts\build.ps1 -Deploy   # ... and copy it into the addons folder
.\scripts\check.ps1           # build, unit tests, UI renders
```

## How it works & how to test

Squad combat events reach addons about **2.6 seconds late**. That is ArcDPS by design, not a fault here, and
it is why cooldowns are measured from the time a cast really happened rather than from when the event
arrived. A revive counts as spent when ArcDPS reports a full cast, or when the cast ran for at least its
base cast time. That rule was checked against roughly 430 casts recorded in the field.

Two things run without starting the game:

- `rezz_tracker_tests.exe` covers the tracker, the live session, order sharing, settings and demo mode as
  plain unit tests.
- `rezz_uishot.exe` renders the addon's real UI code offscreen with Direct3D and saves a PNG per situation,
  then checks each one: the right number of rows, a banner naming whoever has to act, a window that fits on
  screen. `docs/shots/report.txt` is a text dump of every layout, so a change shows up in a diff instead of
  having to be spotted in a picture.

`tools/replay.cpp` replays recorded ArcDPS logs through the same session code.

Some source comments point at write-ups under `notes/`: the measurement results, phase plans and the
roadmap. Those are working notes rather than documentation, and they are not published with the repo.

## Built on

- **[Nexus](https://raidcore.gg/Nexus)** by Raidcore, the addon loader this runs in, and its
  [addon API](https://github.com/RaidcoreGG/Nexus-API) (MIT). **ArcDPS Integration**, also Raidcore, is what
  passes ArcDPS' combat events to other addons, and **[RTAPI](https://github.com/RaidcoreGG/GW2-RealTime-API-Releases)**
  documents the real-time data Nexus addons can read.
- **[ArcDPS](https://www.deltaconnected.com/arcdps/)** by deltaconnected. Every cast, down, rally and death this
  addon reacts to comes from it, and its evtc documentation is what the event handling was written against.
- **[Unofficial Extras](https://github.com/Krappa322/arcdps_unofficial_extras_releases)** by Krappa322 (MIT):
  squad roles, subgroups, instant leave notices and the squad chat messages used for sharing the order.
- **[Dear ImGui](https://github.com/ocornut/imgui)** by Omar Cornut (MIT), through Raidcore's
  [fork](https://github.com/RaidcoreGG/imgui): every window and overlay here is drawn with it.
- **[Mumble Link API](https://github.com/RaidcoreGG/RCGG-lib-mumble-api)** (MIT), for the map and character
  state the recorder writes.
- Profession and elite specialization icons come from the official Guild Wars 2 API
  ([render.guildwars2.com](https://render.guildwars2.com)). Guild Wars 2 and its assets are the property of
  ArenaNet / NCSOFT and are used here under their
  [third party program policy](https://help.guildwars2.com/hc/en-us/articles/360013625034-Policy-Third-Party-Programs).
  They are not covered by this project's licence.

Thank you to the players that helped me test it.

## Licence

[MIT](LICENSE). Do what you like with it; keep the copyright notice. It comes with no warranty, and it is not
affiliated with or endorsed by ArenaNet.
