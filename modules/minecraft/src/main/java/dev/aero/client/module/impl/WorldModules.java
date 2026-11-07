package dev.aero.client.module.impl;

import dev.aero.client.Hooks;
import dev.aero.client.module.Category;
import dev.aero.client.module.Module;
import dev.aero.client.module.ModuleManager;
import dev.aero.client.pathing.Controls;
import dev.aero.client.pathing.Miner;
import dev.aero.client.setting.BoolSetting;
import dev.aero.client.setting.ListSetting;
import dev.aero.client.setting.ModeSetting;
import dev.aero.client.setting.NumberSetting;
import dev.aero.client.util.BlockUtil;
import dev.aero.client.util.Chat;
import dev.aero.client.util.InventoryUtil;
import dev.aero.client.util.MoveUtil;
import dev.aero.client.util.Render3d;
import dev.aero.client.util.Waypoints;
import net.fabricmc.fabric.api.client.rendering.v1.WorldRenderContext;
import net.minecraft.client.gui.GuiGraphics;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.network.protocol.Packet;
import net.minecraft.network.protocol.game.ClientboundSoundPacket;
import net.minecraft.sounds.SoundEvents;
import net.minecraft.tags.BlockTags;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.item.Items;
import net.minecraft.world.level.LightLayer;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.CropBlock;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;

import java.util.concurrent.atomic.AtomicBoolean;

/** World category (AutoMine and Navigator live in their own files). */
public final class WorldModules {
	private WorldModules() {
	}

	public static void register() {
		ModuleManager.registerAll(new Nuker(), new Scaffold(), new AutoTunnel(), new AutoFarm(), new AutoLumber(), new AutoFish(), new AutoTorch(),
				new Follow(), new WaypointsModule());
	}

	private static double reachCap(double wanted) {
		var mc = net.minecraft.client.Minecraft.getInstance();
		return Math.min(wanted, mc.player.blockInteractionRange());
	}

	public static final class Nuker extends Module {
		private final NumberSetting range = num("Range", 4.0, 1.0, 6.0, 0.1);
		private final ModeSetting mode = mode("Mode", "All", "All", "List");
		private final ListSetting blocks = list("Blocks", "stone", "dirt", "cobblestone");
		private final BoolSetting upwardsOnly = bool("Not below feet", true);
		private BlockPos current;

		public Nuker() {
			super("Nuker", Category.WORLD, "Breaks every block in range, or only the ones on the Blocks list, one after another.");
		}

		@Override
		protected void onDisable() {
			current = null;
		}

		@Override
		public void onTick() {
			if (mc.screen != null) return;
			if (current != null && !valid(current)) current = null;
			if (current == null) current = pick();
			if (current != null) Miner.mine(current);
		}

		private boolean valid(BlockPos p) {
			BlockState s = mc.level.getBlockState(p);
			if (s.isAir() || !s.getFluidState().isEmpty() || s.getDestroySpeed(mc.level, p) < 0) return false;
			if (mc.player.getEyePosition().distanceTo(Vec3.atCenterOf(p)) > reachCap(range.d())) return false;
			if (upwardsOnly.get() && p.getY() < mc.player.blockPosition().getY()) return false;
			return mode.is("All") || blocks.contains(BuiltInRegistries.BLOCK.getKey(s.getBlock()).getPath());
		}

		private BlockPos pick() {
			BlockPos base = mc.player.blockPosition();
			int r = (int) Math.ceil(range.d());
			BlockPos best = null;
			double bestDist = Double.MAX_VALUE;
			Vec3 eye = mc.player.getEyePosition();
			for (BlockPos p : BlockPos.betweenClosed(base.offset(-r, -r, -r), base.offset(r, r, r))) {
				if (!valid(p)) continue;
				double d = eye.distanceToSqr(Vec3.atCenterOf(p));
				if (d < bestDist) {
					bestDist = d;
					best = p.immutable();
				}
			}
			return best;
		}
	}

	public static final class Scaffold extends Module {
		private final NumberSetting expand = num("Expand ahead", 0, 0, 4, 1);
		private final BoolSetting safeWalk = bool("Safe walk", true);
		private final BoolSetting swing = bool("Swing arm", true);

		public Scaffold() {
			super("Scaffold", Category.WORLD, "Places blocks under your feet as you walk, so you can bridge over gaps.");
		}

		@Override
		protected void onDisable() {
			Hooks.safeWalk = false;
		}

		@Override
		public void onTick() {
			Hooks.safeWalk = safeWalk.get();
			if (mc.screen != null) return;
			int slot = BlockUtil.findBuildingBlock();
			if (slot < 0) return;
			int previous = mc.player.getInventory().selected;
			double[] dir = MoveUtil.direction(mc.player.getYRot(), mc.player.input.forwardImpulse, mc.player.input.leftImpulse);
			boolean moving = dir[0] != 0 || dir[1] != 0;
			InventoryUtil.select(slot);
			int steps = moving ? expand.i() : 0;
			for (int e = 0; e <= steps; e++) {
				BlockPos pos = BlockPos.containing(mc.player.getX() + dir[0] * e, mc.player.getY() - 1.0, mc.player.getZ() + dir[1] * e);
				if (mc.level.getBlockState(pos).canBeReplaced()) BlockUtil.place(pos, swing.get());
			}
			InventoryUtil.select(previous);
		}
	}

	public static final class AutoTunnel extends Module {
		private final ModeSetting size = mode("Size", "1x2", "1x2", "3x3");
		private Direction facing = Direction.NORTH;

		public AutoTunnel() {
			super("AutoTunnel", Category.WORLD, "Digs a straight tunnel in the direction you face and walks along it. Stops at lava.");
		}

		@Override
		protected void onEnable() {
			facing = mc.player.getDirection();
			mc.player.setYRot(facing.toYRot());
		}

		@Override
		protected void onDisable() {
			Controls.releaseAll();
		}

		@Override
		public String info() {
			return facing.getName();
		}

		@Override
		public void onTick() {
			BlockPos feet = mc.player.blockPosition();
			Direction side = facing.getClockWise();
			int half = size.is("3x3") ? 1 : 0, height = size.is("3x3") ? 3 : 2;
			BlockPos next = null;
			for (int ahead = 1; ahead <= 2; ahead++) {
				for (int up = 0; up < height; up++) {
					for (int lateral = -half; lateral <= half; lateral++) {
						BlockPos p = feet.relative(facing, ahead).above(up).relative(side, lateral);
						BlockState s = mc.level.getBlockState(p);
						if (s.getFluidState().is(net.minecraft.tags.FluidTags.LAVA)) {
							Chat.warn("AutoTunnel: lava ahead, stopped.");
							setEnabled(false);
							return;
						}
						if (next == null && !s.isAir() && s.getFluidState().isEmpty() && mc.player.getEyePosition().distanceTo(Vec3.atCenterOf(p)) <= reachCap(4.5)) {
							if (s.getDestroySpeed(mc.level, p) < 0) {
								Chat.warn("AutoTunnel: unbreakable block ahead, stopped.");
								setEnabled(false);
								return;
							}
							next = p;
						}
					}
				}
			}
			if (next != null) {
				Controls.set(false, false, false, false);
				Miner.mine(next);
			} else {
				mc.player.setYRot(facing.toYRot());
				Controls.set(true, false, false, false);
			}
		}
	}

	public static final class AutoFarm extends Module {
		private final NumberSetting range = num("Range", 4.0, 1.0, 5.0, 0.1);
		private final BoolSetting replant = bool("Replant", true);

		public AutoFarm() {
			super("AutoFarm", Category.WORLD, "Harvests ripe crops around you and replants them from the seeds in your hotbar.");
		}

		@Override
		public void onTick() {
			if (mc.screen != null) return;
			BlockPos base = mc.player.blockPosition();
			int r = (int) Math.ceil(range.d());
			Vec3 eye = mc.player.getEyePosition();
			BlockPos ripe = null, empty = null;
			double ripeD = Double.MAX_VALUE, emptyD = Double.MAX_VALUE;
			for (BlockPos p : BlockPos.betweenClosed(base.offset(-r, -2, -r), base.offset(r, 2, r))) {
				double d = eye.distanceToSqr(Vec3.atCenterOf(p));
				if (d > range.d() * range.d()) continue;
				BlockState s = mc.level.getBlockState(p);
				if (s.getBlock() instanceof CropBlock crop && crop.isMaxAge(s)) {
					if (d < ripeD) {
						ripeD = d;
						ripe = p.immutable();
					}
				} else if (s.isAir() && mc.level.getBlockState(p.below()).is(Blocks.FARMLAND) && d < emptyD) {
					emptyD = d;
					empty = p.immutable();
				}
			}
			if (ripe != null) {
				Miner.mine(ripe);
				return;
			}
			if (replant.get() && empty != null) {
				int slot = InventoryUtil.findHotbar(s -> s.is(Items.WHEAT_SEEDS) || s.is(Items.CARROT) || s.is(Items.POTATO) || s.is(Items.BEETROOT_SEEDS));
				if (slot < 0) return;
				int previous = mc.player.getInventory().selected;
				InventoryUtil.select(slot);
				BlockUtil.place(empty, true);
				InventoryUtil.select(previous);
			}
		}
	}

	public static final class AutoLumber extends Module {
		private final NumberSetting range = num("Range", 4.0, 1.0, 6.0, 0.1);

		public AutoLumber() {
			super("AutoLumber", Category.WORLD, "Chops logs in range, nearest first.");
		}

		@Override
		public void onTick() {
			if (mc.screen != null) return;
			BlockPos base = mc.player.blockPosition();
			int r = (int) Math.ceil(range.d());
			Vec3 eye = mc.player.getEyePosition();
			BlockPos best = null;
			double bestD = Double.MAX_VALUE;
			for (BlockPos p : BlockPos.betweenClosed(base.offset(-r, -r, -r), base.offset(r, r + 2, r))) {
				BlockState s = mc.level.getBlockState(p);
				if (!s.is(BlockTags.LOGS)) continue;
				double d = eye.distanceToSqr(Vec3.atCenterOf(p));
				if (d <= range.d() * range.d() && d < bestD) {
					bestD = d;
					best = p.immutable();
				}
			}
			if (best != null) Miner.mine(best);
		}
	}

	public static final class AutoFish extends Module {
		private final NumberSetting recastDelay = num("Recast delay (ticks)", 20, 5, 100, 1);
		private final AtomicBoolean bite = new AtomicBoolean();
		private int wait;

		public AutoFish() {
			super("AutoFish", Category.WORLD, "Reels in when a fish bites and casts again. Hold a fishing rod.");
		}

		@Override
		public boolean onPacketReceive(Packet<?> packet) {
			if (packet instanceof ClientboundSoundPacket s && s.getSound().value().getLocation().equals(SoundEvents.FISHING_BOBBER_SPLASH.getLocation())) {
				var mcl = net.minecraft.client.Minecraft.getInstance();
				var hook = mcl.player == null ? null : mcl.player.fishing;
				if (hook != null && hook.distanceToSqr(s.getX(), s.getY(), s.getZ()) < 4.0) bite.set(true);
			}
			return false;
		}

		@Override
		public void onTick() {
			if (!mc.player.getMainHandItem().is(Items.FISHING_ROD)) return;
			if (wait > 0) {
				wait--;
				return;
			}
			if (bite.getAndSet(false) && mc.player.fishing != null) {
				mc.gameMode.useItem(mc.player, InteractionHand.MAIN_HAND);
				mc.player.swing(InteractionHand.MAIN_HAND);
				wait = recastDelay.i();
			} else if (mc.player.fishing == null) {
				mc.gameMode.useItem(mc.player, InteractionHand.MAIN_HAND);
				mc.player.swing(InteractionHand.MAIN_HAND);
				wait = 10;
			}
		}
	}

	public static final class AutoTorch extends Module {
		private final NumberSetting level = num("Place below light", 7, 1, 14, 1);
		private final NumberSetting range = num("Range", 4, 1, 5, 1);
		private int wait;

		public AutoTorch() {
			super("AutoTorch", Category.WORLD, "Places torches on dark ground near you.");
		}

		@Override
		public void onTick() {
			if (mc.screen != null) return;
			if (wait > 0) {
				wait--;
				return;
			}
			int slot = InventoryUtil.findHotbar(s -> s.is(Items.TORCH));
			if (slot < 0) return;
			BlockPos base = mc.player.blockPosition();
			int r = range.i();
			for (BlockPos p : BlockPos.betweenClosed(base.offset(-r, -1, -r), base.offset(r, 1, r))) {
				if (!mc.level.getBlockState(p).isAir() || !mc.level.getBlockState(p.below()).isFaceSturdy(mc.level, p.below(), Direction.UP)) continue;
				if (mc.level.getBrightness(LightLayer.BLOCK, p) >= level.i() || mc.level.getBrightness(LightLayer.SKY, p) >= 8) continue;
				int previous = mc.player.getInventory().selected;
				InventoryUtil.select(slot);
				boolean placed = BlockUtil.place(p.immutable(), true);
				InventoryUtil.select(previous);
				if (placed) {
					wait = 10;
					return;
				}
			}
		}
	}

	public static final class Follow extends Module {
		private final ModeSetting who = mode("Follow", "Nearest player", "Nearest player", "Friends only");
		private final NumberSetting distance = num("Stay within (blocks)", 3, 2, 10, 1);
		private final BoolSetting dig = bool("Dig through terrain", false);
		private int wait;

		public Follow() {
			super("Follow", Category.WORLD, "Walks after the nearest player (or a friend), finding its own path.");
		}

		@Override
		protected void onDisable() {
			Navigator nav = ModuleManager.get(Navigator.class);
			if (nav != null && nav.isEnabled()) nav.stopWalking();
		}

		@Override
		public void onTick() {
			if (wait-- > 0) return;
			wait = 10;
			Player target = null;
			double best = Double.MAX_VALUE;
			for (Player p : mc.level.players()) {
				if (p == mc.player || p.isSpectator()) continue;
				if (who.is("Friends only") && !dev.aero.client.util.Friends.is(p.getGameProfile().getName())) continue;
				double d = p.distanceToSqr(mc.player);
				if (d < best) {
					best = d;
					target = p;
				}
			}
			if (target == null || best <= distance.d() * distance.d()) return;
			Navigator nav = ModuleManager.get(Navigator.class);
			BlockPos t = target.blockPosition();
			nav.goTo(t.getX(), t.getY(), t.getZ(), distance.d() - 1, dig.get());
			nav.quiet = true;
		}
	}

	public static final class WaypointsModule extends Module {
		private final BoolSetting tracers = bool("Tracers", true);
		private final BoolSetting labels = bool("Labels", true);

		public WaypointsModule() {
			super("Waypoints", Category.WORLD, "Shows your saved waypoints in the world (.waypoint set <name>).");
		}

		private Iterable<Waypoints.Point> here() {
			String dim = mc.level.dimension().location().toString();
			return Waypoints.all().stream().filter(p -> p.dimension().equals(dim)).toList();
		}

		@Override
		public void onRender3d(WorldRenderContext ctx) {
			Render3d.begin(ctx, true);
			Vec3 cam = ctx.camera().getPosition();
			Vec3 look = ctx.camera().getLookVector() == null ? Vec3.ZERO : new Vec3(ctx.camera().getLookVector().x(), ctx.camera().getLookVector().y(), ctx.camera().getLookVector().z());
			Vec3 from = cam.add(look.scale(1.5));
			for (Waypoints.Point p : here()) {
				Render3d.box(new AABB(p.x(), p.y(), p.z(), p.x() + 1, p.y() + 2, p.z() + 1), 0xFFFFD166);
				if (tracers.get()) Render3d.line(from, new Vec3(p.x() + 0.5, p.y() + 1.0, p.z() + 0.5), 0x80FFD166);
			}
			Render3d.end();
		}

		@Override
		public void onRender2d(GuiGraphics g, float partialTick) {
			if (!labels.get()) return;
			for (Waypoints.Point p : here()) {
				float[] s = Render3d.project(p.x() + 0.5, p.y() + 2.3, p.z() + 0.5);
				if (s == null) continue;
				double d = mc.player.position().distanceTo(new Vec3(p.x() + 0.5, p.y(), p.z() + 0.5));
				String text = p.name() + " " + Math.round(d) + "m";
				g.drawString(mc.font, text, (int) s[0] - mc.font.width(text) / 2, (int) s[1], 0xFFFFD166, true);
			}
		}
	}
}
