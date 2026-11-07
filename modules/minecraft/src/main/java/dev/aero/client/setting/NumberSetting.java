package dev.aero.client.setting;

import com.google.gson.JsonElement;
import com.google.gson.JsonPrimitive;

public final class NumberSetting extends Setting<Double> {
	public final double min, max, step;

	public NumberSetting(String name, double value, double min, double max, double step) {
		super(name, value);
		this.min = min;
		this.max = max;
		this.step = step;
	}

	public double d() {
		return value;
	}

	public float f() {
		return value.floatValue();
	}

	public int i() {
		return (int) Math.round(value);
	}

	@Override
	public void set(Double v) {
		double x = Math.max(min, Math.min(max, v));
		if (step > 0) x = Math.round((x - min) / step) * step + min;
		value = Math.max(min, Math.min(max, x));
	}

	@Override
	public JsonElement toJson() {
		return new JsonPrimitive(value);
	}

	@Override
	public void fromJson(JsonElement e) {
		if (e != null && e.isJsonPrimitive() && e.getAsJsonPrimitive().isNumber()) set(e.getAsDouble());
	}

	@Override
	public String display() {
		if (step >= 1 || value == Math.rint(value)) return String.valueOf(Math.round(value));
		return String.format(java.util.Locale.ROOT, "%.2f", value);
	}

	@Override
	public boolean parse(String text) {
		try {
			double v = Double.parseDouble(text);
			if (Double.isNaN(v) || Double.isInfinite(v)) return false;
			set(v);
			return true;
		} catch (NumberFormatException ex) {
			return false;
		}
	}
}
