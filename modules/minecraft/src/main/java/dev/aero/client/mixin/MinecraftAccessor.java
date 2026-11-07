package dev.aero.client.mixin;

import net.minecraft.client.Minecraft;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(Minecraft.class)
public interface MinecraftAccessor {
	@Accessor("rightClickDelay")
	int aero$getRightClickDelay();

	@Accessor("rightClickDelay")
	void aero$setRightClickDelay(int value);
}
