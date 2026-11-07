package dev.aero.client.util;

import com.mojang.blaze3d.systems.RenderSystem;
import com.mojang.blaze3d.vertex.BufferBuilder;
import com.mojang.blaze3d.vertex.BufferUploader;
import com.mojang.blaze3d.vertex.DefaultVertexFormat;
import com.mojang.blaze3d.vertex.MeshData;
import com.mojang.blaze3d.vertex.PoseStack;
import com.mojang.blaze3d.vertex.Tesselator;
import com.mojang.blaze3d.vertex.VertexFormat;
import net.fabricmc.fabric.api.client.rendering.v1.WorldRenderContext;
import net.minecraft.client.renderer.GameRenderer;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;
import org.joml.Matrix4f;
import org.joml.Vector4f;

/**
 * Immediate-mode lines and boxes in world space, drawn after the world. Colours are 0xAARRGGBB.
 * Usage: begin(ctx, throughWalls), any number of line/box calls, end().
 */
public final class Render3d {
	private static BufferBuilder buffer;
	private static Matrix4f matrix;
	private static Vec3 camera;
	private static final Matrix4f VIEW = new Matrix4f(), PROJECTION = new Matrix4f();
	private static boolean captured;

	private Render3d() {
	}

	/** Remembers this frame's camera and matrices so 2D code can place labels over world positions. */
	public static void capture(WorldRenderContext ctx) {
		camera = ctx.camera().getPosition();
		VIEW.set(ctx.positionMatrix());
		PROJECTION.set(ctx.projectionMatrix());
		captured = true;
	}

	/**
	 * Screen position in GUI-scaled pixels of a world point, or null when it is behind the camera or off screen.
	 * Valid after capture() of the current frame.
	 */
	public static float[] project(double x, double y, double z) {
		if (!captured) return null;
		Vector4f v = new Vector4f((float) (x - camera.x), (float) (y - camera.y), (float) (z - camera.z), 1.0f);
		v.mul(VIEW);
		v.mul(PROJECTION);
		if (v.w <= 0.0f) return null;
		float nx = v.x / v.w, ny = v.y / v.w;
		if (nx < -1.2f || nx > 1.2f || ny < -1.2f || ny > 1.2f) return null;
		var window = net.minecraft.client.Minecraft.getInstance().getWindow();
		return new float[] {(nx * 0.5f + 0.5f) * window.getGuiScaledWidth(), (1.0f - (ny * 0.5f + 0.5f)) * window.getGuiScaledHeight()};
	}

	public static void begin(WorldRenderContext ctx, boolean throughWalls) {
		PoseStack stack = ctx.matrixStack();
		matrix = stack.last().pose();
		camera = ctx.camera().getPosition();
		RenderSystem.enableBlend();
		RenderSystem.defaultBlendFunc();
		if (throughWalls) RenderSystem.disableDepthTest();
		else RenderSystem.enableDepthTest();
		RenderSystem.depthMask(false);
		RenderSystem.disableCull();
		RenderSystem.setShader(GameRenderer::getPositionColorShader);
		RenderSystem.lineWidth(2.0f);
		buffer = Tesselator.getInstance().begin(VertexFormat.Mode.DEBUG_LINES, DefaultVertexFormat.POSITION_COLOR);
	}

	public static void end() {
		if (buffer != null) {
			MeshData mesh = buffer.build();
			if (mesh != null) BufferUploader.drawWithShader(mesh);
		}
		buffer = null;
		RenderSystem.depthMask(true);
		RenderSystem.enableDepthTest();
		RenderSystem.enableCull();
		RenderSystem.disableBlend();
		RenderSystem.lineWidth(1.0f);
	}

	private static void vertex(double x, double y, double z, int argb) {
		buffer.addVertex(matrix, (float) (x - camera.x), (float) (y - camera.y), (float) (z - camera.z))
				.setColor((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, (argb >>> 24) & 0xFF);
	}

	public static void line(double x1, double y1, double z1, double x2, double y2, double z2, int argb) {
		vertex(x1, y1, z1, argb);
		vertex(x2, y2, z2, argb);
	}

	public static void line(Vec3 a, Vec3 b, int argb) {
		line(a.x, a.y, a.z, b.x, b.y, b.z, argb);
	}

	/** Twelve edges of the box. */
	public static void box(AABB b, int argb) {
		double x0 = b.minX, y0 = b.minY, z0 = b.minZ, x1 = b.maxX, y1 = b.maxY, z1 = b.maxZ;
		line(x0, y0, z0, x1, y0, z0, argb);
		line(x1, y0, z0, x1, y0, z1, argb);
		line(x1, y0, z1, x0, y0, z1, argb);
		line(x0, y0, z1, x0, y0, z0, argb);
		line(x0, y1, z0, x1, y1, z0, argb);
		line(x1, y1, z0, x1, y1, z1, argb);
		line(x1, y1, z1, x0, y1, z1, argb);
		line(x0, y1, z1, x0, y1, z0, argb);
		line(x0, y0, z0, x0, y1, z0, argb);
		line(x1, y0, z0, x1, y1, z0, argb);
		line(x1, y0, z1, x1, y1, z1, argb);
		line(x0, y0, z1, x0, y1, z1, argb);
	}

	public static Vec3 cameraPos() {
		return camera;
	}

	public static int rgb(int r, int g, int b, int a) {
		return ((a & 0xFF) << 24) | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
	}

	/** Stable colour for an arbitrary hash, for per-type ESP colours. */
	public static int hashColor(int hash, int alpha) {
		float h = ((hash * 0x9E3779B1) >>> 8 & 0xFFFF) / 65535.0f * 6.0f;
		float s = 0.75f, v = 1.0f;
		int i = (int) Math.floor(h);
		float f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
		float[] c = switch (i % 6) {
			case 0 -> new float[] {v, t, p};
			case 1 -> new float[] {q, v, p};
			case 2 -> new float[] {p, v, t};
			case 3 -> new float[] {p, q, v};
			case 4 -> new float[] {t, p, v};
			default -> new float[] {v, p, q};
		};
		return rgb((int) (c[0] * 255), (int) (c[1] * 255), (int) (c[2] * 255), alpha);
	}
}
