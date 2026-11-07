package dev.aero.client;

import dev.aero.client.Hooks;
import dev.aero.client.module.Module;
import dev.aero.client.module.ModuleManager;
import dev.aero.client.module.impl.AutoMine;
import dev.aero.client.module.impl.Navigator;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.minecraft.client.Minecraft;
import net.minecraft.core.BlockPos;
import net.minecraft.core.registries.Registries;
import net.minecraft.world.Difficulty;
import net.minecraft.world.level.GameRules;
import net.minecraft.world.level.GameType;
import net.minecraft.world.level.LevelSettings;
import net.minecraft.world.level.WorldDataConfiguration;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.levelgen.WorldOptions;
import net.minecraft.world.level.levelgen.presets.WorldPresets;

import java.util.ArrayList;
import java.util.List;
import java.util.function.BooleanSupplier;

/**
 * Started by the `runWorldtest` Gradle task (-Daero.worldtest=true). Creates a flat survival world, builds small
 * arenas with commands, runs AutoMine, the navigator and then every module for a few ticks, and logs one
 * WORLDTEST line per check and a final PASSED/FAILED. It never touches your own saves: the world is new and
 * lives in the dev run folder.
 */
final class WorldTest {
	private static final int FLOOR = -60; // feet level on the default flat world (grass top is y=-61)

	private abstract static class Phase {
		final String name;
		final int timeout;
		int waited;

		Phase(String name, int timeout) {
			this.name = name;
			this.timeout = timeout;
		}

		void begin() {
		}

		abstract boolean done();

		/** Failure text, or null when the phase passed. Called once when done() is true or the timeout hit. */
		abstract String verify(boolean timedOut);

		void end() {
		}
	}

	private static final List<Phase> PHASES = new ArrayList<>();
	private static int index = -1, ticks, failures;
	private static boolean worldRequested;
	private static final List<String> RESULTS = new ArrayList<>();

	private WorldTest() {
	}

	private static void cmd(String c) {
		Minecraft.getInstance().getConnection().sendCommand(c);
	}

	private static boolean airAt(int x, int y, int z) {
		return Minecraft.getInstance().level.getBlockState(new BlockPos(x, y, z)).isAir();
	}

	private static boolean isBlock(int x, int y, int z, net.minecraft.world.level.block.Block b) {
		return Minecraft.getInstance().level.getBlockState(new BlockPos(x, y, z)).is(b);
	}

	private static Phase waitTicks(String name, int n, Runnable begin) {
		int[] left = {n};
		return new Phase(name, n + 20) {
			@Override
			void begin() {
				begin.run();
			}

			@Override
			boolean done() {
				return --left[0] <= 0;
			}

			@Override
			String verify(boolean timedOut) {
				return null;
			}
		};
	}

	/** Runs begin, waits n ticks, then verify decides: null means passed. */
	private static Phase checkAfter(String name, int n, Runnable begin, java.util.function.Supplier<String> verify) {
		int[] left = {n};
		return new Phase(name, n + 20) {
			@Override
			void begin() {
				begin.run();
			}

			@Override
			boolean done() {
				return --left[0] <= 0;
			}

			@Override
			String verify(boolean timedOut) {
				return verify.get();
			}
		};
	}

	private static Phase until(String name, int timeout, Runnable begin, BooleanSupplier success, Runnable end, java.util.function.Supplier<String> failText) {
		return new Phase(name, timeout) {
			@Override
			void begin() {
				begin.run();
			}

			@Override
			boolean done() {
				return success.getAsBoolean();
			}

			@Override
			String verify(boolean timedOut) {
				return timedOut ? failText.get() : null;
			}

			@Override
			void end() {
				end.run();
			}
		};
	}

	private static AutoMine mine() {
		return ModuleManager.get(AutoMine.class);
	}

	private static void buildPhases() {
		var mc = Minecraft.getInstance();

		PHASES.add(waitTicks("world settles", 80, () -> {
			mc.getTutorial().setStep(net.minecraft.client.tutorial.TutorialSteps.NONE);
			cmd("gamerule doDaylightCycle false");
			cmd("gamerule doMobSpawning false");
			cmd("gamerule sendCommandFeedback false");
			cmd("gamerule logAdminCommands false");
			cmd("time set day");
		}));

		// 1. ore standing in the open: walk to it and break it
		PHASES.add(waitTicks("arena 1: open ground", 40, () -> {
			cmd("tp @s 0 " + FLOOR + " 0");
			cmd("fill -14 " + FLOOR + " -14 14 " + (FLOOR + 12) + " 14 air");
			cmd("give @s iron_pickaxe");
			cmd("setblock 7 " + FLOOR + " 0 diamond_ore");
		}));
		PHASES.add(until("AutoMine mines an ore it can walk to", 700, () -> {
			mine().setBlocks(List.of("diamond_ore"));
			mine().setEnabled(true);
		}, () -> airAt(7, FLOOR, 0), () -> mine().setEnabled(false),
				() -> "ore still there; player at " + mc.player.blockPosition().toShortString() + " status=" + mine().info()));

		// 1b. tools: a pickaxe too weak for the ore, no pickaxe at all, and the right pickaxe lying in the main inventory
		for (String[] t : new String[][] {{"a wooden pickaxe cannot take diamond ore", "wooden_pickaxe"}, {"no pickaxe at all", null}}) {
			PHASES.add(waitTicks("tools: setup (" + t[1] + ")", 30, () -> {
				mine().setEnabled(false);
				cmd("clear @s");
				if (t[1] != null) cmd("give @s " + t[1]);
				cmd("tp @s 0 " + FLOOR + " 0");
				cmd("setblock 7 " + FLOOR + " 0 diamond_ore");
			}));
			PHASES.add(new Phase("AutoMine stops: " + t[0], 200) {
				@Override
				void begin() {
					mine().setBlocks(List.of("diamond_ore"));
					mine().setEnabled(true);
				}

				@Override
				boolean done() {
					return !mine().isEnabled() || waited >= 120;
				}

				@Override
				String verify(boolean timedOut) {
					if (mine().isEnabled()) return "AutoMine kept running";
					return isBlock(7, FLOOR, 0, Blocks.DIAMOND_ORE) ? null : "the ore was mined although the tool is not enough";
				}

				@Override
				void end() {
					mine().setEnabled(false);
				}
			});
		}
		PHASES.add(waitTicks("tools: setup (pickaxe only in the main inventory)", 30, () -> {
			mine().setEnabled(false);
			cmd("clear @s");
			cmd("item replace entity @s inventory.3 with minecraft:diamond_pickaxe"); // inventory.3 is index 12, not on the hotbar
			cmd("tp @s 0 " + FLOOR + " 0");
			cmd("setblock 7 " + FLOOR + " 0 diamond_ore");
		}));
		PHASES.add(until("AutoMine takes the pickaxe from the main inventory and mines", 700, () -> {
			mine().setBlocks(List.of("diamond_ore"));
			mine().setEnabled(true);
		}, () -> airAt(7, FLOOR, 0), () -> mine().setEnabled(false),
				() -> "ore still there; status=" + mine().info() + " enabled=" + mine().isEnabled()));
		PHASES.add(checkAfter("the pickaxe was swapped into the hand", 5, () -> {
		}, () -> mc.player.getInventory().getSelected().is(net.minecraft.world.item.Items.DIAMOND_PICKAXE) ? null
				: "hand holds " + mc.player.getInventory().getSelected()));
		PHASES.add(waitTicks("tools: restore the iron pickaxe", 20, () -> {
			cmd("clear @s");
			cmd("give @s iron_pickaxe");
		}));

		// 1c. a straight road with low walls on it: the walk must not circle back to cells it has already passed
		PHASES.add(waitTicks("road: setup", 50, () -> {
			cmd("tp @s 0 " + FLOOR + " 100");
			cmd("fill -4 " + FLOOR + " 94 60 " + (FLOOR + 6) + " 106 air");
			for (int wx = 12; wx < 60; wx += 12) cmd("fill " + wx + " " + FLOOR + " 96 " + wx + " " + FLOOR + " 104 stone");
		}));
		dev.aero.client.pathing.PathExecutor[] road = {null};
		double[] walked = {0}, last = {0, 0};
		PHASES.add(new Phase("road: walks 50 blocks along a road with walls without circling", 900) {
			@Override
			void begin() {
				road[0] = new dev.aero.client.pathing.PathExecutor();
				road[0].config.maxExpansions = 30000;
				road[0].searchBudget = 2500;
				last[0] = mc.player.getX();
				last[1] = mc.player.getZ();
				road[0].start(new dev.aero.client.path.Goal.Position(50, FLOOR, 100, 1.0));
			}

			@Override
			boolean done() {
				road[0].tick();
				walked[0] += Math.hypot(mc.player.getX() - last[0], mc.player.getZ() - last[1]);
				last[0] = mc.player.getX();
				last[1] = mc.player.getZ();
				return !road[0].active();
			}

			@Override
			String verify(boolean timedOut) {
				if (timedOut) return "still walking: state=" + road[0].state() + " at " + mc.player.blockPosition().toShortString();
				if (road[0].state() != dev.aero.client.pathing.PathExecutor.State.ARRIVED) return "state " + road[0].state() + ": " + road[0].message();
				double straight = Math.abs(50 - 0);
				return walked[0] <= straight * 1.2 + 3 ? null : String.format("walked %.0f blocks for a %.0f block road", walked[0], straight);
			}

			@Override
			void end() {
				road[0].cancel();
			}
		});

		// 1c2. the same on bumpy ground: steps up, holes down, at every other block
		PHASES.add(waitTicks("bumpy road: setup", 60, () -> {
			cmd("tp @s 0 " + FLOOR + " 130");
			cmd("fill -4 " + (FLOOR - 3) + " 124 64 " + (FLOOR + 6) + " 136 air");
			cmd("fill -4 " + (FLOOR - 3) + " 124 64 " + (FLOOR - 1) + " 136 stone");
			java.util.Random rnd = new java.util.Random(7);
			for (int bx = 4; bx < 56; bx += 2) {
				int kind = rnd.nextInt(3);
				if (kind == 1) cmd("fill " + bx + " " + FLOOR + " 124 " + bx + " " + FLOOR + " 136 stone"); // a step up
				if (kind == 2) cmd("fill " + bx + " " + (FLOOR - 1) + " 124 " + bx + " " + (FLOOR - 1) + " 136 air"); // a hole one deep
			}
		}));
		double[] walked2 = {0}, last2 = {0, 0};
		PHASES.add(new Phase("bumpy road: walks 50 blocks without circling", 1200) {
			@Override
			void begin() {
				road[0] = new dev.aero.client.pathing.PathExecutor();
				road[0].config.maxExpansions = 60000;
				road[0].searchBudget = 2500;
				last2[0] = mc.player.getX();
				last2[1] = mc.player.getZ();
				road[0].start(new dev.aero.client.path.Goal.Position(50, FLOOR, 130, 1.0));
			}

			@Override
			boolean done() {
				road[0].tick();
				walked2[0] += Math.hypot(mc.player.getX() - last2[0], mc.player.getZ() - last2[1]);
				last2[0] = mc.player.getX();
				last2[1] = mc.player.getZ();
				return !road[0].active();
			}

			@Override
			String verify(boolean timedOut) {
				if (timedOut) return "still walking: state=" + road[0].state() + " at " + mc.player.blockPosition().toShortString() + " walked " + (int) walked2[0];
				if (road[0].state() != dev.aero.client.pathing.PathExecutor.State.ARRIVED) return "state " + road[0].state() + ": " + road[0].message();
				return walked2[0] <= 50 * 1.3 + 4 ? null : String.format("walked %.0f blocks for a 50 block road", walked2[0]);
			}

			@Override
			void end() {
				road[0].cancel();
			}
		});

		// 1d. a whole vein: a diagonal chain of five ores; once the first is mined a closer ore (the decoy) appears,
		// and AutoMine must still finish the vein before it goes for the decoy
		PHASES.add(waitTicks("vein: setup", 40, () -> {
			cmd("tp @s -50 " + FLOOR + " 60");
			cmd("fill -70 " + FLOOR + " 50 -44 " + (FLOOR + 6) + " 72 air");
			for (int i = 0; i < 5; i++) cmd("setblock " + (-60 - i) + " " + FLOOR + " " + (60 + i) + " diamond_ore");
		}));
		BlockPos[] decoy = {null};
		PHASES.add(new Phase("AutoMine finishes the whole vein before another ore", 1500) {
			boolean placed;

			@Override
			void begin() {
				mine().setBlocks(List.of("diamond_ore"));
				mine().setEnabled(true);
			}

			@Override
			boolean done() {
				if (!placed && airAt(-60, FLOOR, 60)) {
					placed = true;
					decoy[0] = mc.player.blockPosition().offset(0, 0, -2);
					cmd("setblock " + decoy[0].getX() + " " + decoy[0].getY() + " " + decoy[0].getZ() + " diamond_ore");
				}
				for (int i = 0; i < 5; i++) if (!airAt(-60 - i, FLOOR, 60 + i)) return false;
				return true;
			}

			@Override
			String verify(boolean timedOut) {
				if (timedOut) return "vein not finished; status=" + mine().info();
				return decoy[0] != null && mc.level.getBlockState(decoy[0]).is(Blocks.DIAMOND_ORE) ? null : "the decoy ore was mined before the vein was finished (decoy=" + decoy[0] + ")";
			}

			@Override
			void end() {
				mine().setEnabled(false);
				if (decoy[0] != null) cmd("setblock " + decoy[0].getX() + " " + decoy[0].getY() + " " + decoy[0].getZ() + " air");
			}
		});

		// 2. ore inside a block of stone: has to dig a tunnel
		PHASES.add(waitTicks("arena 2: ore inside stone", 40, () -> {
			cmd("tp @s 18 " + FLOOR + " 23");
			cmd("fill 20 " + FLOOR + " 18 30 " + (FLOOR + 4) + " 28 stone");
			cmd("setblock 25 " + FLOOR + " 23 diamond_ore");
		}));
		PHASES.add(until("AutoMine digs to an ore inside stone", 1800, () -> {
			mine().setBlocks(List.of("diamond_ore"));
			mine().setEnabled(true);
		}, () -> airAt(25, FLOOR, 23), () -> mine().setEnabled(false),
				() -> "ore still there; player at " + mc.player.blockPosition().toShortString() + " status=" + mine().info()));
		PHASES.add(waitTicks("collect", 5, () -> {
		}));

		// 3. ore next to lava must be skipped
		PHASES.add(waitTicks("arena 3: ore beside lava", 40, () -> {
			cmd("tp @s -20 " + FLOOR + " -20");
			cmd("fill -26 " + FLOOR + " -26 -14 " + (FLOOR + 6) + " -14 air");
			cmd("setblock -24 " + FLOOR + " -20 diamond_ore");
			cmd("setblock -25 " + FLOOR + " -20 lava");
		}));
		PHASES.add(new Phase("AutoMine leaves an ore that touches lava alone", 220) {
			@Override
			void begin() {
				mine().setBlocks(List.of("diamond_ore"));
				mine().setEnabled(true);
			}

			@Override
			boolean done() {
				return waited >= 200;
			}

			@Override
			String verify(boolean timedOut) {
				if (!isBlock(-24, FLOOR, -20, Blocks.DIAMOND_ORE)) return "the ore next to lava was mined";
				String info = mine().info();
				return info != null && info.startsWith("no ore") ? null : "AutoMine did not report an empty search (status=" + info + ")";
			}

			@Override
			void end() {
				mine().setEnabled(false);
			}
		});

		// 3b. ore below the ground: dig down
		PHASES.add(waitTicks("arena 3b: ore under the ground", 40, () -> {
			cmd("tp @s 30 " + FLOOR + " -30");
			cmd("fill 24 " + FLOOR + " -36 36 " + (FLOOR + 6) + " -24 air");
			cmd("setblock 30 " + (FLOOR - 3) + " -30 diamond_ore");
		}));
		PHASES.add(until("AutoMine digs down to an ore under the ground", 900, () -> {
			mine().setBlocks(List.of("diamond_ore"));
			mine().setEnabled(true);
		}, () -> airAt(30, FLOOR - 3, -30), () -> mine().setEnabled(false),
				() -> "ore still there; player at " + mc.player.blockPosition().toShortString() + " status=" + mine().info()));

		// 3c. ore high up inside a stone pillar: climb by digging a staircase
		PHASES.add(waitTicks("arena 3c: ore high in a pillar", 40, () -> {
			cmd("tp @s 40 " + FLOOR + " 30");
			cmd("fill 42 " + FLOOR + " 26 48 " + (FLOOR + 8) + " 34 stone");
			cmd("setblock 45 " + (FLOOR + 4) + " 30 diamond_ore");
		}));
		PHASES.add(until("AutoMine climbs through stone to a high ore", 2500, () -> {
			mine().setBlocks(List.of("diamond_ore"));
			mine().setEnabled(true);
		}, () -> airAt(45, FLOOR + 4, 30), () -> mine().setEnabled(false),
				() -> "ore still there; player at " + mc.player.blockPosition().toShortString() + " status=" + mine().info()));

		// 3d: two ores, one cheap and one costly: the cheap one goes first
		PHASES.add(waitTicks("arena 3d: two ores", 40, () -> {
			cmd("tp @s -40 " + FLOOR + " 30");
			cmd("fill -48 " + FLOOR + " 22 -30 " + (FLOOR + 6) + " 38 air");
			cmd("fill -36 " + FLOOR + " 28 -32 " + (FLOOR + 3) + " 32 stone");
			cmd("setblock -34 " + FLOOR + " 30 diamond_ore");
			cmd("setblock -44 " + FLOOR + " 30 diamond_ore");
		}));
		PHASES.add(until("AutoMine takes the ore in the open before the one behind stone", 900, () -> {
			mine().setBlocks(List.of("diamond_ore"));
			mine().setEnabled(true);
		}, () -> airAt(-44, FLOOR, 30) && airAt(-34, FLOOR, 30), () -> mine().setEnabled(false),
				() -> "open ore gone=" + airAt(-44, FLOOR, 30) + ", walled ore gone=" + airAt(-34, FLOOR, 30) + ", player at " + mc.player.blockPosition().toShortString()));

		// 4. navigation
		PHASES.add(waitTicks("arena 4: clear ground", 40, () -> {
			cmd("tp @s 0 " + FLOOR + " 40");
			cmd("fill -4 " + FLOOR + " 36 40 " + (FLOOR + 6) + " 46 air");
		}));
		PHASES.add(until("Navigator walks 30 blocks", 700, () -> ModuleManager.get(Navigator.class).goTo(30, FLOOR, 41, 1.5, false),
				() -> mc.player.position().distanceTo(new net.minecraft.world.phys.Vec3(30.5, FLOOR, 41.5)) < 2.5,
				() -> ModuleManager.get(Navigator.class).stopWalking(),
				() -> "stopped at " + mc.player.blockPosition().toShortString()));

		// 4b. behaviour of individual modules
		PHASES.add(waitTicks("arena 5: combat setup", 40, () -> {
			cmd("tp @s 0 " + FLOOR + " 0");
			cmd("fill -14 " + FLOOR + " -14 14 " + (FLOOR + 12) + " 14 air");
			cmd("kill @e[type=!player]");
			cmd("clear @s");
			cmd("give @s diamond_sword");
			cmd("summon husk 2 " + FLOOR + " 0 {NoAI:1b,PersistenceRequired:1b}");
		}));
		boolean[] huskSeen = {false};
		PHASES.add(until("KillAura kills a husk in reach", 300, () -> {
			huskSeen[0] = false;
			ModuleManager.byName("KillAura").setEnabled(true);
		}, () -> {
			var near = mc.level.getEntitiesOfClass(net.minecraft.world.entity.monster.Zombie.class, mc.player.getBoundingBox().inflate(8));
			if (near.stream().anyMatch(net.minecraft.world.entity.LivingEntity::isAlive)) huskSeen[0] = true;
			return huskSeen[0] && near.stream().noneMatch(net.minecraft.world.entity.LivingEntity::isAlive);
		}, () -> ModuleManager.byName("KillAura").setEnabled(false), () -> "husk seen alive=" + huskSeen[0] + ", and it did not die"));

		PHASES.add(until("AutoTotem puts a totem in the off-hand", 120, () -> {
			cmd("give @s totem_of_undying");
			ModuleManager.byName("AutoTotem").setEnabled(true);
		}, () -> mc.player.getOffhandItem().is(net.minecraft.world.item.Items.TOTEM_OF_UNDYING),
				() -> ModuleManager.byName("AutoTotem").setEnabled(false), () -> "off-hand holds " + mc.player.getOffhandItem()));

		PHASES.add(until("AutoArmor wears the best armour", 160, () -> {
			cmd("give @s iron_chestplate");
			cmd("give @s iron_boots");
			ModuleManager.byName("AutoArmor").setEnabled(true);
		}, () -> mc.player.getItemBySlot(net.minecraft.world.entity.EquipmentSlot.CHEST).is(net.minecraft.world.item.Items.IRON_CHESTPLATE)
				&& mc.player.getItemBySlot(net.minecraft.world.entity.EquipmentSlot.FEET).is(net.minecraft.world.item.Items.IRON_BOOTS),
				() -> ModuleManager.byName("AutoArmor").setEnabled(false),
				() -> "chest=" + mc.player.getItemBySlot(net.minecraft.world.entity.EquipmentSlot.CHEST) + " feet=" + mc.player.getItemBySlot(net.minecraft.world.entity.EquipmentSlot.FEET)));

		int[] foodBefore = {0};
		PHASES.add(until("AutoEat eats when hungry", 600, () -> {
			cmd("give @s cooked_beef 8");
			cmd("effect give @s hunger 30 255 true");
			ModuleManager.byName("AutoEat").setEnabled(true);
		}, () -> mc.player.getInventory().items.stream().filter(i -> i.is(net.minecraft.world.item.Items.COOKED_BEEF)).mapToInt(net.minecraft.world.item.ItemStack::getCount).sum() < 8,
				() -> {
					ModuleManager.byName("AutoEat").setEnabled(false);
					cmd("effect clear @s");
				}, () -> "food level " + mc.player.getFoodData().getFoodLevel() + ", nothing eaten"));

		PHASES.add(waitTicks("arena 6: blocks around", 40, () -> {
			cmd("effect clear @s");
			cmd("give @s diamond_pickaxe");
			cmd("give @s diamond_axe");
			cmd("fill 1 " + FLOOR + " -2 3 " + (FLOOR + 1) + " 2 stone");
			cmd("setblock -2 " + FLOOR + " 2 oak_log");
			cmd("setblock -2 " + (FLOOR + 1) + " 2 oak_log");
		}));
		PHASES.add(until("Nuker breaks the stone around you", 400, () -> ModuleManager.byName("Nuker").setEnabled(true),
				() -> airAt(1, FLOOR, 0) && airAt(2, FLOOR, 0) && airAt(3, FLOOR + 1, 1),
				() -> ModuleManager.byName("Nuker").setEnabled(false), () -> "stone left: " + !airAt(1, FLOOR, 0) + "/" + !airAt(2, FLOOR, 0) + "/" + !airAt(3, FLOOR + 1, 1)));
		PHASES.add(until("AutoLumber chops the logs", 400, () -> ModuleManager.byName("AutoLumber").setEnabled(true),
				() -> airAt(-2, FLOOR, 2) && airAt(-2, FLOOR + 1, 2), () -> ModuleManager.byName("AutoLumber").setEnabled(false),
				() -> "logs left: " + !airAt(-2, FLOOR, 2) + "/" + !airAt(-2, FLOOR + 1, 2)));

		PHASES.add(until("Fullbright gives night vision", 60, () -> ModuleManager.byName("Fullbright").setEnabled(true),
				() -> mc.player.hasEffect(net.minecraft.world.effect.MobEffects.NIGHT_VISION), () -> ModuleManager.byName("Fullbright").setEnabled(false),
				() -> "no night vision effect"));
		PHASES.add(until("Fly (Creative mode) switches flying on", 60, () -> ModuleManager.byName("Fly").setEnabled(true),
				() -> mc.player.getAbilities().flying, () -> ModuleManager.byName("Fly").setEnabled(false), () -> "flying flag stayed off"));
		PHASES.add(until("Sprint with AutoWalk sprints", 80, () -> {
			ModuleManager.byName("AutoWalk").setEnabled(true);
			ModuleManager.byName("Sprint").setEnabled(true);
		}, () -> mc.player.isSprinting(), () -> {
			ModuleManager.byName("AutoWalk").setEnabled(false);
			ModuleManager.byName("Sprint").setEnabled(false);
		}, () -> "not sprinting"));
		PHASES.add(new Phase("hook values are set and put back", 30) {
			@Override
			void begin() {
				ModuleManager.byName("Velocity").setEnabled(true);
				ModuleManager.byName("Reach").setEnabled(true);
				ModuleManager.byName("Timer").setEnabled(true);
				ModuleManager.byName("NoSlow").setEnabled(true);
				ModuleManager.byName("FovChanger").setEnabled(true);
			}

			@Override
			boolean done() {
				return waited >= 4;
			}

			@Override
			String verify(boolean timedOut) {
				boolean on = Hooks.knockbackHorizontal == 0 && Hooks.reachEntity > 0 && Hooks.timerSpeed > 1 && Hooks.noSlow && Hooks.fovFixed > 0;
				for (String n : new String[] {"Velocity", "Reach", "Timer", "NoSlow", "FovChanger"}) ModuleManager.byName(n).setEnabled(false);
				boolean off = Hooks.knockbackHorizontal == 1 && Hooks.reachEntity == 0 && Hooks.timerSpeed == 1 && !Hooks.noSlow && Hooks.fovFixed == 0;
				return on && off ? null : "on=" + on + " off=" + off;
			}
		});
		int[] diedSeen = {0};
		PHASES.add(until("AutoRespawn brings you back after dying", 400, () -> {
			ModuleManager.byName("AutoRespawn").setEnabled(true);
			cmd("kill @s");
		}, () -> {
			if (mc.player.isDeadOrDying()) diedSeen[0] = 1;
			return diedSeen[0] == 1 && !mc.player.isDeadOrDying();
		}, () -> ModuleManager.byName("AutoRespawn").setEnabled(false), () -> "still dead, died seen=" + diedSeen[0]));

		// 5. every module for a few ticks, one at a time
		PHASES.add(waitTicks("reset before module sweep", 30, () -> {
			cmd("tp @s 0 " + FLOOR + " 0");
			cmd("fill -14 " + FLOOR + " -14 14 " + (FLOOR + 12) + " 14 air");
			cmd("give @s bow");
			cmd("give @s arrow 16");
			cmd("give @s ender_pearl 4");
			cmd("give @s torch 16");
			cmd("give @s oak_planks 64");
			cmd("give @s golden_apple 4");
			cmd("give @s totem_of_undying");
			cmd("give @s iron_chestplate");
			cmd("give @s wheat_seeds 16");
			cmd("give @s fishing_rod");
			cmd("setblock 3 " + FLOOR + " 3 chest");
			cmd("setblock 5 " + FLOOR + " 3 oak_log");
			cmd("give @s diamond_pickaxe"); // AutoMine now refuses to run without a pickaxe that can take its ore
			cmd("summon husk 6 " + FLOOR + " 6 {NoAI:1b,PersistenceRequired:1b}");
			cmd("summon cow -6 " + FLOOR + " 6 {NoAI:1b}");
		}));
		List<Module> sweep = new ArrayList<>();
		for (Module m : ModuleManager.all()) {
			if (m.hidden || m.name.equals("Panic") || m.name.equals("AutoLog")) continue;
			sweep.add(m);
		}
		for (Module m : sweep) {
			PHASES.add(new Phase("module " + m.name, 60) {
				int run;

				@Override
				void begin() {
					m.setEnabled(true);
				}

				@Override
				boolean done() {
					return ++run >= 12;
				}

				@Override
				String verify(boolean timedOut) {
					return m.isEnabled() ? null : "was switched off by a crash (see FAULT line above)";
				}

				@Override
				void end() {
					m.setEnabled(false);
					if (mc.player != null) {
						cmd("tp @s 0 " + FLOOR + " 0");
					}
				}
			});
		}
		// 6. screenshots for the documentation
		PHASES.add(waitTicks("showcase: build scene", 60, () -> {
			cmd("kill @e[type=!player]");
			cmd("effect clear @s");
			cmd("tp @s 0 " + FLOOR + " 0 29 8");
			cmd("fill -8 " + FLOOR + " 12 8 " + (FLOOR + 5) + " 14 stone");
			cmd("setblock -4 " + (FLOOR + 2) + " 13 diamond_ore");
			cmd("setblock 1 " + (FLOOR + 3) + " 13 ancient_debris");
			cmd("setblock 5 " + (FLOOR + 1) + " 13 emerald_ore");
			cmd("setblock -6 " + (FLOOR + 1) + " 13 gold_ore");
			cmd("setblock 4 " + FLOOR + " 6 chest");
			cmd("summon husk -2 " + FLOOR + " 5 {NoAI:1b,PersistenceRequired:1b}");
			cmd("summon cow 3 " + FLOOR + " 4 {NoAI:1b}");
			cmd("summon stray -5 " + FLOOR + " 7 {NoAI:1b,PersistenceRequired:1b}");
		}));
		PHASES.add(waitTicks("showcase: modules on", 40, () -> {
			((dev.aero.client.setting.BoolSetting) ModuleManager.byName("ESP").setting("Passive mobs")).set(true);
			((dev.aero.client.setting.BoolSetting) ModuleManager.byName("Tracers").setting("Hostile mobs")).set(true);
			for (String n : new String[] {"HUD", "ESP", "Tracers", "Search", "StorageESP", "TargetHud", "Radar", "TPS", "Fullbright", "Sprint"}) ModuleManager.byName(n).setEnabled(true);
			mc.gui.getChat().clearMessages(true);
			mc.player.setYRot(29f);
			mc.player.setXRot(8f);
		}));
		PHASES.add(waitTicks("showcase: screenshot of the world", 15, () -> {
			mc.gui.getChat().clearMessages(true);
			shot("world");
		}));
		PHASES.add(waitTicks("showcase: open the module menu", 20, () -> mc.setScreen(new dev.aero.client.gui.ClickGuiScreen())));
		PHASES.add(waitTicks("showcase: screenshot of the menu", 15, () -> shot("menu")));
		PHASES.add(waitTicks("showcase: close", 10, () -> {
			mc.setScreen(null);
			ModuleManager.disableAll();
		}));
		// 7. AutoMine at scale, with a screenshot of it working
		PHASES.add(waitTicks("arena 7: a tall stone tower", 50, () -> {
			cmd("kill @e[type=!player]");
			cmd("clear @s");
			cmd("give @s diamond_pickaxe");
			cmd("tp @s 80 " + FLOOR + " 0");
			cmd("fill 76 " + FLOOR + " -6 90 " + (FLOOR + 30) + " 6 stone");
			cmd("setblock 84 " + (FLOOR + 22) + " 1 diamond_ore");
			cmd("tp @s 74 " + FLOOR + " 0 -90 -25");
		}));
		boolean[] shotTaken = {false};
		PHASES.add(new Phase("AutoMine climbs 22 blocks through stone to an ore", 4000) {
			@Override
			void begin() {
				shotTaken[0] = false;
				mine().setBlocks(List.of("diamond_ore"));
				mine().setEnabled(true);
			}

			@Override
			boolean done() {
				if (!shotTaken[0] && waited == 160) {
					shotTaken[0] = true;
					mc.gui.getChat().clearMessages(true);
					shot("automine");
				}
				return airAt(84, FLOOR + 22, 1);
			}

			@Override
			String verify(boolean timedOut) {
				return timedOut ? "ore still there; player at " + mc.player.blockPosition().toShortString() + " status=" + mine().info() : null;
			}

			@Override
			void end() {
				mine().setEnabled(false);
			}
		});
		PHASES.add(waitTicks("arena 8: an ore far away", 50, () -> {
			cmd("clear @s");
			cmd("give @s diamond_pickaxe");
			cmd("tp @s 0 " + FLOOR + " 60");
			cmd("fill 60 " + FLOOR + " 70 80 " + (FLOOR + 6) + " 90 air");
			cmd("setblock 70 " + FLOOR + " 82 diamond_ore");
			((dev.aero.client.setting.NumberSetting) mine().setting("Range")).set(120.0);
		}));
		PHASES.add(until("AutoMine walks a long way to an ore 90 blocks off", 2500, () -> {
			mine().setBlocks(List.of("diamond_ore"));
			mine().setEnabled(true);
		}, () -> airAt(70, FLOOR, 82), () -> {
			mine().setEnabled(false);
			((dev.aero.client.setting.NumberSetting) mine().setting("Range")).set(64.0);
		}, () -> "ore still there; player at " + mc.player.blockPosition().toShortString() + " status=" + mine().info()));

		// 8. X-Ray: a stone mass with ores inside, seen through
		PHASES.add(waitTicks("xray: build the scene", 70, () -> {
			cmd("kill @e[type=!player]");
			cmd("tp @s 0 " + FLOOR + " -60 0 0");
			cmd("fill -16 " + FLOOR + " -54 16 " + (FLOOR + 30) + " -26 stone");
			cmd("setblock -4 " + (FLOOR + 6) + " -40 diamond_ore");
			cmd("setblock 3 " + (FLOOR + 9) + " -34 emerald_ore");
			cmd("setblock 0 " + (FLOOR + 3) + " -46 gold_ore");
			cmd("setblock 7 " + (FLOOR + 11) + " -42 ancient_debris");
			cmd("setblock -8 " + (FLOOR + 4) + " -32 spawner");
			cmd("setblock 5 " + (FLOOR + 2) + " -30 chest");
			cmd("setblock -2 " + (FLOOR + 14) + " -38 iron_ore");
			cmd("setblock 9 " + (FLOOR + 5) + " -48 redstone_ore");
		}));
		PHASES.add(waitTicks("xray: before", 25, () -> {
			mc.gui.getChat().clearMessages(true);
			shot("xray-off");
		}));
		PHASES.add(waitTicks("xray: switch on and let the chunks rebuild", 120, () -> ModuleManager.byName("XRay").setEnabled(true)));
		PHASES.add(waitTicks("xray: screenshot", 20, () -> {
			mc.gui.getChat().clearMessages(true);
			shot("xray-on");
		}));
		PHASES.add(waitTicks("xray: switch off", 60, () -> ModuleManager.byName("XRay").setEnabled(false)));

		// 8b. X-Ray and night vision: X-Ray adds it only if it was not there, and takes away only what it added
		var nv = net.minecraft.world.effect.MobEffects.NIGHT_VISION;
		PHASES.add(checkAfter("nightvision: nothing before, X-Ray on gives it", 40, () -> {
			ModuleManager.byName("Fullbright").setEnabled(false);
			mc.player.removeEffect(nv);
			ModuleManager.byName("XRay").setEnabled(true);
		}, () -> mc.player.hasEffect(nv) ? null : "no night vision with X-Ray on"));
		PHASES.add(checkAfter("nightvision: nothing before, X-Ray off removes it", 30, () -> ModuleManager.byName("XRay").setEnabled(false),
				() -> !mc.player.hasEffect(nv) && !ModuleManager.byName("Fullbright").isEnabled() ? null : "night vision stayed after X-Ray off"));
		PHASES.add(checkAfter("nightvision: Fullbright was on, X-Ray on", 30, () -> {
			ModuleManager.byName("Fullbright").setEnabled(true);
			ModuleManager.byName("XRay").setEnabled(true);
		}, () -> mc.player.hasEffect(nv) ? null : "no night vision"));
		PHASES.add(checkAfter("nightvision: Fullbright was on, X-Ray off keeps it", 30, () -> ModuleManager.byName("XRay").setEnabled(false),
				() -> mc.player.hasEffect(nv) && ModuleManager.byName("Fullbright").isEnabled() ? null : "night vision was removed although it was on before"));
		PHASES.add(checkAfter("nightvision: a real potion, X-Ray on and off keeps it", 40, () -> {
			ModuleManager.byName("Fullbright").setEnabled(false);
			cmd("effect give @s minecraft:night_vision 600 0 true");
		}, () -> null));
		PHASES.add(checkAfter("nightvision: potion stays through X-Ray on", 30, () -> ModuleManager.byName("XRay").setEnabled(true),
				() -> mc.player.hasEffect(nv) ? null : "potion lost"));
		PHASES.add(checkAfter("nightvision: potion stays after X-Ray off", 30, () -> ModuleManager.byName("XRay").setEnabled(false),
				() -> mc.player.hasEffect(nv) ? null : "the potion night vision was removed by X-Ray"));
		PHASES.add(checkAfter("nightvision: cleanup", 10, () -> {
			cmd("effect clear @s");
			ModuleManager.byName("Fullbright").setEnabled(false);
		}, () -> null));

		// 9. the ore filter editor
		dev.aero.client.gui.ListEditorScreen[] editor = {null};
		PHASES.add(new Phase("the ore filter editor adds and removes an entry on click", 80) {
			@Override
			void begin() {
				mine().setBlocks(List.of("diamond_ore", "ancient_debris"));
				editor[0] = new dev.aero.client.gui.ListEditorScreen(new dev.aero.client.gui.ClickGuiScreen(), mine().blocks);
				mc.setScreen(editor[0]);
			}

			@Override
			boolean done() {
				return waited >= 10;
			}

			@Override
			String verify(boolean timedOut) {
				double x = mc.getWindow().getGuiScaledWidth() / 2.0, y = 14 + 44 + 10; // the first row of the list
				boolean before = mine().blocks.contains("ancient_debris") && mine().blocks.contains("diamond_ore");
				editor[0].mouseClicked(x, y, 0);
				int afterOne = mine().blocks.get().size();
				editor[0].mouseClicked(x, y, 0);
				int afterTwo = mine().blocks.get().size();
				return before && afterOne == 1 && afterTwo == 2 ? null : "before=" + before + " afterOne=" + afterOne + " afterTwo=" + afterTwo;
			}
		});
		PHASES.add(waitTicks("filter: screenshot", 15, () -> shot("filter")));
		PHASES.add(waitTicks("filter: close", 5, () -> mc.setScreen(null)));

		PHASES.add(waitTicks("pause", 10, () -> {
		}));
	}

	static void start(Minecraft mc) {
		int[] n = {0};
		ClientTickEvents.END_CLIENT_TICK.register(client -> tick(client, n));
	}

	private static void tick(Minecraft mc, int[] n) {
		n[0]++;
		if (!worldRequested) {
			if (n[0] < 100) return;
			worldRequested = true;
			AeroClient.LOG.info("WORLDTEST creating a flat survival world");
			LevelSettings settings = new LevelSettings("AeroTest", GameType.SURVIVAL, false, Difficulty.EASY, true, new GameRules(), WorldDataConfiguration.DEFAULT);
			mc.createWorldOpenFlows().createFreshLevel("AeroTest" + System.currentTimeMillis(), settings, new WorldOptions(20241106L, false, false),
					registries -> registries.registryOrThrow(Registries.WORLD_PRESET).getHolderOrThrow(WorldPresets.FLAT).value().createWorldDimensions(), mc.screen);
			return;
		}
		if (mc.player == null || mc.level == null || mc.getConnection() == null) return;
		if (index == -1) {
			buildPhases();
			ticks = 0;
			index = 0;
			PHASES.get(0).begin();
		}
		if (index >= PHASES.size()) {
			if (++ticks == 5) finish(mc);
			return;
		}
		Phase p = PHASES.get(index);
		p.waited++;
		boolean done = false;
		try {
			done = p.done();
		} catch (RuntimeException e) {
			AeroClient.LOG.error("WORLDTEST phase {} threw", p.name, e);
			failures++;
			RESULTS.add("FAIL " + p.name + " (exception)");
			p.end();
			advance();
			return;
		}
		boolean timedOut = p.waited > p.timeout;
		if (done || timedOut) {
			String problem = p.verify(timedOut && !done);
			if (problem != null) {
				failures++;
				RESULTS.add("FAIL " + p.name + ": " + problem);
				AeroClient.LOG.error("WORLDTEST FAIL {}: {}", p.name, problem);
			} else if (!p.name.startsWith("world") && !p.name.startsWith("arena") && !p.name.startsWith("showcase") && !p.name.equals("pause") && !p.name.equals("collect") && !p.name.startsWith("reset")) {
				RESULTS.add("PASS " + p.name + " (" + p.waited + " ticks)");
				AeroClient.LOG.info("WORLDTEST PASS {} ({} ticks)", p.name, p.waited);
			}
			p.end();
			advance();
		}
	}

	private static void shot(String name) {
		var mc = Minecraft.getInstance();
		net.minecraft.client.Screenshot.grab(mc.gameDirectory, "aero-" + name + ".png", mc.getMainRenderTarget(), c -> AeroClient.LOG.info("WORLDTEST screenshot {}", c.getString()));
	}

	private static void advance() {
		index++;
		if (index < PHASES.size()) PHASES.get(index).begin();
	}

	private static void finish(Minecraft mc) {
		long passed = RESULTS.stream().filter(r -> r.startsWith("PASS")).count();
		AeroClient.LOG.info("WORLDTEST summary: {} passed, {} failed", passed, failures);
		for (String r : RESULTS) if (r.startsWith("FAIL")) AeroClient.LOG.info("WORLDTEST {}", r);
		AeroClient.LOG.info("WORLDTEST {}", failures == 0 ? "PASSED" : "FAILED");
		mc.stop();
	}
}
