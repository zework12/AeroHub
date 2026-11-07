package dev.aero.client.mixin;

import dev.aero.client.Hooks;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.state.BlockState;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(Block.class)
public abstract class BlockMixin {
	/** X-Ray, vanilla chunk renderer: a listed block draws all its faces, even when stone touches it. */
	@Inject(method = "shouldRenderFace", at = @At("HEAD"), cancellable = true)
	private static void aero$xrayFace(BlockState state, BlockGetter level, BlockPos pos, Direction face, BlockPos neighbour, CallbackInfoReturnable<Boolean> cir) {
		if (Hooks.xray && Hooks.xrayBlocks.contains(state.getBlock())) cir.setReturnValue(true);
	}
}
