package dev.aero.client.util;

/**
 * Arrow flight in Minecraft's own integration order: each tick the position advances by the velocity, then the
 * velocity is multiplied by 0.99 and loses 0.05 on Y. Units are blocks and ticks, +Y is up.
 */
public final class Ballistics {
	public static final double DRAG = 0.99, GRAVITY = 0.05;
	/** Bow arrow speed at full draw, blocks per tick. */
	public static final double FULL_DRAW_SPEED = 3.0;

	private Ballistics() {
	}

	/** Draw power for a bow held `ticks` ticks: 0..1. */
	public static double bowPower(int ticks) {
		double f = ticks / 20.0;
		f = (f * f + f * 2.0) / 3.0;
		return Math.min(1.0, f);
	}

	/**
	 * Closest approach of an arrow fired from (sx,sy,sz) with the given yaw/pitch (degrees) and speed to the
	 * point (tx,ty,tz) within `maxTicks`. Returns {minimumDistance, tickOfClosestApproach}.
	 */
	public static double[] closestApproach(double sx, double sy, double sz, float yaw, float pitch, double speed, double tx, double ty, double tz, int maxTicks) {
		double[] dir = Rotations.direction(yaw, pitch);
		double vx = dir[0] * speed, vy = dir[1] * speed, vz = dir[2] * speed;
		double x = sx, y = sy, z = sz;
		double best = Double.MAX_VALUE;
		int bestTick = 0;
		for (int t = 1; t <= maxTicks; t++) {
			double px = x, py = y, pz = z;
			x += vx;
			y += vy;
			z += vz;
			vx *= DRAG;
			vy = vy * DRAG - GRAVITY;
			vz *= DRAG;
			// the game sweeps the arrow along the whole step, so measure to the segment, not just the end point
			double d = distanceToSegment(px, py, pz, x, y, z, tx, ty, tz);
			if (d < best) {
				best = d;
				bestTick = t;
			}
		}
		return new double[] {best, bestTick};
	}

	/** Distance from point (qx,qy,qz) to the segment from (ax,ay,az) to (bx,by,bz). */
	static double distanceToSegment(double ax, double ay, double az, double bx, double by, double bz, double qx, double qy, double qz) {
		double abx = bx - ax, aby = by - ay, abz = bz - az;
		double len2 = abx * abx + aby * aby + abz * abz;
		double u = len2 < 1.0e-12 ? 0 : ((qx - ax) * abx + (qy - ay) * aby + (qz - az) * abz) / len2;
		u = Math.max(0, Math.min(1, u));
		double dx = ax + abx * u - qx, dy = ay + aby * u - qy, dz = az + abz * u - qz;
		return Math.sqrt(dx * dx + dy * dy + dz * dz);
	}

	/**
	 * Yaw and pitch that make the arrow pass closest to a target moving with constant velocity (per tick).
	 * Returns {yaw, pitch, missDistance, flightTicks}, or null when nothing within 120 ticks gets near.
	 */
	public static double[] aim(double sx, double sy, double sz, double tx, double ty, double tz, double tvx, double tvy, double tvz, double speed) {
		double px = tx, py = ty, pz = tz;
		double[] result = null;
		for (int iteration = 0; iteration < 4; iteration++) {
			float yaw = (float) Math.toDegrees(Math.atan2(-(px - sx), pz - sz));
			double bestMiss = Double.MAX_VALUE, bestPitch = 0, bestTick = 0;
			for (double pitch = -89; pitch <= 89; pitch += 1.0) {
				double[] r = closestApproach(sx, sy, sz, yaw, (float) pitch, speed, px, py, pz, 120);
				if (r[0] < bestMiss) {
					bestMiss = r[0];
					bestPitch = pitch;
					bestTick = r[1];
				}
			}
			for (double pitch = bestPitch - 1; pitch <= bestPitch + 1; pitch += 0.05) {
				double[] r = closestApproach(sx, sy, sz, yaw, (float) pitch, speed, px, py, pz, 120);
				if (r[0] < bestMiss) {
					bestMiss = r[0];
					bestPitch = pitch;
					bestTick = r[1];
				}
			}
			result = new double[] {yaw, bestPitch, bestMiss, bestTick};
			px = tx + tvx * bestTick;
			py = ty + tvy * bestTick;
			pz = tz + tvz * bestTick;
		}
		return result != null && result[2] < 3.0 ? result : null;
	}
}
