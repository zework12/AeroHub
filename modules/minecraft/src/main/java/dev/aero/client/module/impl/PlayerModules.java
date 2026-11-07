package dev.aero.client.module.impl;

import dev.aero.client.Hooks;
import dev.aero.client.mixin.MinecraftAccessor;
import dev.aero.client.mixin.MultiPlayerGameModeAccessor;
import dev.aero.client.module.Category;
import dev.aero.client.module.Module;
import dev.aero.client.module.ModuleManager;
import dev.aero.client.pathing.Miner;
import dev.aero.client.setting.BoolSetting;
import dev.aero.client.setting.ListSetting;
import dev.aero.client.setting.NumberSetting;
import dev.aero.client.util.Chat;
import dev.aero.client.util.Friends;
import dev.aero.client.util.InventoryUtil;
import dev.aero.client.util.ItemUtil;
import dev.aero.client.util.KeyUtil;
import dev.aero.client.util.PacketUtil;
import net.minecraft.client.gui.screens.inventory.AbstractContainerScreen;
import net.minecraft.core.component.DataComponents;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.network.protocol.Packet;
import net.minecraft.network.protocol.game.ServerboundContainerClosePacket;
import net.minecraft.network.protocol.game.ServerboundMovePlayerPacket;
import net.minecraft.network.protocol.game.ServerboundPlayerCommandPacket;
import net.minecraft.world.food.FoodProperties;
import net.minecraft.world.inventory.AbstractContainerMenu;
import net.minecraft.world.inventory.ChestMenu;
import net.minecraft.world.inventory.ClickType;
import net.minecraft.world.inventory.ShulkerBoxMenu;
import net.minecraft.world.inventory.Slot;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.item.BlockItem;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.EntityHitResult;
import org.lwjgl.glfw.GLFW;

/** Player category. */
public final class PlayerModules {
	private PlayerModules() {
	}

	public static void register() {
		ModuleManager.registerAll(new AutoRespawn(), new AutoEat(), new AutoTool(), new FastPlace(), new FastUse(), new FastBreak(), new ChestStealer(),
				new InventoryCleaner(), new XCarry(), new AntiHunger(), new NoRotate(), new AutoReplenish(), new MiddleClickFriend());
	}

	public static final class AutoRespawn extends Module {
		private final NumberSetting delay = num("Delay (ticks)", 10, 0, 60, 1);
		private int wait;

		public AutoRespawn() {
			super("AutoRespawn", Category.PLAYER, "Respawns as soon as you die.");
		}

		@Override
		public void onTick() {
			if (!mc.player.isDeadOrDying()) {
				wait = 0;
				return;
			}
			if (++wait >= delay.i()) {
				mc.player.respawn();
				wait = 0;
			}
		}
	}

	public static final class AutoEat extends Module {
		private final NumberSetting hunger = num("Eat when food at or below", 14, 1, 19, 1);
		private final NumberSetting health = num("Golden apple below health", 8, 0, 19, 1);
		private boolean eating;
		private int previousSlot, ticks;

		public AutoEat() {
			super("AutoEat", Category.PLAYER, "Eats from your hotbar when you are hungry or hurt.");
		}

		@Override
		protected void onDisable() {
			if (eating) finish();
		}

		private void finish() {
			eating = false;
			if (mc.player != null) {
				KeyUtil.release(mc.options.keyUse);
				InventoryUtil.select(previousSlot);
			}
		}

		private static boolean safeFood(ItemStack s) {
			return ItemUtil.isFood(s) && !s.is(Items.ROTTEN_FLESH) && !s.is(Items.SPIDER_EYE) && !s.is(Items.POISONOUS_POTATO) && !s.is(Items.PUFFERFISH)
					&& !s.is(Items.CHICKEN) && !s.is(Items.SUSPICIOUS_STEW);
		}

		private int bestFood(boolean goldenApple) {
			int best = -1;
			int bestValue = -1;
			for (int i = 0; i < 9; i++) {
				ItemStack s = mc.player.getInventory().items.get(i);
				if (!safeFood(s)) continue;
				if (goldenApple != (s.is(Items.GOLDEN_APPLE) || s.is(Items.ENCHANTED_GOLDEN_APPLE))) continue;
				FoodProperties f = s.get(DataComponents.FOOD);
				int value = f == null ? 0 : f.nutrition();
				if (value > bestValue) {
					bestValue = value;
					best = i;
				}
			}
			return best;
		}

		@Override
		public void onTick() {
			if (mc.player.getAbilities().instabuild) return;
			if (eating) {
				if (!mc.player.isUsingItem() && ++ticks > 3) {
					finish();
					return;
				}
				mc.options.keyUse.setDown(true);
				return;
			}
			if (mc.screen != null) return;
			int slot = -1;
			if (mc.player.getHealth() <= health.f()) slot = bestFood(true);
			if (slot < 0 && mc.player.getFoodData().getFoodLevel() <= hunger.i()) slot = bestFood(false);
			if (slot < 0) return;
			previousSlot = mc.player.getInventory().selected;
			InventoryUtil.select(slot);
			eating = true;
			ticks = 0;
			mc.options.keyUse.setDown(true);
		}
	}

	public static final class AutoTool extends Module {
		private final BoolSetting restore = bool("Switch back afterwards", true);
		private int previous = -1;

		public AutoTool() {
			super("AutoTool", Category.PLAYER, "Picks the fastest tool in your hotbar for the block you are mining.");
		}

		@Override
		public void onTick() {
			boolean mining = mc.screen == null && mc.options.keyAttack.isDown() && mc.hitResult instanceof BlockHitResult hit
					&& !mc.level.getBlockState(hit.getBlockPos()).isAir();
			if (mining) {
				if (previous < 0) previous = mc.player.getInventory().selected;
				InventoryUtil.select(Miner.bestSlot(((BlockHitResult) mc.hitResult).getBlockPos()));
			} else if (previous >= 0) {
				if (restore.get()) InventoryUtil.select(previous);
				previous = -1;
			}
		}
	}

	public static final class FastPlace extends Module {
		public FastPlace() {
			super("FastPlace", Category.PLAYER, "No delay between placing blocks while you hold the use key.");
		}

		@Override
		public void onTick() {
			if (mc.player.getMainHandItem().getItem() instanceof BlockItem) ((MinecraftAccessor) mc).aero$setRightClickDelay(0);
		}
	}

	public static final class FastUse extends Module {
		public FastUse() {
			super("FastUse", Category.PLAYER, "No delay between throwing items such as snowballs, pearls and experience bottles.");
		}

		@Override
		public void onTick() {
			if (!(mc.player.getMainHandItem().getItem() instanceof BlockItem)) ((MinecraftAccessor) mc).aero$setRightClickDelay(0);
		}
	}

	public static final class FastBreak extends Module {
		private final NumberSetting boost = num("Progress boost", 1.5, 1.0, 5.0, 0.1);
		private float lastProgress;

		public FastBreak() {
			super("FastBreak", Category.PLAYER, "No delay between blocks and faster breaking. Servers that track break time may put the block back.");
		}

		@Override
		public void onTick() {
			MultiPlayerGameModeAccessor acc = (MultiPlayerGameModeAccessor) mc.gameMode;
			acc.aero$setDestroyDelay(0);
			float now = acc.aero$getDestroyProgress();
			if (mc.gameMode.isDestroying() && now > lastProgress) {
				acc.aero$setDestroyProgress(now + (now - lastProgress) * (boost.f() - 1.0f));
			}
			lastProgress = acc.aero$getDestroyProgress();
		}
	}

	public static final class ChestStealer extends Module {
		private final NumberSetting delay = num("Delay (ticks)", 1, 0, 10, 1);
		private final BoolSetting close = bool("Close when empty", true);
		private int wait;

		public ChestStealer() {
			super("ChestStealer", Category.PLAYER, "Takes everything out of an open chest, barrel or shulker box.");
		}

		@Override
		public void onTick() {
			if (!(mc.screen instanceof AbstractContainerScreen<?> screen)) return;
			AbstractContainerMenu menu = screen.getMenu();
			if (!(menu instanceof ChestMenu) && !(menu instanceof ShulkerBoxMenu)) return;
			if (wait > 0) {
				wait--;
				return;
			}
			for (Slot s : menu.slots) {
				if (s.container == mc.player.getInventory() || !s.hasItem()) continue;
				mc.gameMode.handleInventoryMouseClick(menu.containerId, s.index, 0, ClickType.QUICK_MOVE, mc.player);
				wait = delay.i();
				return;
			}
			if (close.get()) mc.player.closeContainer();
		}
	}

	public static final class InventoryCleaner extends Module {
		private final ListSetting junk = list("Items", ListSetting.Kind.ITEM, "rotten_flesh", "spider_eye", "poisonous_potato", "pufferfish", "string", "gunpowder", "bone", "feather");
		private final NumberSetting delay = num("Delay (ticks)", 5, 1, 40, 1);
		private int wait;

		public InventoryCleaner() {
			super("InventoryCleaner", Category.PLAYER, "Drops the items on its list. Edit the list with .set InventoryCleaner Items add <id>.");
		}

		@Override
		public void onTick() {
			if (mc.screen != null || !InventoryUtil.inventoryClosed()) return;
			if (wait > 0) {
				wait--;
				return;
			}
			for (int i = 0; i < 36; i++) {
				ItemStack s = mc.player.getInventory().items.get(i);
				if (s.isEmpty()) continue;
				if (junk.contains(BuiltInRegistries.ITEM.getKey(s.getItem()).getPath())) {
					InventoryUtil.drop(InventoryUtil.menuSlot(i), true);
					wait = delay.i();
					return;
				}
			}
		}
	}

	public static final class XCarry extends Module {
		public XCarry() {
			super("XCarry", Category.PLAYER, "Items left in the inventory crafting grid stay there when you close the inventory.");
		}

		@Override
		public boolean onPacketSend(Packet<?> packet) {
			return packet instanceof ServerboundContainerClosePacket close && close.getContainerId() == 0;
		}
	}

	public static final class AntiHunger extends Module {
		public AntiHunger() {
			super("AntiHunger", Category.PLAYER, "Hides sprinting and jumping from the server, so you lose less hunger.");
		}

		@Override
		public boolean onPacketSend(Packet<?> packet) {
			if (packet instanceof ServerboundPlayerCommandPacket) {
				return false;
			}
			if (packet instanceof ServerboundMovePlayerPacket m && mc.player != null && mc.player.fallDistance <= 0.5f && !mc.player.isFallFlying()) {
				if (!m.isOnGround()) return false;
				Packet<?> replacement;
				if (m.hasPosition() && m.hasRotation()) replacement = new ServerboundMovePlayerPacket.PosRot(m.getX(0), m.getY(0), m.getZ(0), m.getYRot(0), m.getXRot(0), false);
				else if (m.hasPosition()) replacement = new ServerboundMovePlayerPacket.Pos(m.getX(0), m.getY(0), m.getZ(0), false);
				else if (m.hasRotation()) replacement = new ServerboundMovePlayerPacket.Rot(m.getYRot(0), m.getXRot(0), false);
				else replacement = new ServerboundMovePlayerPacket.StatusOnly(false);
				PacketUtil.send(replacement);
				return true;
			}
			return false;
		}
	}

	public static final class NoRotate extends Module {
		public NoRotate() {
			super("NoRotate", Category.PLAYER, "Ignores the direction the server tells you to look.");
		}

		@Override
		protected void onEnable() {
			Hooks.noRotate = true;
		}

		@Override
		protected void onDisable() {
			Hooks.noRotate = false;
		}
	}

	public static final class AutoReplenish extends Module {
		private final NumberSetting threshold = num("Refill at or below", 8, 1, 32, 1);
		private final NumberSetting delay = num("Delay (ticks)", 4, 1, 20, 1);
		private int wait;

		public AutoReplenish() {
			super("AutoReplenish", Category.PLAYER, "Refills a hotbar stack from your inventory when it runs low.");
		}

		@Override
		public void onTick() {
			if (mc.screen != null || !InventoryUtil.inventoryClosed()) return;
			if (wait > 0) {
				wait--;
				return;
			}
			for (int h = 0; h < 9; h++) {
				ItemStack s = mc.player.getInventory().items.get(h);
				if (s.isEmpty() || !s.isStackable() || s.getCount() > threshold.i() || s.getCount() >= s.getMaxStackSize()) continue;
				for (int i = 9; i < 36; i++) {
					ItemStack from = mc.player.getInventory().items.get(i);
					if (ItemStack.isSameItemSameComponents(s, from)) {
						InventoryUtil.quickMove(InventoryUtil.menuSlot(i));
						wait = delay.i();
						return;
					}
				}
			}
		}
	}

	public static final class MiddleClickFriend extends Module {
		private boolean wasDown;

		public MiddleClickFriend() {
			super("MiddleClickFriend", Category.PLAYER, "Middle-click a player to add or remove them as a friend.");
		}

		@Override
		public void onTick() {
			boolean down = mc.screen == null && GLFW.glfwGetMouseButton(mc.getWindow().getWindow(), GLFW.GLFW_MOUSE_BUTTON_MIDDLE) == GLFW.GLFW_PRESS;
			if (down && !wasDown && mc.hitResult instanceof EntityHitResult hit && hit.getEntity() instanceof Player p) {
				String name = p.getGameProfile().getName();
				if (Friends.is(name)) {
					Friends.remove(name);
					Chat.info(name + " removed from friends");
				} else {
					Friends.add(name);
					Chat.info(name + " added to friends");
				}
			}
			wasDown = down;
		}
	}
}
