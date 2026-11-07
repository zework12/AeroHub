package dev.aero.client.mixin;

import net.minecraft.world.entity.LivingEntity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(LivingEntity.class)
public interface LivingEntityAccessor {
	@Accessor("noJumpDelay")
	void aero$setNoJumpDelay(int value);

	@Accessor("useItemRemaining")
	int aero$getUseItemRemaining();

	@Accessor("useItemRemaining")
	void aero$setUseItemRemaining(int value);
}
