package dev.aero.client.mixin;

import dev.aero.client.Hooks;
import net.minecraft.client.Minecraft;
import net.minecraft.world.entity.Entity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(Entity.class)
public abstract class EntityMixin {
	private boolean aero$isLocal() {
		return (Object) this == Minecraft.getInstance().player;
	}

	@Inject(method = "getPickRadius", at = @At("RETURN"), cancellable = true)
	private void aero$hitbox(CallbackInfoReturnable<Float> cir) {
		if (Hooks.hitboxExpand > 0 && !aero$isLocal()) cir.setReturnValue(cir.getReturnValue() + Hooks.hitboxExpand);
	}

	@Inject(method = "push(DDD)V", at = @At("HEAD"), cancellable = true)
	private void aero$noPush(double x, double y, double z, CallbackInfo ci) {
		if (Hooks.noPush && aero$isLocal()) ci.cancel();
	}

	@Inject(method = "isPushedByFluid", at = @At("HEAD"), cancellable = true)
	private void aero$noFluidPush(CallbackInfoReturnable<Boolean> cir) {
		if (Hooks.noPush && aero$isLocal()) cir.setReturnValue(false);
	}

	/** With Freecam on, the mouse moves the camera instead of the player. */
	@Inject(method = "turn", at = @At("HEAD"), cancellable = true)
	private void aero$freecamTurn(double yaw, double pitch, CallbackInfo ci) {
		if (Hooks.freecam && aero$isLocal()) {
			Hooks.camYaw += (float) yaw * 0.15f;
			Hooks.camPitch = Math.max(-90f, Math.min(90f, Hooks.camPitch + (float) pitch * 0.15f));
			ci.cancel();
		}
	}
}
