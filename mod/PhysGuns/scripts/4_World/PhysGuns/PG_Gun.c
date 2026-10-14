// The two guns: rifles to the game (the stockless AKS74U's hands and animations), but the trigger never reaches the weapon's
// state machine - nothing loads, chambers or fires. The owner's client turns a trigger pull into a request to the
// server along the camera's line of sight (PG_RPC.PRIMARY); the secondary - holding breath while aiming down the
// sights, or the middle mouse button (UAPGSecondary, rebindable) - is read in PG_Client. The server does the rest (PG_GravityGun.c, PG_PortalGun.c).
class PG_GunBase extends AKS74U
{
	override bool ProcessWeaponEvent(WeaponEventBase e)
	{
		int id = e.GetEventID();
		if (id == WeaponEventID.TRIGGER || id == WeaponEventID.TRIGGER_JAM)
			return true;		// both guns read the fire button themselves (PG_Client.Update)
		return super.ProcessWeaponEvent(e);
	}

	override bool CanChamberBullet(int muzzleIndex, Magazine mag)
	{
		return false;
	}

	//! a debug / admin spawn comes charged: six fresh batteries
	override void OnDebugSpawn()
	{
		for (int i = 1; i <= BATTERIES; i++)
			GetInventory().CreateAttachment("Battery9V");
	}

	// ---- power: six 9V batteries in its slots (PG_Battery1..6, config.cpp), drawn on evenly -----------------
	static const int BATTERIES = 6;
	static ref map<PlayerBase, float> s_Warned = new map<PlayerBase, float>;

	ItemBase PG_Battery(int i)
	{
		return ItemBase.Cast(GetInventory().FindAttachment(InventorySlots.GetSlotIdFromString("PG_Battery" + i)));
	}

	//! all six in, each with its share of this much left
	bool PG_HasPower(float cost)
	{
		for (int i = 1; i <= BATTERIES; i++)
		{
			ItemBase b = PG_Battery(i);
			if (!b || !b.GetCompEM() || b.GetCompEM().GetEnergy() < cost / BATTERIES)
				return false;
		}
		return true;
	}

	//! (server) draw this much from the batteries; false - and the holder told - when they cannot give it
	bool PG_Use(PlayerBase p, float cost)
	{
		if (!PG_HasPower(cost))
		{
			PG_NoPower(p);
			return false;
		}
		for (int i = 1; i <= BATTERIES; i++)
			PG_Battery(i).GetCompEM().ConsumeEnergy(cost / BATTERIES);
		return true;
	}

	static void PG_NoPower(PlayerBase p)
	{
		if (!p)
			return;
		float now = g_Game.GetTickTime();
		if (s_Warned.Contains(p) && now - s_Warned.Get(p) < 3)
			return;
		s_Warned.Set(p, now);
		NotificationSystem.SendNotificationToPlayerExtended(p, 4, "No power", "Needs six charged 9V batteries", "set:dayz_inventory image:batteryd");
	}

	//! server: the trigger pulled / the secondary button pressed or released, along a camera ray
	void PG_Primary(PlayerBase p, vector from, vector dir);
	void PG_Secondary(PlayerBase p, vector from, vector dir, bool pressed);
	void PG_Aim(PlayerBase p, vector from, vector dir);
	void PG_Hold(PlayerBase p, vector from, vector dir, bool down);
	void PG_Reset(PlayerBase p, vector from, vector dir);
	void PG_SwitchMode(PlayerBase p);
}

// client side: the owner's input to the server
class PG_Client
{
	static const float TAP = 0.22;		// s: a shorter trigger pull is a punt, a longer one grabs

	static bool s_SecondaryDown;
	static float s_AimTimer;
	static float s_FireTime = -1;		// how long the gravity gun's trigger has been down (-1: up)
	static bool s_Grabbing;				// ... and it has turned into a grab
	static bool s_FireWas;				// the portal gun's fire button, last frame
	static bool s_ModeWas;				// the fire mode button, last frame ...
	static float s_ModeTime = -1;		// ... and when it last switched

	static PG_GunBase InHands()
	{
		PlayerBase p = PlayerBase.Cast(g_Game.GetPlayer());
		if (!p || !p.IsAlive())
			return null;
		return PG_GunBase.Cast(p.GetHumanInventory().GetEntityInHands());
	}

	static void Primary()
	{
		PlayerBase p = PlayerBase.Cast(g_Game.GetPlayer());
		if (!p)
			return;
		Param2<vector, vector> prm = new Param2<vector, vector>(g_Game.GetCurrentCameraPosition(), g_Game.GetCurrentCameraDirection());
		g_Game.RPCSingleParam(p, PG_RPC.PRIMARY, prm, true, null);
	}

	static void SendHold(PlayerBase p, vector from, vector dir, bool down)
	{
		Param3<vector, vector, bool> prm = new Param3<vector, vector, bool>(from, dir, down);
		g_Game.RPCSingleParam(p, PG_RPC.HOLD, prm, true, null);
	}

	//! every frame (MissionGameplay.OnUpdate)
	static void Update(float dt)
	{
		PlayerBase p = PlayerBase.Cast(g_Game.GetPlayer());
		PG_GunBase gun = InHands();
		bool menu = g_Game.GetUIManager() && g_Game.GetUIManager().GetMenu() != null;
		// the secondary: holding breath while aiming down the sights (whatever that is bound to - on a gamepad too),
		// or its own binding (middle mouse)
		UAInput sec = GetUApi().GetInputByName("UAPGSecondary");
		bool breath = p && p.GetInputController() && p.GetInputController().IsHoldBreath() && (p.IsInIronsights() || p.IsInOptics());
		bool down = gun && !menu && ((sec && sec.LocalValue() > 0.5) || breath);
		vector from = g_Game.GetCurrentCameraPosition();
		vector dir = g_Game.GetCurrentCameraDirection();
		if (down != s_SecondaryDown)
		{
			s_SecondaryDown = down;
			if (p)
			{
				Param3<vector, vector, bool> prm = new Param3<vector, vector, bool>(from, dir, down);
				g_Game.RPCSingleParam(p, PG_RPC.SECONDARY, prm, true, null);
			}
		}
		// the portal gun, aimed: where its portal would open; its lit mode lamp kept on it
		PG_PortalAim.Update(p, gun, menu, from, dir);
		PG_PortalGun.PG_LampsUpdate();
		// the fire mode button: the portal gun swaps which portal it opens
		// (the game's fire mode signal can stay on for several frames: once per press, and not again for a moment)
		UAInput modeIn = GetUApi().GetInputByName("UAPGMode");
		bool modeDown = (modeIn && modeIn.LocalValue() > 0.5) || (p && p.GetInputController() && p.GetInputController().IsFireModeChange());
		bool modeNow = modeDown && !s_ModeWas && g_Game.GetTickTime() - s_ModeTime > 0.3;
		s_ModeWas = modeDown;
		if (p && gun && !menu && modeNow)
		{
			s_ModeTime = g_Game.GetTickTime();
			Param2<vector, vector> ms = new Param2<vector, vector>(from, dir);
			g_Game.RPCSingleParam(p, PG_RPC.MODE, ms, true, null);
		}
		// the reload button: the gravity gun throws what it holds, the portal gun closes both its portals
		UAInput reload = GetUApi().GetInputByName("UAReloadMagazine");
		if (p && gun && !menu && reload && reload.LocalPress())
		{
			Param2<vector, vector> rs = new Param2<vector, vector>(from, dir);
			g_Game.RPCSingleParam(p, PG_RPC.RESET, rs, true, null);
		}
		// the gravity gun's trigger: let go quickly - a punt; held - a grab, kept until it is let go
		// the button itself (on a gamepad the controller's own "attack" state drops after a moment)
		UAInput fireIn = GetUApi().GetInputByName("UAFire");
		bool fireDown = (fireIn && fireIn.LocalValue() > 0.5) || (p && p.GetInputController() && p.GetInputController().IsAttackButton());
		bool fire = p && gun && !menu && fireDown;
		// the portal gun: a portal on each press (the game never passes a magazine-less gun's trigger on)
		if (gun && !PG_GravityGun.Cast(gun))
		{
			if (fire && !s_FireWas)
				Primary();
			s_FireWas = fire;
			s_FireTime = -1;
			s_Grabbing = false;
		}
		else if (fire)
		{
			if (s_FireTime < 0)
				s_FireTime = 0;
			else
				s_FireTime += dt;
			if (!s_Grabbing && s_FireTime >= TAP)
			{
				s_Grabbing = true;
				SendHold(p, from, dir, true);
			}
		}
		else if (s_FireTime >= 0)
		{
			if (s_Grabbing)
				SendHold(p, from, dir, false);
			else if (p)
				Primary();
			s_FireTime = -1;
			s_Grabbing = false;
		}
		// the gravity gun holds things where the player looks: keep the server told
		if (p && PG_GravityGun.Cast(gun))
		{
			s_AimTimer -= dt;
			if (s_AimTimer <= 0)
			{
				s_AimTimer = 0.05;
				Param2<vector, vector> aim = new Param2<vector, vector>(from, dir);
				g_Game.RPCSingleParam(p, PG_RPC.AIM, aim, false, null);
			}
		}
	}
}

// RPCs on the player: requests from its own client (server), its own motion (client)
modded class PlayerBase
{
	override void OnRPC(PlayerIdentity sender, int rpc_type, ParamsReadContext ctx)
	{
		super.OnRPC(sender, rpc_type, ctx);
		if (rpc_type == PG_RPC.MOTION)
		{
			if (!g_Game.IsClient())
				return;
			Param4<int, vector, vector, vector> m = new Param4<int, vector, vector, vector>(0, vector.Zero, vector.Zero, vector.Zero);
			if (!ctx.Read(m))
				return;
			if (m.param4 != vector.Zero)
				SetPosition(m.param4);
			PG_Motion.Apply(this, m.param1, m.param2, m.param3);
			return;
		}
		if (rpc_type == PG_RPC.FX)
		{
			if (!g_Game.IsClient())
				return;
			Param2<int, Object> fx = new Param2<int, Object>(0, null);
			if (ctx.Read(fx))
				PG_Fx.Event(this, fx.param1, fx.param2);
			return;
		}
		if (rpc_type != PG_RPC.PRIMARY && rpc_type != PG_RPC.SECONDARY && rpc_type != PG_RPC.AIM && rpc_type != PG_RPC.HOLD && rpc_type != PG_RPC.RESET && rpc_type != PG_RPC.MODE)
			return;
		if (!g_Game.IsServer() || !sender || !GetIdentity() || sender.GetId() != GetIdentity().GetId() || !IsAlive())
			return;
		PG_GunBase gun = PG_GunBase.Cast(GetHumanInventory().GetEntityInHands());
		if (!gun)
			return;
		if (rpc_type == PG_RPC.SECONDARY || rpc_type == PG_RPC.HOLD)
		{
			Param3<vector, vector, bool> s = new Param3<vector, vector, bool>(vector.Zero, vector.Zero, false);
			if (!ctx.Read(s) || !PG_Server.SaneRay(this, s.param1, s.param2))
				return;
			if (rpc_type == PG_RPC.HOLD)
				gun.PG_Hold(this, s.param1, s.param2.Normalized(), s.param3);
			else
				gun.PG_Secondary(this, s.param1, s.param2.Normalized(), s.param3);
			return;
		}
		Param2<vector, vector> r = new Param2<vector, vector>(vector.Zero, vector.Zero);
		if (!ctx.Read(r) || !PG_Server.SaneRay(this, r.param1, r.param2))
			return;
		if (rpc_type == PG_RPC.MODE)
			gun.PG_SwitchMode(this);
		else if (rpc_type == PG_RPC.RESET)
			gun.PG_Reset(this, r.param1, r.param2.Normalized());
		else if (rpc_type == PG_RPC.PRIMARY)
			gun.PG_Primary(this, r.param1, r.param2.Normalized());
		else
			gun.PG_Aim(this, r.param1, r.param2.Normalized());
	}
}

class PG_Server
{
	//! a camera ray from this player's client: the camera is near the player, the direction is a direction
	static bool SaneRay(PlayerBase p, vector from, vector dir)
	{
		if (dir.Length() < 0.5 || dir.Length() > 1.5)
			return false;
		return vector.Distance(from, p.GetPosition()) < 6.0;
	}

	//! the player's eye (head bone), where things are held in front of
	static vector Eye(PlayerBase p)
	{
		int bone = p.GetBoneIndexByName("Head");
		if (bone >= 0)
			return p.GetBonePositionWS(bone);
		return p.GetPosition() + "0 1.6 0";
	}

	//! the muzzle of the gun in the player's hands (its "usti hlavne" point), else the eye
	static vector Muzzle(PlayerBase p)
	{
		EntityAI gun = p.GetHumanInventory().GetEntityInHands();
		if (gun && gun.MemoryPointExists("usti hlavne"))
			return gun.ModelToWorld(gun.GetMemoryPointPos("usti hlavne"));
		return Eye(p);
	}

	//! start a camera ray level with the player, so nothing between a third-person camera and the player counts
	static vector RayStart(PlayerBase p, vector from, vector dir)
	{
		vector eye = Eye(p);
		float along = vector.Dot(eye - from, dir);
		if (along < 0)
			along = 0;
		return from + dir * along;
	}

	//! what the ray hits first (not the player, not what is in the player's hands), within range; hitPos is set
	//! (and null returned) when that is the ground
	static Object Pick(PlayerBase p, vector from, vector dir, float range, float radius, out vector hitPos, out vector hitNormal)
	{
		vector beg = RayStart(p, from, dir);
		RaycastRVParams rp = new RaycastRVParams(beg, beg + dir * range, p, radius);
		rp.sorted = true;
		rp.type = ObjIntersectGeom;
		array<ref RaycastRVResult> res = new array<ref RaycastRVResult>;
		array<Object> excl = new array<Object>;
		excl.Insert(p);
		EntityAI hands = p.GetHumanInventory().GetEntityInHands();
		if (hands)
			excl.Insert(hands);
		hitPos = vector.Zero;
		hitNormal = vector.Zero;
		if (!DayZPhysics.RaycastRVProxy(rp, res, excl))
			return null;
		foreach (RaycastRVResult r : res)
		{
			Object o = r.obj;
			if (r.hierLevel > 0 && r.parent)
				o = r.parent;
			if (o && (o == p || o == hands))
				continue;
			hitPos = r.pos;
			hitNormal = r.dir;
			return o;		// null: the ground
		}
		return null;
	}
}
