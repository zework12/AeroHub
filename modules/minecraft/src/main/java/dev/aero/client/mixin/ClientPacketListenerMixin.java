package dev.aero.client.mixin;

import dev.aero.client.Hooks;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientPacketListener;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.network.protocol.game.ClientboundExplodePacket;
import net.minecraft.network.protocol.game.ClientboundPlayerPositionPacket;
import net.minecraft.network.protocol.game.ClientboundSetEntityMotionPacket;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * Packet handlers run twice (network thread, then game thread); only the game-thread pass is touched.
 * Knockback is scaled by comparing the player's motion before and after the vanilla handler.
 */
@Mixin(ClientPacketListener.class)
public abstract class ClientPacketListenerMixin {
	@Unique
	private Vec3 aero$motionBefore;
	@Unique
	private float aero$yawBefore, aero$pitchBefore;

	@Unique
	private static boolean aero$gameThread() {
		Minecraft mc = Minecraft.getInstance();
		return mc.isSameThread() && mc.player != null;
	}

	@Unique
	private void aero$scaleKnockback() {
		if (aero$motionBefore == null) return;
		LocalPlayer p = Minecraft.getInstance().player;
		if (p != null && (Hooks.knockbackHorizontal != 1.0 || Hooks.knockbackVertical != 1.0)) {
			Vec3 now = p.getDeltaMovement();
			p.setDeltaMovement(aero$motionBefore.x + (now.x - aero$motionBefore.x) * Hooks.knockbackHorizontal,
					aero$motionBefore.y + (now.y - aero$motionBefore.y) * Hooks.knockbackVertical,
					aero$motionBefore.z + (now.z - aero$motionBefore.z) * Hooks.knockbackHorizontal);
		}
		aero$motionBefore = null;
	}

	@Inject(method = "handleSetEntityMotion", at = @At("HEAD"))
	private void aero$motionHead(ClientboundSetEntityMotionPacket packet, CallbackInfo ci) {
		aero$motionBefore = aero$gameThread() && packet.getId() == Minecraft.getInstance().player.getId() ? Minecraft.getInstance().player.getDeltaMovement() : null;
	}

	@Inject(method = "handleSetEntityMotion", at = @At("RETURN"))
	private void aero$motionTail(ClientboundSetEntityMotionPacket packet, CallbackInfo ci) {
		aero$scaleKnockback();
	}

	@Inject(method = "handleExplosion", at = @At("HEAD"))
	private void aero$explodeHead(ClientboundExplodePacket packet, CallbackInfo ci) {
		aero$motionBefore = aero$gameThread() ? Minecraft.getInstance().player.getDeltaMovement() : null;
	}

	@Inject(method = "handleExplosion", at = @At("RETURN"))
	private void aero$explodeTail(ClientboundExplodePacket packet, CallbackInfo ci) {
		aero$scaleKnockback();
	}

	@Inject(method = "handleMovePlayer", at = @At("HEAD"))
	private void aero$teleportHead(ClientboundPlayerPositionPacket packet, CallbackInfo ci) {
		if (aero$gameThread()) {
			LocalPlayer p = Minecraft.getInstance().player;
			aero$yawBefore = p.getYRot();
			aero$pitchBefore = p.getXRot();
		}
	}

	@Inject(method = "handleMovePlayer", at = @At("RETURN"))
	private void aero$teleportTail(ClientboundPlayerPositionPacket packet, CallbackInfo ci) {
		if (Hooks.noRotate && aero$gameThread()) {
			LocalPlayer p = Minecraft.getInstance().player;
			p.setYRot(aero$yawBefore);
			p.setXRot(aero$pitchBefore);
		}
	}
}
