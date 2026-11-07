package dev.aero.client.path;

/** What the pathfinder needs to know about one block cell. */
public enum Kind {
	/** Chunk not loaded: never entered, never dug. */
	UNKNOWN,
	/** Nothing there (also plants and other non-solid blocks the body passes through). */
	AIR,
	/** Full solid block: can be stood on, or dug when its dig cost is finite. */
	SOLID,
	WATER,
	LAVA,
	/** Passable but harmful (fire, cactus, magma, berries, cobweb...): never entered or stood on. */
	DANGER
}
