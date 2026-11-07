package dev.aero.client.mixin;

import dev.aero.client.Hooks;
import net.minecraft.client.player.Input;
import net.minecraft.client.player.KeyboardInput;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(KeyboardInput.class)
public abstract class KeyboardInputMixin {
	/** While Freecam moves the camera, the player itself gets no input. */
	@Inject(method = "tick", at = @At("TAIL"))
	private void aero$freecamInput(boolean slow, float factor, CallbackInfo ci) {
		if (!Hooks.freecam) return;
		Input in = (Input) (Object) this;
		in.up = in.down = in.left = in.right = in.jumping = in.shiftKeyDown = false;
		in.forwardImpulse = 0;
		in.leftImpulse = 0;
	}
}
