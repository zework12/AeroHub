package dev.aero.client.util;

import dev.aero.client.module.Module;
import dev.aero.client.module.impl.AntiBot;
import dev.aero.client.module.ModuleManager;
import dev.aero.client.setting.BoolSetting;
import dev.aero.client.setting.NumberSetting;
import net.minecraft.client.Minecraft;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.decoration.ArmorStand;
import net.minecraft.world.entity.monster.Enemy;
import net.minecraft.world.entity.player.Player;

import java.util.ArrayList;
import java.util.List;

/** Target selection shared by the combat modules; adds its own settings to the module that owns it. */
public final class Targets {
	public final BoolSetting players, hostile, passive, invisible, throughWalls, teamCheck;
	public final NumberSetting range;

	public Targets(Module owner, double defaultRange, boolean defPlayers, boolean defHostile, boolean defPassive) {
		range = owner.num("Range", defaultRange, 1, 12, 0.1);
		players = owner.bool("Players", defPlayers);
		hostile = owner.bool("Hostile mobs", defHostile);
		passive = owner.bool("Passive mobs", defPassive);
		invisible = owner.bool("Invisible", false);
		throughWalls = owner.bool("Through walls", false);
		teamCheck = owner.bool("Ignore teammates", true);
	}

	public boolean valid(Entity e) {
		Minecraft mc = Minecraft.getInstance();
		if (!(e instanceof LivingEntity le) || e == mc.player || e instanceof ArmorStand) return false;
		if (!le.isAlive() || le.getHealth() <= 0) return false;
		if (le.isInvisible() && !invisible.get()) return false;
		if (e instanceof Player p) {
			if (!players.get() || p.isSpectator() || Friends.is(p.getGameProfile().getName())) return false;
			if (teamCheck.get() && mc.player.isAlliedTo(p)) return false;
			AntiBot bots = ModuleManager.get(AntiBot.class);
			if (bots != null && bots.isEnabled() && bots.isBot(p)) return false;
		} else if (e instanceof Enemy) {
			if (!hostile.get()) return false;
		} else if (!passive.get()) {
			return false;
		}
		return throughWalls.get() || mc.player.hasLineOfSight(e);
	}

	/** Valid targets within `maxRange` of the eyes, nearest first. */
	public List<LivingEntity> within(double maxRange) {
		Minecraft mc = Minecraft.getInstance();
		List<LivingEntity> out = new ArrayList<>();
		for (Entity e : mc.level.entitiesForRendering()) {
			if (valid(e) && mc.player.distanceTo(e) <= maxRange) out.add((LivingEntity) e);
		}
		out.sort((a, b) -> Float.compare(mc.player.distanceTo(a), mc.player.distanceTo(b)));
		return out;
	}

	public List<LivingEntity> within() {
		return within(range.d());
	}

	public LivingEntity nearest() {
		List<LivingEntity> l = within();
		return l.isEmpty() ? null : l.get(0);
	}
}
