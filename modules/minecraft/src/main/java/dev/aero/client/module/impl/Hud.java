package dev.aero.client.module.impl;

import dev.aero.client.module.Category;
import dev.aero.client.module.Module;
import dev.aero.client.module.ModuleManager;
import dev.aero.client.setting.BoolSetting;
import dev.aero.client.setting.ModeSetting;
import net.minecraft.client.gui.Font;
import net.minecraft.client.gui.GuiGraphics;
import net.minecraft.client.multiplayer.PlayerInfo;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.phys.Vec3;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;

/** Watermark, module list, coordinates, speed, FPS, ping and armour durability. */
public final class Hud extends Module {
	private final BoolSetting watermark = bool("Watermark", true);
	private final BoolSetting arrayList = bool("Module list", true);
	private final BoolSetting coords = bool("Coordinates", true);
	private final BoolSetting speed = bool("Speed", true);
	private final BoolSetting fps = bool("FPS and ping", true);
	private final BoolSetting armor = bool("Armour durability", true);
	private final ModeSetting listSide = mode("List side", "Right", "Right", "Left");

	private double lastX, lastZ, blocksPerSecond;
	private long lastNanos;

	public Hud() {
		super("HUD", Category.RENDER, "On-screen information.");
		restoreEnabled(true);
		persist = true;
	}

	@Override
	public void onTick() {
		long now = System.nanoTime();
		if (lastNanos != 0 && mc.player != null) {
			double dt = (now - lastNanos) / 1.0e9;
			if (dt > 0) {
				double d = Math.hypot(mc.player.getX() - lastX, mc.player.getZ() - lastZ) / dt;
				blocksPerSecond = blocksPerSecond * 0.8 + d * 0.2;
			}
		}
		lastNanos = now;
		lastX = mc.player.getX();
		lastZ = mc.player.getZ();
	}

	@Override
	public void onRender2d(GuiGraphics g, float partialTick) {
		if (mc.options.hideGui || mc.screen != null && !(mc.screen instanceof net.minecraft.client.gui.screens.ChatScreen)) return;
		Font font = mc.font;
		int w = mc.getWindow().getGuiScaledWidth(), h = mc.getWindow().getGuiScaledHeight();
		if (watermark.get()) {
			g.drawString(font, "Aero", 4, 4, 0xFF4DA3FF, true);
			g.drawString(font, "Client", 4 + font.width("Aero"), 4, 0xFFFFFFFF, true);
		}
		if (arrayList.get()) drawList(g, font, w);
		int y = h - 11;
		if (coords.get()) {
			Vec3 p = mc.player.position();
			g.drawString(font, String.format("XYZ %.1f %.1f %.1f", p.x, p.y, p.z), 4, y, 0xFFFFFFFF, true);
			y -= 10;
		}
		if (speed.get()) {
			g.drawString(font, String.format("%.1f b/s", blocksPerSecond), 4, y, 0xFFB5C4D3, true);
			y -= 10;
		}
		if (fps.get()) {
			PlayerInfo info = mc.getConnection() == null ? null : mc.getConnection().getPlayerInfo(mc.player.getUUID());
			String text = mc.getFps() + " FPS" + (info != null ? "  " + info.getLatency() + " ms" : "");
			g.drawString(font, text, 4, y, 0xFFB5C4D3, true);
		}
		if (armor.get()) drawArmor(g, font, w, h);
	}

	private void drawList(GuiGraphics g, Font font, int w) {
		List<String> lines = new ArrayList<>();
		for (Module m : ModuleManager.all()) {
			if (!m.isEnabled() || m.hidden) continue;
			String info = m.info();
			lines.add(info == null ? m.name : m.name + " §7" + info);
		}
		lines.sort(Comparator.comparingInt((String s) -> font.width(s)).reversed());
		int y = 4;
		boolean right = listSide.is("Right");
		for (String s : lines) {
			int tw = font.width(s);
			int x = right ? w - tw - 5 : 4;
			if (!watermark.get() || right) g.fill(x - 2, y - 1, x + tw + 2, y + 9, 0x70000000);
			g.drawString(font, s, x, y, 0xFF7CF29A, true);
			y += 10;
		}
	}

	private void drawArmor(GuiGraphics g, Font font, int w, int h) {
		Player p = mc.player;
		int x = w / 2 + 10, y = h - 56;
		for (ItemStack s : p.getArmorSlots()) {
			if (s.isEmpty() || !s.isDamageableItem()) continue;
			int left = s.getMaxDamage() - s.getDamageValue();
			int pct = left * 100 / s.getMaxDamage();
			g.renderItem(s, x, y);
			g.drawString(font, pct + "%", x + 18, y + 4, pct > 50 ? 0xFF7CF29A : pct > 20 ? 0xFFFFD166 : 0xFFFF6B6B, true);
			y -= 16;
		}
	}
}
