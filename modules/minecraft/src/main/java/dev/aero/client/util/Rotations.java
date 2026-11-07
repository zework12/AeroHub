package dev.aero.client.util;

/**
 * Angle math in Minecraft's convention: degrees, +Y is up. Yaw 0 faces +Z (south), 90 faces west (-X), -90 faces east (+X),
 * 180 faces north. Pitch -90 looks straight up and +90 straight down.
 */
public final class Rotations {
	private Rotations() {
	}

	/** Yaw and pitch that look along the vector (dx, dy, dz). */
	public static float[] to(double dx, double dy, double dz) {
		float yaw = (float) Math.toDegrees(Math.atan2(-dx, dz));
		float pitch = (float) Math.toDegrees(-Math.atan2(dy, Math.sqrt(dx * dx + dz * dz)));
		return new float[] {yaw, pitch};
	}

	/** Wraps an angle into [-180, 180). */
	public static float wrap(float degrees) {
		float d = degrees % 360.0f;
		if (d >= 180.0f) d -= 360.0f;
		if (d < -180.0f) d += 360.0f;
		return d;
	}

	/** Shortest signed turn from `from` to `to`. */
	public static float delta(float from, float to) {
		return wrap(to - from);
	}

	/** Turns `current` towards `target` by at most `maxStep` degrees along the short way. */
	public static float approach(float current, float target, float maxStep) {
		float d = delta(current, target);
		if (Math.abs(d) <= maxStep) return current + d;
		return current + Math.signum(d) * maxStep;
	}

	/** Unit vector for a yaw/pitch pair: the direction the player looks. */
	public static double[] direction(float yaw, float pitch) {
		double y = Math.toRadians(yaw), p = Math.toRadians(pitch);
		double cp = Math.cos(p);
		return new double[] {-Math.sin(y) * cp, -Math.sin(p), Math.cos(y) * cp};
	}

	/** Angle in degrees between the look direction and the direction to (dx, dy, dz). */
	public static double angleBetween(float yaw, float pitch, double dx, double dy, double dz) {
		double[] a = direction(yaw, pitch);
		double len = Math.sqrt(dx * dx + dy * dy + dz * dz);
		if (len < 1.0e-9) return 0;
		double dot = (a[0] * dx + a[1] * dy + a[2] * dz) / len;
		return Math.toDegrees(Math.acos(Math.max(-1, Math.min(1, dot))));
	}
}
