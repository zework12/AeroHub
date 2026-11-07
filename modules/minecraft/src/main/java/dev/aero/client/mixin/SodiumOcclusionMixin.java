package dev.aero.client.mixin;

import dev.aero.client.Hooks;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.level.block.state.BlockState;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * X-Ray with Sodium installed: Sodium meshes chunks with its own face culling instead of Block.shouldRenderFace.
 * Written against Sodium 0.8.13 for 1.21.1; when Sodium is absent or the method is gone this mixin does nothing.
 */
@Pseudo
@Mixin(targets = "net.caffeinemc.mods.sodium.client.render.chunk.compile.pipeline.BlockOcclusionCache", remap = false)
public abstract class SodiumOcclusionMixin {
	@Inject(method = "shouldDrawSide", at = @At("HEAD"), cancellable = true, remap = false, require = 0)
	private void aero$xrayFace(BlockState state, BlockGetter level, BlockPos pos, Direction face, CallbackInfoReturnable<Boolean> cir) {
		if (Hooks.xray && Hooks.xrayBlocks.contains(state.getBlock())) cir.setReturnValue(true);
	}
}
