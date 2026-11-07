package dev.aero.client.module.impl;

import dev.aero.client.Hooks;
import dev.aero.client.module.Category;
import dev.aero.client.module.Module;
import dev.aero.client.module.ModuleManager;
import dev.aero.client.setting.BoolSetting;
import dev.aero.client.setting.ModeSetting;
import dev.aero.client.setting.NumberSetting;
import dev.aero.client.util.Ballistics;
import dev.aero.client.util.BlockUtil;
import dev.aero.client.util.InventoryUtil;
import dev.aero.client.util.ItemUtil;
import dev.aero.client.util.PacketUtil;
import dev.aero.client.util.Rotations;
import dev.aero.client.util.Targets;
import net.fabricmc.fabric.api.event.player.AttackEntityCallback;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.network.chat.Component;
import net.minecraft.network.protocol.game.ServerboundMovePlayerPacket;
import net.minecraft.util.Mth;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.InteractionResult;
import net.minecraft.world.entity.EquipmentSlot;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.inventory.ClickType;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.EntityHitResult;
import net.minecraft.world.phys.Vec3;

import java.util.List;
import java.util.concurrent.ThreadLocalRandom;

/** Combat category. */
public final class CombatModules {
	private CombatModules() {
	}

	public static void register() {
		ModuleManager.registerAll(new KillAura(), new TriggerBot(), new AutoClicker(), new Criticals(), new AimAssist(), new AutoTotem(),
				new AutoArmor(), new Velocity(), new Reach(), new HitboxExpand(), new AutoPot(), new FastBow(), new BowAimbot(), new AutoLog(),
				new AntiBot(), new AutoWeapon(), new Surround());
	}

	private static boolean ready(boolean waitForCooldown) {
		return !waitForCooldown || mc().player.getAttackStrengthScale(0.5f) >= 0.95f;
	}

	private static net.minecraft.client.Minecraft mc() {
		return net.minecraft.client.Minecraft.getInstance();
	}

	private static Vec3 aimPoint(LivingEntity t, Vec3 eye) {
		AABB bb = t.getBoundingBox();
		return new Vec3(t.getX(), Mth.clamp(eye.y, bb.minY, bb.maxY), t.getZ());
	}

	public static final class KillAura extends Module {
		private final Targets targets = new Targets(this, 3.0, true, true, false);
		private final BoolSetting cooldown = bool("Wait for cooldown", true);
		private final ModeSetting rotate = mode("Rotation", "Smooth", "Smooth", "Snap", "None");
		private final NumberSetting turn = num("Turn speed", 45, 5, 180, 1);
		private final NumberSetting fov = num("FOV", 360, 30, 360, 1);
		private final ModeSetting priority = mode("Priority", "Distance", "Distance", "Health", "Angle");
		private final BoolSetting keepSprint = bool("Keep sprint", false);
		private final BoolSetting autoWeapon = bool("Auto weapon", true);
		public LivingEntity target;

		public KillAura() {
			super("KillAura", Category.COMBAT, "Attacks the chosen target in range when your attack is ready.");
		}

		@Override
		protected void onDisable() {
			target = null;
		}

		@Override
		public String info() {
			return target == null ? null : target.getName().getString();
		}

		private double angle(LivingEntity e) {
			Vec3 eye = mc.player.getEyePosition();
			Vec3 a = aimPoint(e, eye).subtract(eye);
			return Rotations.angleBetween(mc.player.getYRot(), mc.player.getXRot(), a.x, a.y, a.z);
		}

		@Override
		public void onTick() {
			target = null;
			if (mc.screen != null) return;
			List<LivingEntity> list = targets.within();
			list.removeIf(e -> angle(e) > fov.d() / 2.0);
			if (list.isEmpty()) return;
			if (priority.is("Health")) list.sort((a, b) -> Float.compare(a.getHealth(), b.getHealth()));
			else if (priority.is("Angle")) list.sort((a, b) -> Double.compare(angle(a), angle(b)));
			target = list.get(0);

			Vec3 eye = mc.player.getEyePosition();
			Vec3 aim = aimPoint(target, eye);
			Vec3 d = aim.subtract(eye);
			float[] rot = Rotations.to(d.x, d.y, d.z);
			if (rotate.is("Snap")) {
				mc.player.setYRot(rot[0]);
				mc.player.setXRot(rot[1]);
			} else if (rotate.is("Smooth")) {
				mc.player.setYRot(Rotations.approach(mc.player.getYRot(), rot[0], turn.f()));
				mc.player.setXRot(Rotations.approach(mc.player.getXRot(), rot[1], turn.f()));
			}
			boolean aimed = rotate.is("None") || Rotations.angleBetween(mc.player.getYRot(), mc.player.getXRot(), d.x, d.y, d.z) < 12.0;
			if (!aimed || !ready(cooldown.get())) return;

			if (autoWeapon.get()) AutoWeapon.selectBest();
			Vec3 before = mc.player.getDeltaMovement();
			boolean sprinting = mc.player.isSprinting();
			mc.gameMode.attack(mc.player, target);
			mc.player.swing(InteractionHand.MAIN_HAND);
			if (keepSprint.get()) {
				mc.player.setDeltaMovement(before);
				mc.player.setSprinting(sprinting);
			}
		}
	}

	public static final class TriggerBot extends Module {
		private final Targets targets = new Targets(this, 3.0, true, true, false);
		private final BoolSetting cooldown = bool("Wait for cooldown", true);
		private final NumberSetting delay = num("Extra delay (ticks)", 0, 0, 10, 1);
		private int wait;

		public TriggerBot() {
			super("TriggerBot", Category.COMBAT, "Attacks whatever your crosshair is on.");
		}

		@Override
		public void onTick() {
			if (mc.screen != null) return;
			if (wait > 0) {
				wait--;
				return;
			}
			if (mc.hitResult instanceof EntityHitResult hit && targets.valid(hit.getEntity()) && ready(cooldown.get())) {
				mc.gameMode.attack(mc.player, hit.getEntity());
				mc.player.swing(InteractionHand.MAIN_HAND);
				wait = delay.i();
			}
		}
	}

	public static final class AutoClicker extends Module {
		private final NumberSetting cps = num("CPS", 10, 1, 20, 1);
		private final NumberSetting jitter = num("Jitter", 2, 0, 6, 1);
		private double charge;

		public AutoClicker() {
			super("AutoClicker", Category.COMBAT, "Clicks for you at a set speed while you hold attack and aim at an entity.");
		}

		@Override
		public void onTick() {
			if (mc.screen != null || !mc.options.keyAttack.isDown()) {
				charge = 0;
				return;
			}
			if (!(mc.hitResult instanceof EntityHitResult hit)) return;
			double rate = Math.max(1, cps.d() + (jitter.d() > 0 ? ThreadLocalRandom.current().nextDouble(-jitter.d(), jitter.d()) : 0));
			charge += rate / 20.0;
			if (charge >= 1.0) {
				charge -= 1.0;
				mc.gameMode.attack(mc.player, hit.getEntity());
				mc.player.swing(InteractionHand.MAIN_HAND);
			}
		}
	}

	public static final class Criticals extends Module {
		private final ModeSetting mode = mode("Mode", "Packet", "Packet", "Jump");

		public Criticals() {
			super("Criticals", Category.COMBAT, "Turns ground hits into critical hits.");
			AttackEntityCallback.EVENT.register((player, world, hand, entity, hit) -> {
				if (isEnabled() && world.isClientSide() && player == mc.player) crit();
				return InteractionResult.PASS;
			});
		}

		private void crit() {
			if (!mc.player.onGround() || mc.player.isInWater() || mc.player.onClimbable()) return;
			if (mode.is("Jump")) {
				mc.player.jumpFromGround();
				return;
			}
			double x = mc.player.getX(), y = mc.player.getY(), z = mc.player.getZ();
			PacketUtil.send(new ServerboundMovePlayerPacket.Pos(x, y + 0.0625, z, false));
			PacketUtil.send(new ServerboundMovePlayerPacket.Pos(x, y, z, false));
		}
	}

	public static final class AimAssist extends Module {
		private final Targets targets = new Targets(this, 5.0, true, true, false);
		private final NumberSetting speed = num("Speed", 6, 1, 40, 0.5);
		private final NumberSetting fov = num("FOV", 90, 10, 360, 1);
		private final BoolSetting onlyClicking = bool("Only while attacking", true);

		public AimAssist() {
			super("AimAssist", Category.COMBAT, "Nudges your aim towards the closest target near the crosshair.");
		}

		@Override
		public void onTick() {
			if (mc.screen != null || onlyClicking.get() && !mc.options.keyAttack.isDown()) return;
			LivingEntity best = null;
			double bestAngle = fov.d() / 2.0;
			Vec3 eye = mc.player.getEyePosition();
			for (LivingEntity e : targets.within()) {
				Vec3 d = aimPoint(e, eye).subtract(eye);
				double a = Rotations.angleBetween(mc.player.getYRot(), mc.player.getXRot(), d.x, d.y, d.z);
				if (a < bestAngle) {
					bestAngle = a;
					best = e;
				}
			}
			if (best == null) return;
			Vec3 d = aimPoint(best, eye).subtract(eye);
			float[] rot = Rotations.to(d.x, d.y, d.z);
			mc.player.setYRot(Rotations.approach(mc.player.getYRot(), rot[0], speed.f()));
			mc.player.setXRot(Rotations.approach(mc.player.getXRot(), rot[1], speed.f() * 0.6f));
		}
	}

	public static final class AutoTotem extends Module {
		private final NumberSetting delay = num("Delay (ticks)", 2, 0, 20, 1);
		private final NumberSetting health = num("Only below health", 0, 0, 20, 1);
		private int wait;

		public AutoTotem() {
			super("AutoTotem", Category.COMBAT, "Keeps a totem of undying in your off-hand.");
		}

		@Override
		public void onTick() {
			if (mc.screen != null || !InventoryUtil.inventoryClosed()) return;
			if (wait > 0) {
				wait--;
				return;
			}
			if (ItemUtil.isTotem(mc.player.getOffhandItem())) return;
			if (health.d() > 0 && mc.player.getHealth() > health.f()) return;
			int slot = InventoryUtil.findAny(ItemUtil::isTotem);
			if (slot < 0) return;
			InventoryUtil.swapWithOffhand(slot);
			wait = delay.i();
		}
	}

	public static final class AutoArmor extends Module {
		private final NumberSetting delay = num("Delay (ticks)", 4, 0, 20, 1);
		private int wait;

		public AutoArmor() {
			super("AutoArmor", Category.COMBAT, "Wears the best armour found in your inventory.");
		}

		@Override
		public void onTick() {
			if (mc.screen != null || !InventoryUtil.inventoryClosed()) return;
			if (wait > 0) {
				wait--;
				return;
			}
			EquipmentSlot[] slots = {EquipmentSlot.HEAD, EquipmentSlot.CHEST, EquipmentSlot.LEGS, EquipmentSlot.FEET};
			for (int n = 0; n < slots.length; n++) {
				EquipmentSlot es = slots[n];
				ItemStack worn = mc.player.getItemBySlot(es);
				if (es == EquipmentSlot.CHEST && worn.is(Items.ELYTRA)) continue;
				double bestScore = ItemUtil.armorScore(worn);
				int best = -1;
				for (int i = 0; i < 36; i++) {
					ItemStack s = mc.player.getInventory().items.get(i);
					if (ItemUtil.armorSlot(s) != es) continue;
					double sc = ItemUtil.armorScore(s);
					if (sc > bestScore) {
						bestScore = sc;
						best = i;
					}
				}
				if (best < 0) continue;
				int from = InventoryUtil.menuSlot(best), armorMenu = 5 + n;
				InventoryUtil.click(from, 0, ClickType.PICKUP);
				InventoryUtil.click(armorMenu, 0, ClickType.PICKUP);
				InventoryUtil.click(from, 0, ClickType.PICKUP);
				wait = delay.i();
				return;
			}
		}
	}

	public static final class Velocity extends Module {
		private final NumberSetting horizontal = num("Horizontal %", 0, 0, 100, 1);
		private final NumberSetting vertical = num("Vertical %", 0, 0, 100, 1);

		public Velocity() {
			super("Velocity", Category.COMBAT, "Reduces the knockback you take from hits and explosions.");
		}

		@Override
		public void onTick() {
			Hooks.knockbackHorizontal = horizontal.d() / 100.0;
			Hooks.knockbackVertical = vertical.d() / 100.0;
		}

		@Override
		protected void onDisable() {
			Hooks.knockbackHorizontal = Hooks.knockbackVertical = 1.0;
		}

		@Override
		public String info() {
			return horizontal.i() + "% " + vertical.i() + "%";
		}
	}

	public static final class Reach extends Module {
		private final NumberSetting entity = num("Entity reach +", 1.0, 0, 3, 0.1);
		private final NumberSetting block = num("Block reach +", 1.0, 0, 4, 0.1);

		public Reach() {
			super("Reach", Category.COMBAT, "Lets the client attempt interactions from further away. Servers decide whether they accept them.");
		}

		@Override
		public void onTick() {
			Hooks.reachEntity = entity.d();
			Hooks.reachBlock = block.d();
		}

		@Override
		protected void onDisable() {
			Hooks.reachEntity = Hooks.reachBlock = 0;
		}

		@Override
		public String info() {
			return entity.display() + " / " + block.display();
		}
	}

	public static final class HitboxExpand extends Module {
		private final NumberSetting size = num("Size", 0.3, 0.05, 1.0, 0.05);

		public HitboxExpand() {
			super("Hitboxes", Category.COMBAT, "Makes other entities easier to aim at by enlarging their pick radius.");
		}

		@Override
		public void onTick() {
			Hooks.hitboxExpand = size.f();
		}

		@Override
		protected void onDisable() {
			Hooks.hitboxExpand = 0;
		}
	}

	public static final class AutoPot extends Module {
		private final NumberSetting health = num("Throw below health", 10, 1, 19, 1);
		private final NumberSetting cooldown = num("Cooldown (ticks)", 20, 5, 60, 1);
		private int wait;

		public AutoPot() {
			super("AutoPot", Category.COMBAT, "Throws a healing splash potion at your feet when your health is low.");
		}

		@Override
		public void onTick() {
			if (wait > 0) {
				wait--;
				return;
			}
			if (mc.screen != null || mc.player.getHealth() > health.f()) return;
			int slot = InventoryUtil.findHotbar(ItemUtil::isHealingSplash);
			if (slot < 0) return;
			int previous = mc.player.getInventory().selected;
			float yaw = mc.player.getYRot(), pitch = mc.player.getXRot();
			InventoryUtil.select(slot);
			PacketUtil.send(new ServerboundMovePlayerPacket.Rot(yaw, 90.0f, mc.player.onGround()));
			mc.gameMode.useItem(mc.player, InteractionHand.MAIN_HAND);
			mc.player.swing(InteractionHand.MAIN_HAND);
			PacketUtil.send(new ServerboundMovePlayerPacket.Rot(yaw, pitch, mc.player.onGround()));
			InventoryUtil.select(previous);
			wait = cooldown.i();
		}
	}

	public static final class FastBow extends Module {
		private final NumberSetting ticks = num("Release after (ticks)", 5, 3, 20, 1);

		public FastBow() {
			super("FastBow", Category.COMBAT, "Releases the bow early; shorter draws shoot weaker arrows.");
		}

		@Override
		public void onTick() {
			if (mc.player.isUsingItem() && ItemUtil.isBow(mc.player.getUseItem()) && mc.player.getTicksUsingItem() >= ticks.i()) {
				mc.gameMode.releaseUsingItem(mc.player);
			}
		}
	}

	public static final class BowAimbot extends Module {
		private final Targets targets = new Targets(this, 12.0, true, true, false);
		private final NumberSetting maxRange = num("Max distance", 50, 10, 100, 1);

		public BowAimbot() {
			super("BowAimbot", Category.COMBAT, "While drawing a bow, aims where the arrow will meet the closest moving target.");
		}

		@Override
		public void onTick() {
			if (!mc.player.isUsingItem() || !ItemUtil.isBow(mc.player.getUseItem())) return;
			double speed = Ballistics.FULL_DRAW_SPEED * Ballistics.bowPower(mc.player.getTicksUsingItem());
			if (speed < 0.5) return;
			List<LivingEntity> list = targets.within(maxRange.d());
			if (list.isEmpty()) return;
			LivingEntity t = list.get(0);
			Vec3 eye = mc.player.getEyePosition();
			Vec3 center = t.getBoundingBox().getCenter();
			double[] r = Ballistics.aim(eye.x, eye.y - 0.1, eye.z, center.x, center.y, center.z,
					t.getX() - t.xo, t.getY() - t.yo, t.getZ() - t.zo, speed);
			if (r == null) return;
			mc.player.setYRot((float) r[0]);
			mc.player.setXRot((float) r[1]);
		}
	}

	public static final class AutoLog extends Module {
		private final NumberSetting health = num("Leave below health", 6, 1, 19, 1);

		public AutoLog() {
			super("AutoLog", Category.COMBAT, "Leaves the world when your health drops too low.");
		}

		@Override
		public void onTick() {
			if (mc.player.getHealth() > health.f() || mc.player.isDeadOrDying()) return;
			String text = "AutoLog: health " + Math.round(mc.player.getHealth());
			setEnabled(false);
			if (mc.getConnection() != null) mc.getConnection().getConnection().disconnect(Component.literal(text));
		}
	}

	public static final class AutoWeapon extends Module {
		public AutoWeapon() {
			super("AutoWeapon", Category.COMBAT, "Switches to your strongest melee item when you attack an entity.");
		}

		static void selectBest() {
			var mc = net.minecraft.client.Minecraft.getInstance();
			int best = mc.player.getInventory().selected;
			double bestDamage = ItemUtil.attackDamage(mc.player.getInventory().items.get(best));
			for (int i = 0; i < 9; i++) {
				double d = ItemUtil.attackDamage(mc.player.getInventory().items.get(i));
				if (d > bestDamage) {
					bestDamage = d;
					best = i;
				}
			}
			InventoryUtil.select(best);
		}

		@Override
		public void onTick() {
			if (mc.screen == null && mc.options.keyAttack.isDown() && mc.hitResult instanceof EntityHitResult) selectBest();
		}
	}

	public static final class Surround extends Module {
		private final NumberSetting perTick = num("Blocks per tick", 2, 1, 4, 1);
		private final BoolSetting obsidianFirst = bool("Prefer obsidian", true);

		public Surround() {
			super("Surround", Category.COMBAT, "Builds blocks on the four sides of your feet.");
		}

		@Override
		public void onTick() {
			if (mc.screen != null) return;
			BlockPos feet = mc.player.blockPosition();
			int slot = obsidianFirst.get() ? InventoryUtil.findHotbar(s -> s.is(Items.OBSIDIAN)) : -1;
			if (slot < 0) slot = BlockUtil.findBuildingBlock();
			if (slot < 0) return;
			int previous = mc.player.getInventory().selected;
			int placed = 0;
			for (Direction d : Direction.Plane.HORIZONTAL) {
				if (placed >= perTick.i()) break;
				BlockPos p = feet.relative(d);
				if (!mc.level.getBlockState(p).canBeReplaced()) continue;
				InventoryUtil.select(slot);
				if (BlockUtil.place(p, true)) placed++;
			}
			InventoryUtil.select(previous);
		}
	}
}
