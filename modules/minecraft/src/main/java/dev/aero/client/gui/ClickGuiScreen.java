package dev.aero.client.gui;

import com.mojang.blaze3d.platform.InputConstants;
import dev.aero.client.config.Config;
import dev.aero.client.module.Category;
import dev.aero.client.module.Module;
import dev.aero.client.module.ModuleManager;
import dev.aero.client.setting.BoolSetting;
import dev.aero.client.setting.ListSetting;
import dev.aero.client.setting.ModeSetting;
import dev.aero.client.setting.NumberSetting;
import dev.aero.client.setting.Setting;
import net.minecraft.client.gui.GuiGraphics;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.network.chat.Component;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * One draggable panel per category. Left click toggles a module, right click opens its settings, middle click
 * binds a key (next key press; Escape clears). Sliders are dragged with the left button.
 */
public final class ClickGuiScreen extends Screen {
	private static final int PANEL_W = 120, HEADER_H = 16, ROW_H = 14, SETTING_H = 13;
	private static final int BG = 0xE0101820, HEADER = 0xFF1F6FEB, ON = 0xFF22C55E, HOVER = 0x30FFFFFF, TEXT = 0xFFE6EDF3, DIM = 0xFF8B98A5, ROW = 0xB0161F2A;

	private static final class Panel {
		final Category category;
		int x, y, scroll;
		boolean open = true;

		Panel(Category c, int x, int y) {
			category = c;
			this.x = x;
			this.y = y;
		}
	}

	private final List<Panel> panels = new ArrayList<>();
	private final Set<Module> expanded = new HashSet<>();
	private Panel dragging;
	private int dragDx, dragDy;
	private NumberSetting slider;
	private int sliderX, sliderW;
	private Module binding;
	/** Whole-menu scale: the six panels need about 800 GUI pixels, so on a narrower window everything is drawn smaller. */
	private float uiScale = 1.0f;

	public ClickGuiScreen() {
		super(Component.literal("AeroClient"));
		int x = 12;
		for (Category c : Category.values()) {
			panels.add(new Panel(c, x, 12));
			x += PANEL_W + 8;
		}
	}

	@Override
	public boolean isPauseScreen() {
		return false;
	}

	@Override
	public void onClose() {
		Config.save("");
		super.onClose();
	}

	private int contentHeight(Panel p) {
		int h = 0;
		for (Module m : ModuleManager.in(p.category)) {
			if (m.hidden) continue;
			h += ROW_H;
			if (expanded.contains(m)) h += m.settings.size() * SETTING_H + (m.settings.isEmpty() ? SETTING_H : 0);
		}
		return h;
	}

	private int maxBodyHeight() {
		return Math.max(60, (int) (this.height / uiScale) - 40);
	}

	@Override
	public void render(GuiGraphics g, int rawX, int rawY, float delta) {
		uiScale = Math.min(1.0f, this.width / 800.0f);
		g.fill(0, 0, this.width, this.height, 0x60000000);
		g.pose().pushPose();
		g.pose().scale(uiScale, uiScale, 1.0f);
		int mx = (int) (rawX / uiScale), my = (int) (rawY / uiScale);
		int vw = (int) (this.width / uiScale), vh = (int) (this.height / uiScale);
		for (Panel p : panels) drawPanel(g, p, mx, my);
		Module hover = hoveredModule(mx, my);
		if (hover != null) {
			String tip = binding == hover ? "Press a key to bind (Esc = clear)" : hover.description + (hover.key() >= 0 ? "  [" + keyName(hover.key()) + "]" : "");
			int w = this.font.width(tip) + 8;
			int tx = Math.max(2, Math.min(mx + 10, vw - w - 2)), ty = Math.min(my + 12, vh - 14);
			g.fill(tx, ty, tx + w, ty + 12, 0xF0000000);
			g.drawString(this.font, tip, tx + 4, ty + 2, TEXT, false);
		}
		g.pose().popPose();
	}

	private void drawPanel(GuiGraphics g, Panel p, int mx, int my) {
		int body = p.open ? Math.min(contentHeight(p), maxBodyHeight()) : 0;
		g.fill(p.x, p.y, p.x + PANEL_W, p.y + HEADER_H + body, BG);
		g.fill(p.x, p.y, p.x + PANEL_W, p.y + HEADER_H, HEADER);
		g.drawString(this.font, p.category.label, p.x + 6, p.y + 4, 0xFFFFFFFF, false);
		g.drawString(this.font, p.open ? "-" : "+", p.x + PANEL_W - 10, p.y + 4, 0xFFFFFFFF, false);
		if (!p.open) return;

		int y = p.y + HEADER_H - p.scroll;
		// enableScissor takes GUI pixels and does not follow the pose scale, so convert by hand
		g.enableScissor((int) Math.floor(p.x * uiScale), (int) Math.floor((p.y + HEADER_H) * uiScale),
				(int) Math.ceil((p.x + PANEL_W) * uiScale), (int) Math.ceil((p.y + HEADER_H + body) * uiScale));
		for (Module m : ModuleManager.in(p.category)) {
			if (m.hidden) continue;
			boolean over = mx >= p.x && mx < p.x + PANEL_W && my >= y && my < y + ROW_H && my >= p.y + HEADER_H && my < p.y + HEADER_H + body;
			g.fill(p.x, y, p.x + PANEL_W, y + ROW_H, m.isEnabled() ? 0x8022C55E : ROW);
			if (over) g.fill(p.x, y, p.x + PANEL_W, y + ROW_H, HOVER);
			g.drawString(this.font, m.name, p.x + 5, y + 3, m.isEnabled() ? 0xFFFFFFFF : TEXT, false);
			if (binding == m) g.drawString(this.font, "...", p.x + PANEL_W - 16, y + 3, DIM, false);
			else if (!m.settings.isEmpty()) g.drawString(this.font, expanded.contains(m) ? "v" : ">", p.x + PANEL_W - 10, y + 3, DIM, false);
			y += ROW_H;
			if (expanded.contains(m)) {
				for (Setting<?> s : m.settings) {
					drawSetting(g, p, s, y);
					y += SETTING_H;
				}
				if (m.settings.isEmpty()) {
					g.drawString(this.font, "no settings", p.x + 10, y + 2, DIM, false);
					y += SETTING_H;
				}
			}
		}
		g.disableScissor();
	}

	private void drawSetting(GuiGraphics g, Panel p, Setting<?> s, int y) {
		g.fill(p.x, y, p.x + PANEL_W, y + SETTING_H, 0xC00B1118);
		if (s instanceof NumberSetting n) {
			double frac = (n.d() - n.min) / (n.max - n.min);
			g.fill(p.x + 4, y + SETTING_H - 3, p.x + 4 + (int) ((PANEL_W - 8) * frac), y + SETTING_H - 1, HEADER);
			g.drawString(this.font, n.name + ": " + n.display(), p.x + 6, y + 1, TEXT, false);
		} else if (s instanceof BoolSetting b) {
			g.drawString(this.font, b.name, p.x + 6, y + 2, TEXT, false);
			g.drawString(this.font, b.get() ? "on" : "off", p.x + PANEL_W - 22, y + 2, b.get() ? ON : DIM, false);
		} else if (s instanceof ModeSetting m) {
			g.drawString(this.font, m.name + ": " + m.get(), p.x + 6, y + 2, TEXT, false);
		} else if (s instanceof ListSetting l) {
			boolean pickable = l.kind != ListSetting.Kind.TEXT;
			g.drawString(this.font, l.name + " (" + l.get().size() + ")", p.x + 6, y + 2, pickable ? TEXT : DIM, false);
			g.drawString(this.font, pickable ? "edit" : ".set", p.x + PANEL_W - (pickable ? 22 : 20), y + 2, pickable ? HEADER : DIM, false);
		}
	}

	private Module hoveredModule(int mx, int my) {
		for (Panel p : panels) {
			Module m = moduleAt(p, mx, my);
			if (m != null) return m;
		}
		return null;
	}

	private Module moduleAt(Panel p, int mx, int my) {
		if (!p.open || mx < p.x || mx >= p.x + PANEL_W) return null;
		int body = Math.min(contentHeight(p), maxBodyHeight());
		if (my < p.y + HEADER_H || my >= p.y + HEADER_H + body) return null;
		int y = p.y + HEADER_H - p.scroll;
		for (Module m : ModuleManager.in(p.category)) {
			if (m.hidden) continue;
			if (my >= y && my < y + ROW_H) return m;
			y += ROW_H;
			if (expanded.contains(m)) y += m.settings.size() * SETTING_H + (m.settings.isEmpty() ? SETTING_H : 0);
		}
		return null;
	}

	/** The setting under the cursor, with the row's top y stored in rowY[0]. */
	private Setting<?> settingAt(Panel p, int mx, int my, int[] rowY) {
		if (!p.open || mx < p.x || mx >= p.x + PANEL_W) return null;
		int body = Math.min(contentHeight(p), maxBodyHeight());
		if (my < p.y + HEADER_H || my >= p.y + HEADER_H + body) return null;
		int y = p.y + HEADER_H - p.scroll;
		for (Module m : ModuleManager.in(p.category)) {
			if (m.hidden) continue;
			y += ROW_H;
			if (!expanded.contains(m)) continue;
			for (Setting<?> s : m.settings) {
				if (my >= y && my < y + SETTING_H) {
					rowY[0] = y;
					return s;
				}
				y += SETTING_H;
			}
			if (m.settings.isEmpty()) y += SETTING_H;
		}
		return null;
	}

	private void setSlider(NumberSetting n, double mouseX) {
		double frac = Math.max(0, Math.min(1, (mouseX - sliderX) / sliderW));
		n.set(n.min + (n.max - n.min) * frac);
	}

	@Override
	public boolean mouseClicked(double rawX, double rawY, int button) {
		double mx = rawX / uiScale, my = rawY / uiScale;
		int imx = (int) mx, imy = (int) my;
		for (int i = panels.size() - 1; i >= 0; i--) {
			Panel p = panels.get(i);
			if (imx >= p.x && imx < p.x + PANEL_W && imy >= p.y && imy < p.y + HEADER_H) {
				if (button == 0) {
					dragging = p;
					dragDx = imx - p.x;
					dragDy = imy - p.y;
					panels.remove(i);
					panels.add(p);
				} else if (button == 1) {
					p.open = !p.open;
				}
				return true;
			}
			Module m = moduleAt(p, imx, imy);
			if (m != null) {
				if (button == 0) m.toggle();
				else if (button == 1) {
					if (!expanded.remove(m)) expanded.add(m);
				} else if (button == 2) binding = m;
				return true;
			}
			int[] rowY = new int[1];
			Setting<?> s = settingAt(p, imx, imy, rowY);
			if (s != null) {
				if (s instanceof BoolSetting b && button == 0) b.toggle();
				else if (s instanceof ModeSetting ms && button == 0) ms.next();
				else if (s instanceof ListSetting ls && button == 0 && ls.kind != ListSetting.Kind.TEXT) this.minecraft.setScreen(new ListEditorScreen(this, ls));
				else if (s instanceof NumberSetting n && button == 0) {
					slider = n;
					sliderX = p.x + 4;
					sliderW = PANEL_W - 8;
					setSlider(n, mx);
				}
				return true;
			}
		}
		return super.mouseClicked(rawX, rawY, button);
	}

	@Override
	public boolean mouseDragged(double rawX, double rawY, int button, double dx, double dy) {
		double mx = rawX / uiScale, my = rawY / uiScale;
		if (dragging != null) {
			dragging.x = (int) mx - dragDx;
			dragging.y = (int) my - dragDy;
			return true;
		}
		if (slider != null) {
			setSlider(slider, mx);
			return true;
		}
		return super.mouseDragged(rawX, rawY, button, dx, dy);
	}

	@Override
	public boolean mouseReleased(double mx, double my, int button) {
		dragging = null;
		slider = null;
		return super.mouseReleased(mx, my, button);
	}

	@Override
	public boolean mouseScrolled(double rawX, double rawY, double horizontal, double vertical) {
		double mx = rawX / uiScale, my = rawY / uiScale;
		for (int i = panels.size() - 1; i >= 0; i--) {
			Panel p = panels.get(i);
			if (mx >= p.x && mx < p.x + PANEL_W && my >= p.y && my < p.y + HEADER_H + Math.min(contentHeight(p), maxBodyHeight())) {
				int max = Math.max(0, contentHeight(p) - maxBodyHeight());
				p.scroll = Math.max(0, Math.min(max, p.scroll - (int) (vertical * 12)));
				return true;
			}
		}
		return super.mouseScrolled(rawX, rawY, horizontal, vertical);
	}

	@Override
	public boolean keyPressed(int key, int scancode, int mods) {
		if (binding != null) {
			binding.setKey(key == InputConstants.KEY_ESCAPE ? -1 : key);
			binding = null;
			return true;
		}
		return super.keyPressed(key, scancode, mods);
	}

	public static String keyName(int key) {
		try {
			return InputConstants.Type.KEYSYM.getOrCreate(key).getDisplayName().getString();
		} catch (RuntimeException e) {
			return "key " + key;
		}
	}
}
