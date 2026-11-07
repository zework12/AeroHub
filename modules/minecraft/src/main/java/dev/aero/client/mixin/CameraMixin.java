package dev.aero.client.mixin;

import dev.aero.client.Hooks;
import net.minecraft.client.Camera;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(Camera.class)
public abstract class CameraMixin {
	@Shadow
	protected abstract void setPosition(Vec3 position);

	@Shadow
	protected abstract void setRotation(float yaw, float pitch);

	@Inject(method = "setup", at = @At("TAIL"))
	private void aero$freecam(BlockGetter level, Entity entity, boolean thirdPerson, boolean mirror, float partialTick, CallbackInfo ci) {
		if (Hooks.freecam) {
			setRotation(Hooks.camYaw, Hooks.camPitch);
			setPosition(Hooks.camPosPrev.lerp(Hooks.camPos, partialTick));
		}
	}
}
