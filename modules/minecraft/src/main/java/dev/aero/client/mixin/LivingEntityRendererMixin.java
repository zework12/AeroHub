package dev.aero.client.mixin;

import dev.aero.client.Hooks;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.entity.LivingEntityRenderer;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.player.Player;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(LivingEntityRenderer.class)
public abstract class LivingEntityRendererMixin {
	@Inject(method = "shouldShowName", at = @At("HEAD"), cancellable = true)
	private void aero$nametags(LivingEntity entity, CallbackInfoReturnable<Boolean> cir) {
		if (Hooks.nametags && entity instanceof Player && entity != Minecraft.getInstance().player) cir.setReturnValue(true);
	}
}
