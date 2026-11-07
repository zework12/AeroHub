package dev.aero.client.mixin;

import net.minecraft.client.multiplayer.MultiPlayerGameMode;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(MultiPlayerGameMode.class)
public interface MultiPlayerGameModeAccessor {
	@Accessor("destroyDelay")
	void aero$setDestroyDelay(int value);

	@Accessor("destroyProgress")
	float aero$getDestroyProgress();

	@Accessor("destroyProgress")
	void aero$setDestroyProgress(float value);
}
