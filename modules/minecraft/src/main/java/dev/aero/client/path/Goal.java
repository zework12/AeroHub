package dev.aero.client.path;

import java.util.List;

/** Where a search ends. Positions given to the methods are the player's feet cell. */
public interface Goal {
	/** The cell to work on (the ore to mine, the point to stand near) when standing here finishes the search, or null. */
	int[] hit(Terrain t, int x, int y, int z);

	/** Lower bound of the remaining cost in ticks. */
	double heuristic(int x, int y, int z);

	double EYE_HEIGHT = 1.62;
	/** Cheapest horizontal block in ticks, kept below the real sprint cost so the heuristic never overestimates. */
	double MIN_BLOCK_TICKS = 3.0;

	/** Any of several blocks (ores) that can be reached and hit from the standing position. */
	final class Blocks implements Goal {
		private final List<int[]> targets;
		private final double reach;

		public Blocks(List<int[]> targets, double reach) {
			this.targets = targets;
			this.reach = reach;
		}

		@Override
		public int[] hit(Terrain t, int x, int y, int z) {
			double ex = x + 0.5, ey = y + EYE_HEIGHT, ez = z + 0.5;
			int[] best = null;
			double bestDist = Double.MAX_VALUE;
			for (int[] p : targets) {
				double dx = p[0] + 0.5 - ex, dy = p[1] + 0.5 - ey, dz = p[2] + 0.5 - ez;
				double d = Math.sqrt(dx * dx + dy * dy + dz * dz);
				if (d > reach || d >= bestDist) continue;
				if (!VoxelRay.clear(t, ex, ey, ez, p[0], p[1], p[2])) continue;
				best = p;
				bestDist = d;
			}
			return best;
		}

		@Override
		public double heuristic(int x, int y, int z) {
			double best = Double.MAX_VALUE;
			for (int[] p : targets) {
				double horizontal = Math.max(0, Math.abs(p[0] - x) + Math.abs(p[2] - z) - reach) * MIN_BLOCK_TICKS;
				double vertical = Math.max(0, Math.abs(p[1] - y) - reach) * MIN_BLOCK_TICKS;
				double h = Math.max(horizontal, vertical);
				if (h < best) best = h;
			}
			return best == Double.MAX_VALUE ? 0 : best;
		}
	}

	/** A fixed standing position, or any position within `range` blocks of it. */
	final class Position implements Goal {
		private final int gx, gy, gz;
		private final double range;

		public Position(int x, int y, int z, double range) {
			gx = x;
			gy = y;
			gz = z;
			this.range = range;
		}

		@Override
		public int[] hit(Terrain t, int x, int y, int z) {
			double dx = x - gx, dy = y - gy, dz = z - gz;
			boolean ok = range <= 0 ? (x == gx && y == gy && z == gz) : dx * dx + dy * dy + dz * dz <= range * range;
			return ok ? new int[] {gx, gy, gz} : null;
		}

		@Override
		public double heuristic(int x, int y, int z) {
			double horizontal = Math.max(0, Math.abs(gx - x) + Math.abs(gz - z) - range) * MIN_BLOCK_TICKS;
			double vertical = Math.max(0, Math.abs(gy - y) - range) * MIN_BLOCK_TICKS;
			return Math.max(horizontal, vertical);
		}
	}
}
