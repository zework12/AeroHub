package dev.aero.client.gui;

import dev.aero.client.setting.ListSetting;
import net.minecraft.client.gui.GuiGraphics;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.components.EditBox;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.network.chat.Component;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.world.item.Item;
import net.minecraft.world.item.ItemStack;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Pick blocks (or items) for a list setting: every entry of the game's registry with its icon and a check mark,
 * a search box, scrolling and a few shortcuts. Used for the AutoMine ore filter, XRay, Search and the other lists.
 */
public final class ListEditorScreen extends Screen {
	private static final int ROW_H = 20, PANEL_W = 300;

	private final Screen parent;
	private final ListSetting setting;
	private final List<String> all = new ArrayList<>();
	private List<String> shown = new ArrayList<>();
	private EditBox search;
	private int scroll;
	private int left, top, listTop, listBottom;

	public ListEditorScreen(Screen parent, ListSetting setting) {
		super(Component.literal(setting.name));
		this.parent = parent;
		this.setting = setting;
		var registry = setting.kind == ListSetting.Kind.ITEM ? BuiltInRegistries.ITEM.keySet() : BuiltInRegistries.BLOCK.keySet();
		for (ResourceLocation rl : registry) {
			if (!rl.getNamespace().equals("minecraft")) continue;
			String id = rl.getPath();
			if (id.equals("air") || id.equals("cave_air") || id.equals("void_air")) continue;
			all.add(id);
		}
		java.util.Collections.sort(all);
	}

	@Override
	protected void init() {
		left = this.width / 2 - PANEL_W / 2;
		top = 14;
		search = new EditBox(this.font, left + 6, top + 22, PANEL_W - 12, 16, Component.literal("Search"));
		search.setHint(Component.literal("Search, for example ore or diamond"));
		search.setResponder(text -> refilter());
		addRenderableWidget(search);
		setInitialFocus(search);
		int buttonY = this.height - 28;
		addRenderableWidget(Button.builder(Component.literal("Done"), b -> onClose()).bounds(left + PANEL_W - 70, buttonY, 64, 20).build());
		addRenderableWidget(Button.builder(Component.literal("Clear"), b -> {
			setting.get().clear();
			refilter();
		}).bounds(left + 6, buttonY, 56, 20).build());
		if (setting.kind == ListSetting.Kind.BLOCK) {
			addRenderableWidget(Button.builder(Component.literal("All ores"), b -> {
				for (String id : all) if (id.endsWith("_ore") || id.equals("ancient_debris")) setting.add(id);
				refilter();
			}).bounds(left + 66, buttonY, 66, 20).build());
		}
		listTop = top + 44;
		listBottom = buttonY - 6;
		refilter();
	}

	private void refilter() {
		String q = search == null ? "" : search.getValue().trim().toLowerCase(Locale.ROOT).replace(' ', '_');
		List<String> picked = new ArrayList<>(), others = new ArrayList<>();
		for (String id : all) {
			if (!q.isEmpty() && !id.contains(q)) continue;
			(setting.contains(id) ? picked : others).add(id);
		}
		picked.addAll(others); // the ones already in the list come first
		shown = picked;
		scroll = 0;
	}

	private int maxScroll() {
		return Math.max(0, shown.size() * ROW_H - (listBottom - listTop));
	}

	/** The default background blurs whatever was drawn before it, which would smear the list; the screen draws its own. */
	@Override
	public void renderBackground(GuiGraphics g, int mx, int my, float delta) {
	}

	@Override
	public void render(GuiGraphics g, int mx, int my, float delta) {
		g.fill(0, 0, this.width, this.height, 0x90000000);
		g.fill(left, top, left + PANEL_W, this.height - 6, 0xF0101820);
		g.fill(left, top, left + PANEL_W, top + 18, 0xFF1F6FEB);
		g.drawString(this.font, setting.name + "  (" + setting.get().size() + " selected)", left + 6, top + 5, 0xFFFFFFFF, false);

		g.enableScissor(left, listTop, left + PANEL_W, listBottom);
		int y = listTop - scroll;
		for (String id : shown) {
			if (y + ROW_H >= listTop && y <= listBottom) {
				boolean on = setting.contains(id);
				boolean hover = mx >= left && mx < left + PANEL_W && my >= y && my < y + ROW_H && my >= listTop && my < listBottom;
				g.fill(left + 3, y, left + PANEL_W - 3, y + ROW_H - 1, on ? 0x8022C55E : hover ? 0x40FFFFFF : 0x20000000);
				ResourceLocation rl = ResourceLocation.withDefaultNamespace(id);
				Item item = BuiltInRegistries.ITEM.getOptional(rl).orElse(null);
				if (item != null) g.renderItem(new ItemStack(item), left + 6, y + 2);
				g.drawString(this.font, id, left + 28, y + 6, on ? 0xFFFFFFFF : 0xFFC9D1D9, false);
				if (on) g.drawString(this.font, "\u2714", left + PANEL_W - 18, y + 6, 0xFF7CF29A, false);
			}
			y += ROW_H;
		}
		g.disableScissor();
		if (shown.isEmpty()) g.drawString(this.font, "Nothing matches", left + 10, listTop + 8, 0xFF8B98A5, false);
		super.render(g, mx, my, delta);
	}

	@Override
	public boolean mouseClicked(double mx, double my, int button) {
		if (super.mouseClicked(mx, my, button)) return true;
		if (button == 0 && mx >= left && mx < left + PANEL_W && my >= listTop && my < listBottom) {
			int index = (int) ((my - listTop + scroll) / ROW_H);
			if (index >= 0 && index < shown.size()) {
				String id = shown.get(index);
				if (!setting.remove(id)) setting.add(id);
				return true;
			}
		}
		return false;
	}

	@Override
	public boolean mouseScrolled(double mx, double my, double horizontal, double vertical) {
		scroll = Math.max(0, Math.min(maxScroll(), scroll - (int) (vertical * ROW_H * 2)));
		return true;
	}

	@Override
	public boolean isPauseScreen() {
		return false;
	}

	@Override
	public void onClose() {
		this.minecraft.setScreen(parent);
	}
}
