package dev.aero.client.mixin;

import dev.aero.client.Hooks;
import net.minecraft.client.Minecraft;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(Minecraft.class)
public abstract class MinecraftMixin {
	/** Milliseconds per game tick: a smaller value makes the game run faster (Timer). */
	@Inject(method = "getTickTargetMillis", at = @At("RETURN"), cancellable = true)
	private void aero$timer(float base, CallbackInfoReturnable<Float> cir) {
		if (Hooks.timerSpeed != 1.0 && Hooks.timerSpeed > 0) cir.setReturnValue((float) (cir.getReturnValue() / Hooks.timerSpeed));
	}
}
