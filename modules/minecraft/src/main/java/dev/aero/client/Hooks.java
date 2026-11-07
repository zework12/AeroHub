package dev.aero.client;

import net.minecraft.network.chat.Component;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.phys.Vec3;

import java.util.List;
import java.util.Set;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.function.UnaryOperator;

/**
 * State the mixins read. Modules write these in onEnable / onTick / onDisable, so the mixins stay one-liners
 * and a disabled module leaves every value at its neutral default.
 */
public final class Hooks {
	private Hooks() {
	}

	/** Added to the entity / block interaction range of the local player (blocks). */
	public static double reachEntity, reachBlock;
	/** Added to every other entity's pick radius (blocks), makes them easier to hit. */
	public static float hitboxExpand;

	public static boolean safeWalk, noSlow, noPush, noHurtCam, noFireOverlay, noWaterOverlay, noWallOverlay, noPumpkin, noWeather, antiBlind, nametags;

	/** Game speed multiplier; 1 = normal. */
	public static double timerSpeed = 1.0;

	/** Field-of-view: multiplied by fovScale, or replaced by fovFixed when it is above zero (degrees). */
	public static double fovScale = 1.0, fovFixed = 0;

	/** X-Ray: when on, only these blocks are drawn. Read by the chunk meshing threads, so it is swapped as a whole. */
	public static volatile boolean xray;
	public static volatile Set<Block> xrayBlocks = Set.of();

	public static boolean freecam;
	public static Vec3 camPos = Vec3.ZERO, camPosPrev = Vec3.ZERO;
	public static float camYaw, camPitch;

	/** Keep the server's rotation out of the local player (NoRotate). */
	public static boolean noRotate;

	/** Packet-time knockback scaling: 1 = vanilla, 0 = none. */
	public static double knockbackHorizontal = 1.0, knockbackVertical = 1.0;

	/** Text transforms applied to every chat window line (timestamps, name protection); each decides for itself whether it is on. */
	public static final List<UnaryOperator<Component>> chatModifiers = new CopyOnWriteArrayList<>();

	public static Component modifyChat(Component message) {
		Component out = message;
		for (UnaryOperator<Component> op : chatModifiers) out = op.apply(out);
		return out;
	}

	public static double adjustFov(double fov, boolean fromOptions) {
		if (!fromOptions) return fov;
		if (fovFixed > 0) return fovFixed;
		return fov * fovScale;
	}

	public static void reset() {
		reachEntity = reachBlock = 0;
		hitboxExpand = 0;
		safeWalk = noSlow = noPush = noHurtCam = noFireOverlay = noWaterOverlay = noWallOverlay = noPumpkin = noWeather = antiBlind = nametags = false;
		timerSpeed = 1.0;
		fovScale = 1.0;
		fovFixed = 0;
		freecam = false;
		xray = false;
		noRotate = false;
		knockbackHorizontal = knockbackVertical = 1.0;
	}
}
