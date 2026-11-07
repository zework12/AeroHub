package dev.aero.client.pathing;

import dev.aero.client.path.Goal;
import dev.aero.client.path.Kind;
import dev.aero.client.path.PathSearch;
import dev.aero.client.util.Rotations;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.core.BlockPos;

import java.util.List;

/**
 * Plans a path with {@link PathSearch} (spread over ticks) and walks it by pressing the game's movement keys,
 * digging the blocks the plan says are in the way. Replans when the player leaves the route or gets stuck.
 */
public final class PathExecutor {
	public enum State { IDLE, SEARCHING, WALKING, ARRIVED, FAILED }

	public final PathSearch.Config config = new PathSearch.Config();
	public boolean protectContainers = true;
	/** When true, digging may use tools from the whole inventory (they are swapped into the hand), not only the hotbar. */
	public boolean toolsFromInventory;
	/** Expansions per tick while searching. */
	public int searchBudget = 1500;
	public int maxReplans = 6;
	/** For plain "go to" goals: when no full path exists, walk to the closest reachable point and search again. */
	public boolean walkPartial;

	private State state = State.IDLE;
	private Goal goal;
	private PathSearch search;
	private List<PathSearch.Step> steps = List.of();
	private int index, stepTicks, replans, stuckTicks;
	private int startX, startY, startZ;
	private double lastX, lastZ;
	private int[] hit;
	private double pathCost;
	private String message = "";

	public State state() {
		return state;
	}

	public boolean active() {
		return state == State.SEARCHING || state == State.WALKING;
	}

	/** The cell the goal wants worked on once ARRIVED (the ore to mine), or null. */
	public int[] hit() {
		return hit;
	}

	public String message() {
		return message;
	}

	public int stepsLeft() {
		return state == State.WALKING ? steps.size() - index : 0;
	}

	/** The steps of the current path and the one being walked (for drawing). */
	public List<PathSearch.Step> steps() {
		return steps;
	}

	public int index() {
		return index;
	}

	/** Cost in ticks of the full path just found; 0 for a partial walk towards a far target. */
	public double pathCost() {
		return pathCost;
	}

	/** Blocks still to be dug on the remaining steps. */
	public int digsLeft() {
		int n = 0;
		for (int i = index; i < steps.size(); i++) n += steps.get(i).dig().length / 3;
		return n;
	}

	public void start(Goal g) {
		replans = 0;
		begin(g);
	}

	private void begin(Goal g) {
		Minecraft mc = Minecraft.getInstance();
		LocalPlayer p = mc.player;
		if (p == null || mc.level == null) {
			fail("no world");
			return;
		}
		goal = g;
		BlockPos feet = p.blockPosition();
		startX = feet.getX();
		startY = feet.getY();
		startZ = feet.getZ();
		search = new PathSearch(new LevelTerrain(protectContainers, toolsFromInventory), g, config, startX, startY, startZ);
		steps = List.of();
		index = 0;
		stepTicks = 0;
		stuckTicks = 0;
		hit = null;
		state = State.SEARCHING;
		message = "searching";
	}

	public void cancel() {
		if (state != State.IDLE) Controls.releaseAll();
		state = State.IDLE;
		search = null;
		steps = List.of();
		message = "";
	}

	private void fail(String why) {
		Controls.releaseAll();
		state = State.FAILED;
		message = why;
	}

	private void replan(String why) {
		if (++replans > maxReplans) {
			fail("gave up: " + why);
			return;
		}
		Controls.releaseAll();
		begin(goal);
		message = "replanning (" + why + ")";
	}

	public void tick() {
		Minecraft mc = Minecraft.getInstance();
		if (mc.player == null || mc.level == null) {
			cancel();
			return;
		}
		if (state == State.SEARCHING) searchTick();
		else if (state == State.WALKING) walkTick(mc.player);
	}

	private void searchTick() {
		PathSearch.Status s = search.step(searchBudget);
		if (s == PathSearch.Status.FOUND) {
			steps = search.path();
			hit = search.hitCell();
			pathCost = search.cost();
			index = 0;
			stepTicks = 0;
			if (steps.isEmpty()) {
				state = State.ARRIVED;
				message = "arrived";
			} else {
				state = State.WALKING;
				message = "walking " + steps.size() + " steps";
			}
		} else if (s == PathSearch.Status.FAILED) {
			List<PathSearch.Step> part = search.partial();
			if (walkPartial && !part.isEmpty() && replans < maxReplans) {
				steps = part;
				hit = null;
				pathCost = 0;
				index = 0;
				stepTicks = 0;
				replans++;
				state = State.WALKING;
				message = "walking towards the closest point";
			} else {
				fail("no path found (" + search.expansions() + " cells searched)");
			}
		}
	}

	private void walkTick(LocalPlayer p) {
		if (index >= steps.size()) {
			Controls.releaseAll();
			// a partial walk ends by searching again from where it got to
			if (walkPartial && goal.hit(new LevelTerrain(protectContainers, toolsFromInventory), p.blockPosition().getX(), p.blockPosition().getY(), p.blockPosition().getZ()) == null) {
				replan("continuing");
				return;
			}
			state = State.ARRIVED;
			message = "arrived";
			return;
		}
		PathSearch.Step st = steps.get(index);
		BlockPos feet = p.blockPosition();
		boolean settled = p.onGround() || p.isInWater();
		// Sprinting overshoots cells. Accept being in any of the next few steps (the furthest one wins) and skip
		// ahead, otherwise the player turns round to reach a cell he has already passed and walks in circles.
		for (int j = Math.min(steps.size() - 1, index + 3); j >= index; j--) {
			PathSearch.Step c = steps.get(j);
			if (feet.getX() == c.x() && feet.getY() == c.y() && feet.getZ() == c.z() && settled && !hasDigAhead(index, j)) {
				index = j + 1;
				stepTicks = 0;
				return;
			}
		}

		int px = index == 0 ? startX : steps.get(index - 1).x();
		int py = index == 0 ? startY : steps.get(index - 1).y();
		int pz = index == 0 ? startZ : steps.get(index - 1).z();
		double dPrev = dist(p, px, py, pz), dNext = dist(p, st.x(), st.y(), st.z());
		if (dPrev > 3.5 && dNext > 3.5) {
			replan("left the route");
			return;
		}

		stepTicks++;
		int digCells = st.dig().length / 3;
		if (stepTicks > 140 + 60 * digCells) {
			replan("step took too long");
			return;
		}

		LevelTerrain terrain = new LevelTerrain(protectContainers, toolsFromInventory);
		for (int i = 0; i < st.dig().length; i += 3) {
			BlockPos cell = new BlockPos(st.dig()[i], st.dig()[i + 1], st.dig()[i + 2]);
			Kind k = terrain.kind(cell.getX(), cell.getY(), cell.getZ());
			if (k == Kind.SOLID) {
				if (st.move() == PathSearch.Move.DIG_DOWN) {
					double ox = p.getX() - (px + 0.5), oz = p.getZ() - (pz + 0.5);
					if (Math.hypot(ox, oz) > 0.25) {
						steer(p, px + 0.5, pz + 0.5, false, false);
						return;
					}
				}
				Controls.set(false, false, false, false);
				Miner.mine(cell, toolsFromInventory);
				stepTicks = Math.max(0, stepTicks - 1); // digging time is not walking time
				return;
			}
			if (k == Kind.LAVA || k == Kind.UNKNOWN || k == Kind.DANGER) {
				replan("hazard at " + cell.toShortString());
				return;
			}
		}

		double tx = st.x() + 0.5, tz = st.z() + 0.5;
		if (st.move() == PathSearch.Move.DIG_DOWN) {
			Controls.set(false, false, false, false); // the floor is gone, gravity does the move
			return;
		}
		boolean jump = st.move() == PathSearch.Move.ASCEND || p.horizontalCollision && p.onGround() || p.isInWater() && st.y() >= feet.getY();
		steer(p, tx, tz, jump, canSprint(st, px, pz));

		double moved = Math.hypot(p.getX() - lastX, p.getZ() - lastZ);
		lastX = p.getX();
		lastZ = p.getZ();
		stuckTicks = moved < 0.01 && p.onGround() ? stuckTicks + 1 : 0;
		if (stuckTicks > 30) replan("stuck");
	}

	/** Skipping steps is only safe when none of the skipped ones (from..to-1) still has blocks to dig. */
	private boolean hasDigAhead(int from, int to) {
		for (int i = from; i < to; i++) if (steps.get(i).dig().length > 0) return true;
		return false;
	}

	/** Sprint only along a straight run of flat walking steps; at corners, climbs, drops and digs walk so the player stops on the cell. */
	private boolean canSprint(PathSearch.Step st, int px, int pz) {
		if (st.move() != PathSearch.Move.TRAVERSE || st.dig().length > 0 || index + 1 >= steps.size()) return false;
		PathSearch.Step next = steps.get(index + 1);
		return next.move() == PathSearch.Move.TRAVERSE && next.dig().length == 0 && next.x() - st.x() == st.x() - px && next.z() - st.z() == st.z() - pz;
	}

	private static double dist(LocalPlayer p, int x, int y, int z) {
		double dx = p.getX() - (x + 0.5), dy = p.getY() - y, dz = p.getZ() - (z + 0.5);
		return Math.sqrt(dx * dx + dy * dy + dz * dz);
	}

	/** Turns towards (tx, tz) and walks forward. */
	private static void steer(LocalPlayer p, double tx, double tz, boolean jump, boolean sprint) {
		double dx = tx - p.getX(), dz = tz - p.getZ();
		double horizontal = Math.hypot(dx, dz);
		if (horizontal > 0.05) {
			float[] rot = Rotations.to(dx, 0, dz);
			p.setYRot(Rotations.approach(p.getYRot(), rot[0], 50.0f));
		}
		p.setXRot(Rotations.approach(p.getXRot(), 20.0f, 25.0f));
		Controls.set(horizontal > 0.1, jump, sprint && horizontal > 0.8, false);
	}
}
