package dev.aero.client.util;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;

import java.util.ArrayList;
import java.util.List;

/** Named positions, stored per dimension id. */
public final class Waypoints {
	public record Point(String name, String dimension, int x, int y, int z) {
	}

	private static final List<Point> POINTS = new ArrayList<>();

	private Waypoints() {
	}

	public static List<Point> all() {
		return POINTS;
	}

	public static void set(Point p) {
		remove(p.name());
		POINTS.add(p);
	}

	public static Point find(String name) {
		for (Point p : POINTS) if (p.name().equalsIgnoreCase(name)) return p;
		return null;
	}

	public static boolean remove(String name) {
		return POINTS.removeIf(p -> p.name().equalsIgnoreCase(name));
	}

	public static JsonElement toJson() {
		JsonArray a = new JsonArray();
		for (Point p : POINTS) {
			JsonObject o = new JsonObject();
			o.addProperty("name", p.name());
			o.addProperty("dimension", p.dimension());
			o.addProperty("x", p.x());
			o.addProperty("y", p.y());
			o.addProperty("z", p.z());
			a.add(o);
		}
		return a;
	}

	public static void fromJson(JsonElement e) {
		POINTS.clear();
		if (e == null || !e.isJsonArray()) return;
		for (JsonElement x : e.getAsJsonArray()) {
			if (!x.isJsonObject()) continue;
			JsonObject o = x.getAsJsonObject();
			try {
				POINTS.add(new Point(o.get("name").getAsString(), o.get("dimension").getAsString(), o.get("x").getAsInt(), o.get("y").getAsInt(), o.get("z").getAsInt()));
			} catch (RuntimeException ignored) {
				// a malformed entry is skipped, the rest still load
			}
		}
	}
}
