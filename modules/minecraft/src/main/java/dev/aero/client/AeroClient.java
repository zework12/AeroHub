package dev.aero.client;

import dev.aero.client.command.Commands;
import dev.aero.client.config.Config;
import dev.aero.client.module.ModuleManager;
import dev.aero.client.module.ModuleRegistry;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientLifecycleEvents;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.fabricmc.fabric.api.client.message.v1.ClientSendMessageEvents;
import net.fabricmc.fabric.api.client.rendering.v1.HudRenderCallback;
import net.fabricmc.fabric.api.client.rendering.v1.WorldRenderEvents;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public final class AeroClient implements ClientModInitializer {
	public static final Logger LOG = LoggerFactory.getLogger("AeroClient");

	@Override
	public void onInitializeClient() {
		ModuleRegistry.registerAll();
		Config.load("", false);

		ClientTickEvents.END_CLIENT_TICK.register(client -> {
			ModuleManager.tick();
			LiveStatus.tick(client);
		});
		WorldRenderEvents.LAST.register(ModuleManager::render3d);
		HudRenderCallback.EVENT.register((graphics, tick) -> ModuleManager.render2d(graphics, tick.getGameTimeDeltaPartialTick(true)));
		ClientSendMessageEvents.ALLOW_CHAT.register(message -> !Commands.handle(message));
		ClientLifecycleEvents.CLIENT_STOPPING.register(client -> {
			LiveStatus.remove();
			if (!Boolean.getBoolean("aero.selftest") && !Boolean.getBoolean("aero.worldtest")) Config.save("");
		});
		if (Boolean.getBoolean("aero.selftest")) ClientLifecycleEvents.CLIENT_STARTED.register(Selftest::start);
		if (Boolean.getBoolean("aero.worldtest")) ClientLifecycleEvents.CLIENT_STARTED.register(WorldTest::start);

		LOG.info("AeroClient loaded with {} modules", ModuleManager.all().size());
	}
}
