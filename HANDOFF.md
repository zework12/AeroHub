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

## Update 18: AeroClient, Minecraft Java 1.21.1 client mod (modules/minecraft)

Request: cheats for Minecraft with 100+ functions, in the spirit of the old Impact client, above all auto-mining: take an ore filter, find that ore nearby, plan the shortest path to it and dig.
Chosen architecture (stated to the user): a Fabric client mod, not an injected DLL, because Minecraft Java has no native module to hook. It is loaded from the profile's `mods` folder. The hub does not launch or manage it yet.

Measured / taken from artifacts:
- Fabric Loader 0.20.1 and Fabric API 0.116.17+1.21.1 (matches the user's Modrinth profile "Fabric 1.21.1", which runs loader 0.19.5). Loom 1.15.5 because 1.19.x needs Java 25 to run the build; the mod itself targets Java 21 with Mojang mappings.
- Toolchain on the D drive (C had 5 GB free): JDK 21 Temurin in `D:\dev\jdk21` (SHA-256 checked against the Adoptium API), Gradle home `D:\dev\gradle-home`, decompiled game sources in `D:\dev\mcsrc` (for grepping names).
- Every game API name used was either compiled against the real 1.21.1 mappings or grepped in the decompiled sources.

Layout: `dev.aero.client` with `path/` (pure A*: `PathSearch`, `Goal`, `VoxelRay`, no game classes), `pathing/` (game adapters: `LevelTerrain`, `PathExecutor`, `Miner`, `Controls`), `module/` (framework and `impl/*Modules` per category, `AutoMine`, `Navigator`, `Hud`), `mixin/`, `gui/ClickGuiScreen`, `command/Commands`, `config/Config`, `util/`.

Tests, all run on this machine:
- Pure checks (scratch, not kept in the project): path search on toy worlds (open ground, buried ore, lava, bedrock, two ores, sand, hill, cap), angle and ballistics maths, settings. They found and I fixed: a path search that dug one block too many (dug cells were not counted when checking reach), `ListSetting.reset` that restored nothing (the default shared the live set), and arrow aim that measured only tick end points.
- `gradlew runSelftest`: starts the real client, applies all mixins, checks the registry and config.
- `gradlew runWorldtest`: creates a flat survival world and runs AutoMine in six scenarios (open ground, inside stone, beside lava, under the ground, up a stone pillar, two ores), the navigator, behaviour checks of eleven modules and a sweep that switches every module on for 12 ticks. Last run: 129 passed, 0 failed.

Not verified: other servers (their anti-cheat decides what is accepted), shader packs, most modules beyond "does not crash", other Minecraft versions. No anti-cheat evasion was written and none is planned.
Unfinished by choice: hub integration (launch the profile from AeroHub), Xray that edits chunk rendering (Search boxes the ores instead, which works with Sodium), Tower and crystal modules.

Result files: `modules/minecraft/build/libs/aeroclient-0.1.0.jar` (SHA-256 11e95906109aba16dd3af37cc01505be38119b5adf7a780ecf6abfebf8ddcf39, Java 21 class files). Mixin targets are remapped inside the classes at remapJar time (no refmap file is produced by this Loom); checked with javap: accessors, shadows and injects all carry intermediary names. The jar was NOT started in a production-style profile (the user's Modrinth profile was not touched).
Build and test commands (Windows, from modules/minecraft, with JAVA_HOME at a JDK 21 and GRADLE_USER_HOME on a drive with space): `gradlew build`, `gradlew runSelftest`, `gradlew runWorldtest`.
Screenshots in the READMEs (`docs/mc-world.png`, `docs/mc-menu.png`) were taken by runWorldtest itself.
Next verifiable step: copy the jar into the Fabric 1.21.1 profile's mods folder, start it, press Right Shift, then `.mine diamond_ore` in a world with ore, and check `logs/latest.log` for `AeroClient loaded` and any `FAULT` line.

## Update 19: Minecraft is launched and managed through AeroHub

Request: run the Minecraft cheat through the AeroHub app instead of as a plain mod.
Stated limit: loading the code into an already running game is not possible with this design, because the mixins only apply while the game's classes load; a rewrite as a Java agent was not attempted. So the hub does what the DLL path does for Forts, in the way Java allows: it finds the Fabric instances, installs the jar, opens the launcher and shows the mod's live status. The Play button of the Modrinth App stays with the launcher (no account handling in the hub).

Done:
- `profiles/minecraft.ini` is a new profile kind `Kind=FabricMod` (core/Profile.*: `GameKind`, `modFile`, `gameVersion`, `instanceName`, `instanceDir`). The hub expands it into one sidebar entry per instance.
- `core/Minecraft.*`: Modrinth profile discovery (`mods` folder holding `fabric-api-*+<GameVersion>*.jar`; `ModrinthRoot`, `ModrinthExe`, `MinecraftInstances` in AeroHub.ini override), install (temp file then replace, other `aeroclient*.jar` moved to `mods\aeroclient-old`), remove, hash comparison (current / outdated), and the reader of `aeroclient-live.txt`.
- `core/Process.*`: `FindProcessIds`, `ProcessCommandLine` (NtQueryInformationProcess class 60).
- hub: states Not installed / Update ready / Installed / Starting / Restart needed / Active, buttons Install|Update|Open launcher, Remove mod, live card from the status file, the module log tab shows the instance's logs\latest.log.
- mod: `LiveStatus` writes `<gameDir>\aeroclient-live.txt` once a second (pid, version, world, dimension, position, fps, modules, enabled, automine) and deletes it on exit.
- CMake copies `modules/minecraft/build/libs/aeroclient-*.jar` to `build/out/modules/aeroclient.jar` (re-run CMake after the first jar build).

Checked here: sandbox copy of the hub with a fake Modrinth root, buttons driven with posted mouse messages: the 1.21.1 profile found and the 1.20.1 one ignored, Install (stray older jar moved aside, file identical to the hub's copy), Open launcher (notepad stood in), outdated detection after editing the installed jar, Update, Remove. Live link: the hub watched a real dev client run (`runWorldtest`) and showed Active with the live rows and the game log; the status file disappears when the game closes. A read-only start against the user's real Modrinth folder listed "Minecraft · Fabric 1.21.1 / Not installed" and changed nothing.
Not checked: Install into the user's real profile and a launch through the real Modrinth App; the running-game detection by command line against a Modrinth-launched game (the live file is the primary signal, the command line is only used before the mod has reported in).

## Update 20: one Minecraft tab, two buttons (replaces the install flow of Update 19)

Request: a single tab "Minecraft" with two buttons. Auto-detect (works for TLauncher and Modrinth; if nothing is found, ask the user to start Minecraft and find it through the game process) and Install mod (the jar goes to Downloads and the user drops it into the game).
Done: the per-instance sidebar entries, installing into a profile, Remove mod and Open launcher are gone. `core/Minecraft.*` now holds: `ParseLaunchArgs` / `LooksLikeMinecraft` (pure text), `RunningGames` (java.exe / javaw.exe command lines), `InstalledGames` (extra folders from AeroHub.ini, Modrinth profiles with fabric-api for GameVersion, TLauncher gamedir from `%APPDATA%\.tlauncher\tlauncher-2.0.properties`, `.minecraft`), `HasMod`, `DownloadsFolder`, `SaveToDownloads`, `ReadLive`. The hub keeps the result in `g_mc`, remembers it in `AeroHub.ini [Minecraft]`, runs detection once at startup, and while waiting looks at the processes every second. A running game wins over anything found on disk because it is the folder the game really uses.
Checked: `ParseLaunchArgs` and friends on Modrinth style and TLauncher style command lines (12 checks, scratch program); sandbox hub copy driven by posted mouse messages: TLauncher folder from a fake properties file, Modrinth profile for 1.21.1 (1.20.1 ignored), nothing found gives "Waiting" with the request to start Minecraft, a java process with a Modrinth style command line was picked up by the waiting hub within 1 s (launcher "Modrinth App", folder with spaces correct), Install mod put a byte-identical jar into Downloads (test copy deleted afterwards), the live card and game log on the new tab against a real dev client run.
Known limits: the Loom dev client keeps its arguments in a config file, so its command line has no --gameDir and it cannot be found by process (use MinecraftInstances for it); a launcher that does not pass --gameDir cannot be found by process either; TLauncher and the official launcher were not available here, so only their folder logic (properties file, .minecraft) was exercised on fake folders; Modrinth App with the real profile was only listed (read-only), never launched.

## Update 21: X-Ray, visible AutoMine, ore filter editor

Request: add X-Ray; the user could not see how the auto-miner works and compared it with Impact (not available to me: closed source; the Baritone-style feedback was reproduced from how Baritone behaves: path line, targets, chat); then: "where is the ore filter?" (there was no filter in the GUI, only commands).
Done:
- `XRay` (module, `XRay.java`): hides every block that is not on its list by returning RenderShape.INVISIBLE from `BlockStateBase.getRenderShape` (`BlockStateBaseMixin`), forces all faces of listed blocks (`BlockMixin` for the vanilla mesher, `SodiumOcclusionMixin` for Sodium's `BlockOcclusionCache.shouldDrawSide`, written against Sodium 0.8.13 and @Pseudo/require=0 so a missing Sodium is harmless), turns `Minecraft.smartCull` off while on, re-meshes with `levelRenderer.allChanged()`, optional Fullbright. Block entities with their own renderers (chests, signs, banners) stay visible.
- AutoMine: path line, to-dig boxes and target box in the world, a status panel, chat messages (found / path / mining / mined / nothing / no path), range 64 (max 128), far targets are walked in parts (`walkPartial`, 60 replans, 200000 expansions). `PathSearch`: heuristic now counts height too, and the search sees cells dug on its last 8 steps (a staircase could step onto a cell it had dug); `PathSearch.cost()`; `PathExecutor` exposes steps/index/cost/digsLeft.
- `ListEditorScreen` (registry list with icons, search, check marks, All ores / Clear / Done) opens from the `edit` button of any block/item list row in the click GUI; `ListSetting.Kind` (BLOCK / ITEM / TEXT; text lists stay on `.set`).
- Build: Loom 1.16.1 (Sodium 0.8.13 is built with 1.16.1 and Loom 1.15.5 refused to remap it for the dev client). `-PsodiumJar=<path>` adds Sodium to the dev client for `runWorldtest`.
Checked: path program 33 checks incl. 30 blocks up/down inside stone; `runSelftest` passed (116 registered, 114 visible); `runWorldtest` 140 passed / 0 failed on the vanilla renderer and again with the user's Sodium 0.8.13 loaded (X-Ray screenshots on both show the stone and ground gone and only ores, spawner and chest left; the "off" shot on Sodium shows the solid wall); new scenarios: 22 blocks up through stone, an ore 90 blocks away, the filter editor toggling an entry by click.
Not checked: Iris shader packs with X-Ray, Sodium versions other than 0.8.13, the installed jar in the user's real profile, a real key press for the menu keys.

## Update 22: X-Ray night vision, tool choice, whole veins, walking without loops

Requests: (a) X-Ray turns on Fullbright, but only turns it off again if X-Ray was the one that turned it on; (b) AutoMine picks the pickaxe itself, stops when none is good enough (wooden pickaxe vs diamond ore) or none exists, and may take it from the main inventory; (c) AutoMine must mine the whole vein, and it walked in circles on a straight road.
Done:
- `XRay`: `addedFullbright`; if the user switches Fullbright off by hand meanwhile, X-Ray forgets it owns it.
- `Miner.evaluate(state, pos, slots)` puts each candidate stack in the selected slot for a moment and asks the game for `getDestroyProgress`; ranks "block drops its item" first, then speed, ties to the selected slot. `Miner.canHarvest`, `Miner.mine(pos, fromInventory)` swaps a main-inventory tool into the hand (`InventoryUtil.swapWithHotbar`, only with no screen open). `LevelTerrain` and `PathExecutor` take `toolsFromInventory` (setting "Tools from inventory", default on). `AutoMine.scanTick` drops ores no tool can harvest and stops with a reason when none is left; `mineTick` re-checks before each block.
- Vein: after each ore `AutoMine` runs a breadth-first search over the scanned ores (26 neighbours) from the mined ones and hands only those to the path goal until the vein is finished; drops are collected afterwards.
- Walking: the cause was sprint overshoot. The player passed a cell, the step was only counted when he stood exactly on it, so he turned back. Now the furthest of the next 3 path cells he stands on counts (no skipping across steps that still have blocks to dig), and sprint is allowed only on a straight run of flat, dig-free steps.
Checked here: `gradlew build` exit 0; `runSelftest` PASSED; `runWorldtest` 162 passed / 0 failed. New scenarios: night vision (8 phases), wooden pickaxe stops, no pickaxe stops, pickaxe only in the main inventory, 50-block flat road, 50-block bumpy road, 5-ore diagonal vein with a nearer decoy ore. The bumpy road reproduces the loop with the old code (75 blocks walked for a 50-block road) and passes with the fix (269 ticks); the flat road did not reproduce it with the old code.
Not verified: the circling in the user's real world (cause matched on a synthetic road only); tool durability is ignored (a nearly broken pickaxe can be chosen); a tool breaking mid-vein is handled by the pre-block check but has no test; X-Ray with Iris and other Sodium versions; servers that strip the night vision effect; Nuker and AutoLumber still use the hotbar only; the new jar was not run in the user's real profile.
Next verifiable step: put `build/out/modules/aeroclient.jar` into the profile, mine a diamond vein with an iron pickaxe, then a road over hills; if it loops again, take `logs/latest.log` and the terrain description.

## Update 23: terrain/water-aware trajectory, incoming shells, aim calculator, weak links, enemy timers, battle stats

Request: everything from "information and analysis" list 1 (works online without the mod because it only reads); and the trajectory must run to the end of the ground, with water treated as a separate surface (the user had a water problem in an earlier trajectory mod).

Measured (static, Forts.exe on disk, buildid 26181732; `tools/forts_api_table.py` dumps every Lua-API name -> handler from the registration code, the pairs `lea r8,[handler]` / `lea rdx,[name]`; 1333 names, known handlers matched FortsConfig.hpp):
- GetWaterLevel 0xE05B0 `float(float x)`: piecewise-constant table (world+0x12E8..0x12F0, 12-byte entries); ~2.1e9 on maps without water. GetWaterDepthAt 0xE0540 = pos.y - level, so deeper is a LARGER y (gravity +y): in the water when y >= GetWaterLevel(x). All real calibrations in FortsModule.log say gravitySign=1.
- CastGroundRay 0xD9570 `int(const Vec3* a, const Vec3* b, int flags)` (1 = terrain hit; flags 0 as the game's own mods pass it), GetRayHitPosition 0xD9E60 `Vec3*(Vec3* out)`.
- IsWeaponReadyToFire 0xD7610 `bool(int id)`, GetWeaponReloadTime 0xE41B0 `float(int id)` (= period * fraction at weapon+0x4B8), GetWeaponReloadPeriodById 0xE3F30; none checks the team. Node field +0x18 = terrain block index (GetNodeGroundBlockIndex 0xE2690).
- Config entries are in FortsConfig.hpp; their RVAs and prologues were cross-checked against the dump.

Done: `Tactics.hpp` (pure: TraceShot with ground + water, SolveAim, FindWeakLinks (bridges of the support graph), LostLinks, DragFromSpeeds), `Ballistics.hpp::ArcAt`, GameApi (GroundRay, WaterLevelAt, WeaponReady, WeaponReload, BaseGraphOf), Features.cpp: trajectory stops at terrain or water and marks the landing point with the flight time (yellow = ground, blue = water); menu items "Incoming shells warning", "Aim calculator (needs a target)", "Enemy gun timers", "Weak links: enemy base / your base", "Battle stats panel" (all read-only, allowed online; 23 of the hub channel's 24 feature slots are used). Battle stats append one line per match to `FortsStats.csv` next to the module.
Checked here: Tactics checks (4254, incl. brute-force comparison of weak links on random graphs, round-trip of the aim solver, water/terrain cases); menu and panels rendered offline from the real Menu.cpp / Hud.cpp (no truncation). Built: `cmake --build build --config Release --target FortsModule`, exit 0.

NOT verified in game (the log lines to look at are in brackets):
- That calling the new handlers from the SwapBuffers hook is safe and returns what the disassembly says; the price of a ray cast ("terrain ray: N casts, X us per cast"). If it is high, trace steps in `TraceCached` need to be coarser.
- Terrain contacts within 30 units of the barrel are ignored (kTraceGrace); what CastGroundRay does for a segment that starts inside terrain is unknown.
- ProjectileIds(enemy team code) returns the enemy's shells (shells are probably listed per side; ids are de-duplicated). Threat radius 90 units is not calibrated against real splash.
- Weak links assume node+0x18 >= 0 means anchored to terrain; a device hangs on the nearest link within 60 units; weights (reactor 1000, gun 120, other 10) are priorities, not measurements. If no anchored node is found the panel says so.
- Enemy timers: the sign of GetWeaponReloadTime is learned from the first reloads seen to the end ("timers: the reload value counts ..."); until then the bar is amber and only READY is exact.
- Battle stats are estimates (shots from our projectiles appearing, hit = last point within 90 units of an enemy device, damage = loss of device HP between refreshes).

Next verifiable step: start Forts on OpenGL, inject, in a match turn the features on; on a water map (dlc2_MP 2) check that shells end on the surface; read FortsModule.log for the lines above; send the log with Collect logs.


## Update 24: exact trajectory physics (frame model, measured drag, stops)

Request: "Траектория должна быть максимально точная".

Measured (501 recorded shots, Physics.FramesRate=25, 4998 positions):
- The game steps projectiles in fixed frames h = 0.04 s: `vx -= k*vx*h; vy += (g - k*vy)*h; x += vx*h; y += vy*h` (semi-implicit, drag from the old velocity, linear drag). y points down.
- k = 4.0 * ProjectileDrag (kDragFactor evidence comes from ONE type: machinegun 0.021 -> 0.0840; cannon 0). The module also measures k live from consecutive velocity samples (EstimateDragFrames) and prefers that (n >= 2) over the formula.
- Shells with ProjectileDrag >= 2 (missile 64) are thrust driven: not predicted, no trajectory drawn.
- Old model vs recorded positions (mean error, world units): cannon <=1 s 10.5 -> 0.05, 1-2 s 23.0 -> 0.06; machinegun <=1 s 27.4 -> 0.06, 1-2 s 130 -> 0.07 (max 235 -> 0.19). The old numbers use the learned drag the old FortsDrag.ini held, so they show what the user saw.

Code: `modules/forts/Tactics.hpp` (`TraceShot` frame by frame with terrain ray per chunk, water crossing interpolated, grace distance; `EstimateDragFrames`; `SolveAim` on the same physics), `Features.cpp` (`DragFor`: measured -> formula -> 0; learned values in `FortsDrag.v2.ini`, the old biased file is ignored), `Ballistics.hpp` (`ArcAt`, legacy).

Isolated checks (scratchpad tactics_test.cpp): 593 passed, 0 failed, including real-data validation (worst position error 0.186, rms 0.066) and the drag estimator exact on 4975/4975 real pairs.

Not verified in game / not modeled:
- Launch offset: the first shell state seen vs barrel + FireVelocity. `launch:` lines (<=40 per session) are written to FortsModule.log for calibration.
- Stops on structures (CastRay semantics unverified), shields/fields speed drops, thrust shells, fire spread, moving barrels.
- Cost of terrain rays and thread safety of the new handlers in the SwapBuffers hook ("terrain ray: ... us per cast" in the log).

Release: nothing committed/pushed; no v1.3.0 approval. dist/ installer is still v1.2.0.

Update 25: menu language (EN/RU). First row of the General tab ("Language / Язык") switches; the choice is saved in FortsLang.ini next to FortsModule.dll. Rows are grouped under captions. Hub window feature names and in-game HUD panels (stats, aim, weak links) stay English. Not verified in game; rendered in isolation with the real Menu.cpp (the Russian strings fit after shortening the repair-speed values and the online note).

Update 26: menu redesign, key binds, calculator on the selected gun, passive self-test
- Menu (Menu.cpp/.hpp rewritten): 820-unit wide window, a card per function with a one-line description, a key box and a Toggle/Hold box per function, header drag to move, Settings tab (language, menu size 85-200%). Binds are stored in FortsBinds.ini (item<N>=vk,hold; defaults F6 unlimited, End all off, F9 pick target), language/size in FortsLang.ini.
- Aim calculator: uses the gun returned by GetLocalSelectedDeviceId (0xCF710, int(), prologue-checked; "selected" semantics unverified in game); target is chosen with the new "Pick target under cursor" bind via GetMousePos (0xE6700) + ScreenToWorld (0xE68B0) (same screen space is unverified; the self-test logs a round trip).
- Self-test (passive, never fires): a file FortsSelfTest.txt next to the DLL enables it. It logs, about 4 s into a match, camera water level, terrain ray cost, selected device, node+0x18 values of every base, weapon timers, mouse->world; and for every shell it predicts the path after the drag is measured and logs how far the last seen point was from the path and whether the shell ended where predicted ("selftest:" lines in FortsModule.log). Old 40 MB log kept as FortsModule.before-selftest.log.
- Weak links "no ground-anchored nodes found": likely floating (ship) bases; the self-test node+0x18 distribution decides. Not changed yet.
- Built: cmake --build build --config Release --target FortsModule, exit 0. Not run in game.

Update 27 (test run, interrupted): first in-game run with FortsSelfTest.txt. Measured: ScreenToWorld round trip exact; terrain ray 1.78 us/cast; GetLocalSelectedDeviceId faulted with no gun selected (re-check with a gun selected); environment block fired before the lists were filled (fixed in Features.cpp: runs when own/enemy lists are non-empty, again 60 s later; compiles, DLL not yet rebuilt because Forts held it). Next: close Forts, rebuild FortsModule, play a skirmish (water map dlc2_MP 2), select a gun, run `python -I selftest_report.py <FortsModule.log>` (scratchpad) for the shell/landing report; decide weak-links anchor from the node+0x18 distribution.

## Update 28 (2026-11-09): movable HUD panels
- Request: "Все оверлеи тоже должны перетаскиваться". Panels aimcalc, weak, stats, intel, radar (Features.cpp, section "movable panels": PlacePanel, PanelPointer, FortsPanels.ini).
- Works only while the menu is open: left-drag moves a panel, right-click resets it. Position is clamped to the viewport; the game does not receive the mouse over a panel (same path as the menu).
- Built (cmake --build build --config Release --target FortsModule, exit 0, DLL 18:12). NOT run in game. World-attached overlays (enemy labels, timers, trajectories) are not panels and are not movable.
- Offline checks this session: tactics_test 589 passed / 0 failed; preview_ru renders the menu (820x722 and 820x548), images not visually reviewed.
