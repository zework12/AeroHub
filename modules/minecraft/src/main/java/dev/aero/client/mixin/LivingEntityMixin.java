package dev.aero.client.mixin;

import dev.aero.client.Hooks;
import net.minecraft.client.Minecraft;
import net.minecraft.core.Holder;
import net.minecraft.world.effect.MobEffect;
import net.minecraft.world.effect.MobEffects;
import net.minecraft.world.entity.LivingEntity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(LivingEntity.class)
public abstract class LivingEntityMixin {
	/** The renderer asks hasEffect for blindness / darkness / nausea; AntiBlind answers no for the local player. */
	@Inject(method = "hasEffect", at = @At("HEAD"), cancellable = true)
	private void aero$antiBlind(Holder<MobEffect> effect, CallbackInfoReturnable<Boolean> cir) {
		if (Hooks.antiBlind && (Object) this == Minecraft.getInstance().player
				&& (effect.is(MobEffects.BLINDNESS) || effect.is(MobEffects.DARKNESS) || effect.is(MobEffects.CONFUSION))) {
			cir.setReturnValue(false);
		}
	}
}
