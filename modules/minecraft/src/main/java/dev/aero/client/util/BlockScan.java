package dev.aero.client.util;

import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.chunk.ChunkAccess;
import net.minecraft.world.level.chunk.LevelChunkSection;
import net.minecraft.world.level.chunk.status.ChunkStatus;

import java.util.ArrayList;
import java.util.List;
import java.util.Set;

/** Finds blocks of given types in the loaded chunks around the player, skipping chunk sections that cannot contain them. */
public final class BlockScan {
	private BlockScan() {
	}

	/** All positions {x, y, z} of the wanted blocks within `range` blocks of the player, in no particular order. */
	public static List<int[]> scan(Set<Block> want, int range) {
		Minecraft mc = Minecraft.getInstance();
		ClientLevel level = mc.level;
		LocalPlayer p = mc.player;
		List<int[]> out = new ArrayList<>();
		if (want.isEmpty()) return out;
		double r2 = (double) range * range;
		int cx0 = ((int) Math.floor(p.getX()) - range) >> 4, cx1 = ((int) Math.floor(p.getX()) + range) >> 4;
		int cz0 = ((int) Math.floor(p.getZ()) - range) >> 4, cz1 = ((int) Math.floor(p.getZ()) + range) >> 4;
		for (int cx = cx0; cx <= cx1; cx++) {
			for (int cz = cz0; cz <= cz1; cz++) {
				ChunkAccess chunk = level.getChunk(cx, cz, ChunkStatus.FULL, false);
				if (chunk == null) continue;
				for (int si = 0; si < chunk.getSectionsCount(); si++) {
					LevelChunkSection sec = chunk.getSection(si);
					if (sec.hasOnlyAir() || !sec.maybeHas(s -> want.contains(s.getBlock()))) continue;
					int baseY = level.getSectionYFromSectionIndex(si) << 4;
					for (int ly = 0; ly < 16; ly++) {
						for (int lz = 0; lz < 16; lz++) {
							for (int lx = 0; lx < 16; lx++) {
								if (!want.contains(sec.getBlockState(lx, ly, lz).getBlock())) continue;
								int x = (cx << 4) + lx, y = baseY + ly, z = (cz << 4) + lz;
								double dx = x + 0.5 - p.getX(), dy = y + 0.5 - p.getY(), dz = z + 0.5 - p.getZ();
								if (dx * dx + dy * dy + dz * dz <= r2) out.add(new int[] {x, y, z});
							}
						}
					}
				}
			}
		}
		return out;
	}
}
