package dev.aero.client.mixin;

import dev.aero.client.Hooks;
import net.minecraft.world.level.block.RenderShape;
import net.minecraft.world.level.block.state.BlockBehaviour;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(BlockBehaviour.BlockStateBase.class)
public abstract class BlockStateBaseMixin {
	/** X-Ray: every block that is not on the list is left out of the chunk meshes (vanilla and Sodium both ask this). */
	@Inject(method = "getRenderShape", at = @At("HEAD"), cancellable = true)
	private void aero$xrayShape(CallbackInfoReturnable<RenderShape> cir) {
		BlockBehaviour.BlockStateBase self = (BlockBehaviour.BlockStateBase) (Object) this;
		if (Hooks.xray && !self.isAir() && !Hooks.xrayBlocks.contains(self.getBlock())) cir.setReturnValue(RenderShape.INVISIBLE);
	}
}
