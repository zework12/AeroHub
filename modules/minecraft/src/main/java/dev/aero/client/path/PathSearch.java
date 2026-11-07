package dev.aero.client.path;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.PriorityQueue;

/**
 * A* over player feet cells. Cost is time in ticks, so digging a hard block costs more than walking around it and
 * the cheapest path is the fastest one, including the digging it needs. Resumable: call {@link #step} once per tick.
 *
 * Coordinates are block cells, +Y up. A feet cell is standable when the cell below is SOLID or WATER, and passable
 * when it and the cell above it can be occupied (air, water, or a solid cell that is dug first).
 */
public final class PathSearch {
	public enum Move { TRAVERSE, ASCEND, DESCEND, DIG_DOWN }

	public enum Status { SEARCHING, FOUND, FAILED }

	/** One transition. `dig` holds x,y,z triples to break, in order, before moving into (x,y,z). */
	public record Step(Move move, int x, int y, int z, int[] dig) {
	}

	public static final class Config {
		public int maxFall = 3;
		public int maxExpansions = 40000;
		public boolean allowDig = true;
		public boolean allowWater = true;
	}

	static final double WALK = 4.6, JUMP = 8.0, FALL_PER_BLOCK = 3.0, WATER_CELL = 1.5;
	static final double GRAVITY_ABOVE_PENALTY = 30.0, WATER_SIDE_PENALTY = 6.0;

	private static final int[][] DIRS = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

	private static final class Node {
		final int x, y, z;
		double g, f, h;
		Node parent;
		Move move;
		int[] dig;
		boolean closed;

		Node(int x, int y, int z) {
			this.x = x;
			this.y = y;
			this.z = z;
		}
	}

	private final Terrain terrain;
	private final Goal goal;
	private final Config cfg;
	private final HashMap<Long, Node> nodes = new HashMap<>();
	private final PriorityQueue<Node> open = new PriorityQueue<>((a, b) -> {
		int c = Double.compare(a.f, b.f);
		return c != 0 ? c : Double.compare(b.g, a.g);
	});
	private Node best;
	private Node goalNode;
	private int[] hitCell;
	private int expansions;
	private Status status = Status.SEARCHING;

	private final ArrayList<Integer> digBuf = new ArrayList<>();
	/** The terrain as seen from the node being expanded: cells dug on its last few steps count as open. */
	private Terrain view;

	public PathSearch(Terrain terrain, Goal goal, Config cfg, int sx, int sy, int sz) {
		this.terrain = terrain;
		this.view = terrain;
		this.goal = goal;
		this.cfg = cfg;
		Node start = new Node(sx, sy, sz);
		start.h = goal.heuristic(sx, sy, sz);
		start.f = start.h;
		nodes.put(key(sx, sy, sz), start);
		open.add(start);
		best = start;
	}

	public Status status() {
		return status;
	}

	public int expansions() {
		return expansions;
	}

	/** Cost of the found path in ticks (valid after FOUND): walking, jumping, falling and digging time. */
	public double cost() {
		return goalNode == null ? 0 : goalNode.g;
	}

	/** The block to work on once the path is walked (valid after FOUND). */
	public int[] hitCell() {
		return hitCell;
	}

	public Status step(int budget) {
		if (status != Status.SEARCHING) return status;
		for (int i = 0; i < budget; i++) {
			Node cur = open.poll();
			if (cur == null) {
				status = Status.FAILED;
				return status;
			}
			if (cur.closed) continue;
			cur.closed = true;
			int[] hit = goal.hit(viewAt(cur), cur.x, cur.y, cur.z);
			if (hit != null) {
				goalNode = cur;
				hitCell = hit;
				status = Status.FOUND;
				return status;
			}
			if (cur.h < best.h || (cur.h == best.h && cur.g < best.g)) best = cur;
			if (++expansions >= cfg.maxExpansions) {
				status = Status.FAILED;
				return status;
			}
			expand(cur);
		}
		return status;
	}

	/** Steps from the start to the goal cell (valid after FOUND). */
	public List<Step> path() {
		return status == Status.FOUND ? build(goalNode) : List.of();
	}

	/** Steps towards the closest cell reached so far, for a failed search. */
	public List<Step> partial() {
		return build(best);
	}

	private static List<Step> build(Node end) {
		ArrayList<Step> out = new ArrayList<>();
		for (Node n = end; n != null && n.parent != null; n = n.parent) out.add(new Step(n.move, n.x, n.y, n.z, n.dig));
		Collections.reverse(out);
		return out;
	}

	/** The terrain as it will be once the cells dug on the last few steps of this node's path are open. */
	private Terrain viewAt(Node n) {
		HashSet<Long> dug = null;
		int depth = 0;
		for (Node p = n; p != null && depth < 8; p = p.parent, depth++) {
			if (p.dig == null || p.dig.length == 0) continue;
			if (dug == null) dug = new HashSet<>();
			for (int i = 0; i < p.dig.length; i += 3) dug.add(key(p.dig[i], p.dig[i + 1], p.dig[i + 2]));
		}
		if (dug == null) return terrain;
		final HashSet<Long> cells = dug;
		return new Terrain() {
			@Override
			public Kind kind(int x, int y, int z) {
				return cells.contains(key(x, y, z)) ? Kind.AIR : terrain.kind(x, y, z);
			}

			@Override
			public double digTicks(int x, int y, int z) {
				return terrain.digTicks(x, y, z);
			}

			@Override
			public boolean falls(int x, int y, int z) {
				return terrain.falls(x, y, z);
			}
		};
	}

	private static long key(int x, int y, int z) {
		return ((long) (x & 0x3FFFFFF) << 38) | ((long) (z & 0x3FFFFFF) << 12) | ((y + 2048) & 0xFFF);
	}

	/** Cost of putting the body in this cell; appends the cell to digBuf when it has to be dug. INF when not allowed. */
	private double cell(int x, int y, int z) {
		Kind k = view.kind(x, y, z);
		switch (k) {
			case AIR:
				return 0;
			case WATER:
				return cfg.allowWater ? WATER_CELL : Double.POSITIVE_INFINITY;
			case SOLID: {
				if (!cfg.allowDig) return Double.POSITIVE_INFINITY;
				double dig = view.digTicks(x, y, z);
				if (Double.isInfinite(dig)) return Double.POSITIVE_INFINITY;
				double extra = 0;
				if (view.kind(x, y + 1, z) == Kind.LAVA || view.kind(x, y + 1, z) == Kind.WATER) return Double.POSITIVE_INFINITY;
				if (view.kind(x, y - 1, z) == Kind.LAVA) return Double.POSITIVE_INFINITY;
				for (int[] d : DIRS) {
					Kind s = view.kind(x + d[0], y, z + d[1]);
					if (s == Kind.LAVA) return Double.POSITIVE_INFINITY;
					if (s == Kind.WATER) extra += WATER_SIDE_PENALTY;
				}
				if (view.falls(x, y + 1, z)) extra += GRAVITY_ABOVE_PENALTY;
				digBuf.add(x);
				digBuf.add(y);
				digBuf.add(z);
				return dig + extra;
			}
			default:
				return Double.POSITIVE_INFINITY;
		}
	}

	private boolean standOn(int x, int y, int z) {
		Kind k = view.kind(x, y, z);
		return k == Kind.SOLID || k == Kind.WATER;
	}

	private void offer(Node from, int x, int y, int z, double cost, Move move) {
		if (Double.isInfinite(cost)) return;
		long key = key(x, y, z);
		Node n = nodes.get(key);
		double g = from.g + cost;
		if (n == null) {
			n = new Node(x, y, z);
			n.h = goal.heuristic(x, y, z);
			nodes.put(key, n);
		} else if (n.closed || g >= n.g) {
			return;
		}
		n.g = g;
		n.f = g + n.h;
		n.parent = from;
		n.move = move;
		n.dig = new int[digBuf.size()];
		for (int i = 0; i < n.dig.length; i++) n.dig[i] = digBuf.get(i);
		open.add(n);
	}

	private void expand(Node cur) {
		view = viewAt(cur);
		int x = cur.x, y = cur.y, z = cur.z;
		for (int[] d : DIRS) {
			int nx = x + d[0], nz = z + d[1];
			Kind floor = view.kind(nx, y - 1, nz);

			if (floor == Kind.SOLID || floor == Kind.WATER) {
				digBuf.clear();
				double c = cell(nx, y + 1, nz) + cell(nx, y, nz);
				offer(cur, nx, y, nz, WALK + c + (floor == Kind.WATER ? WATER_CELL : 0), Move.TRAVERSE);
			}

			if (view.kind(nx, y, nz) == Kind.SOLID) {
				digBuf.clear();
				double c = cell(x, y + 2, z) + cell(nx, y + 2, nz) + cell(nx, y + 1, nz);
				offer(cur, nx, y + 1, nz, JUMP + c, Move.ASCEND);
			}

			if (floor == Kind.AIR || floor == Kind.WATER) {
				digBuf.clear();
				double c = cell(nx, y + 1, nz) + cell(nx, y, nz);
				if (!Double.isInfinite(c)) fall(cur, nx, y, nz, WALK + c, Move.DESCEND);
			}
		}

		if (view.kind(x, y - 1, z) == Kind.SOLID) {
			digBuf.clear();
			double c = cell(x, y - 1, z);
			if (!Double.isInfinite(c)) fall(cur, x, y, z, c, Move.DIG_DOWN);
		}
	}

	/**
	 * Drops down column (x,z) from feet height y until something solid or water is below. For DIG_DOWN the first cell
	 * (y-1) is the block just dug and already paid for in baseCost; every other cell has to be open already.
	 */
	private void fall(Node cur, int x, int y, int z, double baseCost, Move move) {
		for (int k = 1; k <= cfg.maxFall; k++) {
			int fy = y - k;
			boolean dugCell = move == Move.DIG_DOWN && k == 1;
			if (!dugCell && !view.passable(x, fy, z)) return;
			Kind below = view.kind(x, fy - 1, z);
			if (below == Kind.SOLID || below == Kind.WATER) {
				offer(cur, x, fy, z, baseCost + FALL_PER_BLOCK * k, move);
				return;
			}
			if (below != Kind.AIR) return;
		}
	}
}
