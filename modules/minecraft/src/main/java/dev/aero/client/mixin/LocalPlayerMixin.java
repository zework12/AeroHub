package dev.aero.client.mixin;

import dev.aero.client.Hooks;
import net.minecraft.client.player.LocalPlayer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

@Mixin(LocalPlayer.class)
public abstract class LocalPlayerMixin {
	/** aiStep slows movement to 20% while an item is in use; NoSlow makes that check come out false. */
	@Redirect(method = "aiStep", at = @At(value = "INVOKE", target = "Lnet/minecraft/client/player/LocalPlayer;isUsingItem()Z"))
	private boolean aero$noSlow(LocalPlayer self) {
		return !Hooks.noSlow && self.isUsingItem();
	}
}
