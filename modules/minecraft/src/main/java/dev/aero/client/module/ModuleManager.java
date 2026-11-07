package dev.aero.client.module;

import com.mojang.blaze3d.platform.InputConstants;
import dev.aero.client.AeroClient;
import dev.aero.client.util.Chat;
import net.fabricmc.fabric.api.client.rendering.v1.WorldRenderContext;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiGraphics;
import net.minecraft.network.protocol.Packet;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public final class ModuleManager {
	private static final List<Module> MODULES = new ArrayList<>();
	private static final Map<Module, Boolean> KEY_DOWN = new HashMap<>();
	private static final Map<Class<?>, Module> BY_CLASS = new HashMap<>();

	private ModuleManager() {
	}

	public static void register(Module m) {
		MODULES.add(m);
		BY_CLASS.put(m.getClass(), m);
	}

	public static void registerAll(Module... modules) {
		for (Module m : modules) register(m);
	}

	public static List<Module> all() {
		return MODULES;
	}

	public static List<Module> in(Category c) {
		List<Module> out = new ArrayList<>();
		for (Module m : MODULES) if (m.category == c) out.add(m);
		return out;
	}

	@SuppressWarnings("unchecked")
	public static <T extends Module> T get(Class<T> c) {
		return (T) BY_CLASS.get(c);
	}

	public static boolean on(Class<? extends Module> c) {
		Module m = BY_CLASS.get(c);
		return m != null && m.isEnabled();
	}

	public static Module byName(String name) {
		String n = name.replace(" ", "").replace("_", "");
		for (Module m : MODULES) if (m.name.replace(" ", "").equalsIgnoreCase(n)) return m;
		return null;
	}

	/** A module threw: switch it off so one bug does not break every tick. */
	static void fault(Module m, RuntimeException e) {
		AeroClient.LOG.error("FAULT module {} failed and was disabled", m.name, e);
		m.restoreEnabled(false);
		Chat.error(m.name + " crashed and was turned off: " + e);
	}

	public static void disableAll() {
		for (Module m : MODULES) if (m.isEnabled() && !m.hidden) m.setEnabled(false);
	}

	private static boolean inWorld() {
		Minecraft mc = Minecraft.getInstance();
		return mc.player != null && mc.level != null;
	}

	public static void tick() {
		Minecraft mc = Minecraft.getInstance();
		if (mc.screen == null && mc.getWindow() != null) {
			long window = mc.getWindow().getWindow();
			for (Module m : MODULES) {
				if (m.key() < 0 && m.altKey() < 0) continue;
				boolean down = m.key() >= 0 && InputConstants.isKeyDown(window, m.key())
						|| m.altKey() >= 0 && InputConstants.isKeyDown(window, m.altKey());
				boolean was = KEY_DOWN.getOrDefault(m, false);
				KEY_DOWN.put(m, down);
				if (down && !was) m.toggle();
			}
		}
		if (!inWorld()) return;
		for (Module m : MODULES) {
			if (!m.isEnabled()) continue;
			try {
				m.onTick();
			} catch (RuntimeException e) {
				fault(m, e);
			}
		}
	}

	public static void render3d(WorldRenderContext ctx) {
		if (!inWorld()) return;
		dev.aero.client.util.Render3d.capture(ctx);
		for (Module m : MODULES) {
			if (!m.isEnabled()) continue;
			try {
				m.onRender3d(ctx);
			} catch (RuntimeException e) {
				fault(m, e);
			}
		}
	}

	public static void render2d(GuiGraphics g, float partialTick) {
		if (!inWorld()) return;
		for (Module m : MODULES) {
			if (!m.isEnabled()) continue;
			try {
				m.onRender2d(g, partialTick);
			} catch (RuntimeException e) {
				fault(m, e);
			}
		}
	}

	/** True when a module wants the outgoing packet dropped. */
	public static boolean packetSend(Packet<?> p) {
		boolean cancel = false;
		for (Module m : MODULES) {
			if (!m.isEnabled()) continue;
			try {
				cancel |= m.onPacketSend(p);
			} catch (RuntimeException e) {
				fault(m, e);
			}
		}
		return cancel;
	}

	public static boolean packetReceive(Packet<?> p) {
		boolean cancel = false;
		for (Module m : MODULES) {
			if (!m.isEnabled()) continue;
			try {
				cancel |= m.onPacketReceive(p);
			} catch (RuntimeException e) {
				fault(m, e);
			}
		}
		return cancel;
	}
}
