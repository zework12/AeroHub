package dev.aero.client.command;

import com.mojang.blaze3d.platform.InputConstants;
import dev.aero.client.config.Config;
import dev.aero.client.module.Category;
import dev.aero.client.module.Module;
import dev.aero.client.module.ModuleManager;
import dev.aero.client.module.impl.AutoMine;
import dev.aero.client.module.impl.Navigator;
import dev.aero.client.setting.Setting;
import dev.aero.client.util.Chat;
import dev.aero.client.util.Friends;
import dev.aero.client.util.Waypoints;
import net.minecraft.client.Minecraft;
import net.minecraft.core.BlockPos;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;

/** Chat commands typed with the "." prefix. Nothing here is sent to the server. */
public final class Commands {
	public static final String PREFIX = ".";

	private Commands() {
	}

	/** Returns true when the line was a command (and must not be sent as chat). */
	public static boolean handle(String line) {
		if (!line.startsWith(PREFIX) || line.startsWith(PREFIX + PREFIX)) return false;
		String[] a = line.substring(1).trim().split("\\s+");
		if (a.length == 0 || a[0].isEmpty()) {
			help();
			return true;
		}
		String cmd = a[0].toLowerCase(Locale.ROOT);
		String[] args = Arrays.copyOfRange(a, 1, a.length);
		try {
			switch (cmd) {
				case "help", "?" -> help();
				case "toggle", "t" -> toggle(args, null);
				case "enable", "on" -> toggle(args, true);
				case "disable", "off" -> toggle(args, false);
				case "modules", "list" -> modules(args);
				case "bind" -> bind(args);
				case "set" -> set(args);
				case "get" -> get(args);
				case "reset" -> reset(args);
				case "config", "cfg" -> config(args);
				case "friend", "friends" -> friend(args);
				case "waypoint", "wp" -> waypoint(args);
				case "goto" -> go(args);
				case "stop" -> stop();
				case "mine" -> mine(args);
				case "panic" -> {
					ModuleManager.disableAll();
					Chat.info("All modules are off.");
				}
				case "say" -> say(line.substring(Math.min(line.length(), 4)).trim());
				default -> Chat.warn("Unknown command \"" + cmd + "\". Type .help");
			}
		} catch (RuntimeException e) {
			Chat.error("Command failed: " + e);
		}
		return true;
	}

	private static void help() {
		Chat.info("Commands (prefix \".\"):");
		Chat.info(".toggle|.enable|.disable <module>   .modules [category]   .bind <module> <key|none>");
		Chat.info(".set <module> <setting> <value>   .get <module> [setting]   .reset <module>");
		Chat.info(".mine <block ids...>  (also: .mine list|add|remove|clear|ores|stop)   .goto <x y z | waypoint>   .stop");
		Chat.info(".waypoint set|remove|list|goto   .friend add|remove|list|clear   .config save|load [name]   .panic   .say <text>");
		Chat.info("Right Shift opens the module menu.");
	}

	private static Module module(String name) {
		Module m = ModuleManager.byName(name);
		if (m == null || m.hidden) Chat.warn("No module called \"" + name + "\". Type .modules");
		return m == null || m.hidden ? null : m;
	}

	private static void toggle(String[] args, Boolean state) {
		if (args.length < 1) {
			Chat.warn("Usage: .toggle <module>");
			return;
		}
		Module m = module(args[0]);
		if (m == null) return;
		if (state == null) m.toggle();
		else m.setEnabled(state);
		Chat.info(m.name + " is " + (m.isEnabled() ? "on" : "off"));
	}

	private static void modules(String[] args) {
		for (Category c : Category.values()) {
			if (args.length > 0 && !c.label.equalsIgnoreCase(args[0])) continue;
			List<String> names = new ArrayList<>();
			for (Module m : ModuleManager.in(c)) if (!m.hidden) names.add((m.isEnabled() ? "§a" : "§7") + m.name + "§r");
			Chat.info(c.label + " (" + names.size() + "): " + String.join(", ", names));
		}
	}

	private static void bind(String[] args) {
		if (args.length < 2) {
			Chat.warn("Usage: .bind <module> <key|none>   e.g. .bind Fly g");
			return;
		}
		Module m = module(args[0]);
		if (m == null) return;
		if (args[1].equalsIgnoreCase("none")) {
			m.setKey(-1);
			Chat.info(m.name + " unbound");
			return;
		}
		try {
			InputConstants.Key k = InputConstants.getKey("key.keyboard." + args[1].toLowerCase(Locale.ROOT));
			m.setKey(k.getValue());
			Chat.info(m.name + " bound to " + k.getDisplayName().getString());
		} catch (IllegalArgumentException e) {
			Chat.warn("Unknown key \"" + args[1] + "\". Use names like g, f4, left.shift, keypad.1");
		}
	}

	private static void set(String[] args) {
		if (args.length < 3) {
			Chat.warn("Usage: .set <module> <setting> <value>   (list settings: .get <module>)");
			return;
		}
		Module m = module(args[0]);
		if (m == null) return;
		Setting<?> s = m.setting(args[1]);
		if (s == null) {
			Chat.warn(m.name + " has no setting \"" + args[1] + "\". Settings: " + settingNames(m));
			return;
		}
		String value = String.join(" ", Arrays.copyOfRange(args, 2, args.length));
		if (s.parse(value)) Chat.info(m.name + "." + s.name + " = " + s.display());
		else Chat.warn("\"" + value + "\" does not fit " + s.name + " (now " + s.display() + ")");
	}

	private static String settingNames(Module m) {
		List<String> n = new ArrayList<>();
		for (Setting<?> s : m.settings) n.add(s.name.replace(" ", ""));
		return n.isEmpty() ? "(none)" : String.join(", ", n);
	}

	private static void get(String[] args) {
		if (args.length < 1) {
			Chat.warn("Usage: .get <module> [setting]");
			return;
		}
		Module m = module(args[0]);
		if (m == null) return;
		if (args.length >= 2) {
			Setting<?> s = m.setting(args[1]);
			Chat.info(s == null ? "No such setting" : m.name + "." + s.name + " = " + s.display());
			return;
		}
		Chat.info(m.name + " (" + (m.isEnabled() ? "on" : "off") + ") - " + m.description);
		for (Setting<?> s : m.settings) Chat.info("  " + s.name.replace(" ", "") + " = " + s.display());
	}

	private static void reset(String[] args) {
		if (args.length < 1) {
			Chat.warn("Usage: .reset <module>");
			return;
		}
		Module m = module(args[0]);
		if (m == null) return;
		for (Setting<?> s : m.settings) s.reset();
		Chat.info(m.name + " settings reset");
	}

	private static void config(String[] args) {
		String sub = args.length > 0 ? args[0].toLowerCase(Locale.ROOT) : "";
		String name = args.length > 1 ? args[1] : "";
		if (!Config.validProfile(name)) {
			Chat.warn("Config names may use letters, digits, - and _ (up to 32).");
			return;
		}
		switch (sub) {
			case "save" -> Chat.info(Config.save(name) ? "Saved " + Config.file(name).getFileName() : "Could not save (see the game log)");
			case "load" -> Chat.info(Config.load(name, true) ? "Loaded " + Config.file(name).getFileName() : "No readable config " + Config.file(name).getFileName());
			default -> Chat.warn("Usage: .config save|load [name]");
		}
	}

	private static void friend(String[] args) {
		String sub = args.length > 0 ? args[0].toLowerCase(Locale.ROOT) : "";
		switch (sub) {
			case "add" -> {
				if (args.length < 2) Chat.warn("Usage: .friend add <name>");
				else Chat.info(Friends.add(args[1]) ? args[1] + " added" : args[1] + " is already a friend");
			}
			case "remove", "del" -> {
				if (args.length < 2) Chat.warn("Usage: .friend remove <name>");
				else Chat.info(Friends.remove(args[1]) ? args[1] + " removed" : args[1] + " was not a friend");
			}
			case "list" -> Chat.info("Friends: " + (Friends.all().isEmpty() ? "(none)" : String.join(", ", Friends.all())));
			case "clear" -> {
				Friends.clear();
				Chat.info("Friend list cleared");
			}
			default -> Chat.warn("Usage: .friend add|remove|list|clear");
		}
	}

	private static String dimension() {
		return Minecraft.getInstance().level.dimension().location().toString();
	}

	private static void waypoint(String[] args) {
		Minecraft mc = Minecraft.getInstance();
		String sub = args.length > 0 ? args[0].toLowerCase(Locale.ROOT) : "";
		switch (sub) {
			case "set", "add" -> {
				if (args.length < 2) {
					Chat.warn("Usage: .waypoint set <name> [x y z]");
					return;
				}
				BlockPos p = mc.player.blockPosition();
				if (args.length >= 5) p = new BlockPos(Integer.parseInt(args[2]), Integer.parseInt(args[3]), Integer.parseInt(args[4]));
				Waypoints.set(new Waypoints.Point(args[1], dimension(), p.getX(), p.getY(), p.getZ()));
				Chat.info("Waypoint " + args[1] + " = " + p.toShortString());
			}
			case "remove", "del" -> Chat.info(args.length >= 2 && Waypoints.remove(args[1]) ? "Removed " + args[1] : "No such waypoint");
			case "list" -> {
				if (Waypoints.all().isEmpty()) Chat.info("No waypoints");
				for (Waypoints.Point p : Waypoints.all()) Chat.info(p.name() + "  " + p.x() + " " + p.y() + " " + p.z() + "  " + p.dimension());
			}
			case "goto" -> go(Arrays.copyOfRange(args, Math.min(1, args.length), args.length));
			default -> Chat.warn("Usage: .waypoint set|remove|list|goto");
		}
	}

	private static void go(String[] args) {
		Navigator nav = ModuleManager.get(Navigator.class);
		int x, y, z;
		if (args.length >= 3) {
			x = Integer.parseInt(args[0]);
			y = Integer.parseInt(args[1]);
			z = Integer.parseInt(args[2]);
		} else if (args.length == 1) {
			Waypoints.Point p = Waypoints.find(args[0]);
			if (p == null) {
				Chat.warn("No waypoint called " + args[0]);
				return;
			}
			if (!p.dimension().equals(dimension())) {
				Chat.warn("Waypoint " + p.name() + " is in " + p.dimension());
				return;
			}
			x = p.x();
			y = p.y();
			z = p.z();
		} else {
			Chat.warn("Usage: .goto <x y z | waypoint>");
			return;
		}
		nav.goTo(x, y, z, 0, true);
		Chat.info("Walking to " + x + " " + y + " " + z + " (.stop to cancel)");
	}

	private static void stop() {
		ModuleManager.get(Navigator.class).stopWalking();
		AutoMine mine = ModuleManager.get(AutoMine.class);
		if (mine.isEnabled()) mine.setEnabled(false);
		Chat.info("Stopped");
	}

	private static void mine(String[] args) {
		AutoMine mine = ModuleManager.get(AutoMine.class);
		String sub = args.length > 0 ? args[0].toLowerCase(Locale.ROOT) : "";
		switch (sub) {
			case "" -> {
				mine.toggle();
				Chat.info("AutoMine " + (mine.isEnabled() ? "on" : "off") + ". Blocks: " + mine.blocks.display());
			}
			case "stop", "off" -> {
				mine.setEnabled(false);
				Chat.info("AutoMine off");
			}
			case "list" -> Chat.info("AutoMine blocks: " + mine.blocks.display());
			case "ores" -> Chat.info("Ores: " + String.join(", ", AutoMine.knownOres()));
			case "clear" -> {
				mine.blocks.parse("clear");
				Chat.info("AutoMine list cleared");
			}
			case "add", "remove" -> {
				if (args.length < 2) {
					Chat.warn("Usage: .mine " + sub + " <block id>");
					return;
				}
				mine.blocks.parse(sub + " " + String.join(" ", Arrays.copyOfRange(args, 1, args.length)));
				Chat.info("AutoMine blocks: " + mine.blocks.display());
			}
			default -> {
				mine.setBlocks(Arrays.asList(args));
				mine.setEnabled(true);
				Chat.info("AutoMine on. Blocks: " + mine.blocks.display());
			}
		}
	}

	private static void say(String text) {
		if (text.isEmpty()) {
			Chat.warn("Usage: .say <text>");
			return;
		}
		Minecraft mc = Minecraft.getInstance();
		if (mc.getConnection() == null) return;
		if (text.startsWith("/")) mc.getConnection().sendCommand(text.substring(1));
		else mc.getConnection().sendChat(text);
	}
}
