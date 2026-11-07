package dev.aero.client.module;

import dev.aero.client.module.impl.AutoMine;
import dev.aero.client.module.impl.ClickGui;
import dev.aero.client.module.impl.CombatModules;
import dev.aero.client.module.impl.Hud;
import dev.aero.client.module.impl.MiscModules;
import dev.aero.client.module.impl.MovementModules;
import dev.aero.client.module.impl.PlayerModules;
import dev.aero.client.module.impl.RenderModules;
import dev.aero.client.module.impl.WorldModules;
import dev.aero.client.module.impl.Navigator;

/** Every module, registered once at startup. Order inside a category is the order shown in the menu. */
public final class ModuleRegistry {
	private ModuleRegistry() {
	}

	public static void registerAll() {
		ModuleManager.register(new ClickGui());
		ModuleManager.register(new Navigator());
		ModuleManager.register(new Hud());
		ModuleManager.register(new AutoMine());
		CombatModules.register();
		MovementModules.register();
		PlayerModules.register();
		WorldModules.register();
		RenderModules.register();
		MiscModules.register();
	}
}
