package dev.aero.client.module.impl;

import dev.aero.client.module.Category;
import dev.aero.client.module.Module;
import dev.aero.client.setting.BoolSetting;
import net.minecraft.world.entity.player.Player;

/** Combat modules skip "players" that are not in the server's tab list: most server-side fake players and NPCs. */
public final class AntiBot extends Module {
	private final BoolSetting tabList = bool("Must be in tab list", true);
	private final BoolSetting oddNames = bool("Reject odd names", true);

	public AntiBot() {
		super("AntiBot", Category.COMBAT, "Combat modules ignore fake players that are not in the tab list.");
	}

	public boolean isBot(Player p) {
		if (tabList.get() && (mc.getConnection() == null || mc.getConnection().getPlayerInfo(p.getUUID()) == null)) return true;
		if (oddNames.get()) {
			String n = p.getGameProfile().getName();
			if (n.length() < 3 || n.length() > 16 || !n.matches("[A-Za-z0-9_]+")) return true;
		}
		return false;
	}
}
