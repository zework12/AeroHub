package dev.aero.client;

import dev.aero.client.module.Module;
import dev.aero.client.module.ModuleManager;
import dev.aero.client.module.impl.AutoMine;
import net.fabricmc.loader.api.FabricLoader;
import net.minecraft.client.Minecraft;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.List;

/**
 * Writes `aeroclient-live.txt` into the game folder once a second as key=value lines, so the AeroHub app can tell
 * that the mod is loaded in a running game and show what it is doing. The file is removed when the game closes.
 */
public final class LiveStatus {
	static final String FILE = "aeroclient-live.txt";
	private static final long PID = ProcessHandle.current().pid();
	private static final String VERSION = FabricLoader.getInstance().getModContainer("aeroclient").map(c -> c.getMetadata().getVersion().getFriendlyString()).orElse("?");
	private static int ticks;

	private LiveStatus() {
	}

	static Path file() {
		return FabricLoader.getInstance().getGameDir().resolve(FILE);
	}

	static void tick(Minecraft mc) {
		if (++ticks % 20 != 1) return;
		write(mc);
	}

	static void write(Minecraft mc) {
		List<String> on = new ArrayList<>();
		for (Module m : ModuleManager.all()) if (m.isEnabled() && !m.hidden) on.add(m.name);
		boolean inWorld = mc.player != null && mc.level != null;
		AutoMine mine = ModuleManager.get(AutoMine.class);
		StringBuilder sb = new StringBuilder();
		sb.append("pid=").append(PID).append('\n');
		sb.append("version=").append(VERSION).append('\n');
		sb.append("world=").append(inWorld ? "yes" : "no").append('\n');
		if (inWorld) {
			sb.append("dimension=").append(mc.level.dimension().location()).append('\n');
			sb.append("position=").append((int) Math.floor(mc.player.getX())).append(' ').append((int) Math.floor(mc.player.getY())).append(' ').append((int) Math.floor(mc.player.getZ())).append('\n');
		}
		sb.append("fps=").append(mc.getFps()).append('\n');
		sb.append("modules=").append(on.size()).append(" of ").append(ModuleManager.all().stream().filter(m -> !m.hidden).count()).append('\n');
		sb.append("enabled=").append(on.isEmpty() ? "none" : String.join(", ", on)).append('\n');
		if (mine != null && mine.isEnabled()) sb.append("automine=").append(mine.info() == null ? "on" : mine.info()).append('\n');
		Path target = file();
		Path tmp = target.resolveSibling(FILE + ".tmp");
		try {
			Files.writeString(tmp, sb.toString(), StandardCharsets.UTF_8);
			Files.move(tmp, target, StandardCopyOption.REPLACE_EXISTING);
		} catch (IOException e) {
			// the hub may be reading the file this very moment; the next second writes it again
		}
	}

	static void remove() {
		try {
			Files.deleteIfExists(file());
		} catch (IOException ignored) {
			// nothing useful to do while the game is closing
		}
	}
}
