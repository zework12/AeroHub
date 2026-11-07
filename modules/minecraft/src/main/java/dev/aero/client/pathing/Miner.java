package dev.aero.client.pathing;

import dev.aero.client.util.InventoryUtil;
import dev.aero.client.util.Rotations;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.phys.Vec3;

/** Breaks one block per call sequence through the game's own digging code (progress, cooldown, server packets). */
public final class Miner {
	private Miner() {
	}

	/**
	 * The best way to break a block with what the player carries. `index` is an inventory index (0-8 hotbar, 9-35 main),
	 * `progress` is the game's own per-tick destroy progress with that item, `harvests` says whether the block would
	 * drop its item (a wooden pickaxe breaks diamond ore, but it drops nothing).
	 */
	public record Pick(int index, float progress, boolean harvests) {
	}

	/** Picks among the hotbar only. */
	public static int bestSlot(BlockPos pos) {
		Minecraft mc = Minecraft.getInstance();
		return evaluate(mc.level.getBlockState(pos), pos, 9).index();
	}

	/**
	 * Ranks the first `slots` inventory slots (9 = hotbar, 36 = whole inventory) for breaking `state` at `pos`: items that
	 * make the block drop come first, then the fastest. The current slot wins ties, so the choice does not flip around.
	 */
	public static Pick evaluate(BlockState state, BlockPos pos, int slots) {
		Minecraft mc = Minecraft.getInstance();
		LocalPlayer p = mc.player;
		var items = p.getInventory().items;
		int sel = p.getInventory().selected;
		ItemStack original = items.get(sel);
		Pick best = null;
		try {
			for (int i = 0; i < slots; i++) {
				ItemStack stack = items.get(i);
				if (i >= 9 && stack.isEmpty()) continue;
				items.set(sel, stack); // the game computes the speed from the selected slot, so the candidate is put there for a moment
				float progress = state.getDestroyProgress(p, mc.level, pos);
				boolean harvests = !state.requiresCorrectToolForDrops() || stack.isCorrectToolForDrops(state);
				Pick cand = new Pick(i, progress, harvests);
				if (best == null || better(cand, best, sel)) best = cand;
				items.set(sel, original);
			}
		} finally {
			items.set(sel, original);
		}
		return best != null ? best : new Pick(sel, 0, !state.requiresCorrectToolForDrops());
	}

	private static boolean better(Pick a, Pick b, int selected) {
		if (a.harvests() != b.harvests()) return a.harvests();
		if (a.progress() != b.progress()) return a.progress() > b.progress();
		return a.index() == selected && b.index() != selected;
	}

	/** True when something in the first `slots` inventory slots makes the block drop its item. */
	public static boolean canHarvest(BlockState state, int slots) {
		return evaluate(state, BlockPos.ZERO, slots).harvests();
	}

	/** Puts the item at inventory index `index` into the selected hotbar slot (swapping, not losing anything) or selects the hotbar slot. */
	private static boolean equip(int index) {
		Minecraft mc = Minecraft.getInstance();
		LocalPlayer p = mc.player;
		if (index < 9) {
			if (index != p.getInventory().selected) p.getInventory().selected = index;
			return true;
		}
		if (mc.screen != null || !InventoryUtil.inventoryClosed()) return false;
		InventoryUtil.swapWithHotbar(index, p.getInventory().selected);
		return true;
	}

	/** Face of the block that points at the player's eyes. */
	public static Direction faceTowards(BlockPos pos, Vec3 eye) {
		double dx = eye.x - (pos.getX() + 0.5), dy = eye.y - (pos.getY() + 0.5), dz = eye.z - (pos.getZ() + 0.5);
		double ax = Math.abs(dx), ay = Math.abs(dy), az = Math.abs(dz);
		if (ay >= ax && ay >= az) return dy > 0 ? Direction.UP : Direction.DOWN;
		if (ax >= az) return dx > 0 ? Direction.EAST : Direction.WEST;
		return dz > 0 ? Direction.SOUTH : Direction.NORTH;
	}

	/** Turns the camera towards the block's centre, limited to `maxStep` degrees per call. */
	public static void lookAt(BlockPos pos, float maxStep) {
		LocalPlayer p = Minecraft.getInstance().player;
		Vec3 eye = p.getEyePosition();
		float[] rot = Rotations.to(pos.getX() + 0.5 - eye.x, pos.getY() + 0.5 - eye.y, pos.getZ() + 0.5 - eye.z);
		p.setYRot(Rotations.approach(p.getYRot(), rot[0], maxStep));
		p.setXRot(Rotations.approach(p.getXRot(), rot[1], maxStep));
	}

	/**
	 * One tick of digging `pos`. Returns true when the block no longer needs digging (already gone or just broken
	 * on an earlier tick); false while it is still being worked on.
	 */
	public static boolean mine(BlockPos pos) {
		return mine(pos, false);
	}

	/** As {@link #mine(BlockPos)}; with `fromInventory` the best tool may also come from the main inventory (swapped into the selected slot). */
	public static boolean mine(BlockPos pos, boolean fromInventory) {
		Minecraft mc = Minecraft.getInstance();
		LocalPlayer p = mc.player;
		BlockState state = mc.level.getBlockState(pos);
		if (state.isAir() || !state.getFluidState().isEmpty() && state.getCollisionShape(mc.level, pos).isEmpty()) return true;
		Pick pick = evaluate(state, pos, fromInventory ? 36 : 9);
		if (pick.index() != p.getInventory().selected && !equip(pick.index())) return false; // inventory screen open: wait
		lookAt(pos, 45.0f);
		Direction face = faceTowards(pos, p.getEyePosition());
		if (mc.gameMode.continueDestroyBlock(pos, face)) p.swing(InteractionHand.MAIN_HAND);
		return false;
	}
}
