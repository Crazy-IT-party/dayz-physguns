// The portal gun, as in Portal: fire opens a portal where you look - blue or orange, as the fire mode button sets
// (its lit lamp shows which) - the secondary button (middle mouse) the orange one, reload closes both; each gun has its own pair, and a new portal of a colour replaces the old. Once both are open,
// whatever comes into one - players, infected, animals, items, bodies - comes out of the other, turned the way
// the exit faces, with the speed it went in at ("speedy thing goes in, speedy thing comes out"): fall into a
// floor portal and you are flung out of a wall one. Portals open on the ground, walls, rocks - anything fixed,
// not on things that move. A portal is only a coloured glowing oval: the game cannot show the view through it.
class PG_Portal extends House
{
	protected ScriptedLightBase m_Light;
	protected AbstractWave m_Hum;						// its low static, while it is open
	protected ref SoundObject m_HumObj;
	protected ref SoundObjectBuilder m_HumBuilder;

	override void EEInit()
	{
		super.EEInit();
		if (!g_Game.IsDedicatedServer())
			g_Game.GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(MakeLight, 100);
	}

	override void EEDelete(EntityAI parent)
	{
		super.EEDelete(parent);
		if (m_Light)
			m_Light.Destroy();
		if (m_Hum)
			m_Hum.Stop();
	}

	protected void MakeLight()
	{
		if (m_Light)
			return;
		PG_Fx.Play("PG_PortalOpen_SoundSet", GetPosition());
		m_Hum = PG_Fx.PlayLoop("PG_PortalHum_SoundSet", GetPosition(), m_HumObj, m_HumBuilder);
		m_Light = ScriptedLightBase.CreateLight(PG_PortalLight, GetPosition());
		if (!m_Light)
			return;
		m_Light.AttachOnObject(this, "0 0 0.4");
		vector c = Colour();
		m_Light.SetDiffuseColor(c[0], c[1], c[2]);
		m_Light.SetAmbientColor(c[0], c[1], c[2]);
	}

	vector Colour()
	{
		return "0.2 0.55 1.0";
	}

	//! where a point in front of it, in its frame, comes out of the other one (turned half round about its up)
	static vector Mirror(vector v)
	{
		return Vector(-v[0], v[1], -v[2]);
	}
}

class PG_PortalBlue extends PG_Portal
{
}

class PG_PortalOrange extends PG_Portal
{
	override vector Colour()
	{
		return "1.0 0.5 0.1";
	}
}

// the portal gun's lit mode lamp (PG_PortalGun.PG_Lamp)
class PG_LampBlue extends House
{
}

class PG_LampOrange extends House
{
}

// where the portal gun would open a portal: a faint outline shown to its holder alone while aiming (PG_PortalAim)
class PG_PortalGhost extends House
{
}

class PG_PortalAim
{
	protected static PG_PortalGhost s_Ghost;

	//! (client, every frame) show the outline where a shot would land while the portal gun is raised
	static void Update(PlayerBase p, PG_GunBase gun, bool menu, vector from, vector dir)
	{
		vector m[4];
		bool show = p && PG_PortalGun.Cast(gun) && !menu && p.IsRaised() && PG_PortalGun.Place(p, from, dir, m);
		if (!show)
		{
			if (s_Ghost)
				g_Game.ObjectDelete(s_Ghost);
			s_Ghost = null;
			return;
		}
		if (!s_Ghost)
			s_Ghost = PG_PortalGhost.Cast(g_Game.CreateObjectEx("PG_PortalGhost", m[3], ECE_LOCAL));
		if (s_Ghost)
			s_Ghost.SetTransform(m);
	}
}

class PG_PortalLight extends PointLightBase
{
	void PG_PortalLight()
	{
		SetVisibleDuringDaylight(true);
		SetRadiusTo(5);
		SetBrightnessTo(2.5);
		SetCastShadow(false);
		SetFlareVisible(false);
		SetFadeOutTime(0.2);
	}
}

class PG_Track
{
	vector Pos;
	float Time;
	vector Vel;
}

class PG_Pair
{
	PG_PortalGun Gun;
	PG_Portal BluePortal;
	PG_Portal OrangePortal;
}

class PG_PortalGun extends PG_GunBase
{
	static const float RANGE = 250.0;
	static const float HALF_W = 0.62;			// the opening, half width and half height
	static const float HALF_H = 1.05;
	static const float DEPTH_IN = 0.35;			// how close to the surface something has to come to go in
	static const float EXIT_OUT = 0.55;			// how far in front of the exit it comes out
	static const float MIN_EXIT_SPEED = 2.5;	// m/s out of the exit, so nothing sits in a floor portal
	static const float COOLDOWN = 0.35;
	static const float TRACK_TIME = 0.1;		// s: speeds measured over at least this long
	static const float MAX_WALK_SPEED = 12.0;	// m/s: faster is a measuring error (a flight has its own speed)

	static ref array<ref PG_Pair> s_Pairs = new array<ref PG_Pair>;
	static ref map<Object, float> s_Cool = new map<Object, float>;
	static ref map<Object, ref PG_Track> s_Last = new map<Object, ref PG_Track>;
	static bool s_Running;
	static float s_Time;

	static const float POWER_SHOT = 2.0;		// a portal, from its batteries (PG_GunBase.PG_Use)

	// ---- the mode: which portal fire opens, synced to every client for its lamp --------------------------------
	protected bool m_PG_Orange;
	protected Object m_PG_Lamp;		// (client) the lit lamp's dome

	void PG_PortalGun()
	{
		RegisterNetSyncVariableBool("m_PG_Orange");
	}

	override void PG_SwitchMode(PlayerBase p)
	{
		m_PG_Orange = !m_PG_Orange;
		SetSynchDirty();
		PG_GravityGun.Fx(p, PG_FxEvent.DRY, null);		// a click
	}

	override void OnVariablesSynchronized()
	{
		super.OnVariablesSynchronized();
		PG_Lamp();
	}

	override void EEInit()
	{
		super.EEInit();
		if (!g_Game.IsDedicatedServer())
			g_Game.GetCallQueue(CALL_CATEGORY_GUI).CallLater(PG_Lamp, 200);
	}

	override void EEItemLocationChanged(notnull InventoryLocation oldLoc, notnull InventoryLocation newLoc)
	{
		super.EEItemLocationChanged(oldLoc, newLoc);
		if (!g_Game.IsDedicatedServer())
			g_Game.GetCallQueue(CALL_CATEGORY_GUI).CallLater(PG_Lamp, 50);
	}

	override void EEHealthLevelChanged(int oldLevel, int newLevel, string zone)
	{
		super.EEHealthLevelChanged(oldLevel, newLevel, zone);
		if (!g_Game.IsDedicatedServer())
			PG_Lamp();
	}

	//! (client) light the lamp of the mode: a glowing dome over it - none while it is packed away or ruined
	void PG_Lamp()
	{
		if (g_Game.IsDedicatedServer())
			return;
		if (m_PG_Lamp)
			g_Game.ObjectDelete(m_PG_Lamp);
		m_PG_Lamp = null;
		InventoryLocation il = new InventoryLocation();
		GetInventory().GetCurrentInventoryLocation(il);
		if (IsRuined() || il.GetType() == InventoryLocationType.CARGO || il.GetType() == InventoryLocationType.UNKNOWN)
			return;
		string point = "lamp_blue";
		string type = "PG_LampBlue";
		if (m_PG_Orange)
		{
			point = "lamp_orange";
			type = "PG_LampOrange";
		}
		if (!MemoryPointExists(point))
			return;
		m_PG_Lamp = g_Game.CreateObjectEx(type, GetPosition(), ECE_LOCAL);
		if (!m_PG_Lamp)
			return;
		m_PG_LampPoint = GetMemoryPointPos(point) + Vector(0, LAMP_LIFT, 0);
		PG_LampFollow();
		if (s_Lit.Find(this) < 0)
			s_Lit.Insert(this);
	}

	// a child of a gun in hands is not drawn: the dome is put on the lamp every frame instead (PG_LampsUpdate)
	protected vector m_PG_LampPoint;
	static const float LAMP_LIFT = 0.003;		// m: the dome's middle over the lamp's base (portal_lamp_*.p3d)
	static ref array<PG_PortalGun> s_Lit = new array<PG_PortalGun>;

	void PG_LampFollow()
	{
		if (!m_PG_Lamp)
			return;
		vector m[4];
		GetTransform(m);
		m[3] = ModelToWorld(m_PG_LampPoint);
		m_PG_Lamp.SetTransform(m);
	}

	//! (client, every frame) every lit lamp onto its gun
	static void PG_LampsUpdate()
	{
		for (int i = s_Lit.Count() - 1; i >= 0; i--)
		{
			PG_PortalGun g = s_Lit[i];
			if (!g || !g.m_PG_Lamp)
				s_Lit.Remove(i);
			else
				g.PG_LampFollow();
		}
	}

	override void PG_Primary(PlayerBase p, vector from, vector dir)
	{
		Shoot(p, from, dir, m_PG_Orange);
	}

	override void PG_Secondary(PlayerBase p, vector from, vector dir, bool pressed)
	{
		if (pressed)
			Shoot(p, from, dir, true);
	}

	//! the reload button: both portals close, to start over
	override void PG_Reset(PlayerBase p, vector from, vector dir)
	{
		PG_Pair pr = Pair();
		if (pr.BluePortal)
			g_Game.ObjectDelete(pr.BluePortal);
		if (pr.OrangePortal)
			g_Game.ObjectDelete(pr.OrangePortal);
		pr.BluePortal = null;
		pr.OrangePortal = null;
	}

	override void EEDelete(EntityAI parent)
	{
		super.EEDelete(parent);
		if (m_PG_Lamp)
			g_Game.ObjectDelete(m_PG_Lamp);
		if (!g_Game.IsServer())
			return;
		for (int i = s_Pairs.Count() - 1; i >= 0; i--)
		{
			PG_Pair pr = s_Pairs[i];
			if (pr.Gun != this)
				continue;
			if (pr.BluePortal)
				g_Game.ObjectDelete(pr.BluePortal);
			if (pr.OrangePortal)
				g_Game.ObjectDelete(pr.OrangePortal);
			s_Pairs.Remove(i);
		}
	}

	PG_Pair Pair()
	{
		foreach (PG_Pair pr : s_Pairs)
			if (pr.Gun == this)
				return pr;
		PG_Pair n = new PG_Pair();
		n.Gun = this;
		s_Pairs.Insert(n);
		return n;
	}

	//! where a shot along this ray would open a portal (its transform: facing out of the surface, upright on a wall,
	//! its top away from the shooter on a floor); false - nowhere: nothing in range, or a thing that moves
	static bool Place(PlayerBase p, vector from, vector dir, out vector m[4])
	{
		vector hit, normal;
		Object o = PG_Server.Pick(p, from, dir, RANGE, 0.0, hit, normal);
		if (hit == vector.Zero)
			return false;
		if (o && (PG_GravityGun.KindOf(o) != PG_Kind.NONE || o.IsInherited(PG_Portal)))
			return false;			// not on things that move
		if (o)
			normal = FaceNormal(p, from, dir, hit, normal);
		else
			normal = g_Game.SurfaceGetNormal(hit[0], hit[2]);
		normal.Normalize();
		if (vector.Dot(normal, dir) > 0)
			normal = -normal;		// facing the shooter
		vector up;
		if (Math.AbsFloat(normal[1]) < 0.7)
		{
			up = "0 1 0" - normal * normal[1];		// a wall: upright
		}
		else
		{
			vector fwd = dir;						// a floor or ceiling: its top away from the shooter
			fwd[1] = 0;
			if (fwd.Length() < 0.01)
				fwd = p.GetDirection();
			up = fwd - normal * vector.Dot(fwd, normal);
		}
		up.Normalize();
		Math3D.DirectionAndUpMatrix(normal, up, m);
		m[3] = hit + normal * 0.03;
		return true;
	}

	protected void Shoot(PlayerBase p, vector from, vector dir, bool orange)
	{
		vector m[4];
		if (!Place(p, from, dir, m))
			return;

		PG_Pair pr = Pair();
		PG_Portal other = pr.BluePortal;
		if (!orange)
			other = pr.OrangePortal;
		if (other && vector.Distance(other.GetPosition(), m[3]) < HALF_H * 2)
			return;					// would overlap the other one
		if (!PG_Use(p, POWER_SHOT))
			return;					// a portal opened costs power; a shot that opens none does not
		PG_Portal old = pr.OrangePortal;
		if (!orange)
			old = pr.BluePortal;
		if (old)
			g_Game.ObjectDelete(old);
		string type = "PG_PortalBlue";
		if (orange)
			type = "PG_PortalOrange";
		PG_Portal portal = PG_Portal.Cast(g_Game.CreateObjectEx(type, m[3], ECE_NONE));
		if (!portal)
			return;
		portal.SetTransform(m);
		portal.Update();
		if (orange)
			PG_GravityGun.Fx(p, PG_FxEvent.PORTAL_ORANGE, portal);
		else
			PG_GravityGun.Fx(p, PG_FxEvent.PORTAL_BLUE, portal);
		if (orange)
			pr.OrangePortal = portal;
		else
			pr.BluePortal = portal;
		Run();
	}

	//! the face of an object the ray hit: the engine's own hit normal comes unnormalised or zero, so it is taken from
	//! two more rays a few centimetres beside the first (the plane through the three hits)
	static vector FaceNormal(PlayerBase p, vector from, vector dir, vector hit, vector given)
	{
		vector side = dir * "0 1 0";
		if (side.Length() < 0.01)
			side = "1 0 0";
		side.Normalize();
		vector up = side * dir;
		up.Normalize();
		vector h1, h2, n1, n2;
		PG_Server.Pick(p, from + side * 0.06, dir, RANGE, 0.0, h1, n1);
		PG_Server.Pick(p, from + up * 0.06, dir, RANGE, 0.0, h2, n2);
		if (h1 != vector.Zero && h2 != vector.Zero && vector.Distance(h1, hit) < 0.5 && vector.Distance(h2, hit) < 0.5)
		{
			vector n = (h1 - hit) * (h2 - hit);
			if (n.Length() > 0.0001)
				return n.Normalized();
		}
		if (given.Length() > 0.1)
			return given.Normalized();
		return -dir;
	}

	// ---- passing through (server, every frame while any pair is open) -----------------------------------------
	static void Run()
	{
		if (s_Running)
			return;
		s_Running = true;
		g_Game.GetUpdateQueue(CALL_CATEGORY_SYSTEM).Insert(PG_Tick);
	}

	static void PG_Tick(float dt)
	{
		if (dt <= 0)
			return;
		s_Time += dt;
		int open = 0;
		for (int i = s_Pairs.Count() - 1; i >= 0; i--)
		{
			PG_Pair pr = s_Pairs[i];
			if (!pr.Gun)
			{
				if (pr.BluePortal)
					g_Game.ObjectDelete(pr.BluePortal);
				if (pr.OrangePortal)
					g_Game.ObjectDelete(pr.OrangePortal);
				s_Pairs.Remove(i);
				continue;
			}
			if (!pr.BluePortal || !pr.OrangePortal)
				continue;
			open++;
			Check(pr.BluePortal, pr.OrangePortal, dt);
			Check(pr.OrangePortal, pr.BluePortal, dt);
		}
		if (s_Pairs.Count() == 0)
		{
			g_Game.GetUpdateQueue(CALL_CATEGORY_SYSTEM).Remove(PG_Tick);
			s_Running = false;
			s_Last.Clear();
			s_Cool.Clear();
		}
	}

	//! how high above its feet a creature meets this portal: its feet a floor one, its head a ceiling one, its
	//! middle a wall one
	static float CreatureHeight(PG_Portal portal)
	{
		float facing = portal.GetDirection()[1];
		if (facing > 0.7)
			return 0.1;
		if (facing < -0.7)
			return 1.8;
		return 0.9;
	}

	//! the point of a thing that has to go through this portal: a creature's (CreatureHeight), an item's centre
	static vector Probe(Object o, int kind, PG_Portal portal)
	{
		if (kind == PG_Kind.CREATURE)
		{
			float h = CreatureHeight(portal);
			if (Math.AbsFloat(portal.GetDirection()[1]) <= 0.7)		// a wall one: whatever part of it is level with its middle
				h = Math.Clamp(portal.GetPosition()[1] - o.GetPosition()[1], 0.2, 1.7);
			return o.GetPosition() + Vector(0, h, 0);
		}
		if (kind == PG_Kind.CORPSE)
			return o.GetPosition() + "0 0.3 0";
		return PG_GravityGun.Centre(o);
	}

	static void Check(PG_Portal a, PG_Portal b, float dt)
	{
		array<Object> near = new array<Object>;
		g_Game.GetObjectsAtPosition3D(a.GetPosition(), 5.0, near, null);		// wide: something fast is far off a frame before
		foreach (Object o : near)
		{
			if (!o || o.IsInherited(PG_Portal) || o.IsInherited(Transport))
				continue;
			int kind = PG_GravityGun.KindOf(o);
			if (kind == PG_Kind.NONE)
				continue;
			vector probe = Probe(o, kind, a);
			vector walked = Track(o);
			float until;
			if (s_Cool.Find(o, until) && until > s_Time)
				continue;
			vector vel = vector.Zero;
			if (kind == PG_Kind.RIGID && dBodyIsSet(o))
				vel = GetVelocity(o);
			else
				vel = walked;
			PG_MoveState ms = PG_Motion.Get(o);
			if (ms && ms.Mode == PG_Mode.FLY)
				vel = ms.Vel;
			vector lv = a.VectorToLocal(vel);
			if (lv[2] > 0.3)
				continue;		// coming out of it, not going in
			vector l = a.WorldToModel(probe);
			bool inside = Math.AbsFloat(l[0]) <= HALF_W && Math.AbsFloat(l[1]) <= HALF_H && l[2] <= DEPTH_IN && l[2] >= -1.0;
			if (!inside)
			{
				// fast: over a metre a frame it can skip the thin zone - did its path since the last frame cross the opening?
				vector lp = a.WorldToModel(probe - vel * dt);
				if (lp[2] < 0 || l[2] > 0 || lp[2] - l[2] < 0.001)
					continue;
				vector c = lp + (l - lp) * (lp[2] / (lp[2] - l[2]));
				if (Math.AbsFloat(c[0]) > HALF_W || Math.AbsFloat(c[1]) > HALF_H)
					continue;
				probe = a.ModelToWorld(c);
			}
			Teleport(o, kind, a, b, probe, vel);
			s_Cool.Set(o, s_Time + COOLDOWN);
		}
	}

	//! how fast it has been going, over the last tenth of a second or more: on the server a player's position
	//! comes in network steps, so from one frame to the next it can seem to jump many times its speed
	static vector Track(Object o)
	{
		PG_Track t = s_Last.Get(o);
		if (!t)
		{
			t = new PG_Track();
			t.Pos = o.GetPosition();
			t.Time = s_Time;
			s_Last.Set(o, t);
			return vector.Zero;
		}
		float since = s_Time - t.Time;
		if (since >= TRACK_TIME)
		{
			if (since < TRACK_TIME * 4)
			{
				t.Vel = (o.GetPosition() - t.Pos) * (1.0 / since);
				if (t.Vel.Length() > MAX_WALK_SPEED)
					t.Vel = t.Vel.Normalized() * MAX_WALK_SPEED;
			}
			else
				t.Vel = vector.Zero;		// it was away: a new start
			t.Pos = o.GetPosition();
			t.Time = s_Time;
		}
		return t.Vel;
	}

	static void Teleport(Object o, int kind, PG_Portal a, PG_Portal b, vector probe, vector vel)
	{
		vector lp = a.WorldToModel(probe);
		lp[2] = 0;
		vector outP = PG_Portal.Mirror(lp);
		outP[0] = Math.Clamp(outP[0], -HALF_W * 0.5, HALF_W * 0.5);
		outP[1] = Math.Clamp(outP[1], -HALF_H * 0.5, HALF_H * 0.5);
		outP[2] = EXIT_OUT;
		vector newProbe = b.ModelToWorld(outP);
		vector lv = PG_Portal.Mirror(a.VectorToLocal(vel));
		if (lv[2] < MIN_EXIT_SPEED)
			lv[2] = MIN_EXIT_SPEED;
		vector newVel = b.VectorToParent(lv);
		vector newPos = newProbe + o.GetPosition() - Probe(o, kind, a);		// probe may be where its path crossed, not where it is
		if (kind == PG_Kind.CREATURE)
			newPos = newProbe - Vector(0, CreatureHeight(b), 0);		// feet out of a floor one, head a ceiling one

		if (kind == PG_Kind.RIGID)
		{
			vector m[4];
			o.GetTransform(m);
			for (int i = 0; i < 3; i++)
				m[i] = b.VectorToParent(PG_Portal.Mirror(a.VectorToLocal(m[i])));
			m[3] = newPos;
			vector spin = dBodyGetAngularVelocity(o);
			o.SetTransform(m);
			SetVelocity(o, newVel);
			dBodySetAngularVelocity(o, b.VectorToParent(PG_Portal.Mirror(a.VectorToLocal(spin))));
		}
		else if (kind == PG_Kind.CREATURE)
		{
			vector d = o.GetDirection();
			d = b.VectorToParent(PG_Portal.Mirror(a.VectorToLocal(d)));
			d[1] = 0;
			if (d.Length() < 0.1)
				d = b.GetDirection();
			d[1] = 0;
			d.Normalize();
			o.SetPosition(newPos);
			o.SetDirection(d);
			PG_Motion.Set(EntityAI.Cast(o), PG_Mode.FLY, vector.Zero, newVel, newPos);
		}
		else
		{
			o.SetPosition(newPos);
			PG_Thrown t = new PG_Thrown();
			t.Obj = o;
			t.Vel = newVel;
			PG_GravityGun.s_Thrown.Insert(t);
			PG_GravityGun.Run();
		}
		s_Last.Remove(o);
	}
}
