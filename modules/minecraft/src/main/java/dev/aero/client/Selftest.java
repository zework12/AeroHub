package dev.aero.client;

import dev.aero.client.config.Config;
import dev.aero.client.module.Module;
import dev.aero.client.module.ModuleManager;
import dev.aero.client.setting.Setting;
import net.minecraft.client.Minecraft;
import org.spongepowered.asm.mixin.MixinEnvironment;

import java.io.IOException;
import java.nio.file.Files;
import java.util.HashSet;
import java.util.Set;

/**
 * Started by the `runSelftest` Gradle task (-Daero.selftest=true): applies every mixin, checks the module
 * registry and the config round trip, logs one PASSED/FAILED line and closes the game.
 */
final class Selftest {
	private Selftest() {
	}

	/** Waits for the title screen to settle before closing, so the game is not stopped in the middle of loading. */
	static void start(Minecraft mc) {
		int[] ticks = {0};
		net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents.END_CLIENT_TICK.register(client -> {
			if (++ticks[0] == 80) run(client);
		});
	}

	/** Writes aero-modules.md next to the game's config, one table per category, straight from the registry. */
	private static void writeModuleList() throws IOException {
		StringBuilder md = new StringBuilder();
		for (dev.aero.client.module.Category c : dev.aero.client.module.Category.values()) {
			md.append("### ").append(c.label).append("\n\n| Module | What it does |\n|---|---|\n");
			for (Module m : ModuleManager.in(c)) {
				if (m.hidden) continue;
				md.append("| ").append(m.name).append(" | ").append(m.description.replace("|", "/")).append(" |\n");
			}
			md.append("\n");
		}
		Files.writeString(net.fabricmc.loader.api.FabricLoader.getInstance().getGameDir().resolve("aero-modules.md"), md.toString());
		AeroClient.LOG.info("SELFTEST module list written");
	}

	private static void run(Minecraft mc) {
		boolean ok = true;
		try {
			MixinEnvironment.getCurrentEnvironment().audit();
			AeroClient.LOG.info("SELFTEST mixins: all applied");
		} catch (Throwable t) {
			ok = false;
			AeroClient.LOG.error("SELFTEST mixins: FAILED", t);
		}

		Set<String> names = new HashSet<>();
		for (Module m : ModuleManager.all()) {
			if (!names.add(m.name.toLowerCase())) {
				ok = false;
				AeroClient.LOG.error("SELFTEST duplicate module name {}", m.name);
			}
			Set<String> settingNames = new HashSet<>();
			for (Setting<?> s : m.settings) {
				if (!settingNames.add(s.name.toLowerCase())) {
					ok = false;
					AeroClient.LOG.error("SELFTEST {} has two settings named {}", m.name, s.name);
				}
			}
		}
		AeroClient.LOG.info("SELFTEST modules: {} registered ({} visible)", ModuleManager.all().size(), ModuleManager.all().stream().filter(m -> !m.hidden).count());

		try {
			writeModuleList();
		} catch (IOException | RuntimeException e) {
			ok = false;
			AeroClient.LOG.error("SELFTEST module list: FAILED", e);
		}

		try {
			boolean saved = Config.save("selftest");
			boolean loaded = Config.load("selftest", false);
			Files.deleteIfExists(Config.file("selftest"));
			if (!saved || !loaded) throw new IOException("save=" + saved + " load=" + loaded);
			AeroClient.LOG.info("SELFTEST config: round trip ok");
		} catch (IOException | RuntimeException e) {
			ok = false;
			AeroClient.LOG.error("SELFTEST config: FAILED", e);
		}

		AeroClient.LOG.info("SELFTEST {}", ok ? "PASSED" : "FAILED");
		mc.stop();
	}
}
