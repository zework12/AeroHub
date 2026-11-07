package dev.aero.client.module.impl;

import dev.aero.client.gui.ClickGuiScreen;
import dev.aero.client.module.Category;
import dev.aero.client.module.Module;
import org.lwjgl.glfw.GLFW;

/** Opens the click GUI. Bound to Right Shift by default; switches itself off right away. */
public final class ClickGui extends Module {
	public ClickGui() {
		super("ClickGui", Category.RENDER, "Opens the module menu.");
		setKey(GLFW.GLFW_KEY_RIGHT_SHIFT);
		setAltKey(GLFW.GLFW_KEY_INSERT);
		hidden = true;
	}

	@Override
	protected void onEnable() {
		mc.setScreen(new ClickGuiScreen());
		setEnabled(false);
	}
}
