package dev.aero.client.util;

import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.world.inventory.ClickType;
import net.minecraft.world.item.ItemStack;

import java.util.function.Predicate;

/** Inventory searches and slot clicks. Inventory indices: 0-8 hotbar, 9-35 main, as in Inventory.items. */
public final class InventoryUtil {
	private InventoryUtil() {
	}

	public static int findHotbar(Predicate<ItemStack> match) {
		LocalPlayer p = Minecraft.getInstance().player;
		for (int i = 0; i < 9; i++) if (match.test(p.getInventory().items.get(i))) return i;
		return -1;
	}

	/** First match in the main inventory (9-35), or -1. */
	public static int findMain(Predicate<ItemStack> match) {
		LocalPlayer p = Minecraft.getInstance().player;
		for (int i = 9; i < 36; i++) if (match.test(p.getInventory().items.get(i))) return i;
		return -1;
	}

	/** First match in hotbar then main inventory, or -1. */
	public static int findAny(Predicate<ItemStack> match) {
		int h = findHotbar(match);
		return h >= 0 ? h : findMain(match);
	}

	public static void select(int hotbarSlot) {
		Minecraft.getInstance().player.getInventory().selected = hotbarSlot;
	}

	/** True when only the player's own inventory is the open container, which is when slot ids below are valid. */
	public static boolean inventoryClosed() {
		LocalPlayer p = Minecraft.getInstance().player;
		return p.containerMenu == p.inventoryMenu;
	}

	/** Slot id in the player's inventory menu for an inventory index. */
	public static int menuSlot(int inventoryIndex) {
		return inventoryIndex < 9 ? inventoryIndex + 36 : inventoryIndex;
	}

	public static void click(int slotId, int button, ClickType type) {
		Minecraft mc = Minecraft.getInstance();
		mc.gameMode.handleInventoryMouseClick(mc.player.containerMenu.containerId, slotId, button, type, mc.player);
	}

	/** Swaps the stack at an inventory index with a hotbar slot (0-8). */
	public static void swapWithHotbar(int inventoryIndex, int hotbarSlot) {
		click(menuSlot(inventoryIndex), hotbarSlot, ClickType.SWAP);
	}

	/** Swaps the stack at an inventory index with the off-hand. */
	public static void swapWithOffhand(int inventoryIndex) {
		click(menuSlot(inventoryIndex), 40, ClickType.SWAP);
	}

	public static void drop(int menuSlot, boolean wholeStack) {
		click(menuSlot, wholeStack ? 1 : 0, ClickType.THROW);
	}

	public static void quickMove(int menuSlot) {
		click(menuSlot, 0, ClickType.QUICK_MOVE);
	}
}
