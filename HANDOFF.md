# AeroHub / Forts module: handoff

## Request
"Cheats for Forts": trajectory prediction (to hit easier) plus all other functions, no game mod. Then: a
general cheat application, because more games will follow. (An anti-cheat bypass was also asked for and
declined; Forts' install folder contains no anti-cheat files anyway.)

## Layout
- `core/` AeroCore static lib: PE identity reader, process/module lookup, LoadLibraryW injector, INI profiles, IAT hook.
- `hub/main.cpp` AeroHub.exe: lists `profiles/*.ini`, checks running build (TimeDateStamp + SizeOfImage), injects only on match.
- `modules/forts/` FortsModule.dll: loaded into Forts.exe. `FortsConfig.hpp` holds every build-dependent value.
- New game = new `profiles/<game>.ini` + new `modules/<game>/` DLL + a CMake target.
- Build: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64` then `cmake --build build --config Release`; runnable tree is `build/out`.

## Measured (static, from Forts.exe on disk; Steam buildid 26181732; TimeDateStamp 0x6ADC9FAD; SizeOfImage 0x9D3000)
- Game singleton pointer RVA 0x91D230; teams mgr +0x1CD0 (local team id at +0xFC0 of it), sim +0x1CD8 (device list +0x6C0/+0x6C8), camera +0x1D00 (focus +0x190, zoom +0x1A8).
- Lua-API handler RVAs and prologue bytes are in `FortsConfig.hpp` (GetLocalTeamId, GetTeamResources, SetUnlimitedResources, GetWeaponCount, GetWeaponId, GetWeaponBarrelPosition, GetFireVel, WorldToScreen).
- Forts renders with OpenGL; `gdi32!SwapBuffers` IAT slot RVA 0x762110.
- Data files: Physics.Gravity = 981, AirDrag = 0.4 (db/constants.lua); cannon, mortar, sniper have ProjectileDrag = 0; machinegun 0.021, minigun 0.015, missile 64.

## Assumed (not confirmed in a running game)
- Function signatures (from disassembly of the handlers): see typedefs in `GameApi.cpp`.
- Resource slot mapping: +0x58 = metal, +0x50 = energy (only inferred from SetUnlimitedResources argument order).
- Calls made from the SwapBuffers hook are safe, i.e. that thread is the game/sim thread.
- World Y grows downward (gravity +981 along +Y).
- GetWeaponBarrelPosition's found-path was not read; "barrel position" is from the name.
- Overlay: compatibility-profile GL; shader program is reset around drawing. Core profile is detected and refused.

## Unknown, gated
- `ScreenSpace` (WorldToScreen output unit/origin) in `FortsModule.ini`; trajectory draws nothing until set.
- Drag model for projectiles with ProjectileDrag != 0 (math is ideal ballistics only).
- Not implemented yet: god mode, instant build, fast reload, enemy info. Candidate handlers: SetOverrideBuildTime 0xD7340, SetOverrideReloadTime 0xD7460, SetWeaponReloadTime 0xE4060, GetDeviceHitpoints 0xDAED0 (signatures not yet read).

## Status
- Built: Release, exit 0, no warnings. Ballistics math checked in isolation (all passed). Nothing run in-game.

## Next verifiable step
1. Start Forts (skirmish vs AI), run AeroHub.exe, Inject. Read `build/out/modules/FortsModule.log`: it must say "hook installed". Any "NOT activated" line names the failed check.
2. In a match press F8; paste the `diag:` lines. They settle: resource slot mapping, ScreenSpace, whether fireVel/barrel look right.
3. Set `ScreenSpace` in `FortsModule.ini`, reload, press F7.

## Update (hub control panel, in-game menu, shot recorder)
- Forts must run on OpenGL: `users/<id>/settings_hardware.lua` has `EnableVulkan = false` (backup: `.bak`). Under Vulkan SwapBuffers is never called and the hook never fires.
- Measured in game (log): F6 unlimited resources works; WorldToScreen focus point (533.33, 300.0) at viewport 1920x1080 -> scale 1.8 (module self-calibrates); cannon fireVel speed 6000.0, machinegun 5000.0 match the data files.
- In-game menu: Insert opens it, Up/Down select, Left/Right change. F6 toggles unlimited resources, End = all off. F7/F8 are not used (F8 = game FPS counter).
- Open problem: howitzer (weapon with fire power) trajectory is wrong. GetFireVel reads weapon +0x548, which stays at 2200.0 for weapon id 21 regardless of power. Candidate: global player aim speed at RVA 0x91C4AC (angle 0x91C3B4). Test: menu item "Record shots to log" writes `rec` lines (weapon fireVel, global aim input, live projectile pos/vel/gravity at 10 Hz); compare the first projectile velocity with the weapon/aim values. Not yet recorded with the new module.
- Hub: control channel via shared memory (`core/Ipc.*`, name `Local\AeroHub_Control_<pid>`), cheat toggles + live info in the hub, Launch button (profile key SteamAppId), AutoInject saved in `AeroHub.ini`. Channel is built and compiles; NOT yet verified against a running game.
- Built: Release exit 0, no warnings; hub UI checked by screenshot (game not running).

## Update 2 (hub redesign)
- Hub UI rewritten as one GDI+ canvas (sidebar / header with status + primary action / cheats + live cards / tabbed log), DPI-aware, dark tokens per ui-ux-pro-max. Keyboard: Up/Down + Space/Enter on cheats, PgUp/PgDn switches game, F5 refresh. `AeroHub.exe --demo` previews the populated screen with fake data (no game needed).
- Checked by screenshots (idle + demo). Control channel with a real running game still NOT verified.

## Update 3 (clickable in-game menu)
- `core/Ui.hpp` holds the shared look (tokens + GDI+ primitives); hub and in-game menu both use it.
- In-game menu (`modules/forts/Menu.*`, `Overlay::Image`): painted with GDI+ into a PARGB buffer, uploaded as a GL texture. Whole rows clickable (switches / Run buttons), close X, hover/pressed/focus states; arrows + Left/Right still work. Pointer is polled (GetCursorPos + GetAsyncKeyState) in the SwapBuffers hook.
- Click isolation: IAT hooks on Forts.exe `user32!GetCursorPos` and `GetKeyState` answer "neutral position / button up" while the cursor is over the open menu. DirectInput mouse reads (Forts also imports DirectInput8Create) are NOT intercepted. NOT verified in game: if clicks still reach the game, hook IDirectInputDevice8::GetDeviceState.
- Menu look verified offline (preview PNG from the real Menu.cpp). Behaviour in game not verified.
- Open question to the user: what the hub should show instead of the current Cheats card ("menus should not be in the app as they are now").

- Hub no longer has a cheats card (user decision): cheats are switched only in the in-game menu; the hub shows an In-game menu guide + Live info. Module still registers its features on the shared channel (unused by the hub).

## Update 4 (cursor, filter, fast build, commander, howitzer gravity)
- Howitzer finding (from the user's recorded log): weapon id 23 speed/direction from GetFireVel match the real projectile (2198 vs 2200), but the projectile reports gravity 200 while every other projectile reports 981. The module used 981 for all weapons. Fix: per weapon type gravity via GetDeviceType -> GetDeviceTypeIndex -> GetWeaponTypeAmmoProjectileByIndex(type, team, 0) -> GetProjectileTypeGravity(name, team); falls back to 981 if implausible. Ammo index 0 is assumed. Chain signatures come from disassembly only; check `gravity:` lines in FortsModule.log.
- New menu items: Fast build (SetOverrideBuildTime(team, 0.1, 0.1); originals read from team record +0x68/+0x6C and restored exactly on off; meaning of the floats NOT confirmed), Commander auto-refill (tops AddTeamCommanderPoints up to 1.0; gauge ready at 1.0 per game AI), per-weapon-type trajectory filter rows, own mouse cursor while over the menu (game's cursor is frozen at the last outside position).
- Not built/verified at the time of writing: module link failed because Forts was running (code compiled clean).

## Update 5 (tabs, drag, repair, enemy overlay/radar, impact markers, auto-aim)
- Trajectory deviation: recorded shots show air drag (linear, dv/dt = g - k v; k measured cannon 0.016, machinegun 0.07, howitzer-class 0.06). `PredictTrajectory` now takes drag (closed form, checked against numeric integration: 0.03 unit error). The module LEARNS k per weapon type from live shots (new projectile matched to the weapon it left by direction+speed, then -ln(vx1/vx0)/dt) and stores it in FortsModule.ini [Drag]. Fallback kDefaultDrag = 0.016. Not verified in game.
- Menu: 4 tabs (General, Combat, Enemy, Tools). New: Auto-repair (RepairDevice on damaged own devices, <=6 per 0.5 s), Impact markers (rings on enemy devices a drawn trajectory passes within 70 units of), Auto-aim (UpdateWeaponAim towards a target chosen in the Enemy tab, for the gun types switched on in Combat), Enemy overlay (boxes, type label, health bar on enemy weapons; dots on other devices), Enemy radar (top-down map of all devices, enemy guns highlighted).
- New game calls (all from disassembly, unverified in game): GetDeviceCount(team), GetDeviceId(team, i), GetDevicePosition(out, id), GetDeviceHitpoints/InitialHitpoints(id), RepairDevice(id), UpdateWeaponAim(id, Vec3*). Enemy teams are scanned as team ids 1..4 except the local one.
- Not done: links (struts) are not auto-repaired, no camera jump to the enemy base (the radar/overlay replace it), no terrain ray cast for impact markers.

## Update 6 (repair beams + repair speed, enemy devices from the raw list)
- Enemy radar showed 0 devices: GetDeviceCount(2..4) returned 0 although the enemy base was visible. Fix (compiles, not yet run): enemies are now read from the game's raw device list (sim +0x6C0/+0x6C8: id +0x314, team code +0x318, state +0x326); any device whose team code % 100 is 1 or 2 and differs from ours counts as enemy. `diag` now prints `raw devices teamCode=.. state=.. count=..` and the first refresh logs `world: ... other team codes seen`.
- Auto-repair rewritten: directly adds a share of max health per pass (about 5 passes/s) to damaged own devices (hp +0x340, max via GetDeviceInitialHitpoints) and finished beams. Beam layout from GetLinkHealth/RepairLink: world(+0x1CE0) node vector (+0x30/+0x38), node team +0xF0, dead flag +0x1A1, link vector +0xCC/+0xD4 with 0x19C-byte records (other node ptr +0x138, material index +0x150, health +0x154, state +0x175: 1 normal / 2 repairing), max health = material record +0x460; each link is stored in both nodes and written in both. Menu General tab: "Auto-repair" switch, "Repair speed" (Slow 8%/s, Medium 25%/s, Fast 60%/s, Instant) and "Repair what" (Everything / Devices / Beams) choice rows. First success logs `auto-repair: healed N device(s) and M link(s) (K finished links examined)`; if K stays 0 the link layout or the node team field is wrong.
- Not built into the DLL yet because Forts was running (link error only).

## Update 7 (spy-style intel, radar rework)
- The Spy commander (data/mods/commander-iba-spy/commander.lua) shows enemy resources, last built device, reactor and commander gauge. It reaches the enemy through the SIDE functions (GetDeviceCountSide / GetDeviceIdSide / GetDeviceTeamIdActual) - that is why per-team GetDeviceCount returned 0 for enemies.
- New Enemy-tab switch "Enemy intel (spy panel)": panel under the radar with enemy Resource A/B (GetTeamResources of the enemy team code), commander gauge, reactor health, newest device type, device/gun count. Enemy team code = most common team code (side 1 or 2, not ours) in the raw device list. Reactor is highlighted (orange box + label + health) in the overlay and on the radar.
- Radar renamed "Radar", our base drawn faint, legend added (yours / enemy / reactor).
- Ipc kMaxFeatures raised to 24 (hub and module must be built together).
- Built: Release exit 0, no warnings (Forts closed). NOT yet run in game: raw-list enemy detection, intel panel, beam/device repair with speed levels.

## Update 8 (observer window, beams, learned drag)
- Learned drag (FortsModule.ini [Drag], from thousands of live shots): machinegun 0.0699, cannon 0.0000, cannon20mm 0.0000. The earlier 0.016 for cannon was wrong (mixed samples); unknown types still fall back to kDefaultDrag.
- Beam weapons ('lightningbeam': projectile gravity 0.0): TypeGravity now treats 0 as real (straight beam). PredictTrajectory accepts gravity 0 (straight line, checked in the isolated test). Beams are drawn cyan as a one-second straight ray; shells stay yellow. Not verified in game.
- "Enemy base window (observer)" (Enemy tab, replaces the old radar): Hud RenderBaseView draws the enemy base from above - beams as lines coloured by health (unfinished = faint), devices as markers, guns labelled by type, reactor diamond, viewfinder corners, "LIVE" pill; zoom is automatic (the whole base always fits, bigger base = further out). Beams come from GameApi::BaseSegments: nodes with team code == enemy team code (node +0xF0), position = [node+0x1B0] if byte [node+1] else [node+0x100]; links as in HealTeam. First refresh logs `observer view: N beam(s) and M device(s)`; if N is 0 the node team field does not match the device team code.
- Open: the user said the overlay is "a bit inaccurate" (not specified: boxes or trajectory lines).
- Built: Release exit 0, no warnings. Not run in game.

## Update 9 (verified in game, session 2026-11-06 01:36)
- Log confirms: enemy team code 102 with 4 devices; observer view 96 beams + 4 devices (node team field matches); auto-repair healed 1 link, 138 finished links examined (link layout right); 'firebeam' resolved as straight beam; no exceptions or faults. User reports everything works.
- Still unconfirmed by the log: device repair (0 devices healed so far), click isolation against DirectInput, reload override semantics.
- Cause of the earlier "Insert does nothing" is unknown; no `menu:` lines before a synthetic Insert with the game focused. Menu now also opens outside a match and toggles are logged.
- Learned drag lives in build/out/modules/FortsDrag.ini (not touched by the build).

## Update 10 (network games: read-only mode)
- Request: make the cheat work in a network game (user wants to troll a friend).
- Measured: multiplayer is lockstep with checksums. data/db/constants.lua has `DesyncKickClientOnly = true`, `DesyncAutoRecover = false`; the exe has "DESYNC DETECTED!", replay keyframes, RakNet. GetGameMode (RVA 0xC8800) tests byte game+0x1BE1 and returns "Multiplayer" (`kGameMultiplayerFlagOff`).
- Done: `GameApi::Multiplayer`, `UpdateOnline`. Online, Unlimited, Fast build (both), Minigun, Commander, Auto-repair and Auto-aim are forced off and show "Offline only"; the menu pill shows "Online: view only". Trajectory, impact markers, enemy overlay, observer, intel, record and diagnostics stay on. Material times are restored on the first match frame online.
- Assumption: UpdateWeaponAim counts as a simulation write (not measured). Unknown: whether the flag clears when returning to a solo game; check `network game: 0` in FortsModule.log.
- Not built: lockstep-safe resource/build/repair cheats. Candidate: a lobby mod that all clients load, triggered by SendScriptEvent (the API exists, mods have Update/Load); not designed or tested.
- Built: `cmake --build build --config Release --target FortsModule`, exit 0. Isolated checks not run. Not verified in game.

## Update 11 (Aero Tools lobby mod)
- Request: make resources/build/repair work in a network game through a mod.
- Renamed to `mods/aerohub/` in Update 14. Files: `mods/aero-tools/` (mod.lua, displayname.lua, script.lua), `mods/aero-tools.zip` for the second player; installed copy in `D:\SteamLibrary\steamapps\common\Forts\data\mods\aero-tools`.
- Design: OnKey (local) calls SendScriptEvent("AeroCommand", "team, feature", "", true); AeroCommand runs on every client and keeps state in `data.aero`; Update every 25 frames applies it. Keys: Alt+1 resources, Alt+2 tech/guns build, Alt+3 reload, Alt+4 commander, Alt+5 repair, Alt+0 all off. Each player has own switches.
- Measured: script.lua at the mod root and OnKey/SendScriptEvent/Update/Load patterns from workshop mods; EnumerateLinks/RepairLink/RepairDevice from data/ai/ai.lua; override defaults a=0, b=-1 from FortsModule.log. SendScriptEvent has a hash check on one event name starting with 'F' online (unknown purpose), so the event name must not start with F.
- Assumptions: digit key names reach OnKey as "1".."5"; Lua arity of SetOverrideBuildTime/SetOverrideReloadTime matches the native (int, float, float); a mod not listed in db/mods.lua appears in the lobby (minidrills and others are unlisted); Update frame numbers are every sim frame.
- Not available: fast beams per team (material BuildTime has no Lua setter; a material mod would speed both sides). Repair uses the game's normal repair, so it costs resources and takes normal time.
- Checks: two-client Lua stub test passed (scratch, not in the project); LuaPlusC -p exit 0. Not run in game.

## Update 12 (mod needs the module)
- Request: the mod must not work without AeroHub. OnKey and the Alt keys were removed; `AeroCommand(team, feature, on)` has no caller inside the mod. Update 11's key table no longer applies.
- Measured: SendScriptEvent RVA 0xC6F60 takes (const char* function, const char* params, const char* file, bool useContext). Its worker at 0xFDB60 limits function to 64 and params to 128 bytes and queues packet 0xEA; the receiver (around 0xFD960) loops all script contexts, filters by file / context if those are non-empty, and only runs `function(params)` where that global exists, so an absent mod is a no-op.
- Module: `SyncMod` sends the General-tab switches online (Unlimited, Fast build tech, Minigun, Commander, Auto-repair) with team code, feature and state; active features are resent every 300 frames for restarted matches. Fast beams and auto-aim stay "Offline only". Repair speed/what show fixed values online.
- Unknown: the module cannot tell whether the mod is loaded, so a switch can look on while nothing happens. The worker drops the event when [teams+0x20]+0xD07 is non-zero (meaning unknown). Calling SendScriptEvent outside a script context is measured from code only.
- Checks: two-client Lua stub test passed; LuaPlusC -p exit 0; `cmake --build build --config Release --target FortsModule` exit 0. Not run in game.

## Update 13 (Steam Workshop upload prepared)
- Request: publish the mod on Steam so the friend does not unpack a zip.
- Measured: the game uploads from `users/<steamid>/WorkshopUploads` through Workshop > Upload > Upload Mod (data/ui/screens/workshop.lua, upload.lua); it reads preview*.jpg. Steam returns an error if the Workshop legal agreement is not accepted.
- Done: `mods/aero-tools/preview.jpg` (640x360, generated) and a copy of the mod in `WorkshopUploads/aero-tools`. Publishing is left to the user: it happens in the game UI on the user's Steam account.
- After publishing: delete `data/mods/aero-tools`, otherwise the local copy and the Workshop copy both exist. Not verified: lobby auto-download with Friends-only visibility.

## Update 14 (renamed to AeroHub, HUD panel theme)
- Request: name the mod AeroHub and add a colour theme for the bottom panel (boards, background boards, resources).
- Mod folder and display name are now `aerohub`; old `aero-tools` copies removed from data/mods and WorkshopUploads (it had not been published). No zip any more: the friend gets it from the Workshop.
- Measured: HUDPanel holds PanelLeft, PanelCenter, ResourcePanel with PanelCenterL/R nearby (strings in compiled ui/screens/hud.lua; its Lua header is non-standard, lupa cannot load it). Textures in ui/textures/HUD/skin_base. SetControlColour (0xCC270) finds parent then name under the screen root; missing controls are ignored; it requires script context flag 2 (0x91C444).
- Assumptions: an empty parent finds a nested control (workshop mods call `SetControlColour("", "Reload", ...)`, code path read); hud-tab buttons and text are not tinted.
- Design: `AeroTheme(team)` is sent by `SyncTheme` every 300 frames in any match and applies only where team == GetLocalTeamId(), so the friend's panel stays normal and nothing happens without the module. Colour 150,190,255 chosen from a tinted texture preview.
- Checks: two-client Lua stub test passed; LuaPlusC -p exit 0; FortsModule build exit 0. Not run in game.

## Update 15: online switches were never sent (team code 101)

Reported: Workshop mod `AeroHub` (id 3834651515) enabled, online match, nothing works, same with the local copy.

Measured: `log.txt` shows `Activating mod AeroHub (3834651515)`, so the mod loads. The game log says the client gets team 101.
Live read from the running Forts (buildid 26181732, read-only): GetLocalTeamId = 101, flag game+0x1BE1 = 1, send gate
[[game+0x1CD0]+0x20]+0xD07 = 0 (open). `GameApi::LocalTeam` rejected team >= 64, so Tick returned before SyncMod and SyncTheme,
and no event was ever sent. Team handlers treat codes >= 100 explicitly (0xD7340 uses the map at +0x1450, commander uses code % 100).

The flag also read 0 for about 1.9 s while each network match loaded, with GetLocalTeamId still 1 (FortsModule.log 15:36:19,
15:36:56), and the direct-write path ran in that window.

Fixed in the module only, the published mod is unchanged: `cfg::kMaxTeamCode`, mod state reset on team change, direct writes need
`kOfflineSettleFrames` frames of flag 0 in the same team, sends are logged (`mod command sent`, `theme sent for team`).

Build: exit 0 after moving the DLL held by the running game to `build/out/modules/FortsModule.running-old.dll` (delete it once Forts is closed).
Not verified in game. Next step: restart Forts and the hub, enable a switch online, look for `mod command sent` in FortsModule.log and
`AeroHub: team 101 ...` in users/<id>/log.txt.

## Update 16: app icon and Start menu entry

`hub/AeroHub.ico` (16 to 256 px) is embedded through `hub/AeroHub.rc` as icon 1, and `WNDCLASSW::hIcon` uses it. Start menu shortcut:
`%APPDATA%\Microsoft\Windows\Start Menu\Programs\AeroHub.lnk`, target `build\out\AeroHub.exe`. Moving the exe breaks the shortcut.
The running hub was moved to `build/out/AeroHub.running-old.exe` for the build; delete it after closing that hub.

## Update 17: fast build and fast reload only lasted 0.1 s

Reported: with fast build on, a placed device builds slowly; every toggle of the switch finishes it at once.
Measured (disassembly, buildid 26181732): the second float of SetOverrideBuildTime/SetOverrideReloadTime (+0x6C, +0x74) is a countdown in seconds.
Frame tick 0x2F95EF subtracts dt and at <= 0 sets the time to 0 and the countdown to -1; a negative countdown is skipped.
Build rate is 1 / seconds (0x257F40), so 0.1 for 0.1 s finished one device per toggle. The reload time is read at 0x265A80.
Fix: call with (0.1, -1) in `cfg::kOverrideNoExpiry` and in mods/aerohub/script.lua. The module fix is offline; online needs the mod update.
Mod updated in `data/mods/aerohub` and `users/.../WorkshopUploads/aerohub` (publishedfileid.lua kept). NOT yet uploaded to the Workshop.
Both players must run the same mod version: an old and a new client would keep different overrides and desync.
Verified online by the user's log: `AeroHub: team 101 ...` reached the mod. Long-lasting override not verified in game.
