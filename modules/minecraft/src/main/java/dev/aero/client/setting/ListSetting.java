package dev.aero.client.setting;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonPrimitive;

import java.util.LinkedHashSet;
import java.util.Set;

/** A set of registry ids (blocks, items, entity types), stored without the "minecraft:" prefix. */
public final class ListSetting extends Setting<Set<String>> {
	/** What the entries are: block ids, item ids, or free text (the GUI editor lists the registry for the first two). */
	public enum Kind { BLOCK, ITEM, TEXT }

	public final Kind kind;
	private final Set<String> initial = new LinkedHashSet<>();

	public ListSetting(String name, String... initial) {
		this(name, Kind.BLOCK, initial);
	}

	public ListSetting(String name, Kind kind, String... initial) {
		super(name, new LinkedHashSet<>());
		this.kind = kind;
		for (String s : initial) value.add(normalize(s));
		this.initial.addAll(value);
	}

	public static String normalize(String id) {
		String s = id.trim().toLowerCase();
		return s.startsWith("minecraft:") ? s.substring(10) : s;
	}

	public boolean contains(String id) {
		return value.contains(normalize(id));
	}

	public boolean add(String id) {
		return value.add(normalize(id));
	}

	public boolean remove(String id) {
		return value.remove(normalize(id));
	}

	/** The set the setting started with; `defaultValue` shares the live set, so it cannot be used for this. */
	@Override
	public void reset() {
		value.clear();
		value.addAll(initial);
	}

	@Override
	public JsonElement toJson() {
		JsonArray a = new JsonArray();
		for (String s : value) a.add(new JsonPrimitive(s));
		return a;
	}

	@Override
	public void fromJson(JsonElement e) {
		if (e == null || !e.isJsonArray()) return;
		value.clear();
		for (JsonElement x : e.getAsJsonArray()) if (x.isJsonPrimitive()) value.add(normalize(x.getAsString()));
	}

	@Override
	public String display() {
		return value.isEmpty() ? "(empty)" : String.join(", ", value);
	}

	/** Command syntax: "add id", "remove id", "clear". */
	@Override
	public boolean parse(String text) {
		String[] p = text.trim().split("\\s+", 2);
		switch (p[0].toLowerCase()) {
			case "add" -> {
				if (p.length < 2) return false;
				for (String id : p[1].split("[\\s,]+")) if (!id.isEmpty()) add(id);
			}
			case "remove", "del" -> {
				if (p.length < 2) return false;
				for (String id : p[1].split("[\\s,]+")) if (!id.isEmpty()) remove(id);
			}
			case "clear" -> value.clear();
			default -> {
				return false;
			}
		}
		return true;
	}
}
