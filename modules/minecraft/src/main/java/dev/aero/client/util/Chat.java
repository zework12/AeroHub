package dev.aero.client.util;

import net.minecraft.ChatFormatting;
import net.minecraft.client.Minecraft;
import net.minecraft.network.chat.Component;

/** Messages shown to the player only; nothing here is sent to a server. */
public final class Chat {
	private Chat() {
	}

	private static void show(String text, ChatFormatting colour) {
		Minecraft mc = Minecraft.getInstance();
		if (mc.gui == null) return;
		Component prefix = Component.literal("[Aero] ").withStyle(ChatFormatting.AQUA);
		mc.execute(() -> mc.gui.getChat().addMessage(prefix.copy().append(Component.literal(text).withStyle(colour))));
	}

	public static void info(String text) {
		show(text, ChatFormatting.WHITE);
	}

	public static void warn(String text) {
		show(text, ChatFormatting.YELLOW);
	}

	public static void error(String text) {
		show(text, ChatFormatting.RED);
	}
}
