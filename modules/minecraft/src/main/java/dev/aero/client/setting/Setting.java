package dev.aero.client.setting;

import com.google.gson.JsonElement;

/** One named option of a module. Values are stored in the config file as JSON. */
public abstract class Setting<T> {
	public final String name;
	protected T value;
	public final T defaultValue;

	protected Setting(String name, T value) {
		this.name = name;
		this.value = value;
		this.defaultValue = value;
	}

	public T get() {
		return value;
	}

	public void set(T v) {
		value = v;
	}

	public void reset() {
		value = defaultValue;
	}

	public abstract JsonElement toJson();

	public abstract void fromJson(JsonElement e);

	/** Text for the click GUI and for the .set command. */
	public abstract String display();

	/** Parses user text (command argument); returns false when it does not fit this setting. */
	public abstract boolean parse(String text);
}
