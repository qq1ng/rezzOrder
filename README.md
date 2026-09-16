# Rezz Order

A Guild Wars 2 addon for WvW squads with a set revive tool order. It watches everyone's instant revive
skills and shows whose turn it is, so two people don't burn an Illu and Spirit on the
same downed player.

![What it does](docs/images/banner.gif)


## Install

**[Download the latest RezzOrder.dll](https://github.com/qq1ng/rezzOrder/releases/latest)**, then:

1. **Nexus.** Get it from [raidcore.gg/Nexus](https://raidcore.gg/Nexus) and install it into your Guild Wars
   2 folder. Nexus is the addon loader everything else sits on.
2. **ArcDPS, plus the ArcDPS Integration addon.** ArcDPS comes from
   [deltaconnected.com/arcdps](https://www.deltaconnected.com/arcdps/). *ArcDPS Integration* is a separate
   addon you install from the Nexus addon library (Nexus menu, Addons, search for it). It is what passes
   ArcDPS combat events on to other addons, and without it this addon sees nothing at all.
3. **Unofficial Extras** (optional but strongly recommended), from
   [its releases page](https://github.com/Krappa322/arcdps_unofficial_extras_releases). It adds squad roles,
   instant notice when somebody leaves the squad, and the squad chat messages needed for order sharing.
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
- Bright green means **you**. A quieter green means somebody else is up.
- A recharging player shows their CD timer in red, with a bar filling up.
- A downed or dead player fades to grey, they are skipped.
- `ready?` means that player joined too recently for the addon to know if their skill is up.
- `?` next to a number means nothing has been heard from them for the whole fight, so they are out of range
  and their state may be stale.
- A `*` after a name marks a precast player. See below.

Leaving the squad or swapping to another character takes that player out of the order by itself, and
everybody still in it keeps their turn. Moving to another map does not: they show as `away` until they are
back, because they usually are.

### Customizable

Right-click the window and pick **style**:

- Five layouts: a compact list, big bars, a focus card, a horizontal strip for a screen edge, or next up
- **Next up** always shows the player who is up at the top, the backup directly under it, and as many of the following
  players as *max displayed* allows.
- Cooldown as a bar, in seconds, or both.
- Drag the **corner grip** to size the whole window. Drag the window itself to move it.
- **Ctrl+Shift+L** locks it: it stops moving and clicks pass through to the game. Hold **Ctrl+Shift** to
  edit it anyway.

![The layouts](docs/images/layouts.png)

### Try it without a squad

Nexus options, **Demo squad**. A made-up squad fights on a loop so you can place the window, pick a
layout and tune the sound and flash.

### Notifications once it's your turn

In the Nexus options page, under *When the turn reaches you*, each of these can be set for "your turn" and
for "backup" separately:

- a **banner text** across the top of the screen,
- a **sound**, six to choose from with a play button on each, or your own WAV file,
- a **flash** along the edges of the screen, in a colour you pick.


### Share the order with the squad

Addons are not allowed to write in chat, so sharing is done via copy paste:

1. **Ctrl+Shift+K**, or right-click the window and pick *Copy order for squad chat*. Your clipboard now
   holds a line like `!rezzorder Kalden > Orrin* > Halvor`.
2. Paste it into squad chat.

Everyone else running the addon automatically reads it from chat. An order from the commander or a lieutenant is applied
instantly. An order from anybody else opens a window showing the order and where it would put you, with *Use this
order* and *Ignore*.

### Ask for the order

Type **`?rezzorder`** in squad chat. This is for joining a squad that already agreed on an order. The question goes to every client running the addon.

Only one person gets a popup, the one whose order it is, so a squad of ten does not get ten popups.
Their popup names the asking person and offers *Copy it for squad chat*. If you are not in the order yet, which is the
usual reason for asking, the first button instead reads *Add playerXYZ and copy*, which puts you at the end and
copies the line in one click.

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

## Licence

Not chosen yet.
