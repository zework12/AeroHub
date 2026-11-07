package dev.aero.client.mixin;

import dev.aero.client.Hooks;
import net.minecraft.client.gui.Gui;
import net.minecraft.client.gui.GuiGraphics;
import net.minecraft.resources.ResourceLocation;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(Gui.class)
public abstract class GuiMixin {
	@Inject(method = "renderTextureOverlay", at = @At("HEAD"), cancellable = true)
	private void aero$noPumpkin(GuiGraphics graphics, ResourceLocation texture, float alpha, CallbackInfo ci) {
		if (Hooks.noPumpkin && texture.getPath().contains("pumpkinblur")) ci.cancel();
	}
}
