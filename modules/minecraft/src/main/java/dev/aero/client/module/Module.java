package dev.aero.client.module;

import dev.aero.client.setting.BoolSetting;
import dev.aero.client.setting.ListSetting;
import dev.aero.client.setting.ModeSetting;
import dev.aero.client.setting.NumberSetting;
import dev.aero.client.setting.Setting;
import dev.aero.client.util.Chat;
import net.fabricmc.fabric.api.client.rendering.v1.WorldRenderContext;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiGraphics;
import net.minecraft.network.protocol.Packet;

import java.util.ArrayList;
import java.util.List;

/** A switchable feature. Hooks run only while the module is enabled and a world is loaded, unless noted. */
public abstract class Module {
	protected static final Minecraft mc = Minecraft.getInstance();

	public final String name;
	public final String description;
	public final Category category;
	public final List<Setting<?>> settings = new ArrayList<>();

	private boolean enabled;
	private int key = -1;
	private int altKey = -1;
	/** Hide from the on-screen module list and the GUI (internal helpers, the GUI opener). */
	public boolean hidden;
	/** Its on/off state is restored from the config at startup; every other module starts switched off. */
	public boolean persist;

	protected Module(String name, Category category, String description) {
		this.name = name;
		this.category = category;
		this.description = description;
	}

	public boolean isEnabled() {
		return enabled;
	}

	public int key() {
		return key;
	}

	public void setKey(int glfwKey) {
		key = glfwKey;
	}

	/** A second key that does the same as the first one; fixed in code, not saved in the config. */
	public int altKey() {
		return altKey;
	}

	public void setAltKey(int glfwKey) {
		altKey = glfwKey;
	}

	public void toggle() {
		setEnabled(!enabled);
	}

	public void setEnabled(boolean on) {
		if (on == enabled) return;
		enabled = on;
		try {
			if (on) onEnable();
			else onDisable();
		} catch (RuntimeException e) {
			ModuleManager.fault(this, e);
		}
	}

	/** Used by the config loader: restores the saved state without calling onEnable for a module that has no world yet. */
	public void restoreEnabled(boolean on) {
		enabled = on;
	}

	protected void onEnable() {
	}

	protected void onDisable() {
	}

	/** Once per client tick while in a world. */
	public void onTick() {
	}

	/** After the world has been drawn; matrices are camera-relative via {@link dev.aero.client.util.Render3d}. */
	public void onRender3d(WorldRenderContext ctx) {
	}

	public void onRender2d(GuiGraphics g, float partialTick) {
	}

	/** Return true to stop the packet from being sent. */
	public boolean onPacketSend(Packet<?> packet) {
		return false;
	}

	/** Return true to drop an incoming packet before the game handles it. */
	public boolean onPacketReceive(Packet<?> packet) {
		return false;
	}

	/** Short text shown after the name in the module list, or null. */
	public String info() {
		return null;
	}

	public BoolSetting bool(String n, boolean def) {
		BoolSetting s = new BoolSetting(n, def);
		settings.add(s);
		return s;
	}

	public NumberSetting num(String n, double def, double min, double max, double step) {
		NumberSetting s = new NumberSetting(n, def, min, max, step);
		settings.add(s);
		return s;
	}

	public ModeSetting mode(String n, String def, String... options) {
		ModeSetting s = new ModeSetting(n, def, options);
		settings.add(s);
		return s;
	}

	public ListSetting list(String n, String... initial) {
		return list(n, ListSetting.Kind.BLOCK, initial);
	}

	public ListSetting list(String n, ListSetting.Kind kind, String... initial) {
		ListSetting s = new ListSetting(n, kind, initial);
		settings.add(s);
		return s;
	}

	protected void say(String text) {
		Chat.info(name + ": " + text);
	}

	public Setting<?> setting(String n) {
		for (Setting<?> s : settings) if (s.name.replace(" ", "").equalsIgnoreCase(n.replace(" ", ""))) return s;
		return null;
	}
}
