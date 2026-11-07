package dev.aero.client.pathing;

import net.minecraft.client.Minecraft;
import net.minecraft.client.Options;

/** Presses the movement keys in the game's own key mappings, so the normal movement code does the walking. */
public final class Controls {
	private Controls() {
	}

	public static void set(boolean forward, boolean jump, boolean sprint, boolean sneak) {
		Options o = Minecraft.getInstance().options;
		o.keyUp.setDown(forward);
		o.keyJump.setDown(jump);
		o.keySprint.setDown(sprint);
		o.keyShift.setDown(sneak);
	}

	public static void releaseAll() {
		Options o = Minecraft.getInstance().options;
		o.keyUp.setDown(false);
		o.keyDown.setDown(false);
		o.keyLeft.setDown(false);
		o.keyRight.setDown(false);
		o.keyJump.setDown(false);
		o.keySprint.setDown(false);
		o.keyShift.setDown(false);
		o.keyAttack.setDown(false);
	}
}
