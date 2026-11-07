package dev.aero.client.module.impl;

import dev.aero.client.module.Category;
import dev.aero.client.module.Module;
import dev.aero.client.path.Goal;
import dev.aero.client.pathing.Controls;
import dev.aero.client.pathing.PathExecutor;
import dev.aero.client.util.Chat;
import net.fabricmc.fabric.api.client.rendering.v1.WorldRenderContext;
import net.minecraft.world.phys.Vec3;

/** Owns one path executor for the .goto command, Follow and other single-goal walking. */
public final class Navigator extends Module {
	private final PathExecutor exec = new PathExecutor();
	private Goal.Position goal;
	private int gx, gy, gz;
	/** No arrival / failure chat messages (used by Follow). */
	public boolean quiet;

	public Navigator() {
		super("Navigator", Category.WORLD, "Walks to a point (.goto).");
		hidden = true;
	}

	public PathExecutor executor() {
		return exec;
	}

	/** Starts walking to a standing position; digging and falling follow the usual path rules. */
	public void goTo(int x, int y, int z, double range, boolean dig) {
		quiet = false;
		gx = x;
		gy = y;
		gz = z;
		exec.config.allowDig = dig;
		exec.walkPartial = true;
		exec.config.maxExpansions = 60000;
		goal = new Goal.Position(x, y, z, range);
		exec.start(goal);
		if (!isEnabled()) setEnabled(true);
	}

	public void stopWalking() {
		exec.cancel();
		goal = null;
		if (isEnabled()) setEnabled(false);
	}

	@Override
	protected void onDisable() {
		exec.cancel();
		Controls.releaseAll();
	}

	@Override
	public String info() {
		return exec.message().isEmpty() ? null : exec.message();
	}

	@Override
	public void onTick() {
		exec.tick();
		switch (exec.state()) {
			case ARRIVED -> {
				if (!quiet) Chat.info("Arrived at " + gx + " " + gy + " " + gz);
				stopWalking();
			}
			case FAILED -> {
				if (!quiet) Chat.warn("Cannot reach " + gx + " " + gy + " " + gz + ": " + exec.message());
				stopWalking();
			}
			default -> {
			}
		}
	}

	@Override
	public void onRender3d(WorldRenderContext ctx) {
		if (goal == null) return;
		dev.aero.client.util.Render3d.begin(ctx, true);
		Vec3 c = new Vec3(gx + 0.5, gy + 0.5, gz + 0.5);
		dev.aero.client.util.Render3d.box(new net.minecraft.world.phys.AABB(gx, gy, gz, gx + 1, gy + 2, gz + 1), 0xFFFFD166);
		dev.aero.client.util.Render3d.line(ctx.camera().getPosition().add(ctx.camera().getLookVector().x() * 1.5, ctx.camera().getLookVector().y() * 1.5, ctx.camera().getLookVector().z() * 1.5), c, 0x80FFD166);
		dev.aero.client.util.Render3d.end();
	}
}
