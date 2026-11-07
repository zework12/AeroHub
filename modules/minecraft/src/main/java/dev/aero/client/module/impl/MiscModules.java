package dev.aero.client.module.impl;

import com.mojang.authlib.GameProfile;
import dev.aero.client.AeroClient;
import dev.aero.client.Hooks;
import dev.aero.client.module.Category;
import dev.aero.client.module.Module;
import dev.aero.client.module.ModuleManager;
import dev.aero.client.setting.BoolSetting;
import dev.aero.client.setting.ListSetting;
import dev.aero.client.setting.ModeSetting;
import dev.aero.client.setting.NumberSetting;
import dev.aero.client.util.Chat;
import dev.aero.client.util.Friends;
import dev.aero.client.util.InventoryUtil;
import dev.aero.client.util.KeyUtil;
import net.fabricmc.fabric.api.client.message.v1.ClientReceiveMessageEvents;
import net.fabricmc.fabric.api.client.screen.v1.ScreenEvents;
import net.minecraft.ChatFormatting;
import net.minecraft.client.gui.GuiGraphics;
import net.minecraft.client.gui.screens.ConnectScreen;
import net.minecraft.client.gui.screens.DisconnectedScreen;
import net.minecraft.client.multiplayer.ServerData;
import net.minecraft.client.multiplayer.resolver.ServerAddress;
import net.minecraft.client.player.RemotePlayer;
import net.minecraft.client.resources.sounds.SimpleSoundInstance;
import net.minecraft.network.chat.Component;
import net.minecraft.network.protocol.Packet;
import net.minecraft.network.protocol.game.ClientboundSetTimePacket;
import net.minecraft.network.protocol.game.ServerboundMovePlayerPacket;
import net.minecraft.sounds.SoundEvents;
import net.minecraft.util.Mth;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.item.Items;
import org.lwjgl.glfw.GLFW;

import java.time.LocalTime;
import java.time.format.DateTimeFormatter;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** Misc category. */
public final class MiscModules {
	private MiscModules() {
	}

	public static void register() {
		ModuleManager.registerAll(new AutoReconnect(), new AntiSpam(), new ChatTimestamps(), new NameProtect(), new AutoGG(), new AutoAccept(),
				new PlayerAlert(), new PacketLogger(), new TpsMeter(), new FakePlayer(), new MiddleClickPearl(), new Spin(), new Twerk(), new Panic());
	}

	public static final class AutoReconnect extends Module {
		private final NumberSetting delay = num("Delay (seconds)", 5, 1, 60, 1);
		private ServerData last;

		public AutoReconnect() {
			super("AutoReconnect", Category.MISC, "Reconnects to the server after a few seconds when you are disconnected. Esc cancels.");
			ScreenEvents.AFTER_INIT.register((client, screen, width, height) -> {
				if (!isEnabled() || !(screen instanceof DisconnectedScreen) || last == null) return;
				long[] deadline = {System.currentTimeMillis() + (long) (delay.d() * 1000)};
				ScreenEvents.afterRender(screen).register((s, g, mx, my, dt) -> {
					long left = Math.max(0, deadline[0] - System.currentTimeMillis());
					if (deadline[0] != Long.MAX_VALUE) g.drawCenteredString(client.font, "Reconnecting in " + (left / 1000 + 1) + " s  (Esc cancels)", width / 2, 12, 0xFFFFFF55);
				});
				ScreenEvents.afterTick(screen).register(s -> {
					if (System.currentTimeMillis() >= deadline[0] && client.screen == s) {
						deadline[0] = Long.MAX_VALUE;
						ConnectScreen.startConnecting(s, client, ServerAddress.parseString(last.ip), last, false, null);
					}
				});
			});
		}

		@Override
		public void onTick() {
			ServerData current = mc.getCurrentServer();
			if (current != null) last = current;
		}
	}

	public static final class AntiSpam extends Module {
		private final NumberSetting window = num("Same message within (seconds)", 10, 1, 120, 1);
		private final Map<String, Long> seen = new HashMap<>();

		public AntiSpam() {
			super("AntiSpam", Category.MISC, "Hides a chat line that repeats one you just saw.");
			ClientReceiveMessageEvents.ALLOW_GAME.register((message, overlay) -> overlay || allow(message));
			ClientReceiveMessageEvents.ALLOW_CHAT.register((message, signed, sender, params, time) -> allow(message));
		}

		private boolean allow(Component message) {
			if (!isEnabled()) return true;
			long now = System.currentTimeMillis();
			seen.values().removeIf(t -> now - t > window.d() * 1000);
			return seen.put(message.getString(), now) == null;
		}
	}

	public static final class ChatTimestamps extends Module {
		private static final DateTimeFormatter FORMAT = DateTimeFormatter.ofPattern("HH:mm:ss");

		public ChatTimestamps() {
			super("ChatTimestamps", Category.MISC, "Puts the time in front of every chat line.");
			Hooks.chatModifiers.add(c -> isEnabled() ? Component.literal("[" + LocalTime.now().format(FORMAT) + "] ").withStyle(ChatFormatting.GRAY).append(c) : c);
		}
	}

	public static final class NameProtect extends Module {
		private final ModeSetting alias = mode("Shown as", "Player", "Player", "You", "Hidden", "Steve");

		public NameProtect() {
			super("NameProtect", Category.MISC, "Replaces your own name in the chat window, for screenshots and streams. Message formatting on those lines is simplified.");
			Hooks.chatModifiers.add(c -> {
				if (!isEnabled() || mc.getUser() == null) return c;
				String name = mc.getUser().getName();
				String text = c.getString();
				if (name.isEmpty() || !text.contains(name)) return c;
				return Component.literal(text.replace(name, alias.get())).withStyle(c.getStyle());
			});
		}
	}

	public static final class AutoGG extends Module {
		private final ModeSetting message = mode("Message", "gg", "gg", "GG", "good game", "gg wp");
		private final ListSetting triggers = list("Triggers", ListSetting.Kind.TEXT, "won the game", "winner", "you win", "victory");
		private final NumberSetting delay = num("Delay (ticks)", 20, 0, 100, 1);
		private int pending = -1;

		public AutoGG() {
			super("AutoGG", Category.MISC, "Says a message in chat when a game-over line from the server appears. Edit the Triggers list for your server.");
			ClientReceiveMessageEvents.GAME.register((m, overlay) -> {
				if (!isEnabled() || overlay) return;
				String text = m.getString().toLowerCase(Locale.ROOT);
				for (String t : triggers.get()) {
					if (text.contains(t)) {
						pending = delay.i();
						return;
					}
				}
			});
		}

		@Override
		public void onTick() {
			if (pending > 0) pending--;
			else if (pending == 0) {
				pending = -1;
				mc.getConnection().sendChat(message.get());
			}
		}
	}

	public static final class AutoAccept extends Module {
		private static final Pattern REQUEST = Pattern.compile("^(?:\\[.*?\\]\\s*)?(\\w{3,16}) (?:has requested to teleport to you|wants to teleport to you|has requested that you teleport to them|sent you a teleport request).*", Pattern.CASE_INSENSITIVE);
		private final BoolSetting friendsOnly = bool("Friends only", true);
		private String pendingFrom;

		public AutoAccept() {
			super("AutoAccept", Category.MISC, "Accepts teleport requests (/tpaccept) from friends, or from anyone if you turn Friends only off.");
			ClientReceiveMessageEvents.GAME.register((m, overlay) -> {
				if (!isEnabled() || overlay) return;
				Matcher match = REQUEST.matcher(m.getString().trim());
				if (match.matches() && (!friendsOnly.get() || Friends.is(match.group(1)))) pendingFrom = match.group(1);
			});
		}

		@Override
		public void onTick() {
			if (pendingFrom == null) return;
			String from = pendingFrom;
			pendingFrom = null;
			mc.getConnection().sendCommand("tpaccept");
			Chat.info("Accepted teleport request from " + from);
		}
	}

	public static final class PlayerAlert extends Module {
		private final BoolSetting sound = bool("Sound", true);
		private final BoolSetting ignoreFriends = bool("Ignore friends", true);
		private final Set<UUID> inRange = new HashSet<>();

		public PlayerAlert() {
			super("PlayerAlert", Category.MISC, "Tells you when another player comes into view.");
		}

		@Override
		protected void onEnable() {
			inRange.clear();
			if (mc.level != null) for (Player p : mc.level.players()) inRange.add(p.getUUID());
		}

		@Override
		public void onTick() {
			Set<UUID> now = new HashSet<>();
			for (Player p : mc.level.players()) {
				if (p == mc.player) continue;
				now.add(p.getUUID());
				if (inRange.contains(p.getUUID())) continue;
				String name = p.getGameProfile().getName();
				if (ignoreFriends.get() && Friends.is(name)) continue;
				Chat.warn(name + " is near you (" + Math.round(p.distanceTo(mc.player)) + " blocks)");
				if (sound.get()) mc.getSoundManager().play(SimpleSoundInstance.forUI(SoundEvents.NOTE_BLOCK_PLING.value(), 1.0f));
			}
			inRange.clear();
			inRange.addAll(now);
		}
	}

	public static final class PacketLogger extends Module {
		private final ModeSetting direction = mode("Log", "Both", "Both", "Sent", "Received");
		private final BoolSetting hideMovement = bool("Hide movement packets", true);

		public PacketLogger() {
			super("PacketLogger", Category.MISC, "Writes packet names to the game log (logs/latest.log).");
		}

		private void log(String way, Packet<?> p) {
			if (hideMovement.get() && p instanceof ServerboundMovePlayerPacket) return;
			AeroClient.LOG.info("[packet {}] {}", way, p.getClass().getSimpleName());
		}

		@Override
		public boolean onPacketSend(Packet<?> packet) {
			if (!direction.is("Received")) log("out", packet);
			return false;
		}

		@Override
		public boolean onPacketReceive(Packet<?> packet) {
			if (!direction.is("Sent")) log("in", packet);
			return false;
		}
	}

	public static final class TpsMeter extends Module {
		private volatile long lastGameTime = -1, lastMillis;
		private volatile double tps = 20.0;

		public TpsMeter() {
			super("TPS", Category.MISC, "Estimates the server's ticks per second from its time updates and shows it at the top of the screen.");
		}

		@Override
		public boolean onPacketReceive(Packet<?> packet) {
			if (packet instanceof ClientboundSetTimePacket t) {
				long now = System.currentTimeMillis();
				if (lastGameTime >= 0 && now > lastMillis) {
					double measured = (t.getGameTime() - lastGameTime) / ((now - lastMillis) / 1000.0);
					tps = Mth.clamp(tps * 0.5 + Mth.clamp(measured, 0.0, 20.0) * 0.5, 0.0, 20.0);
				}
				lastGameTime = t.getGameTime();
				lastMillis = now;
			}
			return false;
		}

		@Override
		protected void onEnable() {
			lastGameTime = -1;
			tps = 20.0;
		}

		@Override
		public String info() {
			return String.format(Locale.ROOT, "%.1f", tps);
		}

		@Override
		public void onRender2d(GuiGraphics g, float partialTick) {
			if (mc.options.hideGui || mc.screen != null) return;
			String text = String.format(Locale.ROOT, "TPS %.1f", tps);
			int color = tps > 18 ? 0xFF7CF29A : tps > 14 ? 0xFFFFD166 : 0xFFFF6B6B;
			g.drawString(mc.font, text, mc.getWindow().getGuiScaledWidth() / 2 - mc.font.width(text) / 2, 4, color, true);
		}
	}

	public static final class FakePlayer extends Module {
		private static final int FAKE_ID = -4242;

		public FakePlayer() {
			super("FakePlayer", Category.MISC, "Spawns a stand-in player on your screen only, for testing combat modules. Use it in singleplayer.");
		}

		@Override
		protected void onEnable() {
			RemotePlayer fake = new RemotePlayer(mc.level, new GameProfile(UUID.randomUUID(), "FakePlayer"));
			fake.copyPosition(mc.player);
			fake.setId(FAKE_ID);
			mc.level.addEntity(fake);
		}

		@Override
		protected void onDisable() {
			if (mc.level != null) mc.level.removeEntity(FAKE_ID, Entity.RemovalReason.DISCARDED);
		}
	}

	public static final class MiddleClickPearl extends Module {
		private boolean wasDown;

		public MiddleClickPearl() {
			super("MiddleClickPearl", Category.MISC, "Middle-click throws an ender pearl from your hotbar and switches back.");
		}

		@Override
		public void onTick() {
			boolean down = mc.screen == null && GLFW.glfwGetMouseButton(mc.getWindow().getWindow(), GLFW.GLFW_MOUSE_BUTTON_MIDDLE) == GLFW.GLFW_PRESS;
			if (down && !wasDown) {
				int slot = InventoryUtil.findHotbar(s -> s.is(Items.ENDER_PEARL));
				if (slot >= 0) {
					int previous = mc.player.getInventory().selected;
					InventoryUtil.select(slot);
					mc.gameMode.useItem(mc.player, InteractionHand.MAIN_HAND);
					mc.player.swing(InteractionHand.MAIN_HAND);
					InventoryUtil.select(previous);
				}
			}
			wasDown = down;
		}
	}

	public static final class Spin extends Module {
		private final NumberSetting speed = num("Degrees per tick", 30, 1, 90, 1);

		public Spin() {
			super("Spin", Category.MISC, "Turns your character around and around, so other players see it spin.");
		}

		@Override
		public void onTick() {
			mc.player.setYRot(mc.player.getYRot() + speed.f());
		}
	}

	public static final class Twerk extends Module {
		private int ticks;

		public Twerk() {
			super("Twerk", Category.MISC, "Taps sneak over and over.");
		}

		@Override
		public void onTick() {
			mc.options.keyShift.setDown(++ticks % 4 < 2);
		}

		@Override
		protected void onDisable() {
			if (mc.player != null) KeyUtil.release(mc.options.keyShift);
		}
	}

	public static final class Panic extends Module {
		public Panic() {
			super("Panic", Category.MISC, "Switches every other module off at once.");
		}

		@Override
		protected void onEnable() {
			ModuleManager.disableAll();
			Hooks.reset();
			setEnabled(false);
		}
	}
}
