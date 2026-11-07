package dev.aero.client.mixin;

import com.mojang.blaze3d.vertex.PoseStack;
import dev.aero.client.Hooks;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.ScreenEffectRenderer;
import net.minecraft.client.renderer.texture.TextureAtlasSprite;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(ScreenEffectRenderer.class)
public abstract class ScreenEffectRendererMixin {
	@Inject(method = "renderFire", at = @At("HEAD"), cancellable = true)
	private static void aero$noFire(Minecraft mc, PoseStack stack, CallbackInfo ci) {
		if (Hooks.noFireOverlay) ci.cancel();
	}

	@Inject(method = "renderWater", at = @At("HEAD"), cancellable = true)
	private static void aero$noWater(Minecraft mc, PoseStack stack, CallbackInfo ci) {
		if (Hooks.noWaterOverlay) ci.cancel();
	}

	@Inject(method = "renderTex", at = @At("HEAD"), cancellable = true)
	private static void aero$noWall(TextureAtlasSprite sprite, PoseStack stack, CallbackInfo ci) {
		if (Hooks.noWallOverlay) ci.cancel();
	}
}
