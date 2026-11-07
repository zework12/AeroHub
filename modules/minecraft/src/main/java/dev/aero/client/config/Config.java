package dev.aero.client.config;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import dev.aero.client.AeroClient;
import dev.aero.client.module.Module;
import dev.aero.client.module.ModuleManager;
import dev.aero.client.setting.Setting;
import dev.aero.client.util.Friends;
import dev.aero.client.util.Waypoints;
import net.fabricmc.loader.api.FabricLoader;

import java.io.IOException;
import java.io.Reader;
import java.io.Writer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;

/** One JSON file in the game's config folder holding every module's state, key, settings, friends and waypoints. */
public final class Config {
	private static final Gson GSON = new GsonBuilder().setPrettyPrinting().create();

	private Config() {
	}

	public static Path file(String profile) {
		return FabricLoader.getInstance().getConfigDir().resolve("aeroclient" + (profile.isEmpty() ? "" : "-" + profile) + ".json");
	}

	public static boolean validProfile(String name) {
		return name.matches("[A-Za-z0-9_-]{0,32}");
	}

	public static boolean save(String profile) {
		JsonObject root = new JsonObject();
		JsonObject mods = new JsonObject();
		for (Module m : ModuleManager.all()) {
			JsonObject o = new JsonObject();
			o.addProperty("enabled", m.isEnabled() && !m.hidden);
			o.addProperty("key", m.key());
			JsonObject s = new JsonObject();
			for (Setting<?> st : m.settings) s.add(st.name, st.toJson());
			o.add("settings", s);
			mods.add(m.name, o);
		}
		root.add("modules", mods);
		JsonArray friends = new JsonArray();
		for (String f : Friends.all()) friends.add(new JsonPrimitive(f));
		root.add("friends", friends);
		root.add("waypoints", Waypoints.toJson());

		Path target = file(profile);
		Path tmp = target.resolveSibling(target.getFileName() + ".tmp");
		try {
			Files.createDirectories(target.getParent());
			try (Writer w = Files.newBufferedWriter(tmp, StandardCharsets.UTF_8)) {
				GSON.toJson(root, w);
			}
			Files.move(tmp, target, StandardCopyOption.REPLACE_EXISTING);
			return true;
		} catch (IOException e) {
			AeroClient.LOG.error("Could not save config {}", target, e);
			return false;
		}
	}

	public static boolean load(String profile, boolean applyEnabled) {
		Path target = file(profile);
		if (!Files.isRegularFile(target)) return false;
		try (Reader r = Files.newBufferedReader(target, StandardCharsets.UTF_8)) {
			JsonElement parsed = GSON.fromJson(r, JsonElement.class);
			if (parsed == null || !parsed.isJsonObject()) return false;
			JsonObject root = parsed.getAsJsonObject();
			JsonObject mods = root.has("modules") && root.get("modules").isJsonObject() ? root.getAsJsonObject("modules") : new JsonObject();
			for (Module m : ModuleManager.all()) {
				JsonElement e = mods.get(m.name);
				if (e == null || !e.isJsonObject()) continue;
				JsonObject o = e.getAsJsonObject();
				if (o.has("key") && o.get("key").isJsonPrimitive()) m.setKey(o.get("key").getAsInt());
				if (o.has("settings") && o.get("settings").isJsonObject()) {
					JsonObject s = o.getAsJsonObject("settings");
					for (Setting<?> st : m.settings) if (s.has(st.name)) st.fromJson(s.get(st.name));
				}
				if (!m.hidden && o.has("enabled") && o.get("enabled").isJsonPrimitive()) {
					boolean on = o.get("enabled").getAsBoolean();
					if (applyEnabled) m.setEnabled(on);
					else if (m.persist) m.restoreEnabled(on);
				}
			}
			Friends.clear();
			if (root.has("friends") && root.get("friends").isJsonArray()) for (JsonElement f : root.getAsJsonArray("friends")) Friends.add(f.getAsString());
			if (root.has("waypoints")) Waypoints.fromJson(root.get("waypoints"));
			return true;
		} catch (IOException | RuntimeException e) {
			AeroClient.LOG.error("Could not read config {}", target, e);
			return false;
		}
	}
}
