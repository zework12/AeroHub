package dev.aero.client.mixin;

import com.mojang.blaze3d.vertex.PoseStack;
import dev.aero.client.Hooks;
import net.minecraft.client.Camera;
import net.minecraft.client.renderer.GameRenderer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(GameRenderer.class)
public abstract class GameRendererMixin {
	@Inject(method = "getFov", at = @At("RETURN"), cancellable = true)
	private void aero$fov(Camera camera, float partialTick, boolean fromOptions, CallbackInfoReturnable<Double> cir) {
		cir.setReturnValue(Hooks.adjustFov(cir.getReturnValue(), fromOptions));
	}

	@Inject(method = "bobHurt", at = @At("HEAD"), cancellable = true)
	private void aero$noHurtCam(PoseStack stack, float partialTick, CallbackInfo ci) {
		if (Hooks.noHurtCam) ci.cancel();
	}
}
