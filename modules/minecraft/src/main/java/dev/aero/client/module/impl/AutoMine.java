package dev.aero.client.module.impl;

import dev.aero.client.module.Category;
import dev.aero.client.module.Module;
import dev.aero.client.path.Goal;
import dev.aero.client.pathing.Controls;
import dev.aero.client.pathing.LevelTerrain;
import dev.aero.client.pathing.Miner;
import dev.aero.client.pathing.PathExecutor;
import dev.aero.client.path.Kind;
import dev.aero.client.setting.BoolSetting;
import dev.aero.client.setting.ListSetting;
import dev.aero.client.setting.NumberSetting;
import dev.aero.client.util.BlockScan;
import dev.aero.client.util.Chat;
import dev.aero.client.util.Render3d;
import net.fabricmc.fabric.api.client.rendering.v1.WorldRenderContext;
import net.minecraft.client.gui.GuiGraphics;
import net.minecraft.world.phys.Vec3;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.core.BlockPos;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.world.entity.item.ItemEntity;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.phys.AABB;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * Finds the blocks from the filter near the player, plans the cheapest path to the nearest one (digging through
 * stone where that is faster than walking around), walks it, mines the block, picks up the drops and repeats.
 */
public final class AutoMine extends Module {
	public final ListSetting blocks = list("Blocks", "diamond_ore", "deepslate_diamond_ore");
	private final NumberSetting range = num("Range", 64, 8, 128, 1);
	private final NumberSetting reach = num("Reach", 3.0, 2.0, 4.5, 0.1);
	private final BoolSetting dig = bool("Dig through terrain", true);
	private final NumberSetting maxFall = num("Max fall", 3, 1, 8, 1);
	private final BoolSetting avoidLava = bool("Skip ore next to lava", true);
	private final BoolSetting protect = bool("Protect containers", true);
	private final BoolSetting fromInventory = bool("Tools from inventory", true);
	private final BoolSetting pickup = bool("Pick up drops", true);
	private final BoolSetting stopFull = bool("Stop when inventory full", true);
	private final NumberSetting minHealth = num("Stop below health", 6, 0, 20, 1);
	private final BoolSetting showPath = bool("Show path", true);
	private final BoolSetting chat = bool("Chat messages", true);
	private final BoolSetting panel = bool("Status panel", true);

	private enum Phase { SCAN, PATH, MINE, COLLECT }

	private final PathExecutor exec = new PathExecutor();
	private Phase phase = Phase.SCAN;
	private int wait, mineTicks, collectTicks, mined;
	private BlockPos target;
	private String targetName = "";
	private boolean pathAnnounced, emptyAnnounced, toolWarned;
	/** Ores already mined from the vein being worked on, and how many ores at the front of the last scan belong to that vein. */
	private final Set<Long> veinMined = new HashSet<>();
	private int veinFront;
	private final Map<Long, Integer> ignored = new HashMap<>();
	private final Set<Integer> ignoredItems = new HashSet<>();
	private String status = "";

	public AutoMine() {
		super("AutoMine", Category.WORLD, "Finds the ores from the Blocks list, walks the shortest path to the nearest one (digging if needed) and mines it.");
	}

	@Override
	protected void onEnable() {
		phase = Phase.SCAN;
		wait = 0;
		mined = 0;
		target = null;
		ignored.clear();
		ignoredItems.clear();
		veinMined.clear();
		veinFront = 0;
		pathAnnounced = false;
		emptyAnnounced = false;
		if (blocks.get().isEmpty()) {
			Chat.warn("AutoMine: the Blocks list is empty. Use .mine diamond_ore (or add blocks in the GUI / with .set AutoMine Blocks add <id>).");
		} else {
			msg("on. Looking for " + String.join(", ", blocks.get()) + " within " + range.i() + " blocks (loaded chunks only)");
		}
	}

	private void msg(String text) {
		if (chat.get()) Chat.info("AutoMine: " + text);
	}

	private String nameAt(BlockPos p) {
		return BuiltInRegistries.BLOCK.getKey(mc.level.getBlockState(p).getBlock()).getPath();
	}

	@Override
	protected void onDisable() {
		exec.cancel();
		Controls.releaseAll();
		target = null;
	}

	@Override
	public String info() {
		return status.isEmpty() ? null : status;
	}

	private void stop(String why) {
		Chat.info("AutoMine stopped: " + why + " (" + mined + " mined)");
		setEnabled(false);
	}

	@Override
	public void onTick() {
		LocalPlayer p = mc.player;
		if (p.getHealth() < minHealth.f()) {
			stop("health below " + minHealth.i());
			return;
		}
		if (stopFull.get() && p.getInventory().getFreeSlot() == -1 && phase != Phase.COLLECT) {
			stop("inventory full");
			return;
		}
		ignored.replaceAll((k, v) -> v - 1);
		ignored.values().removeIf(v -> v <= 0);

		exec.config.maxFall = maxFall.i();
		exec.config.allowDig = dig.get();
		exec.protectContainers = protect.get();
		exec.toolsFromInventory = fromInventory.get();

		switch (phase) {
			case SCAN -> scanTick(p);
			case PATH -> pathTick();
			case MINE -> mineTick(p);
			case COLLECT -> collectTick(p);
		}
	}

	private Set<Block> wanted() {
		Set<Block> out = new HashSet<>();
		for (String id : blocks.get()) {
			ResourceLocation rl = ResourceLocation.tryParse(id.contains(":") ? id : "minecraft:" + id);
			if (rl != null) BuiltInRegistries.BLOCK.getOptional(rl).ifPresent(out::add);
		}
		return out;
	}

	private void scanTick(LocalPlayer p) {
		if (wait > 0) {
			wait--;
			return;
		}
		Set<Block> want = wanted();
		if (want.isEmpty()) {
			status = "no valid blocks in the list";
			wait = 40;
			return;
		}
		// protection: a block whose drops need a tool that nothing in the inventory provides is not mined at all
		int slots = fromInventory.get() ? 36 : 9;
		Set<Block> blocked = new HashSet<>();
		for (Block b : want) if (!Miner.canHarvest(b.defaultBlockState(), slots)) blocked.add(b);
		if (!blocked.isEmpty()) {
			String names = blocked.stream().map(b -> BuiltInRegistries.BLOCK.getKey(b).getPath()).sorted().collect(java.util.stream.Collectors.joining(", "));
			if (blocked.size() == want.size()) {
				boolean anyPick = false;
				for (int i = 0; i < slots; i++) if (p.getInventory().items.get(i).is(net.minecraft.tags.ItemTags.PICKAXES)) anyPick = true;
				stop(anyPick ? "your pickaxe is too weak for " + names + " (it would break the block but drop nothing)"
						: "no pickaxe in the " + (fromInventory.get() ? "inventory" : "hotbar") + ", needed for " + names);
				return;
			}
			if (!toolWarned) msg("skipping " + names + ": no tool strong enough for the drops");
			toolWarned = true;
			want.removeAll(blocked);
		} else {
			toolWarned = false;
		}
		List<int[]> found = scan(p, want);
		if (found.isEmpty()) {
			status = "no ore within " + range.i() + " blocks";
			if (!emptyAnnounced) {
				emptyAnnounced = true;
				msg("no matching block within " + range.i() + " blocks of you. Only loaded chunks are searched: move on and it tries again.");
			}
			wait = 40;
			return;
		}
		emptyAnnounced = false;
		pathAnnounced = false;
		int[] near = found.get(0);
		msg(String.format("found %d%s target(s), nearest %s at %d %d %d, %.0f blocks away. Planning the path.", found.size(), found.size() >= 64 ? "+" : "",
				nameAt(new BlockPos(near[0], near[1], near[2])), near[0], near[1], near[2], Math.sqrt(d2(p, near))));
		exec.config.maxExpansions = 200000;
		exec.searchBudget = 2500;
		exec.walkPartial = true;
		exec.maxReplans = 60;
		// while a vein is being worked on only its ores are goals, so the planner cannot pick a cheaper ore elsewhere
		exec.start(new Goal.Blocks(veinFront > 0 ? new ArrayList<>(found.subList(0, Math.min(veinFront, found.size()))) : found, reach.d()));
		phase = Phase.PATH;
		status = "planning a path to " + found.size() + " target(s)";
	}

	private List<int[]> scan(LocalPlayer p, Set<Block> want) {
		List<int[]> out = BlockScan.scan(want, range.i());
		out.removeIf(t -> ignored.containsKey(BlockPos.asLong(t[0], t[1], t[2])) || avoidLava.get() && touchesLava(t[0], t[1], t[2]));
		out.sort((a, b) -> Double.compare(d2(p, a), d2(p, b)));
		// the vein just worked on comes first: every ore that touches (also diagonally) a mined one or another vein ore
		veinFront = 0;
		if (!veinMined.isEmpty()) {
			Set<Long> inVein = new HashSet<>(veinMined);
			Set<Long> taken = new HashSet<>();
			boolean grew = true;
			while (grew) {
				grew = false;
				for (int[] t : out) {
					long k = BlockPos.asLong(t[0], t[1], t[2]);
					if (taken.contains(k)) continue;
					if (touchesAny(inVein, t)) {
						taken.add(k);
						inVein.add(k);
						grew = true;
					}
				}
			}
			if (taken.isEmpty()) {
				veinMined.clear(); // the vein is finished
			} else {
				List<int[]> front = new ArrayList<>(), rest = new ArrayList<>();
				for (int[] t : out) (taken.contains(BlockPos.asLong(t[0], t[1], t[2])) ? front : rest).add(t);
				veinFront = front.size();
				front.addAll(rest);
				out = front;
			}
		}
		return out.size() > 64 ? new ArrayList<>(out.subList(0, 64)) : out;
	}

	private static boolean touchesAny(Set<Long> set, int[] t) {
		for (int dx = -1; dx <= 1; dx++)
			for (int dy = -1; dy <= 1; dy++)
				for (int dz = -1; dz <= 1; dz++) if ((dx | dy | dz) != 0 && set.contains(BlockPos.asLong(t[0] + dx, t[1] + dy, t[2] + dz))) return true;
		return false;
	}

	private static double d2(LocalPlayer p, int[] a) {
		double dx = a[0] + 0.5 - p.getX(), dy = a[1] + 0.5 - p.getY(), dz = a[2] + 0.5 - p.getZ();
		return dx * dx + dy * dy + dz * dz;
	}

	private boolean touchesLava(int x, int y, int z) {
		LevelTerrain t = new LevelTerrain(false);
		return t.kind(x + 1, y, z) == Kind.LAVA || t.kind(x - 1, y, z) == Kind.LAVA || t.kind(x, y + 1, z) == Kind.LAVA
				|| t.kind(x, y - 1, z) == Kind.LAVA || t.kind(x, y, z + 1) == Kind.LAVA || t.kind(x, y, z - 1) == Kind.LAVA;
	}

	private void pathTick() {
		exec.tick();
		switch (exec.state()) {
			case SEARCHING -> status = "planning the path";
			case WALKING -> {
				status = "walking: " + exec.stepsLeft() + " steps, " + exec.digsLeft() + " to dig";
				if (!pathAnnounced) {
					pathAnnounced = true;
					if (exec.pathCost() > 0) msg(String.format("path found: %d steps, %d blocks to dig, about %.0f s", exec.stepsLeft(), exec.digsLeft(), exec.pathCost() / 20.0));
					else msg("the target is far: walking towards it and planning again on the way");
				}
			}
			case ARRIVED -> {
				int[] h = exec.hit();
				if (h == null) {
					phase = Phase.SCAN;
					return;
				}
				target = new BlockPos(h[0], h[1], h[2]);
				targetName = nameAt(target);
				msg("at the target, mining " + targetName + " at " + target.toShortString());
				phase = Phase.MINE;
				mineTicks = 0;
				status = "mining " + targetName;
			}
			case FAILED -> {
				status = "no path: " + exec.message();
				msg("no path found (" + exec.message() + "). Trying other targets.");
				// remember which targets cannot be reached so the next scan tries others, then retry later
				if (mc.player != null) for (int[] t : scan(mc.player, wanted()).subList(0, Math.min(3, 64))) ignored.put(BlockPos.asLong(t[0], t[1], t[2]), 600);
				phase = Phase.SCAN;
				wait = 20;
			}
			default -> {
			}
		}
	}

	private void mineTick(LocalPlayer p) {
		if (target == null) {
			phase = Phase.SCAN;
			return;
		}
		BlockState s = mc.level.getBlockState(target);
		if (!wanted().contains(s.getBlock())) {
			Controls.releaseAll();
			mined++;
			status = "mined " + mined;
			msg("mined " + targetName + " (#" + mined + ")");
			veinMined.add(target.asLong());
			scan(p, wanted()); // refreshes veinFront: is there more of this vein?
			if (veinFront > 0) {
				phase = Phase.SCAN; // finish the vein first, the drops are collected at its end
				wait = 0;
				status = "mined " + mined + ", continuing the vein (" + veinFront + " more)";
			} else if (pickup.get()) {
				phase = Phase.COLLECT;
				collectTicks = 0;
				ignoredItems.clear();
			} else {
				phase = Phase.SCAN;
			}
			target = null;
			return;
		}
		// the tool may have broken while mining: stop instead of grinding a block that would drop nothing
		if (!Miner.canHarvest(s, fromInventory.get() ? 36 : 9)) {
			Controls.releaseAll();
			stop("no suitable tool left for " + targetName + " (the pickaxe broke or was moved)");
			return;
		}
		Controls.set(false, false, false, false);
		if (p.getEyePosition().distanceTo(net.minecraft.world.phys.Vec3.atCenterOf(target)) > reach.d() + 0.6) {
			phase = Phase.SCAN; // drifted out of reach: plan again
			return;
		}
		Miner.mine(target, fromInventory.get());
		if (++mineTicks > 400) {
			ignored.put(target.asLong(), 1200);
			status = "could not break " + target.toShortString();
			target = null;
			phase = Phase.SCAN;
		}
	}

	private void collectTick(LocalPlayer p) {
		if (++collectTicks > 160) {
			exec.cancel();
			phase = Phase.SCAN;
			return;
		}
		if (exec.active()) {
			exec.tick();
			status = "collecting drops";
			return;
		}
		ItemEntity best = null;
		double bestD = 36;
		for (ItemEntity e : mc.level.getEntitiesOfClass(ItemEntity.class, p.getBoundingBox().inflate(6.0))) {
			if (ignoredItems.contains(e.getId())) continue;
			double d = e.distanceToSqr(p);
			if (d < bestD) {
				bestD = d;
				best = e;
			}
		}
		if (best == null) {
			phase = Phase.SCAN;
			return;
		}
		ignoredItems.add(best.getId());
		BlockPos ip = best.blockPosition();
		exec.config.maxExpansions = 4000;
		exec.start(new Goal.Position(ip.getX(), ip.getY(), ip.getZ(), 1.0));
	}

	/** The block the module is heading for or mining: the ore at the end of the path. */
	private BlockPos goalCell() {
		if (target != null) return target;
		int[] h = exec.hit();
		return h == null ? null : new BlockPos(h[0], h[1], h[2]);
	}

	@Override
	public void onRender3d(WorldRenderContext ctx) {
		if (!showPath.get()) return;
		var steps = exec.steps();
		int from = exec.index();
		boolean walking = exec.state() == PathExecutor.State.WALKING && !steps.isEmpty();
		BlockPos goal = goalCell();
		if (!walking && goal == null) return;
		Render3d.begin(ctx, true);
		if (walking) {
			Vec3 prev = mc.player.position().add(0, 0.2, 0);
			for (int i = from; i < steps.size(); i++) {
				var st = steps.get(i);
				Vec3 cur = new Vec3(st.x() + 0.5, st.y() + 0.2, st.z() + 0.5);
				Render3d.line(prev, cur, i == from ? 0xFFFFFFFF : 0xFF4DD2FF);
				prev = cur;
			}
			int shown = 0;
			for (int i = from; i < steps.size() && i < from + 16 && shown < 40; i++) {
				int[] d = steps.get(i).dig();
				for (int k = 0; k < d.length && shown < 40; k += 3, shown++) {
					if (mc.level.getBlockState(new BlockPos(d[k], d[k + 1], d[k + 2])).isAir()) continue;
					Render3d.box(new AABB(d[k], d[k + 1], d[k + 2], d[k] + 1, d[k + 1] + 1, d[k + 2] + 1).deflate(0.05), 0xFFFF5555);
				}
			}
		}
		if (goal != null) Render3d.box(new AABB(goal).inflate(0.02), 0xFFFFE066);
		Render3d.end();
	}

	@Override
	public void onRender2d(GuiGraphics g, float partialTick) {
		if (!panel.get() || mc.options.hideGui || mc.screen != null) return;
		List<String> lines = new ArrayList<>();
		String filter = String.join(", ", blocks.get());
		lines.add("AutoMine: " + (filter.length() > 34 ? filter.substring(0, 33) + "..." : filter));
		lines.add(status.isEmpty() ? "starting" : status);
		BlockPos goal = goalCell();
		if (goal != null) lines.add(String.format("target %d %d %d, %.0f blocks", goal.getX(), goal.getY(), goal.getZ(), Math.sqrt(goal.distToCenterSqr(mc.player.position()))));
		lines.add("mined: " + mined);
		int w = 0;
		for (String l : lines) w = Math.max(w, mc.font.width(l));
		int x = mc.getWindow().getGuiScaledWidth() / 2 - w / 2, y = 18;
		g.fill(x - 5, y - 3, x + w + 5, y + 11 * lines.size() + 1, 0xA0000000);
		for (int i = 0; i < lines.size(); i++) g.drawString(mc.font, lines.get(i), x, y + 11 * i, i == 0 ? 0xFF7CF29A : 0xFFFFFFFF, true);
	}

	/** Command helper: `.mine <ids...>` replaces the filter, `.mine` alone just starts. */
	public void setBlocks(List<String> ids) {
		blocks.get().clear();
		for (String id : ids) blocks.add(id);
	}

	/** Block ids that look like ores in the registry, for tab completion hints. */
	public static List<String> knownOres() {
		List<String> out = new ArrayList<>();
		for (ResourceLocation rl : BuiltInRegistries.BLOCK.keySet()) {
			if (rl.getNamespace().equals("minecraft") && (rl.getPath().endsWith("_ore") || rl.getPath().equals("ancient_debris"))) out.add(rl.getPath());
		}
		java.util.Collections.sort(out);
		return out;
	}
}
