package dev.aero.client.pathing;

import dev.aero.client.path.Kind;
import dev.aero.client.path.Terrain;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.core.BlockPos;
import net.minecraft.tags.FluidTags;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.FallingBlock;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.material.FluidState;
import net.minecraft.world.phys.shapes.VoxelShape;

import java.util.IdentityHashMap;

/** {@link Terrain} over the client level. Build one per search; it caches dig times per block state. */
public final class LevelTerrain implements Terrain {
	private final ClientLevel level;
	private final BlockPos.MutableBlockPos pos = new BlockPos.MutableBlockPos();
	private final IdentityHashMap<BlockState, Double> digCache = new IdentityHashMap<>();
	private final boolean protectContainers;

	private final int toolSlots;

	public LevelTerrain(boolean protectContainers) {
		this(protectContainers, false);
	}

	public LevelTerrain(boolean protectContainers, boolean toolsFromInventory) {
		this.level = Minecraft.getInstance().level;
		this.protectContainers = protectContainers;
		this.toolSlots = toolsFromInventory ? 36 : 9;
	}

	private BlockState state(int x, int y, int z) {
		pos.set(x, y, z);
		return level.getBlockState(pos);
	}

	@Override
	public Kind kind(int x, int y, int z) {
		if (y < level.getMinBuildHeight() || y >= level.getMaxBuildHeight()) return Kind.UNKNOWN;
		if (!level.hasChunkAt(pos.set(x, y, z))) return Kind.UNKNOWN;
		BlockState s = level.getBlockState(pos);
		return classify(s, pos);
	}

	private Kind classify(BlockState s, BlockPos p) {
		if (s.isAir()) return Kind.AIR;
		Block b = s.getBlock();
		if (b == Blocks.FIRE || b == Blocks.SOUL_FIRE || b == Blocks.CACTUS || b == Blocks.MAGMA_BLOCK || b == Blocks.SWEET_BERRY_BUSH
				|| b == Blocks.WITHER_ROSE || b == Blocks.COBWEB || b == Blocks.POWDER_SNOW || b == Blocks.CAMPFIRE || b == Blocks.SOUL_CAMPFIRE
				|| b == Blocks.POINTED_DRIPSTONE || b == Blocks.BUBBLE_COLUMN) {
			return Kind.DANGER;
		}
		FluidState f = s.getFluidState();
		if (!f.isEmpty()) {
			if (f.is(FluidTags.LAVA)) return Kind.LAVA;
			if (s.getCollisionShape(level, p).isEmpty()) return Kind.WATER;
		}
		VoxelShape shape = s.getCollisionShape(level, p);
		if (shape.isEmpty()) return Kind.AIR;
		if (shape.max(net.minecraft.core.Direction.Axis.Y) > 1.0) return Kind.UNKNOWN; // fences, walls, gates: neither standable nor passable
		return Kind.SOLID;
	}

	@Override
	public double digTicks(int x, int y, int z) {
		BlockState s = state(x, y, z);
		if (s.getDestroySpeed(level, pos) < 0) return Double.POSITIVE_INFINITY;
		if (protectContainers && s.hasBlockEntity()) return Double.POSITIVE_INFINITY;
		Double c = digCache.get(s);
		if (c != null) return c;
		double ticks = bestTicks(s);
		digCache.put(s, ticks);
		return ticks;
	}

	private double bestTicks(BlockState s) {
		float best = Miner.evaluate(s, pos, toolSlots).progress();
		if (best <= 0) return Double.POSITIVE_INFINITY;
		return Math.max(1.0, Math.ceil(1.0 / best));
	}

	@Override
	public boolean falls(int x, int y, int z) {
		return state(x, y, z).getBlock() instanceof FallingBlock;
	}
}
