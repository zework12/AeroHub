package dev.aero.client.util;

import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientPacketListener;
import net.minecraft.network.protocol.Packet;

/** Sends packets from modules without the module hooks seeing them again. */
public final class PacketUtil {
	private static final ThreadLocal<Boolean> BYPASS = ThreadLocal.withInitial(() -> false);

	private PacketUtil() {
	}

	public static boolean bypass() {
		return BYPASS.get();
	}

	public static void send(Packet<?> packet) {
		ClientPacketListener c = Minecraft.getInstance().getConnection();
		if (c == null) return;
		BYPASS.set(true);
		try {
			c.send(packet);
		} finally {
			BYPASS.set(false);
		}
	}
}
