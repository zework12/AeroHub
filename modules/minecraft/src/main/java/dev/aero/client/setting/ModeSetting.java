package dev.aero.client.setting;

import com.google.gson.JsonElement;
import com.google.gson.JsonPrimitive;

/** One choice out of a fixed list of names. */
public final class ModeSetting extends Setting<String> {
	public final String[] options;

	public ModeSetting(String name, String value, String... options) {
		super(name, value);
		this.options = options;
	}

	public boolean is(String option) {
		return value.equalsIgnoreCase(option);
	}

	public void next() {
		for (int i = 0; i < options.length; i++) {
			if (options[i].equalsIgnoreCase(value)) {
				value = options[(i + 1) % options.length];
				return;
			}
		}
		value = options[0];
	}

	@Override
	public JsonElement toJson() {
		return new JsonPrimitive(value);
	}

	@Override
	public void fromJson(JsonElement e) {
		if (e != null && e.isJsonPrimitive()) parse(e.getAsString());
	}

	@Override
	public String display() {
		return value;
	}

	@Override
	public boolean parse(String text) {
		for (String o : options) {
			if (o.equalsIgnoreCase(text)) {
				value = o;
				return true;
			}
		}
		return false;
	}
}
