package dev.aero.client.path;

/** Read-only view of the world used by {@link PathSearch}. Implemented over the client level in the game, and by a grid in checks. */
public interface Terrain {
	Kind kind(int x, int y, int z);

	/** Ticks needed to break the block with the best tool the player has. POSITIVE_INFINITY if it must not be dug. Only asked for SOLID cells. */
	double digTicks(int x, int y, int z);

	/** True for sand, gravel and other blocks that fall when the block under them is removed. */
	boolean falls(int x, int y, int z);

	default boolean passable(int x, int y, int z) {
		Kind k = kind(x, y, z);
		return k == Kind.AIR || k == Kind.WATER;
	}
}
