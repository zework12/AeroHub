package dev.aero.client.util;

import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.InteractionResult;
import net.minecraft.world.item.BlockItem;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.EntityBlock;
import net.minecraft.world.level.block.FallingBlock;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.Vec3;

public final class BlockUtil {
	private BlockUtil() {
	}

	/** Hotbar slot holding a plain full block that is safe to build with, or -1. */
	public static int findBuildingBlock() {
		return InventoryUtil.findHotbar(BlockUtil::isBuildingBlock);
	}

	public static boolean isBuildingBlock(ItemStack s) {
		if (s.isEmpty() || !(s.getItem() instanceof BlockItem bi)) return false;
		Block b = bi.getBlock();
		if (b instanceof FallingBlock || b instanceof EntityBlock) return false;
		return b.defaultBlockState().isCollisionShapeFullBlock(Minecraft.getInstance().level, BlockPos.ZERO);
	}

	/**
	 * Places the block in the main hand at `pos` by clicking a neighbouring block face. Returns true when the
	 * server-side action was accepted by the client (the usual use-item result), false when there is no surface
	 * to click or `pos` is occupied.
	 */
	public static boolean place(BlockPos pos, boolean swing) {
		Minecraft mc = Minecraft.getInstance();
		ClientLevel level = mc.level;
		if (!level.getBlockState(pos).canBeReplaced()) return false;
		for (Direction d : Direction.values()) {
			BlockPos neighbour = pos.relative(d);
			BlockState s = level.getBlockState(neighbour);
			if (s.isAir() || s.canBeReplaced() || !s.getFluidState().isEmpty()) continue;
			Direction face = d.getOpposite();
			Vec3 hitVec = Vec3.atCenterOf(neighbour).add(Vec3.atLowerCornerOf(face.getNormal()).scale(0.5));
			BlockHitResult hit = new BlockHitResult(hitVec, face, neighbour, false);
			InteractionResult r = mc.gameMode.useItemOn(mc.player, InteractionHand.MAIN_HAND, hit);
			if (r.consumesAction()) {
				if (swing) mc.player.swing(InteractionHand.MAIN_HAND);
				return true;
			}
		}
		return false;
	}
}
