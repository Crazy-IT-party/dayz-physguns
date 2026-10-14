// ============================================================================
//  PhysGuns - a gravity gun and a portal gun.
//
//  Client + server mod. Both guns are rifles to the game (the stockless AKS74U's hands and animations), but
//  their trigger is caught by script (PG_Gun.c): nothing is fired. Neither is in the loot economy (no types.xml
//  entry), so the only way to get one is an admin spawning it (Community Online Tools' item spawner).
// ============================================================================

class CfgPatches
{
	class PhysGuns
	{
		units[] = {"PG_PortalBlue", "PG_PortalOrange", "PG_BeamSeg", "PG_BeamArc"};
		weapons[] = {"PG_GravityGun", "PG_PortalGun"};
		requiredVersion = 0.1;
		requiredAddons[] = {"DZ_Data", "DZ_Scripts", "DZ_Weapons_Firearms", "DZ_Weapons_Firearms_AK74", "DZ_Gear_Consumables"};
	};
};

class CfgMods
{
	class PhysGuns
	{
		dir = "PhysGuns";
		name = "PhysGuns";
		author = "Crazy IT-party";
		version = "0.1.0";
		type = "mod";
		inputs = "PhysGuns/data/inputs.xml";
		dependencies[] = {"Game", "World", "Mission"};
		class defs
		{
			class gameScriptModule
			{
				value = "";
				files[] = {"PhysGuns/scripts/3_Game"};
			};
			class worldScriptModule
			{
				value = "";
				files[] = {"PhysGuns/scripts/4_World"};
			};
			class missionScriptModule
			{
				value = "";
				files[] = {"PhysGuns/scripts/5_Mission"};
			};
		};
	};
};

class CfgWeapons
{
	class AKS74U;
	class PG_GunBase: AKS74U
	{
		scope = 0;
		// own MLOD models: no hidden selections on those (changing one crashes the game)
		hiddenSelections[] = {};
		hiddenSelectionsTextures[] = {};
		hiddenSelectionsMaterials[] = {};
		weight = 4500;
		chamberableFrom[] = {};
		magazines[] = {};
		DisplayMagazine = 0;		// no magazine slot in the inventory
		// six 9V batteries power it (PG_GunBase.PG_Use): nothing works without all six charged
		attachments[] = {"PG_Battery1", "PG_Battery2", "PG_Battery3", "PG_Battery4", "PG_Battery5", "PG_Battery6"};
		repairableWithKits[] = {};
		repairCosts[] = {};
		lootTag[] = {};
		lootCategory = "";
		// the look of each condition: every material swapped for its damaged version (tools/build_guns.py)
		class DamageSystem
		{
			class GlobalHealth
			{
				class Health
				{
					hitpoints = 200;
					healthLevels[] =
					{
						{1.0, {"PhysGuns\data\gun_steel.rvmat", "PhysGuns\data\gun_black.rvmat", "PhysGuns\data\gun_orange.rvmat", "PhysGuns\data\gun_white.rvmat", "PhysGuns\data\gun_grip.rvmat", "PhysGuns\data\gun_plate.rvmat", "PhysGuns\data\gun_wire.rvmat", "PhysGuns\data\gun_glow.rvmat", "PhysGuns\data\lamp_green.rvmat", "PhysGuns\data\lamp_amber.rvmat", "PhysGuns\data\lamp_red.rvmat", "PhysGuns\data\lamp_blue.rvmat", "PhysGuns\data\lamp_orange.rvmat"}},
						{0.7, {"PhysGuns\data\gun_steel.rvmat", "PhysGuns\data\gun_black.rvmat", "PhysGuns\data\gun_orange.rvmat", "PhysGuns\data\gun_white.rvmat", "PhysGuns\data\gun_grip.rvmat", "PhysGuns\data\gun_plate.rvmat", "PhysGuns\data\gun_wire.rvmat", "PhysGuns\data\gun_glow.rvmat", "PhysGuns\data\lamp_green.rvmat", "PhysGuns\data\lamp_amber.rvmat", "PhysGuns\data\lamp_red.rvmat", "PhysGuns\data\lamp_blue.rvmat", "PhysGuns\data\lamp_orange.rvmat"}},
						{0.5, {"PhysGuns\data\gun_steel_damage.rvmat", "PhysGuns\data\gun_black_damage.rvmat", "PhysGuns\data\gun_orange_damage.rvmat", "PhysGuns\data\gun_white_damage.rvmat", "PhysGuns\data\gun_grip_damage.rvmat", "PhysGuns\data\gun_plate_damage.rvmat", "PhysGuns\data\gun_wire_damage.rvmat", "PhysGuns\data\gun_glow_damage.rvmat", "PhysGuns\data\lamp_green_damage.rvmat", "PhysGuns\data\lamp_amber_damage.rvmat", "PhysGuns\data\lamp_red_damage.rvmat", "PhysGuns\data\lamp_blue_damage.rvmat", "PhysGuns\data\lamp_orange_damage.rvmat"}},
						{0.3, {"PhysGuns\data\gun_steel_badly.rvmat", "PhysGuns\data\gun_black_badly.rvmat", "PhysGuns\data\gun_orange_badly.rvmat", "PhysGuns\data\gun_white_badly.rvmat", "PhysGuns\data\gun_grip_badly.rvmat", "PhysGuns\data\gun_plate_badly.rvmat", "PhysGuns\data\gun_wire_badly.rvmat", "PhysGuns\data\gun_glow_badly.rvmat", "PhysGuns\data\lamp_green_badly.rvmat", "PhysGuns\data\lamp_amber_badly.rvmat", "PhysGuns\data\lamp_red_badly.rvmat", "PhysGuns\data\lamp_blue_badly.rvmat", "PhysGuns\data\lamp_orange_badly.rvmat"}},
						{0.0, {"PhysGuns\data\gun_steel_destruct.rvmat", "PhysGuns\data\gun_black_destruct.rvmat", "PhysGuns\data\gun_orange_destruct.rvmat", "PhysGuns\data\gun_white_destruct.rvmat", "PhysGuns\data\gun_grip_destruct.rvmat", "PhysGuns\data\gun_plate_destruct.rvmat", "PhysGuns\data\gun_wire_destruct.rvmat", "PhysGuns\data\gun_glow_destruct.rvmat", "PhysGuns\data\lamp_green_destruct.rvmat", "PhysGuns\data\lamp_amber_destruct.rvmat", "PhysGuns\data\lamp_red_destruct.rvmat", "PhysGuns\data\lamp_blue_destruct.rvmat", "PhysGuns\data\lamp_orange_destruct.rvmat"}}
					};
				};
			};
		};
	};
	class PG_GravityGun: PG_GunBase
	{
		scope = 2;
		displayName = "Gravity Gun";
		descriptionShort = "Picks up, carries and punts anything loose. Tap fire: punt. Hold fire: grab and carry (pulls from afar), let go to drop. Reload while carrying: throw. Runs on six 9V batteries.";
		model = "\PhysGuns\data\gravity_gun.p3d";
	};
	class PG_PortalGun: PG_GunBase
	{
		scope = 2;
		displayName = "Portal Gun";
		descriptionShort = "Opens two linked portals on fixed surfaces; whatever goes into one comes out of the other. Fire: the portal its lit lamp shows. Fire mode (or B): switch blue / orange. Hold breath while aiming (or middle mouse): orange. Reload: close both. Raise it to see where the portal will open. Runs on six 9V batteries.";
		model = "\PhysGuns\data\portal_gun.p3d";
	};
};

// the guns' six battery slots
class CfgSlots
{
	class Slot_PG_Battery1
	{
		name = "PG_Battery1";
		displayName = "9V Battery";
		ghostIcon = "set:dayz_inventory image:batteryd";
	};
	class Slot_PG_Battery2
	{
		name = "PG_Battery2";
		displayName = "9V Battery";
		ghostIcon = "set:dayz_inventory image:batteryd";
	};
	class Slot_PG_Battery3
	{
		name = "PG_Battery3";
		displayName = "9V Battery";
		ghostIcon = "set:dayz_inventory image:batteryd";
	};
	class Slot_PG_Battery4
	{
		name = "PG_Battery4";
		displayName = "9V Battery";
		ghostIcon = "set:dayz_inventory image:batteryd";
	};
	class Slot_PG_Battery5
	{
		name = "PG_Battery5";
		displayName = "9V Battery";
		ghostIcon = "set:dayz_inventory image:batteryd";
	};
	class Slot_PG_Battery6
	{
		name = "PG_Battery6";
		displayName = "9V Battery";
		ghostIcon = "set:dayz_inventory image:batteryd";
	};
};

class CfgVehicles
{
	class Inventory_Base;
	class Battery9V: Inventory_Base
	{
		inventorySlot[] += {"PG_Battery1", "PG_Battery2", "PG_Battery3", "PG_Battery4", "PG_Battery5", "PG_Battery6"};
	};
	class HouseNoDestruct;
	// a portal: no geometry (nothing collides with it), glowing; PG_Portal.c moves what passes through
	class PG_Portal: HouseNoDestruct
	{
		scope = 1;
		model = "\PhysGuns\data\portal_blue.p3d";
	};
	class PG_PortalBlue: PG_Portal
	{
		scope = 1;
		model = "\PhysGuns\data\portal_blue.p3d";
	};
	class PG_PortalOrange: PG_Portal
	{
		scope = 1;
		model = "\PhysGuns\data\portal_orange.p3d";
	};
	// the portal gun's lit mode lamp: a glowing dome over the unlit one, made on each client (PG_PortalGun.PG_Lamp)
	class PG_LampBlue: HouseNoDestruct
	{
		scope = 1;
		model = "\PhysGuns\data\portal_lamp_blue.p3d";
	};
	class PG_LampOrange: HouseNoDestruct
	{
		scope = 1;
		model = "\PhysGuns\data\portal_lamp_orange.p3d";
	};
	// where the portal gun would open a portal: a faint outline, made on its holder's client only (PG_PortalAim)
	class PG_PortalGhost: HouseNoDestruct
	{
		scope = 1;
		model = "\PhysGuns\data\portal_ghost.p3d";
	};
	// the gravity gun's beam: pieces of glowing tube, made on each client only (PG_Fx.c), laid end to end
	class PG_BeamSeg: HouseNoDestruct
	{
		scope = 1;
		model = "\PhysGuns\data\beam_seg.p3d";
	};
	class PG_BeamArc: HouseNoDestruct
	{
		scope = 1;
		model = "\PhysGuns\data\beam_arc.p3d";
	};
};

// the guns' sounds (tools/build_sounds.py): two loops the script shapes (the hum; the howl that rises as
// the beam swings) and the one-shots
class CfgSoundShaders
{
	class PG_SoundShader_Base
	{
		range = 45;
		volume = 1;
		limitation = 0;
	};
	class PG_Hum_SoundShader: PG_SoundShader_Base { samples[] = {{"PhysGuns\data\sound\hum", 1}}; volume = 0.7; };
	class PG_Howl_SoundShader: PG_SoundShader_Base { samples[] = {{"PhysGuns\data\sound\howl", 1}}; volume = 0.6; };
	class PG_Grab_SoundShader: PG_SoundShader_Base { samples[] = {{"PhysGuns\data\sound\grab", 1}}; volume = 0.9; };
	class PG_Drop_SoundShader: PG_SoundShader_Base { samples[] = {{"PhysGuns\data\sound\drop", 1}}; volume = 0.8; };
	class PG_Punt_SoundShader: PG_SoundShader_Base { samples[] = {{"PhysGuns\data\sound\punt", 1}}; volume = 1; range = 80; };
	class PG_Launch_SoundShader: PG_SoundShader_Base { samples[] = {{"PhysGuns\data\sound\launch", 1}}; volume = 1; range = 100; };
	class PG_Dry_SoundShader: PG_SoundShader_Base { samples[] = {{"PhysGuns\data\sound\dry", 1}}; volume = 0.8; };
	class PG_PortalFire_SoundShader: PG_SoundShader_Base { samples[] = {{"PhysGuns\data\sound\portal_fire", 1}}; volume = 0.9; range = 60; };
	class PG_PortalOpen_SoundShader: PG_SoundShader_Base { samples[] = {{"PhysGuns\data\sound\portal_open", 1}}; volume = 0.9; range = 50; };
	class PG_PortalHum_SoundShader: PG_SoundShader_Base { samples[] = {{"PhysGuns\data\sound\portal_hum", 1}}; volume = 0.5; range = 18; };
};

class CfgSoundSets
{
	class PG_SoundSet_Base
	{
		volumeFactor = 1;
		frequencyFactor = 1;
		spatial = 1;
		loop = 0;
	};
	class PG_Hum_SoundSet: PG_SoundSet_Base { soundShaders[] = {"PG_Hum_SoundShader"}; loop = 1; };
	class PG_Howl_SoundSet: PG_SoundSet_Base { soundShaders[] = {"PG_Howl_SoundShader"}; loop = 1; };
	class PG_Grab_SoundSet: PG_SoundSet_Base { soundShaders[] = {"PG_Grab_SoundShader"}; };
	class PG_Drop_SoundSet: PG_SoundSet_Base { soundShaders[] = {"PG_Drop_SoundShader"}; };
	class PG_Punt_SoundSet: PG_SoundSet_Base { soundShaders[] = {"PG_Punt_SoundShader"}; };
	class PG_Launch_SoundSet: PG_SoundSet_Base { soundShaders[] = {"PG_Launch_SoundShader"}; };
	class PG_Dry_SoundSet: PG_SoundSet_Base { soundShaders[] = {"PG_Dry_SoundShader"}; };
	// the portal gun: its shot (the orange one a little lower), a portal opening, a portal's own static
	class PG_PortalBlue_SoundSet: PG_SoundSet_Base { soundShaders[] = {"PG_PortalFire_SoundShader"}; };
	class PG_PortalOrange_SoundSet: PG_SoundSet_Base { soundShaders[] = {"PG_PortalFire_SoundShader"}; frequencyFactor = 0.84; };
	class PG_PortalOpen_SoundSet: PG_SoundSet_Base { soundShaders[] = {"PG_PortalOpen_SoundShader"}; };
	class PG_PortalHum_SoundSet: PG_SoundSet_Base { soundShaders[] = {"PG_PortalHum_SoundShader"}; loop = 1; };
};
