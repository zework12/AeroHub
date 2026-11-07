package dev.aero.client.module.impl;

import dev.aero.client.Hooks;
import dev.aero.client.module.Category;
import dev.aero.client.module.Module;
import dev.aero.client.module.ModuleManager;
import dev.aero.client.setting.BoolSetting;
import dev.aero.client.setting.ModeSetting;
import dev.aero.client.setting.NumberSetting;
import dev.aero.client.util.KeyUtil;
import dev.aero.client.util.MoveUtil;
import dev.aero.client.util.PacketUtil;
import net.minecraft.client.gui.screens.inventory.AbstractContainerScreen;
import net.minecraft.core.BlockPos;
import net.minecraft.network.protocol.Packet;
import net.minecraft.network.protocol.game.ServerboundMovePlayerPacket;
import net.minecraft.tags.FluidTags;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.ai.attributes.Attributes;
import net.minecraft.world.entity.player.Abilities;
import net.minecraft.world.phys.Vec3;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ThreadLocalRandom;

/** Movement category. Values are blocks per tick unless a name says otherwise (20 ticks = 1 second). */
public final class MovementModules {
	private MovementModules() {
	}

	public static void register() {
		ModuleManager.registerAll(new Fly(), new Sprint(), new Speed(), new BunnyHop(), new Step(), new HighJump(), new LongJump(), new Jesus(),
				new Spider(), new NoFall(), new NoSlow(), new SafeWalk(), new Parkour(), new Blink(), new Timer(), new ElytraFly(), new BoatFly(),
				new EntitySpeed(), new AirJump(), new AntiAFK(), new AutoWalk(), new AutoJump(), new Sneak(), new NoJumpDelay(), new InventoryMove(),
				new AntiVoid(), new NoClip(), new ReverseStep(), new FastLadder(), new FastSwim(), new Glide(), new Jetpack(), new NoPush());
	}

	public static final class Fly extends Module {
		private final ModeSetting mode = mode("Mode", "Creative", "Creative", "Motion");
		private final NumberSetting speed = num("Speed", 1.0, 0.1, 5.0, 0.1);
		private final NumberSetting vertical = num("Vertical speed", 0.6, 0.1, 3.0, 0.1);
		private final BoolSetting antiKick = bool("Anti-kick dip", true);
		private int ticks;

		public Fly() {
			super("Fly", Category.MOVEMENT, "Flight. Creative uses the game's own fly state, Motion moves you directly.");
		}

		@Override
		protected void onDisable() {
			if (mc.player == null) return;
			Abilities a = mc.player.getAbilities();
			boolean creative = mc.gameMode != null && mc.gameMode.getPlayerMode().isCreative() || mc.player.isSpectator();
			a.flying = a.flying && creative;
			a.mayfly = creative;
			a.setFlyingSpeed(0.05f);
			mc.player.onUpdateAbilities();
		}

		@Override
		public String info() {
			return mode.get();
		}

		@Override
		public void onTick() {
			ticks++;
			if (mode.is("Creative")) {
				Abilities a = mc.player.getAbilities();
				a.mayfly = true;
				a.flying = true;
				a.setFlyingSpeed(speed.f() * 0.05f);
				return;
			}
			double y = 0;
			if (mc.options.keyJump.isDown()) y += vertical.d();
			if (mc.options.keyShift.isDown()) y -= vertical.d();
			if (y == 0 && antiKick.get() && ticks % 20 == 0) y = -0.04;
			if (MoveUtil.isMoving()) MoveUtil.setSpeed(speed.d() * 0.5);
			else mc.player.setDeltaMovement(0, mc.player.getDeltaMovement().y, 0);
			MoveUtil.setY(mc.player, y);
			mc.player.fallDistance = 0;
		}
	}

	public static final class Sprint extends Module {
		private final BoolSetting omni = bool("All directions", false);

		public Sprint() {
			super("Sprint", Category.MOVEMENT, "Sprints automatically while you move.");
		}

		@Override
		public void onTick() {
			boolean moving = omni.get() ? MoveUtil.isMoving() : mc.player.input.forwardImpulse > 0;
			if (moving && !mc.player.isShiftKeyDown() && !mc.player.horizontalCollision && mc.player.getFoodData().getFoodLevel() > 6) mc.player.setSprinting(true);
		}
	}

	public static final class Speed extends Module {
		private final NumberSetting multiplier = num("Multiplier", 1.4, 1.0, 4.0, 0.05);
		private final BoolSetting airControl = bool("Air control", true);

		public Speed() {
			super("Speed", Category.MOVEMENT, "Moves faster than sprinting, on the ground and in the air.");
		}

		@Override
		public void onTick() {
			if (!MoveUtil.isMoving() || mc.player.isShiftKeyDown() || mc.player.isInWater() || mc.player.getAbilities().flying) return;
			double base = 0.2806 * multiplier.d();
			if (mc.player.onGround()) MoveUtil.setSpeed(base);
			else if (airControl.get()) MoveUtil.setSpeed(Math.max(MoveUtil.horizontalSpeed(mc.player), base * 0.92));
		}

		@Override
		public String info() {
			return multiplier.display() + "x";
		}
	}

	public static final class BunnyHop extends Module {
		public BunnyHop() {
			super("BunnyHop", Category.MOVEMENT, "Jumps automatically while you move on the ground.");
		}

		@Override
		public void onTick() {
			if (mc.player.onGround() && MoveUtil.isMoving() && !mc.player.isShiftKeyDown() && !mc.player.isInWater()) mc.player.jumpFromGround();
		}
	}

	public static final class Step extends Module {
		private final NumberSetting height = num("Height", 1.0, 0.6, 2.5, 0.1);

		public Step() {
			super("Step", Category.MOVEMENT, "Walk up blocks without jumping.");
		}

		@Override
		public void onTick() {
			var attr = mc.player.getAttribute(Attributes.STEP_HEIGHT);
			if (attr != null) attr.setBaseValue(height.d());
		}

		@Override
		protected void onDisable() {
			if (mc.player == null) return;
			var attr = mc.player.getAttribute(Attributes.STEP_HEIGHT);
			if (attr != null) attr.setBaseValue(attr.getAttribute().value().getDefaultValue());
		}
	}

	/** Detects the tick a jump starts: was on the ground, now rising. */
	private abstract static class JumpBoost extends Module {
		private boolean wasOnGround;

		JumpBoost(String name, String description) {
			super(name, Category.MOVEMENT, description);
		}

		abstract void boost();

		@Override
		public void onTick() {
			boolean launched = wasOnGround && !mc.player.onGround() && mc.player.getDeltaMovement().y > 0.2;
			wasOnGround = mc.player.onGround();
			if (launched) boost();
		}
	}

	public static final class HighJump extends JumpBoost {
		private final NumberSetting factor = num("Height factor", 2.0, 1.0, 6.0, 0.1);

		public HighJump() {
			super("HighJump", "Jumps higher.");
		}

		@Override
		void boost() {
			Vec3 v = mc.player.getDeltaMovement();
			mc.player.setDeltaMovement(v.x, v.y * factor.d(), v.z);
		}
	}

	public static final class LongJump extends JumpBoost {
		private final NumberSetting speed = num("Speed", 0.7, 0.3, 2.0, 0.05);

		public LongJump() {
			super("LongJump", "Jumps further forward.");
		}

		@Override
		void boost() {
			if (MoveUtil.isMoving()) MoveUtil.setSpeed(speed.d());
		}
	}

	public static final class Jesus extends Module {
		public Jesus() {
			super("Jesus", Category.MOVEMENT, "Stay on top of water and lava instead of sinking.");
		}

		@Override
		public void onTick() {
			if (mc.player.isShiftKeyDown() || mc.options.keyJump.isDown()) return;
			var level = mc.level;
			boolean inLiquid = mc.player.isInWater() || mc.player.isInLava();
			Vec3 v = mc.player.getDeltaMovement();
			if (inLiquid) {
				mc.player.setDeltaMovement(v.x, Math.max(v.y, 0.12), v.z);
				return;
			}
			BlockPos below = BlockPos.containing(mc.player.getX(), mc.player.getY() - 0.05, mc.player.getZ());
			boolean liquidBelow = level.getFluidState(below).is(FluidTags.WATER) || level.getFluidState(below).is(FluidTags.LAVA);
			if (liquidBelow && v.y <= 0) {
				mc.player.setDeltaMovement(v.x, 0, v.z);
				mc.player.setOnGround(true);
			}
		}
	}

	public static final class Spider extends Module {
		private final NumberSetting speed = num("Climb speed", 0.2, 0.05, 0.6, 0.01);

		public Spider() {
			super("Spider", Category.MOVEMENT, "Climb walls by pushing against them.");
		}

		@Override
		public void onTick() {
			if (mc.player.horizontalCollision && !mc.player.isShiftKeyDown()) MoveUtil.setY(mc.player, speed.d());
		}
	}

	public static final class NoFall extends Module {
		private final NumberSetting distance = num("After falling (blocks)", 2.5, 1.0, 10.0, 0.5);

		public NoFall() {
			super("NoFall", Category.MOVEMENT, "Tells the server you are on the ground while falling, so you take no fall damage.");
		}

		@Override
		public void onTick() {
			if (mc.player.fallDistance > distance.f() && !mc.player.isFallFlying() && !mc.player.getAbilities().flying) {
				PacketUtil.send(new ServerboundMovePlayerPacket.StatusOnly(true));
			}
		}
	}

	public static final class NoSlow extends Module {
		public NoSlow() {
			super("NoSlow", Category.MOVEMENT, "No slowdown while eating, drinking or drawing a bow.");
		}

		@Override
		protected void onEnable() {
			Hooks.noSlow = true;
		}

		@Override
		protected void onDisable() {
			Hooks.noSlow = false;
		}
	}

	public static final class SafeWalk extends Module {
		public SafeWalk() {
			super("SafeWalk", Category.MOVEMENT, "You cannot walk off the edge of a block, as if you were sneaking.");
		}

		@Override
		protected void onEnable() {
			Hooks.safeWalk = true;
		}

		@Override
		protected void onDisable() {
			Hooks.safeWalk = false;
		}
	}

	public static final class Parkour extends Module {
		public Parkour() {
			super("Parkour", Category.MOVEMENT, "Jumps by itself at the edge of a block.");
		}

		@Override
		public void onTick() {
			if (!mc.player.onGround() || !MoveUtil.isMoving() || mc.player.isShiftKeyDown()) return;
			double[] d = MoveUtil.direction(mc.player.getYRot(), mc.player.input.forwardImpulse, mc.player.input.leftImpulse);
			double x = mc.player.getX() + d[0] * 0.35, z = mc.player.getZ() + d[1] * 0.35;
			BlockPos ground = BlockPos.containing(x, mc.player.getY() - 0.5, z);
			if (mc.level.getBlockState(ground).getCollisionShape(mc.level, ground).isEmpty()) mc.player.jumpFromGround();
		}
	}

	public static final class Blink extends Module {
		private final NumberSetting maxTicks = num("Release after (ticks)", 100, 10, 400, 10);
		private final List<Packet<?>> held = new ArrayList<>();
		private int ticks;

		public Blink() {
			super("Blink", Category.MOVEMENT, "Holds your movement packets back, then sends them all at once when you switch it off.");
		}

		@Override
		protected void onEnable() {
			held.clear();
			ticks = 0;
		}

		@Override
		protected void onDisable() {
			flush();
		}

		@Override
		public String info() {
			return held.size() + " packets";
		}

		private void flush() {
			List<Packet<?>> copy = new ArrayList<>(held);
			held.clear();
			if (mc.getConnection() == null) return;
			for (Packet<?> p : copy) PacketUtil.send(p);
		}

		@Override
		public void onTick() {
			if (++ticks >= maxTicks.i()) {
				flush();
				ticks = 0;
			}
		}

		@Override
		public boolean onPacketSend(Packet<?> packet) {
			if (packet instanceof ServerboundMovePlayerPacket) {
				held.add(packet);
				return true;
			}
			return false;
		}
	}

	public static final class Timer extends Module {
		private final NumberSetting speed = num("Game speed", 1.5, 0.1, 5.0, 0.05);

		public Timer() {
			super("Timer", Category.MOVEMENT, "Runs the game faster or slower.");
		}

		@Override
		public void onTick() {
			Hooks.timerSpeed = speed.d();
		}

		@Override
		protected void onDisable() {
			Hooks.timerSpeed = 1.0;
		}

		@Override
		public String info() {
			return speed.display() + "x";
		}
	}

	public static final class ElytraFly extends Module {
		private final NumberSetting speed = num("Speed", 1.2, 0.3, 4.0, 0.05);
		private final BoolSetting hover = bool("Hover when idle", true);

		public ElytraFly() {
			super("ElytraFly", Category.MOVEMENT, "Steer elytra flight with the forward key; jump climbs, sneak descends.");
		}

		@Override
		public void onTick() {
			if (!mc.player.isFallFlying()) return;
			Vec3 look = mc.player.getLookAngle();
			if (mc.player.input.forwardImpulse > 0) {
				mc.player.setDeltaMovement(look.scale(speed.d()));
			} else if (hover.get()) {
				double y = mc.options.keyJump.isDown() ? 0.5 : mc.options.keyShift.isDown() ? -0.5 : 0;
				mc.player.setDeltaMovement(0, y, 0);
			}
		}
	}

	public static final class BoatFly extends Module {
		private final NumberSetting speed = num("Speed", 1.0, 0.2, 4.0, 0.05);
		private final NumberSetting vertical = num("Vertical speed", 0.5, 0.1, 2.0, 0.05);

		public BoatFly() {
			super("BoatFly", Category.MOVEMENT, "Fly while riding a boat or other vehicle.");
		}

		@Override
		public void onTick() {
			Entity v = mc.player.getVehicle();
			if (v == null) return;
			double[] d = MoveUtil.direction(mc.player.getYRot(), mc.player.input.forwardImpulse, mc.player.input.leftImpulse);
			double y = mc.options.keyJump.isDown() ? vertical.d() : mc.options.keyShift.isDown() ? -vertical.d() : 0;
			v.setDeltaMovement(d[0] * speed.d(), y, d[1] * speed.d());
		}
	}

	public static final class EntitySpeed extends Module {
		private final NumberSetting speed = num("Speed", 1.0, 0.2, 4.0, 0.05);

		public EntitySpeed() {
			super("EntitySpeed", Category.MOVEMENT, "Rides horses, pigs and boats faster.");
		}

		@Override
		public void onTick() {
			Entity v = mc.player.getVehicle();
			if (v == null || !MoveUtil.isMoving()) return;
			double[] d = MoveUtil.direction(mc.player.getYRot(), mc.player.input.forwardImpulse, mc.player.input.leftImpulse);
			Vec3 now = v.getDeltaMovement();
			v.setDeltaMovement(d[0] * speed.d(), now.y, d[1] * speed.d());
		}
	}

	public static final class AirJump extends Module {
		private int cooldown;

		public AirJump() {
			super("AirJump", Category.MOVEMENT, "Jump again while in the air.");
		}

		@Override
		public void onTick() {
			if (cooldown > 0) cooldown--;
			if (mc.options.keyJump.isDown() && !mc.player.onGround() && cooldown == 0 && !mc.player.isInWater() && !mc.player.onClimbable()) {
				mc.player.jumpFromGround();
				cooldown = 6;
			}
		}
	}

	public static final class AntiAFK extends Module {
		private final NumberSetting seconds = num("Every (seconds)", 15, 3, 120, 1);
		private int ticks, step;

		public AntiAFK() {
			super("AntiAFK", Category.MOVEMENT, "Does a small action now and then so you are not kicked for idling.");
		}

		@Override
		public void onTick() {
			if (++ticks < seconds.i() * 20) return;
			ticks = 0;
			switch (step++ % 3) {
				case 0 -> mc.player.jumpFromGround();
				case 1 -> mc.player.swing(InteractionHand.MAIN_HAND);
				default -> mc.player.setYRot(mc.player.getYRot() + ThreadLocalRandom.current().nextInt(-25, 26));
			}
		}
	}

	public static final class AutoWalk extends Module {
		public AutoWalk() {
			super("AutoWalk", Category.MOVEMENT, "Holds the forward key for you.");
		}

		@Override
		public void onTick() {
			mc.options.keyUp.setDown(true);
		}

		@Override
		protected void onDisable() {
			if (mc.player != null) KeyUtil.release(mc.options.keyUp);
		}
	}

	public static final class AutoJump extends Module {
		public AutoJump() {
			super("AutoJump", Category.MOVEMENT, "Jumps over obstacles in front of you.");
		}

		@Override
		public void onTick() {
			if (mc.player.horizontalCollision && mc.player.onGround() && MoveUtil.isMoving()) mc.player.jumpFromGround();
		}
	}

	public static final class Sneak extends Module {
		public Sneak() {
			super("Sneak", Category.MOVEMENT, "Always sneaking.");
		}

		@Override
		public void onTick() {
			mc.options.keyShift.setDown(true);
		}

		@Override
		protected void onDisable() {
			if (mc.player != null) KeyUtil.release(mc.options.keyShift);
		}
	}

	public static final class NoJumpDelay extends Module {
		public NoJumpDelay() {
			super("NoJumpDelay", Category.MOVEMENT, "Removes the pause between jumps when you hold the jump key.");
		}

		@Override
		public void onTick() {
			((dev.aero.client.mixin.LivingEntityAccessor) mc.player).aero$setNoJumpDelay(0);
		}
	}

	public static final class InventoryMove extends Module {
		public InventoryMove() {
			super("InventoryMove", Category.MOVEMENT, "Keep walking, jumping and sprinting while an inventory is open.");
		}

		@Override
		public void onTick() {
			if (!(mc.screen instanceof AbstractContainerScreen<?>)) return;
			for (var km : List.of(mc.options.keyUp, mc.options.keyDown, mc.options.keyLeft, mc.options.keyRight, mc.options.keyJump, mc.options.keySprint)) {
				km.setDown(KeyUtil.physicallyDown(km));
			}
		}
	}

	public static final class AntiVoid extends Module {
		private final NumberSetting above = num("Trigger above void (blocks)", 8, 2, 40, 1);
		private Vec3 lastSafe;

		public AntiVoid() {
			super("AntiVoid", Category.MOVEMENT, "Puts you back on the last solid ground you stood on when you fall towards the void.");
		}

		@Override
		protected void onEnable() {
			lastSafe = null;
		}

		@Override
		public void onTick() {
			if (mc.player.onGround()) lastSafe = mc.player.position();
			double voidY = mc.level.getMinBuildHeight();
			if (lastSafe != null && mc.player.getY() < voidY + above.d() && mc.player.getDeltaMovement().y < -0.3) {
				mc.player.setPos(lastSafe);
				mc.player.setDeltaMovement(Vec3.ZERO);
				mc.player.fallDistance = 0;
				PacketUtil.send(new ServerboundMovePlayerPacket.Pos(lastSafe.x, lastSafe.y, lastSafe.z, true));
			}
		}
	}

	public static final class NoClip extends Module {
		private final NumberSetting speed = num("Speed", 0.6, 0.1, 3.0, 0.05);

		public NoClip() {
			super("NoClip", Category.MOVEMENT, "Move through blocks. Servers usually pull you back.");
		}

		@Override
		public void onTick() {
			mc.player.noPhysics = true;
			mc.player.setOnGround(false);
			double y = mc.options.keyJump.isDown() ? speed.d() : mc.options.keyShift.isDown() ? -speed.d() : 0;
			if (MoveUtil.isMoving()) MoveUtil.setSpeed(speed.d());
			else mc.player.setDeltaMovement(0, 0, 0);
			MoveUtil.setY(mc.player, y);
		}

		@Override
		protected void onDisable() {
			if (mc.player != null) mc.player.noPhysics = false;
		}
	}

	public static final class ReverseStep extends Module {
		private final NumberSetting maxDrop = num("Max drop (blocks)", 1.5, 0.5, 3.0, 0.1);

		public ReverseStep() {
			super("ReverseStep", Category.MOVEMENT, "Drops down short ledges quickly instead of floating down.");
		}

		@Override
		public void onTick() {
			if (mc.player.onGround() || mc.player.getDeltaMovement().y > 0 || mc.player.isInWater() || mc.player.getAbilities().flying) return;
			boolean groundNear = !mc.level.noCollision(mc.player, mc.player.getBoundingBox().move(0, -maxDrop.d(), 0));
			if (groundNear) MoveUtil.setY(mc.player, -1.0);
		}
	}

	public static final class FastLadder extends Module {
		private final NumberSetting speed = num("Climb speed", 0.3, 0.1, 1.0, 0.01);

		public FastLadder() {
			super("FastLadder", Category.MOVEMENT, "Climbs ladders and vines faster.");
		}

		@Override
		public void onTick() {
			if (mc.player.onClimbable() && (mc.player.horizontalCollision || mc.options.keyJump.isDown())) MoveUtil.setY(mc.player, speed.d());
		}
	}

	public static final class FastSwim extends Module {
		private final NumberSetting factor = num("Speed factor", 1.15, 1.0, 2.0, 0.01);

		public FastSwim() {
			super("FastSwim", Category.MOVEMENT, "Swims faster.");
		}

		@Override
		public void onTick() {
			if (!mc.player.isInWater() || !MoveUtil.isMoving()) return;
			Vec3 v = mc.player.getDeltaMovement();
			double s = Math.min(MoveUtil.horizontalSpeed(mc.player) * factor.d(), 1.2);
			double cur = Math.hypot(v.x, v.z);
			if (cur > 1.0e-4) mc.player.setDeltaMovement(v.x / cur * s, v.y, v.z / cur * s);
		}
	}

	public static final class Glide extends Module {
		private final NumberSetting fall = num("Fall speed", 0.05, 0.0, 0.3, 0.01);

		public Glide() {
			super("Glide", Category.MOVEMENT, "Fall slowly.");
		}

		@Override
		public void onTick() {
			if (!mc.player.onGround() && !mc.player.isFallFlying() && !mc.player.isInWater() && mc.player.getDeltaMovement().y < -fall.d()) {
				MoveUtil.setY(mc.player, -fall.d());
				mc.player.fallDistance = 0;
			}
		}
	}

	public static final class Jetpack extends Module {
		private final NumberSetting power = num("Thrust", 0.42, 0.1, 1.5, 0.01);

		public Jetpack() {
			super("Jetpack", Category.MOVEMENT, "Hold jump to keep rising.");
		}

		@Override
		public void onTick() {
			if (mc.options.keyJump.isDown() && !mc.player.isInWater()) MoveUtil.setY(mc.player, power.d());
		}
	}

	public static final class NoPush extends Module {
		public NoPush() {
			super("NoPush", Category.MOVEMENT, "Entities and water cannot push you around.");
		}

		@Override
		protected void onEnable() {
			Hooks.noPush = true;
		}

		@Override
		protected void onDisable() {
			Hooks.noPush = false;
		}
	}
}
