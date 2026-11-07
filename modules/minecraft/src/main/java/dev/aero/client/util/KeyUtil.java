package dev.aero.client.util;

import com.mojang.blaze3d.platform.InputConstants;
import dev.aero.client.mixin.KeyMappingAccessor;
import net.minecraft.client.KeyMapping;
import net.minecraft.client.Minecraft;
import org.lwjgl.glfw.GLFW;

public final class KeyUtil {
	private KeyUtil() {
	}

	/** True when the physical key or mouse button bound to the mapping is held right now. */
	public static boolean physicallyDown(KeyMapping km) {
		InputConstants.Key key = ((KeyMappingAccessor) km).aero$getKey();
		long window = Minecraft.getInstance().getWindow().getWindow();
		return switch (key.getType()) {
			case KEYSYM -> key.getValue() != InputConstants.UNKNOWN.getValue() && InputConstants.isKeyDown(window, key.getValue());
			case MOUSE -> GLFW.glfwGetMouseButton(window, key.getValue()) == GLFW.GLFW_PRESS;
			default -> false;
		};
	}

	/** Gives a mapping back to the real keyboard after a module held it down artificially. */
	public static void release(KeyMapping km) {
		km.setDown(physicallyDown(km));
	}
}
