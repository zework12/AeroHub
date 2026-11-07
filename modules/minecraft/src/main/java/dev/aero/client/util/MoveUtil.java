package dev.aero.client.util;

import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.phys.Vec3;

/** Movement helpers. Yaw/forward/left follow Minecraft: forward is +Z at yaw 0, left is +X at yaw 0. */
public final class MoveUtil {
	private MoveUtil() {
	}

	public static boolean isMoving() {
		LocalPlayer p = Minecraft.getInstance().player;
		return p.input.forwardImpulse != 0 || p.input.leftImpulse != 0;
	}

	/** Unit-length horizontal direction {x, z} for the given yaw and input, or {0, 0} with no input. Pure. */
	public static double[] direction(float yaw, float forward, float left) {
		double rad = Math.toRadians(yaw);
		double fx = -Math.sin(rad), fz = Math.cos(rad);
		double lx = Math.cos(rad), lz = Math.sin(rad);
		double x = forward * fx + left * lx, z = forward * fz + left * lz;
		double len = Math.sqrt(x * x + z * z);
		if (len < 1.0e-6) return new double[] {0, 0};
		return new double[] {x / len, z / len};
	}

	/** Sets the horizontal speed of the local player along the pressed movement keys; Y is kept. */
	public static void setSpeed(double speed) {
		LocalPlayer p = Minecraft.getInstance().player;
		double[] d = direction(p.getYRot(), p.input.forwardImpulse, p.input.leftImpulse);
		Vec3 v = p.getDeltaMovement();
		p.setDeltaMovement(d[0] * speed, v.y, d[1] * speed);
	}

	public static double horizontalSpeed(Entity e) {
		Vec3 v = e.getDeltaMovement();
		return Math.hypot(v.x, v.z);
	}

	public static void setY(Entity e, double y) {
		Vec3 v = e.getDeltaMovement();
		e.setDeltaMovement(v.x, y, v.z);
	}
}
