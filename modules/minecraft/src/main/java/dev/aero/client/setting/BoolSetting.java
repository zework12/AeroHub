package dev.aero.client.setting;

import com.google.gson.JsonElement;
import com.google.gson.JsonPrimitive;

public final class BoolSetting extends Setting<Boolean> {
	public BoolSetting(String name, boolean value) {
		super(name, value);
	}

	public void toggle() {
		value = !value;
	}

	@Override
	public JsonElement toJson() {
		return new JsonPrimitive(value);
	}

	@Override
	public void fromJson(JsonElement e) {
		if (e != null && e.isJsonPrimitive() && e.getAsJsonPrimitive().isBoolean()) value = e.getAsBoolean();
	}

	@Override
	public String display() {
		return value ? "on" : "off";
	}

	@Override
	public boolean parse(String text) {
		switch (text.toLowerCase()) {
			case "on", "true", "1", "yes" -> value = true;
			case "off", "false", "0", "no" -> value = false;
			case "toggle" -> value = !value;
			default -> {
				return false;
			}
		}
		return true;
	}
}
