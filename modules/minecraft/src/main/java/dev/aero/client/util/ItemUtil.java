package dev.aero.client.util;

import net.minecraft.core.component.DataComponents;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.effect.MobEffects;
import net.minecraft.world.entity.EquipmentSlot;
import net.minecraft.world.entity.ai.attributes.Attributes;
import net.minecraft.world.item.ArmorItem;
import net.minecraft.world.item.BowItem;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.item.PotionItem;
import net.minecraft.world.item.SplashPotionItem;
import net.minecraft.world.item.alchemy.PotionContents;

public final class ItemUtil {
	private ItemUtil() {
	}

	public static boolean isTotem(ItemStack s) {
		return s.is(Items.TOTEM_OF_UNDYING);
	}

	public static boolean isFood(ItemStack s) {
		return s.has(DataComponents.FOOD);
	}

	public static boolean isBow(ItemStack s) {
		return s.getItem() instanceof BowItem;
	}

	/** Melee damage of the item in the main hand, 1 for an empty hand. */
	public static double attackDamage(ItemStack s) {
		double[] sum = {1.0};
		s.forEachModifier(EquipmentSlot.MAINHAND, (attribute, modifier) -> {
			if (attribute.is(Attributes.ATTACK_DAMAGE)) sum[0] += modifier.amount();
		});
		return sum[0];
	}

	/** Armour value for comparing two pieces for the same slot: defence, toughness and a little durability. */
	public static double armorScore(ItemStack s) {
		if (!(s.getItem() instanceof ArmorItem a)) return -1;
		double dur = s.isDamageableItem() ? 1.0 - (double) s.getDamageValue() / s.getMaxDamage() : 1.0;
		return a.getDefense() + a.getToughness() * 0.5 + dur * 0.5;
	}

	public static EquipmentSlot armorSlot(ItemStack s) {
		return s.getItem() instanceof ArmorItem a ? a.getEquipmentSlot() : null;
	}

	/** Splash potion whose effects include instant health. */
	public static boolean isHealingSplash(ItemStack s) {
		return s.getItem() instanceof SplashPotionItem && hasEffect(s, MobEffects.HEAL);
	}

	public static boolean isHealingPotion(ItemStack s) {
		return s.getItem() instanceof PotionItem && hasEffect(s, MobEffects.HEAL);
	}

	private static boolean hasEffect(ItemStack s, net.minecraft.core.Holder<net.minecraft.world.effect.MobEffect> effect) {
		PotionContents contents = s.get(DataComponents.POTION_CONTENTS);
		if (contents == null) return false;
		for (MobEffectInstance e : contents.getAllEffects()) if (e.getEffect().is(effect)) return true;
		return false;
	}
}
