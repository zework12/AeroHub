package dev.aero.client.mixin;

import dev.aero.client.Hooks;
import net.minecraft.client.Minecraft;
import net.minecraft.world.entity.player.Player;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(Player.class)
public abstract class PlayerMixin {
	private boolean aero$isLocal() {
		return (Object) this == Minecraft.getInstance().player;
	}

	@Inject(method = "isStayingOnGroundSurface", at = @At("HEAD"), cancellable = true)
	private void aero$safeWalk(CallbackInfoReturnable<Boolean> cir) {
		if (Hooks.safeWalk && aero$isLocal()) cir.setReturnValue(true);
	}

	@Inject(method = "entityInteractionRange", at = @At("RETURN"), cancellable = true)
	private void aero$entityReach(CallbackInfoReturnable<Double> cir) {
		if (Hooks.reachEntity != 0 && aero$isLocal()) cir.setReturnValue(cir.getReturnValue() + Hooks.reachEntity);
	}

	@Inject(method = "blockInteractionRange", at = @At("RETURN"), cancellable = true)
	private void aero$blockReach(CallbackInfoReturnable<Double> cir) {
		if (Hooks.reachBlock != 0 && aero$isLocal()) cir.setReturnValue(cir.getReturnValue() + Hooks.reachBlock);
	}
}
