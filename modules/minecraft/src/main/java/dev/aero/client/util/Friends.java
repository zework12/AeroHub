package dev.aero.client.util;

import java.util.Collections;
import java.util.Locale;
import java.util.Set;
import java.util.TreeSet;

/** Player names that combat modules leave alone. */
public final class Friends {
	private static final Set<String> NAMES = new TreeSet<>();

	private Friends() {
	}

	public static boolean is(String name) {
		return NAMES.contains(name.toLowerCase(Locale.ROOT));
	}

	public static boolean add(String name) {
		return NAMES.add(name.toLowerCase(Locale.ROOT));
	}

	public static boolean remove(String name) {
		return NAMES.remove(name.toLowerCase(Locale.ROOT));
	}

	public static void clear() {
		NAMES.clear();
	}

	public static Set<String> all() {
		return Collections.unmodifiableSet(NAMES);
	}
}
