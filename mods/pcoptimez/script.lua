-- PC optimez: per-team helpers that stay in sync in network games, and the blue HUD panel theme.
-- This script has no input of its own: the companion module (FortsModule.dll) sends AeroCommand through
-- SendScriptEvent, and the game runs it on every client on the same frame. Without the module nothing calls it.
-- AeroTheme only touches the local interface of the player whose module sent it.
-- Simulation state lives in `data` only.
dofile("scripts/forts.lua")

local Resources, FastBuild, FastReload, Commander, Repair = 1, 2, 3, 4, 5
local FeatureNames = { "resources", "fast build", "fast reload", "commander", "repair" }

local TicksPerPass = 25
local ResourcesPerPass = 5000
-- SetOverrideBuildTime/SetOverrideReloadTime(team, seconds, duration): a negative duration never runs out.
local FastSeconds = 0.1
local NoExpiry = -1
-- Seconds the game holds when no override is set, read from a live team (FortsModule.log, buildid 26181732).
local DefaultSeconds = 0
local CommanderFull, CommanderTopUpBelow = 1, 0.95
local CommanderCooldownPasses = 3

-- Multiplies the panel textures, so light parts take the colour and dark parts stay dark.
local ThemeR, ThemeG, ThemeB = 150, 190, 255
-- Parts of HUDPanel in ui/screens/hud.lua: boards, background boards, build details and resources.
local ThemePanels = { "PanelLeft", "PanelCenter", "PanelCenterL", "PanelCenterR", "ResourcePanel" }

local function TeamState(team)
	data.aero = data.aero or {}
	if not data.aero[team] then
		data.aero[team] = { on = {}, commanderWait = 0 }
	end
	return data.aero[team]
end

local function SetOverride(setter, team, on)
	if on then
		setter(team, FastSeconds, NoExpiry)
	else
		setter(team, DefaultSeconds, NoExpiry)
	end
end

function AeroCommand(team, feature, on)
	if type(team) ~= "number" or not FeatureNames[feature] or type(on) ~= "boolean" then return end
	local state = TeamState(team)
	if (state.on[feature] or false) == on then return end
	state.on[feature] = on
	if feature == FastBuild then
		SetOverride(SetOverrideBuildTime, team, on)
	elseif feature == FastReload then
		SetOverride(SetOverrideReloadTime, team, on)
	end
	-- Deliberately silent: this mod prints nothing, neither to the chat nor to the log.
end

function AeroTheme(team)
	if team ~= GetLocalTeamId() then return end
	for _, panel in ipairs(ThemePanels) do
		SetControlColour("", panel, Colour(ThemeR, ThemeG, ThemeB, 255))
	end
end

function AeroRepairLink(nodeA, nodeB, saveName, relativeHealth, stress, segmentsOnFire, deviceId)
	if nodeA and nodeB and (relativeHealth < 1 or segmentsOnFire > 0) then
		RepairLink(nodeA, nodeB)
	end
	return true
end

local function RepairTeam(team)
	EnumerateLinks(team, "AeroRepairLink", 1, 1, "", false)
	for index = 0, GetDeviceCount(team) - 1 do
		local id = GetDeviceId(team, index)
		if GetDeviceHealth(id) < 1 then RepairDevice(id) end -- the game ignores devices it cannot repair
	end
end

local function RefillCommander(team, state)
	if state.commanderWait > 0 then
		state.commanderWait = state.commanderWait - 1
		return
	end
	local active = IsCommanderActive(team)
	if active and active ~= 0 then return end
	local points = GetTeamCommanderPoints(team)
	if points >= 0 and points < CommanderTopUpBelow then
		AddTeamCommanderPoints(team, CommanderFull - points)
		state.commanderWait = CommanderCooldownPasses
	end
end

function Update(frame)
	if frame % TicksPerPass ~= 0 or not data.aero then return end
	for team, state in pairs(data.aero) do
		if state.on[Resources] then
			AddResources(team, Value(ResourcesPerPass, ResourcesPerPass), false, Vec3(0, 0, 0))
		end
		if state.on[Commander] then RefillCommander(team, state) end
		if state.on[Repair] then RepairTeam(team) end
	end
end
