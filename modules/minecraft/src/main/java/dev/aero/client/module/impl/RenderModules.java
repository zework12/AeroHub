package dev.aero.client.module.impl;

import dev.aero.client.Hooks;
import dev.aero.client.mixin.EntityAccessor;
import dev.aero.client.module.Category;
import dev.aero.client.module.Module;
import dev.aero.client.module.ModuleManager;
import dev.aero.client.setting.BoolSetting;
import dev.aero.client.setting.ListSetting;
import dev.aero.client.setting.NumberSetting;
import dev.aero.client.util.Ballistics;
import dev.aero.client.util.BlockScan;
import dev.aero.client.util.Chat;
import dev.aero.client.util.Friends;
import dev.aero.client.util.ItemUtil;
import dev.aero.client.util.Render3d;
import dev.aero.client.util.Rotations;
import net.fabricmc.fabric.api.client.rendering.v1.WorldRenderContext;
import net.minecraft.client.gui.GuiGraphics;
import net.minecraft.client.multiplayer.PlayerInfo;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.effect.MobEffects;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.decoration.ArmorStand;
import net.minecraft.world.entity.item.ItemEntity;
import net.minecraft.world.entity.monster.Enemy;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.item.CrossbowItem;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.level.ClipContext;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.entity.AbstractFurnaceBlockEntity;
import net.minecraft.world.level.block.entity.BarrelBlockEntity;
import net.minecraft.world.level.block.entity.BlockEntity;
import net.minecraft.world.level.block.entity.BrewingStandBlockEntity;
import net.minecraft.world.level.block.entity.ChestBlockEntity;
import net.minecraft.world.level.block.entity.DispenserBlockEntity;
import net.minecraft.world.level.block.entity.EnderChestBlockEntity;
import net.minecraft.world.level.block.entity.HopperBlockEntity;
import net.minecraft.world.level.block.entity.ShulkerBoxBlockEntity;
import net.minecraft.world.level.block.entity.TrappedChestBlockEntity;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.chunk.LevelChunk;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.HitResult;
import net.minecraft.world.phys.Vec3;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;

/** Render category (ClickGui and HUD live in their own files). */
public final class RenderModules {
	private RenderModules() {
	}

	public static void register() {
		ModuleManager.registerAll(new XRay(), new ESP(), new Tracers(), new Nametags(), new Fullbright(), new Search(), new StorageESP(), new SpawnerESP(),
				new ItemESP(), new Trajectories(), new Freecam(), new Zoom(), new FovChanger(), new NoOverlay(), new NoHurtCam(), new NoBob(),
				new NoWeather(), new TimeChanger(), new Breadcrumbs(), new HoleESP(), new LogoutSpots(), new Radar(), new Glow(), new AntiBlind(),
				new ChunkBorders(), new TargetHud());
	}

	private static final int FRIEND = 0xFF55FF55, PLAYER = 0xFF55D6FF, HOSTILE = 0xFFFF5555, PASSIVE = 0xFFB6FF7A;

	private static int colorOf(Entity e) {
		if (e instanceof Player p) return Friends.is(p.getGameProfile().getName()) ? FRIEND : PLAYER;
		return e instanceof Enemy ? HOSTILE : PASSIVE;
	}

	/** Bounding box moved to where the entity is drawn this frame. */
	private static AABB drawnBox(Entity e, float partial) {
		double dx = (e.xo - e.getX()) * (1.0 - partial), dy = (e.yo - e.getY()) * (1.0 - partial), dz = (e.zo - e.getZ()) * (1.0 - partial);
		return e.getBoundingBox().move(dx, dy, dz);
	}

	private static float partial(WorldRenderContext ctx) {
		return ctx.tickCounter().getGameTimeDeltaPartialTick(true);
	}

	private static boolean wanted(Entity e, boolean players, boolean hostile, boolean passive) {
		var mc = net.minecraft.client.Minecraft.getInstance();
		if (e == mc.player || !(e instanceof LivingEntity) || e instanceof ArmorStand) return false;
		if (e instanceof Player) return players;
		return e instanceof Enemy ? hostile : passive;
	}

	public static final class ESP extends Module {
		private final BoolSetting players = bool("Players", true);
		private final BoolSetting hostile = bool("Hostile mobs", true);
		private final BoolSetting passive = bool("Passive mobs", false);
		private final BoolSetting walls = bool("Through walls", true);
		private final NumberSetting range = num("Range", 96, 8, 256, 4);

		public ESP() {
			super("ESP", Category.RENDER, "Boxes around players and mobs.");
		}

		@Override
		public void onRender3d(WorldRenderContext ctx) {
			Render3d.begin(ctx, walls.get());
			float pt = partial(ctx);
			for (Entity e : mc.level.entitiesForRendering()) {
				if (!wanted(e, players.get(), hostile.get(), passive.get()) || mc.player.distanceTo(e) > range.d()) continue;
				Render3d.box(drawnBox(e, pt), colorOf(e));
			}
			Render3d.end();
		}
	}

	public static final class Tracers extends Module {
		private final BoolSetting players = bool("Players", true);
		private final BoolSetting hostile = bool("Hostile mobs", false);
		private final BoolSetting passive = bool("Passive mobs", false);
		private final NumberSetting range = num("Range", 128, 8, 256, 4);

		public Tracers() {
			super("Tracers", Category.RENDER, "Lines from your crosshair to players and mobs.");
		}

		@Override
		public void onRender3d(WorldRenderContext ctx) {
			Render3d.begin(ctx, true);
			float pt = partial(ctx);
			Vec3 cam = ctx.camera().getPosition();
			var look = ctx.camera().getLookVector();
			Vec3 from = cam.add(look.x(), look.y(), look.z());
			for (Entity e : mc.level.entitiesForRendering()) {
				if (!wanted(e, players.get(), hostile.get(), passive.get()) || mc.player.distanceTo(e) > range.d()) continue;
				AABB b = drawnBox(e, pt);
				Render3d.line(from, b.getCenter(), (colorOf(e) & 0x00FFFFFF) | 0xA0000000);
			}
			Render3d.end();
		}
	}

	public static final class Nametags extends Module {
		public Nametags() {
			super("Nametags", Category.RENDER, "Always show other players' name tags, also when they sneak or are far away.");
		}

		@Override
		protected void onEnable() {
			Hooks.nametags = true;
		}

		@Override
		protected void onDisable() {
			Hooks.nametags = false;
		}
	}

	public static final class Fullbright extends Module {
		private boolean added;

		public Fullbright() {
			super("Fullbright", Category.RENDER, "Everything is fully lit (a client-side night vision effect).");
		}

		@Override
		public void onTick() {
			MobEffectInstance now = mc.player.getEffect(MobEffects.NIGHT_VISION);
			if (now == null || added && now.getDuration() < 300) {
				mc.player.addEffect(new MobEffectInstance(MobEffects.NIGHT_VISION, 1200, 0, false, false));
				added = true;
			}
		}

		@Override
		protected void onDisable() {
			if (added && mc.player != null) mc.player.removeEffect(MobEffects.NIGHT_VISION);
			added = false;
		}
	}

	/** Shared base for modules that box blocks found by scanning the loaded chunks every second. */
	private abstract static class BlockBoxes extends Module {
		protected final NumberSetting range = num("Range", 32, 8, 96, 1);
		protected final BoolSetting walls = bool("Through walls", true);
		private List<int[]> found = new ArrayList<>();
		private int ticks = 20;

		BlockBoxes(String name, String description) {
			super(name, Category.RENDER, description);
		}

		abstract Set<Block> targets();

		@Override
		public void onTick() {
			if (++ticks < 20) return;
			ticks = 0;
			List<int[]> list = BlockScan.scan(targets(), range.i());
			list.sort((a, b) -> Double.compare(d2(a), d2(b)));
			found = list.size() > 500 ? new ArrayList<>(list.subList(0, 500)) : list;
		}

		private double d2(int[] p) {
			double dx = p[0] + 0.5 - mc.player.getX(), dy = p[1] + 0.5 - mc.player.getY(), dz = p[2] + 0.5 - mc.player.getZ();
			return dx * dx + dy * dy + dz * dz;
		}

		@Override
		protected void onDisable() {
			found = new ArrayList<>();
			ticks = 20;
		}

		@Override
		public String info() {
			return found.isEmpty() ? null : String.valueOf(found.size());
		}

		@Override
		public void onRender3d(WorldRenderContext ctx) {
			if (found.isEmpty()) return;
			Render3d.begin(ctx, walls.get());
			for (int[] p : found) {
				BlockState s = mc.level.getBlockState(new BlockPos(p[0], p[1], p[2]));
				Render3d.box(new AABB(p[0], p[1], p[2], p[0] + 1, p[1] + 1, p[2] + 1).deflate(0.02), Render3d.hashColor(BuiltInRegistries.BLOCK.getKey(s.getBlock()).hashCode(), 0xFF));
			}
			Render3d.end();
		}
	}

	public static final class Search extends BlockBoxes {
		private final ListSetting blocks = list("Blocks", "diamond_ore", "deepslate_diamond_ore", "ancient_debris", "emerald_ore", "deepslate_emerald_ore",
				"gold_ore", "deepslate_gold_ore", "nether_gold_ore", "iron_ore", "deepslate_iron_ore", "redstone_ore", "deepslate_redstone_ore",
				"lapis_ore", "deepslate_lapis_ore", "coal_ore", "deepslate_coal_ore", "copper_ore", "deepslate_copper_ore", "nether_quartz_ore");

		public Search() {
			super("Search", "Highlights the blocks on the Blocks list through walls (an X-ray for ores that leaves the world untouched).");
		}

		@Override
		Set<Block> targets() {
			Set<Block> out = new HashSet<>();
			for (String id : blocks.get()) {
				ResourceLocation rl = ResourceLocation.tryParse(id.contains(":") ? id : "minecraft:" + id);
				if (rl != null) BuiltInRegistries.BLOCK.getOptional(rl).ifPresent(out::add);
			}
			return out;
		}
	}

	public static final class SpawnerESP extends BlockBoxes {
		public SpawnerESP() {
			super("SpawnerESP", "Highlights monster spawners and trial spawners.");
		}

		@Override
		Set<Block> targets() {
			return Set.of(Blocks.SPAWNER, Blocks.TRIAL_SPAWNER);
		}
	}

	public static final class StorageESP extends Module {
		private final NumberSetting range = num("Range", 64, 8, 128, 4);
		private final BoolSetting walls = bool("Through walls", true);
		private final BoolSetting chests = bool("Chests", true);
		private final BoolSetting ender = bool("Ender chests", true);
		private final BoolSetting shulkers = bool("Shulker boxes", true);
		private final BoolSetting barrels = bool("Barrels", true);
		private final BoolSetting other = bool("Hoppers, furnaces, dispensers", false);
		private record Box(AABB box, int color) {
		}

		private List<Box> boxes = new ArrayList<>();
		private int ticks = 20;

		public StorageESP() {
			super("StorageESP", Category.RENDER, "Boxes around chests, barrels, shulker boxes and other storage.");
		}

		@Override
		protected void onDisable() {
			boxes = new ArrayList<>();
			ticks = 20;
		}

		private int colorFor(BlockEntity be) {
			if (be instanceof TrappedChestBlockEntity) return chests.get() ? 0xFFFF5555 : 0;
			if (be instanceof ChestBlockEntity) return chests.get() ? 0xFFFFA83D : 0;
			if (be instanceof EnderChestBlockEntity) return ender.get() ? 0xFFB65CFF : 0;
			if (be instanceof ShulkerBoxBlockEntity) return shulkers.get() ? 0xFFFF7AD9 : 0;
			if (be instanceof BarrelBlockEntity) return barrels.get() ? 0xFFD9A066 : 0;
			if (be instanceof HopperBlockEntity || be instanceof AbstractFurnaceBlockEntity || be instanceof DispenserBlockEntity || be instanceof BrewingStandBlockEntity) {
				return other.get() ? 0xFFB0B8C0 : 0;
			}
			return 0;
		}

		@Override
		public void onTick() {
			if (++ticks < 20) return;
			ticks = 0;
			List<Box> out = new ArrayList<>();
			int r = range.i();
			int cx0 = ((int) Math.floor(mc.player.getX()) - r) >> 4, cx1 = ((int) Math.floor(mc.player.getX()) + r) >> 4;
			int cz0 = ((int) Math.floor(mc.player.getZ()) - r) >> 4, cz1 = ((int) Math.floor(mc.player.getZ()) + r) >> 4;
			for (int cx = cx0; cx <= cx1; cx++) {
				for (int cz = cz0; cz <= cz1; cz++) {
					LevelChunk chunk = mc.level.getChunkSource().getChunkNow(cx, cz);
					if (chunk == null) continue;
					for (BlockEntity be : chunk.getBlockEntities().values()) {
						int color = colorFor(be);
						if (color == 0) continue;
						BlockPos p = be.getBlockPos();
						if (p.distSqr(mc.player.blockPosition()) > (double) r * r) continue;
						out.add(new Box(new AABB(p).deflate(0.04), color));
					}
				}
			}
			boxes = out;
		}

		@Override
		public String info() {
			return boxes.isEmpty() ? null : String.valueOf(boxes.size());
		}

		@Override
		public void onRender3d(WorldRenderContext ctx) {
			if (boxes.isEmpty()) return;
			Render3d.begin(ctx, walls.get());
			for (Box b : boxes) Render3d.box(b.box(), b.color());
			Render3d.end();
		}
	}

	public static final class ItemESP extends Module {
		private final BoolSetting labels = bool("Labels", true);
		private final BoolSetting walls = bool("Through walls", true);
		private final NumberSetting range = num("Range", 48, 8, 128, 4);

		public ItemESP() {
			super("ItemESP", Category.RENDER, "Boxes and names for dropped items.");
		}

		@Override
		public void onRender3d(WorldRenderContext ctx) {
			Render3d.begin(ctx, walls.get());
			float pt = partial(ctx);
			for (Entity e : mc.level.entitiesForRendering()) {
				if (e instanceof ItemEntity && mc.player.distanceTo(e) <= range.d()) Render3d.box(drawnBox(e, pt), 0xFFFFD166);
			}
			Render3d.end();
		}

		@Override
		public void onRender2d(GuiGraphics g, float partialTick) {
			if (!labels.get()) return;
			for (Entity e : mc.level.entitiesForRendering()) {
				if (!(e instanceof ItemEntity item) || mc.player.distanceTo(e) > range.d()) continue;
				float[] s = Render3d.project(e.getX(), e.getY() + 0.6, e.getZ());
				if (s == null) continue;
				ItemStack stack = item.getItem();
				String text = stack.getHoverName().getString() + (stack.getCount() > 1 ? " x" + stack.getCount() : "");
				g.drawString(mc.font, text, (int) s[0] - mc.font.width(text) / 2, (int) s[1], 0xFFFFD166, true);
			}
		}
	}

	public static final class Trajectories extends Module {
		private final BoolSetting landing = bool("Landing marker", true);

		public Trajectories() {
			super("Trajectories", Category.RENDER, "Shows where arrows, pearls, snowballs, potions and tridents will land.");
		}

		/** {speed, gravity, pitchOffset} for the held item, or null when it throws nothing. */
		private double[] projectile() {
			for (ItemStack s : new ItemStack[] {mc.player.getMainHandItem(), mc.player.getOffhandItem()}) {
				if (ItemUtil.isBow(s)) {
					if (!mc.player.isUsingItem()) continue;
					double power = Ballistics.bowPower(mc.player.getTicksUsingItem());
					return power < 0.1 ? null : new double[] {Ballistics.FULL_DRAW_SPEED * power, 0.05, 0};
				}
				if (s.getItem() instanceof CrossbowItem && CrossbowItem.isCharged(s)) return new double[] {3.15, 0.05, 0};
				if (s.is(Items.SNOWBALL) || s.is(Items.EGG) || s.is(Items.ENDER_PEARL)) return new double[] {1.5, 0.03, 0};
				if (s.is(Items.SPLASH_POTION) || s.is(Items.LINGERING_POTION)) return new double[] {0.5, 0.05, -20};
				if (s.is(Items.EXPERIENCE_BOTTLE)) return new double[] {0.7, 0.07, -20};
				if (s.is(Items.TRIDENT) && mc.player.isUsingItem()) return new double[] {2.5, 0.05, 0};
			}
			return null;
		}

		@Override
		public void onRender3d(WorldRenderContext ctx) {
			double[] p = projectile();
			if (p == null) return;
			float partial = partial(ctx);
			float yaw = Mth(mc.player.yRotO, mc.player.getYRot(), partial), pitch = Mth(mc.player.xRotO, mc.player.getXRot(), partial);
			double[] dir = Rotations.direction(yaw, pitch + (float) p[2]);
			Vec3 pos = new Vec3(mc.player.getX(), mc.player.getEyeY() - 0.1, mc.player.getZ());
			Vec3 vel = new Vec3(dir[0], dir[1], dir[2]).scale(p[0]);
			List<Vec3> points = new ArrayList<>();
			points.add(pos);
			Vec3 hitPoint = null;
			for (int t = 0; t < 200; t++) {
				Vec3 next = pos.add(vel);
				var hit = mc.level.clip(new ClipContext(pos, next, ClipContext.Block.COLLIDER, ClipContext.Fluid.NONE, mc.player));
				if (hit.getType() != HitResult.Type.MISS) {
					hitPoint = hit.getLocation();
					points.add(hitPoint);
					break;
				}
				points.add(next);
				pos = next;
				vel = new Vec3(vel.x * Ballistics.DRAG, vel.y * Ballistics.DRAG - p[1], vel.z * Ballistics.DRAG);
				if (pos.y < mc.level.getMinBuildHeight() - 16) break;
			}
			Render3d.begin(ctx, false);
			for (int i = 1; i < points.size(); i++) Render3d.line(points.get(i - 1), points.get(i), 0xFFFFFFFF);
			if (landing.get() && hitPoint != null) Render3d.box(new AABB(hitPoint, hitPoint).inflate(0.25), 0xFFFF5555);
			Render3d.end();
		}

		private static float Mth(float a, float b, float t) {
			return a + (b - a) * t;
		}
	}

	public static final class Freecam extends Module {
		private final NumberSetting speed = num("Speed", 0.6, 0.1, 3.0, 0.05);

		public Freecam() {
			super("Freecam", Category.RENDER, "Fly the camera around while your character stays where it is.");
		}

		@Override
		protected void onEnable() {
			Hooks.freecam = true;
			Hooks.camPos = Hooks.camPosPrev = mc.player.getEyePosition();
			Hooks.camYaw = mc.player.getYRot();
			Hooks.camPitch = mc.player.getXRot();
		}

		@Override
		protected void onDisable() {
			Hooks.freecam = false;
		}

		@Override
		public void onTick() {
			Hooks.camPosPrev = Hooks.camPos;
			float forward = (mc.options.keyUp.isDown() ? 1 : 0) - (mc.options.keyDown.isDown() ? 1 : 0);
			float left = (mc.options.keyLeft.isDown() ? 1 : 0) - (mc.options.keyRight.isDown() ? 1 : 0);
			float up = (mc.options.keyJump.isDown() ? 1 : 0) - (mc.options.keyShift.isDown() ? 1 : 0);
			double[] look = Rotations.direction(Hooks.camYaw, Hooks.camPitch);
			double[] side = dev.aero.client.util.MoveUtil.direction(Hooks.camYaw, 0, 1);
			Vec3 move = new Vec3(look[0] * forward + side[0] * left, look[1] * forward + up, look[2] * forward + side[1] * left);
			if (move.lengthSqr() > 1.0e-6) Hooks.camPos = Hooks.camPos.add(move.normalize().scale(speed.d()));
		}
	}

	public static final class Zoom extends Module {
		private final NumberSetting factor = num("Zoom", 4.0, 1.5, 12.0, 0.5);
		private double current = 1.0;

		public Zoom() {
			super("Zoom", Category.RENDER, "Zooms the view in. Bind it to a key and tap to toggle.");
		}

		@Override
		protected void onEnable() {
			current = 1.0;
		}

		@Override
		public void onTick() {
			current += (1.0 / factor.d() - current) * 0.45;
			Hooks.fovScale = current;
		}

		@Override
		protected void onDisable() {
			Hooks.fovScale = 1.0;
		}
	}

	public static final class FovChanger extends Module {
		private final NumberSetting fov = num("FOV", 100, 30, 150, 1);

		public FovChanger() {
			super("FovChanger", Category.RENDER, "Sets a fixed field of view.");
		}

		@Override
		public void onTick() {
			Hooks.fovFixed = fov.d();
		}

		@Override
		protected void onDisable() {
			Hooks.fovFixed = 0;
		}
	}

	public static final class NoOverlay extends Module {
		private final BoolSetting fire = bool("Fire", true);
		private final BoolSetting water = bool("Underwater", true);
		private final BoolSetting wall = bool("Inside blocks", true);
		private final BoolSetting pumpkin = bool("Pumpkin", true);

		public NoOverlay() {
			super("NoOverlay", Category.RENDER, "Hides the screen overlays for fire, water, being inside a block and the pumpkin.");
		}

		@Override
		public void onTick() {
			Hooks.noFireOverlay = fire.get();
			Hooks.noWaterOverlay = water.get();
			Hooks.noWallOverlay = wall.get();
			Hooks.noPumpkin = pumpkin.get();
		}

		@Override
		protected void onDisable() {
			Hooks.noFireOverlay = Hooks.noWaterOverlay = Hooks.noWallOverlay = Hooks.noPumpkin = false;
		}
	}

	public static final class NoHurtCam extends Module {
		public NoHurtCam() {
			super("NoHurtCam", Category.RENDER, "The screen no longer shakes when you are hit.");
		}

		@Override
		protected void onEnable() {
			Hooks.noHurtCam = true;
		}

		@Override
		protected void onDisable() {
			Hooks.noHurtCam = false;
		}
	}

	public static final class NoBob extends Module {
		private boolean previous;

		public NoBob() {
			super("NoBob", Category.RENDER, "Turns off view bobbing while walking.");
		}

		@Override
		protected void onEnable() {
			previous = mc.options.bobView().get();
			mc.options.bobView().set(false);
		}

		@Override
		protected void onDisable() {
			mc.options.bobView().set(previous);
		}
	}

	public static final class NoWeather extends Module {
		public NoWeather() {
			super("NoWeather", Category.RENDER, "Rain and thunder are not drawn.");
		}

		@Override
		protected void onEnable() {
			Hooks.noWeather = true;
		}

		@Override
		protected void onDisable() {
			Hooks.noWeather = false;
		}
	}

	public static final class TimeChanger extends Module {
		private final NumberSetting time = num("Time of day (ticks)", 6000, 0, 24000, 250);

		public TimeChanger() {
			super("TimeChanger", Category.RENDER, "Sets the time of day on your screen only. 0 sunrise, 6000 noon, 13000 dusk, 18000 midnight.");
		}

		@Override
		public void onTick() {
			mc.level.setDayTime(time.i());
		}
	}

	public static final class Breadcrumbs extends Module {
		private final NumberSetting length = num("Length (points)", 400, 50, 3000, 50);
		private final LinkedList<Vec3> trail = new LinkedList<>();

		public Breadcrumbs() {
			super("Breadcrumbs", Category.RENDER, "Draws the path you walked, to find your way back.");
		}

		@Override
		protected void onEnable() {
			trail.clear();
		}

		@Override
		public void onTick() {
			Vec3 p = mc.player.position();
			if (trail.isEmpty() || trail.getLast().distanceToSqr(p) > 0.09) trail.add(p);
			while (trail.size() > length.i()) trail.removeFirst();
		}

		@Override
		public void onRender3d(WorldRenderContext ctx) {
			if (trail.size() < 2) return;
			Render3d.begin(ctx, true);
			Vec3 prev = null;
			int i = 0, n = trail.size();
			for (Vec3 p : trail) {
				if (prev != null) Render3d.line(prev.add(0, 0.1, 0), p.add(0, 0.1, 0), Render3d.rgb(80, 180, 255, 80 + 175 * i / n));
				prev = p;
				i++;
			}
			Render3d.end();
		}
	}

	public static final class HoleESP extends Module {
		private final NumberSetting range = num("Range", 8, 3, 16, 1);
		private record Hole(BlockPos pos, boolean allBedrock) {
		}

		private List<Hole> holes = new ArrayList<>();
		private int ticks = 10;

		public HoleESP() {
			super("HoleESP", Category.RENDER, "Marks one-block holes walled in by bedrock (green) or obsidian (orange).");
		}

		@Override
		protected void onDisable() {
			holes = new ArrayList<>();
			ticks = 10;
		}

		private static boolean hard(BlockState s) {
			return s.is(Blocks.BEDROCK) || s.is(Blocks.OBSIDIAN) || s.is(Blocks.CRYING_OBSIDIAN);
		}

		@Override
		public void onTick() {
			if (++ticks < 10) return;
			ticks = 0;
			List<Hole> out = new ArrayList<>();
			BlockPos base = mc.player.blockPosition();
			int r = range.i();
			for (BlockPos p : BlockPos.betweenClosed(base.offset(-r, -4, -r), base.offset(r, 3, r))) {
				if (!mc.level.getBlockState(p).isAir() || !mc.level.getBlockState(p.above()).isAir()) continue;
				BlockState floor = mc.level.getBlockState(p.below());
				if (!hard(floor)) continue;
				boolean bedrock = floor.is(Blocks.BEDROCK), ok = true;
				for (Direction d : Direction.Plane.HORIZONTAL) {
					BlockState s = mc.level.getBlockState(p.relative(d));
					if (!hard(s)) {
						ok = false;
						break;
					}
					bedrock &= s.is(Blocks.BEDROCK);
				}
				if (ok) out.add(new Hole(p.immutable(), bedrock));
				if (out.size() >= 128) break;
			}
			holes = out;
		}

		@Override
		public String info() {
			return holes.isEmpty() ? null : String.valueOf(holes.size());
		}

		@Override
		public void onRender3d(WorldRenderContext ctx) {
			if (holes.isEmpty()) return;
			Render3d.begin(ctx, true);
			for (Hole h : holes) {
				BlockPos p = h.pos();
				Render3d.box(new AABB(p.getX(), p.getY(), p.getZ(), p.getX() + 1, p.getY() + 0.05, p.getZ() + 1), h.allBedrock() ? 0xFF4DFF88 : 0xFFFFA83D);
			}
			Render3d.end();
		}
	}

	public static final class LogoutSpots extends Module {
		private record Spot(String name, Vec3 pos) {
		}

		private final Map<UUID, Vec3> lastSeen = new HashMap<>();
		private final Map<UUID, String> names = new HashMap<>();
		private final Map<UUID, Spot> spots = new HashMap<>();

		public LogoutSpots() {
			super("LogoutSpots", Category.RENDER, "Marks where players were standing when they left the server.");
		}

		@Override
		protected void onEnable() {
			lastSeen.clear();
			names.clear();
			spots.clear();
		}

		@Override
		public void onTick() {
			Set<UUID> online = new HashSet<>();
			for (PlayerInfo info : mc.getConnection().getOnlinePlayers()) online.add(info.getProfile().getId());
			for (Player p : mc.level.players()) {
				if (p == mc.player) continue;
				lastSeen.put(p.getUUID(), p.position());
				names.put(p.getUUID(), p.getGameProfile().getName());
				spots.remove(p.getUUID());
			}
			for (UUID id : new ArrayList<>(lastSeen.keySet())) {
				if (online.contains(id)) continue;
				Vec3 pos = lastSeen.remove(id);
				String name = names.getOrDefault(id, "player");
				spots.put(id, new Spot(name, pos));
				Chat.info(name + " logged out at " + (int) pos.x + " " + (int) pos.y + " " + (int) pos.z);
			}
		}

		@Override
		public String info() {
			return spots.isEmpty() ? null : String.valueOf(spots.size());
		}

		@Override
		public void onRender3d(WorldRenderContext ctx) {
			if (spots.isEmpty()) return;
			Render3d.begin(ctx, true);
			for (Spot s : spots.values()) Render3d.box(new AABB(s.pos().x - 0.3, s.pos().y, s.pos().z - 0.3, s.pos().x + 0.3, s.pos().y + 1.8, s.pos().z + 0.3), 0xFFFF9F43);
			Render3d.end();
		}

		@Override
		public void onRender2d(GuiGraphics g, float partialTick) {
			for (Spot s : spots.values()) {
				float[] p = Render3d.project(s.pos().x, s.pos().y + 2.0, s.pos().z);
				if (p == null) continue;
				String text = s.name() + " (logged out)";
				g.drawString(mc.font, text, (int) p[0] - mc.font.width(text) / 2, (int) p[1], 0xFFFF9F43, true);
			}
		}
	}

	public static final class Radar extends Module {
		private final NumberSetting range = num("Range", 40, 10, 128, 1);
		private final NumberSetting size = num("Size", 80, 50, 160, 2);
		private final BoolSetting items = bool("Items", false);

		public Radar() {
			super("Radar", Category.RENDER, "Top-down map of nearby players and mobs; up is the way you face.");
		}

		@Override
		public void onRender2d(GuiGraphics g, float partialTick) {
			if (mc.options.hideGui || mc.screen != null) return;
			int s = size.i(), x0 = 6, y0 = mc.getWindow().getGuiScaledHeight() / 2 - s / 2;
			int cx = x0 + s / 2, cy = y0 + s / 2;
			g.fill(x0, y0, x0 + s, y0 + s, 0x90000000);
			g.fill(cx, y0, cx + 1, y0 + s, 0x30FFFFFF);
			g.fill(x0, cy, x0 + s, cy + 1, 0x30FFFFFF);
			g.fill(cx - 1, cy - 1, cx + 2, cy + 2, 0xFFFFFFFF);
			double rad = Math.toRadians(mc.player.getYRot());
			double fx = -Math.sin(rad), fz = Math.cos(rad), rx = -Math.cos(rad), rz = -Math.sin(rad);
			double scale = (s / 2.0 - 2) / range.d();
			for (Entity e : mc.level.entitiesForRendering()) {
				int color;
				if (e instanceof ItemEntity) {
					if (!items.get()) continue;
					color = 0xFFFFD166;
				} else if (e != mc.player && e instanceof LivingEntity && !(e instanceof ArmorStand)) {
					color = colorOf(e);
				} else {
					continue;
				}
				double dx = e.getX() - mc.player.getX(), dz = e.getZ() - mc.player.getZ();
				double f = dx * fx + dz * fz, r = dx * rx + dz * rz;
				int px = cx + (int) Math.round(r * scale), py = cy - (int) Math.round(f * scale);
				if (px < x0 + 1 || px > x0 + s - 3 || py < y0 + 1 || py > y0 + s - 3) continue;
				g.fill(px, py, px + 2, py + 2, color);
			}
		}
	}

	public static final class Glow extends Module {
		private final BoolSetting players = bool("Players", true);
		private final BoolSetting hostile = bool("Hostile mobs", true);
		private final BoolSetting passive = bool("Passive mobs", false);
		private final Set<Integer> changed = new HashSet<>();

		public Glow() {
			super("Glow", Category.RENDER, "Gives players and mobs the vanilla glowing outline, visible through walls.");
		}

		@Override
		public void onTick() {
			Set<Integer> now = new HashSet<>();
			for (Entity e : mc.level.entitiesForRendering()) {
				if (!wanted(e, players.get(), hostile.get(), passive.get())) continue;
				((EntityAccessor) e).aero$setSharedFlag(6, true);
				now.add(e.getId());
			}
			for (Integer id : new ArrayList<>(changed)) {
				if (now.contains(id)) continue;
				Entity e = mc.level.getEntity(id);
				if (e != null) ((EntityAccessor) e).aero$setSharedFlag(6, false);
			}
			changed.clear();
			changed.addAll(now);
		}

		@Override
		protected void onDisable() {
			if (mc.level == null) return;
			for (Integer id : changed) {
				Entity e = mc.level.getEntity(id);
				if (e != null) ((EntityAccessor) e).aero$setSharedFlag(6, false);
			}
			changed.clear();
		}
	}

	public static final class AntiBlind extends Module {
		public AntiBlind() {
			super("AntiBlind", Category.RENDER, "Blindness, darkness and nausea effects do not affect your screen.");
		}

		@Override
		protected void onEnable() {
			Hooks.antiBlind = true;
		}

		@Override
		protected void onDisable() {
			Hooks.antiBlind = false;
		}
	}

	public static final class ChunkBorders extends Module {
		private final NumberSetting radius = num("Chunks around you", 1, 0, 3, 1);

		public ChunkBorders() {
			super("ChunkBorders", Category.RENDER, "Draws chunk edges; the yellow rectangles mark the 16-block sections of your chunk.");
		}

		@Override
		public void onRender3d(WorldRenderContext ctx) {
			Render3d.begin(ctx, true);
			int cx = mc.player.blockPosition().getX() >> 4, cz = mc.player.blockPosition().getZ() >> 4;
			double y0 = mc.player.getY() - 24, y1 = mc.player.getY() + 24;
			int r = radius.i();
			for (int x = cx - r; x <= cx + r + 1; x++) {
				for (int z = cz - r; z <= cz + r + 1; z++) Render3d.line(x * 16, y0, z * 16, x * 16, y1, z * 16, 0x905DADFF);
			}
			int minY = mc.level.getMinBuildHeight(), maxY = mc.level.getMaxBuildHeight();
			for (int y = minY; y <= maxY; y += 16) {
				if (y < y0 - 16 || y > y1 + 16) continue;
				double x0 = cx * 16, z0 = cz * 16, x1 = x0 + 16, z1 = z0 + 16;
				int c = 0xA0FFD166;
				Render3d.line(x0, y, z0, x1, y, z0, c);
				Render3d.line(x1, y, z0, x1, y, z1, c);
				Render3d.line(x1, y, z1, x0, y, z1, c);
				Render3d.line(x0, y, z1, x0, y, z0, c);
			}
			Render3d.end();
		}
	}

	public static final class TargetHud extends Module {
		public TargetHud() {
			super("TargetHud", Category.RENDER, "Shows the health of the entity you are fighting or looking at.");
		}

		@Override
		public void onRender2d(GuiGraphics g, float partialTick) {
			if (mc.options.hideGui || mc.screen != null) return;
			LivingEntity t = null;
			var aura = ModuleManager.get(CombatModules.KillAura.class);
			if (aura != null && aura.isEnabled()) t = aura.target;
			if (t == null && mc.hitResult instanceof net.minecraft.world.phys.EntityHitResult hit && hit.getEntity() instanceof LivingEntity le) t = le;
			if (t == null) return;
			int w = 120, x = mc.getWindow().getGuiScaledWidth() / 2 + 24, y = mc.getWindow().getGuiScaledHeight() / 2 + 24;
			g.fill(x, y, x + w, y + 30, 0xA0000000);
			g.drawString(mc.font, t.getName().getString(), x + 4, y + 3, 0xFFFFFFFF, true);
			float hp = Math.max(0, t.getHealth()), max = Math.max(1, t.getMaxHealth());
			g.fill(x + 4, y + 16, x + w - 4, y + 22, 0xFF331111);
			int fill = (int) ((w - 8) * Math.min(1, hp / max));
			g.fill(x + 4, y + 16, x + 4 + fill, y + 22, hp / max > 0.5 ? 0xFF3DDC84 : hp / max > 0.25 ? 0xFFFFD166 : 0xFFFF5555);
			String text = String.format("%.1f / %.0f   %.1fm", hp, max, mc.player.distanceTo(t));
			g.drawString(mc.font, text, x + 4, y + 22, 0xFFB5C4D3, false);
		}
	}
}
