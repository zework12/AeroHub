package dev.aero.client.mixin;

import dev.aero.client.module.ModuleManager;
import dev.aero.client.util.PacketUtil;
import io.netty.channel.ChannelHandlerContext;
import net.minecraft.network.Connection;
import net.minecraft.network.PacketSendListener;
import net.minecraft.network.protocol.Packet;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(Connection.class)
public abstract class ConnectionMixin {
	@Inject(method = "send(Lnet/minecraft/network/protocol/Packet;Lnet/minecraft/network/PacketSendListener;Z)V", at = @At("HEAD"), cancellable = true)
	private void aero$send(Packet<?> packet, PacketSendListener listener, boolean flush, CallbackInfo ci) {
		if (!PacketUtil.bypass() && ModuleManager.packetSend(packet)) ci.cancel();
	}

	/** Runs on the network thread: modules must only look at the packet and hand work to the game thread. */
	@Inject(method = "channelRead0(Lio/netty/channel/ChannelHandlerContext;Lnet/minecraft/network/protocol/Packet;)V", at = @At("HEAD"), cancellable = true)
	private void aero$receive(ChannelHandlerContext ctx, Packet<?> packet, CallbackInfo ci) {
		if (ModuleManager.packetReceive(packet)) ci.cancel();
	}
}
