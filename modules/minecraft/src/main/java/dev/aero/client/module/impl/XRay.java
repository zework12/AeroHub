package dev.aero.client.module.impl;

import dev.aero.client.Hooks;
import dev.aero.client.module.Category;
import dev.aero.client.module.Module;
import dev.aero.client.module.ModuleManager;
import dev.aero.client.setting.BoolSetting;
import dev.aero.client.setting.ListSetting;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.world.level.block.Block;

import java.util.HashSet;
import java.util.Set;

/**
 * Real X-Ray: terrain that is not on the list disappears and the listed blocks (ores by default) stay, so you
 * look through the ground at them. Works with the vanilla renderer and with Sodium; shader packs (Iris) are untested.
 */
public final class XRay extends Module {
	private final ListSetting blocks = list("Blocks", "coal_ore", "deepslate_coal_ore", "iron_ore", "deepslate_iron_ore", "copper_ore", "deepslate_copper_ore",
			"gold_ore", "deepslate_gold_ore", "nether_gold_ore", "redstone_ore", "deepslate_redstone_ore", "lapis_ore", "deepslate_lapis_ore", "diamond_ore",
			"deepslate_diamond_ore", "emerald_ore", "deepslate_emerald_ore", "nether_quartz_ore", "ancient_debris", "spawner", "trial_spawner", "chest",
			"trapped_chest", "ender_chest", "diamond_block", "emerald_block", "gold_block", "iron_block", "netherite_block", "beacon", "end_portal_frame");
	private final BoolSetting fullbright = bool("Fullbright", true);
	private boolean smartCullBefore, addedFullbright;
	private int lastHash;

	public XRay() {
		super("XRay", Category.RENDER, "Look through the ground: only the listed blocks (ores, spawners, chests) stay visible. Edit the list with .set XRay Blocks add <id>.");
	}

	private void rebuild() {
		Set<Block> set = new HashSet<>();
		for (String id : blocks.get()) {
			ResourceLocation rl = ResourceLocation.tryParse(id.contains(":") ? id : "minecraft:" + id);
			if (rl != null) BuiltInRegistries.BLOCK.getOptional(rl).ifPresent(set::add);
		}
		Hooks.xrayBlocks = Set.copyOf(set);
		lastHash = blocks.display().hashCode();
	}

	@Override
	protected void onEnable() {
		rebuild();
		smartCullBefore = mc.smartCull;
		mc.smartCull = false; // the cave culling would hide the sections you are looking at through the rock
		Hooks.xray = true;
		mc.levelRenderer.allChanged();
		Module fb = ModuleManager.byName("Fullbright");
		if (fullbright.get() && fb != null && !fb.isEnabled()) {
			fb.setEnabled(true);
			addedFullbright = true;
		}
	}

	@Override
	protected void onDisable() {
		Hooks.xray = false;
		mc.smartCull = smartCullBefore;
		if (mc.levelRenderer != null) mc.levelRenderer.allChanged();
		Module fb = ModuleManager.byName("Fullbright");
		if (addedFullbright && fb != null) fb.setEnabled(false);
		addedFullbright = false;
	}

	@Override
	public void onTick() {
		// if the player switched Fullbright off by hand while X-Ray runs, it is theirs now and X-Ray must not touch it again
		if (addedFullbright) {
			Module fb = ModuleManager.byName("Fullbright");
			if (fb == null || !fb.isEnabled()) addedFullbright = false;
		}
		if (blocks.display().hashCode() != lastHash) {
			rebuild();
			mc.levelRenderer.allChanged();
		}
	}

	@Override
	public String info() {
		return String.valueOf(Hooks.xrayBlocks.size());
	}
}
