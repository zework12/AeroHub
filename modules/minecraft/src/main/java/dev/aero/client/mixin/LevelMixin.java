package dev.aero.client.mixin;

import dev.aero.client.Hooks;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.world.level.Level;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(Level.class)
public abstract class LevelMixin {
	@Inject(method = "getRainLevel", at = @At("HEAD"), cancellable = true)
	private void aero$noRain(float partialTick, CallbackInfoReturnable<Float> cir) {
		if (Hooks.noWeather && (Object) this instanceof ClientLevel) cir.setReturnValue(0.0f);
	}

	@Inject(method = "getThunderLevel", at = @At("HEAD"), cancellable = true)
	private void aero$noThunder(float partialTick, CallbackInfoReturnable<Float> cir) {
		if (Hooks.noWeather && (Object) this instanceof ClientLevel) cir.setReturnValue(0.0f);
	}
}
