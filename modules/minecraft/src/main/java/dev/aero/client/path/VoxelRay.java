package dev.aero.client.path;

/** Grid traversal (Amanatides-Woo) used to decide whether a target can be mined from a standing position. */
public final class VoxelRay {
	private VoxelRay() {
	}

	/**
	 * True when no SOLID, UNKNOWN or LAVA/DANGER-blocking cell lies between the start point and the target cell.
	 * The cell the ray starts in and the target cell itself are not checked.
	 */
	public static boolean clear(Terrain t, double sx, double sy, double sz, int tx, int ty, int tz) {
		double ex = tx + 0.5, ey = ty + 0.5, ez = tz + 0.5;
		double dx = ex - sx, dy = ey - sy, dz = ez - sz;
		int x = (int) Math.floor(sx), y = (int) Math.floor(sy), z = (int) Math.floor(sz);
		int stepX = dx > 0 ? 1 : -1, stepY = dy > 0 ? 1 : -1, stepZ = dz > 0 ? 1 : -1;
		double tDeltaX = dx == 0 ? Double.POSITIVE_INFINITY : Math.abs(1.0 / dx);
		double tDeltaY = dy == 0 ? Double.POSITIVE_INFINITY : Math.abs(1.0 / dy);
		double tDeltaZ = dz == 0 ? Double.POSITIVE_INFINITY : Math.abs(1.0 / dz);
		double tMaxX = dx == 0 ? Double.POSITIVE_INFINITY : (stepX > 0 ? (x + 1 - sx) : (sx - x)) * tDeltaX;
		double tMaxY = dy == 0 ? Double.POSITIVE_INFINITY : (stepY > 0 ? (y + 1 - sy) : (sy - y)) * tDeltaY;
		double tMaxZ = dz == 0 ? Double.POSITIVE_INFINITY : (stepZ > 0 ? (z + 1 - sz) : (sz - z)) * tDeltaZ;
		int guard = (int) (Math.abs(dx) + Math.abs(dy) + Math.abs(dz)) + 8;
		while (guard-- > 0) {
			if (tMaxX <= tMaxY && tMaxX <= tMaxZ) {
				x += stepX;
				tMaxX += tDeltaX;
			} else if (tMaxY <= tMaxZ) {
				y += stepY;
				tMaxY += tDeltaY;
			} else {
				z += stepZ;
				tMaxZ += tDeltaZ;
			}
			if (x == tx && y == ty && z == tz) return true;
			Kind k = t.kind(x, y, z);
			if (k == Kind.SOLID || k == Kind.UNKNOWN) return false;
		}
		return false;
	}
}
