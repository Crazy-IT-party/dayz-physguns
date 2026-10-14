// The gravity gun, as in Half-Life 2:
//  - a tap of the trigger: punt what is in front (a short reach);
//  - the trigger held down: grab what it points at and carry it in front, where you look, wrapped in a crackling
//    field - from afar it is pulled in first; let go of the trigger: drop it;
//  - the secondary (hold breath while aiming, middle mouse) while holding: throw it along the line of sight.
// Held things hang on a spring: they lag behind a fast turn, swing, knock into walls and keep their attitude
// to you as you turn. What is heavier than the gun can hold (MAX_HOLD_MASS) can only be punted, and the punt
// sends light things far and heavy things a little. It moves items lying about, vehicles (anything with a rigid
// body), and the living - players, infected, animals (PG_Motion.c moves those).
enum PG_Kind
{
	NONE,
	RIGID,		// a rigid body: items, vehicles
	CREATURE,	// a living player: moved through its own character (PG_Motion)
	CORPSE		// moved directly: living infected and animals (their AI paused)
}

class PG_Grab
{
	PlayerBase Holder;
	Object Obj;
	int Kind;
	bool Pulling;			// still on its way in
	float Dist;				// how far past the muzzle its centre is held (HoldPoint)
	float Mass;
	vector Aim = "0 0 1";	// the holder's latest line of sight
	vector From;			// ... from the holder's camera
	vector Axes[3];			// its axes in the holder's heading frame, kept while held
	float SendTimer;
}

class PG_Thrown
{
	Object Obj;
	vector Vel;
	float Age;
}

class PG_GravityGun extends PG_GunBase
{
	// what it draws from its batteries (PG_GunBase.PG_Use; six 9V hold 300 in all)
	static const float POWER_PUNT = 1.0;
	static const float POWER_LAUNCH = 1.5;
	static const float POWER_GRAB = 0.5;		// needed to start a grab
	static const float POWER_HOLD = 0.25;		// a second of holding or pulling
	static const float PUNT_RANGE = 7.0;
	static const float GRAB_RANGE = 2.0;			// nearer than this it is held at once, else pulled in first
	static const float PULL_RANGE = 12.0;
	static const float HOLD_GAP = 1.0;				// m between the muzzle and what it holds
	static const float CREATURE_GAP = 1.3;			// m from the muzzle to a held creature's middle
	static const float TIP = 0.8;					// m from the eye to the barrel's end, along the line of sight
	static const float MAX_HOLD_MASS = 350.0;		// kg
	static const float PUNT_IMPULSE = 1800.0;		// N s: a 1 kg can flies off at the speed cap, a 1.5 t car at 1.2 m/s
	static const float LAUNCH_IMPULSE = 2600.0;
	static const float MAX_SPEED = 32.0;			// m/s
	static const float CREATURE_PUNT = 20.0;		// m/s given to a punted creature
	static const float CREATURE_LAUNCH = 28.0;
	static const float CREATURE_LIFT = 4.0;			// m/s up added to it, so it leaves the ground
	static const float PULL_FORCE = 6000.0;			// N
	static const float HOLD_FORCE = 22000.0;		// N: more than enough for anything holdable, sags with the heaviest
	static const float SPRING = 14.0;				// 1/s
	static const float SPIN = 8.0;					// 1/s: how hard it is turned back to its held attitude

	static ref map<PlayerBase, ref PG_Grab> s_Grabs = new map<PlayerBase, ref PG_Grab>;
	static ref array<ref PG_Thrown> s_Thrown = new array<ref PG_Thrown>;
	static ref array<ref PG_Thrown> s_Loose = new array<ref PG_Thrown>;	// let go, kept physical until it lands
	static bool s_Running;

	//! an infected or animal being carried or thrown: its AI must not move it meanwhile
	static bool IsPuppet(Object o)
	{
		foreach (PlayerBase p, PG_Grab g : s_Grabs)
			if (g.Obj == o)
				return true;
		foreach (PG_Thrown t : s_Thrown)
			if (t.Obj == o)
				return true;
		return false;
	}

	// ---- requests -----------------------------------------------------------------------------------------------
	override void PG_Primary(PlayerBase p, vector from, vector dir)
	{
		PG_Grab g = s_Grabs.Get(p);
		if (g && !g.Pulling)
		{
			if (!PG_Use(p, POWER_LAUNCH))
			{
				Fx(p, PG_FxEvent.DRY, null);
				return;
			}
			Object held = g.Obj;
			int kind = g.Kind;
			float mass = g.Mass;
			Release(p, false);
			Launch(held, kind, mass, dir, LAUNCH_IMPULSE, CREATURE_LAUNCH);
			Fx(p, PG_FxEvent.LAUNCH, held);
			return;
		}
		if (g)
			Release(p);
		Object o = Aimed(p, from, dir, PUNT_RANGE);
		int k = KindOf(o);
		if (k == PG_Kind.NONE || !PG_Use(p, POWER_PUNT))
		{
			Fx(p, PG_FxEvent.DRY, null);
			return;
		}
		Launch(o, k, MassOf(o, k), dir, PUNT_IMPULSE, CREATURE_PUNT);
		Fx(p, PG_FxEvent.PUNT, o);
	}

	//! the reload button: throw what it holds (as the trigger tapped while holding does)
	override void PG_Reset(PlayerBase p, vector from, vector dir)
	{
		PG_Grab g = s_Grabs.Get(p);
		if (g && !g.Pulling)
			PG_Primary(p, from, dir);
	}

	//! the trigger held down: grab what it points at (pulled in from afar) and hold it until the trigger is let go
	override void PG_Hold(PlayerBase p, vector from, vector dir, bool down)
	{
		if (!down)
		{
			Release(p);
			return;
		}
		if (s_Grabs.Get(p))
			Release(p);
		if (!PG_HasPower(POWER_GRAB))
		{
			PG_NoPower(p);
			Fx(p, PG_FxEvent.DRY, null);
			return;
		}
		Grab(p, from, dir);
	}

	//! the secondary (hold breath while aiming, middle mouse): throw what it holds, else punt
	override void PG_Secondary(PlayerBase p, vector from, vector dir, bool pressed)
	{
		if (pressed)
			PG_Primary(p, from, dir);
	}

	static void Grab(PlayerBase p, vector from, vector dir)
	{
		PG_Grab g;
		Object o = Aimed(p, from, dir, PULL_RANGE);
		int k = KindOf(o);
		if (k == PG_Kind.NONE)
		{
			Fx(p, PG_FxEvent.DRY, null);
			return;
		}
		float mass = MassOf(o, k);
		if (mass > MAX_HOLD_MASS)
		{
			Fx(p, PG_FxEvent.DRY, null);
			return;
		}
		g = new PG_Grab();
		g.Holder = p;
		g.Obj = o;
		g.Kind = k;
		g.Mass = mass;
		g.Aim = dir;
		g.From = from;
		g.Pulling = vector.Distance(Centre(o), HoldPoint(g)) > GRAB_RANGE;
		if (k == PG_Kind.RIGID)
			MakeDynamic(o);
		StoreAxes(g);
		s_Grabs.Set(p, g);
		Run();
		if (g.Pulling)
			Fx(p, PG_FxEvent.PULL, o);
		else
			Fx(p, PG_FxEvent.GRAB, o);
	}

	override void PG_Aim(PlayerBase p, vector from, vector dir)
	{
		PG_Grab g = s_Grabs.Get(p);
		if (g)
		{
			g.Aim = dir;
			g.From = from;
		}
	}

	//! where a held thing's centre should be: in front of the muzzle, on the line of sight -
	//! its near side a metre off the muzzle
	static vector HoldPoint(PG_Grab g)
	{
		vector muzzle = Tip(g);
		vector d = g.Aim;
		if (g.Kind == PG_Kind.RIGID)
			g.Dist = Reach(g.Obj, d) + HOLD_GAP;
		else
			g.Dist = CREATURE_GAP;
		return muzzle + d * g.Dist;
	}

	//! where the barrel ends, as the holder sees it: on the line of sight, a little below and ahead of the eye. The
	//! gun's own muzzle point is no good on the server - it does not raise the weapon there, it hangs at the hip
	static vector Tip(PG_Grab g)
	{
		return PG_Server.Eye(g.Holder) + g.Aim * TIP + "0 -0.08 0";
	}

	//! from its middle to the surface of its box, along dir (world)
	static float Reach(Object o, vector dir)
	{
		vector mm[2];
		if (!o.GetCollisionBox(mm))
			o.ClippingInfo(mm);			// the drawn box: a little loose
		vector h = (mm[1] - mm[0]) * 0.5;
		vector d = o.VectorToLocal(dir);
		float t = 2.0;
		for (int i = 0; i < 3; i++)
		{
			float a = Math.AbsFloat(d[i]);
			if (a > 0.001)
				t = Math.Min(t, h[i] / a);
		}
		return Math.Clamp(t, 0.05, 2.0);
	}

	//! tell every client what this gun did (PG_Fx.c draws and plays it)
	static void Fx(PlayerBase p, int ev, Object o)
	{
		if (!p)
			return;
		Param2<int, Object> prm = new Param2<int, Object>(ev, o);
		g_Game.RPCSingleParam(p, PG_RPC.FX, prm, true, null);
	}

	//! what it is pointed at: what the line of sight hits, or else the movable thing closest to it (in a narrow
	//! cone, in plain view) - small things are hard to hit exactly
	static Object Aimed(PlayerBase p, vector from, vector dir, float range)
	{
		vector hit, normal;
		Object o = PG_Server.Pick(p, from, dir, range, 0.0, hit, normal);
		if (KindOf(o) != PG_Kind.NONE)
			return o;
		float reach = range;
		if (hit != vector.Zero)
			reach = Math.Min(range, vector.Distance(PG_Server.RayStart(p, from, dir), hit) + 0.5);
		vector beg = PG_Server.RayStart(p, from, dir);
		array<Object> near = new array<Object>;
		g_Game.GetObjectsAtPosition3D(beg + dir * (reach * 0.5), reach * 0.5 + 1.0, near, null);
		EntityAI hands = p.GetHumanInventory().GetEntityInHands();
		Object best;
		float bestScore = 1e9;
		foreach (Object c : near)
		{
			if (c == p || c == hands || KindOf(c) == PG_Kind.NONE)
				continue;
			vector cp = Centre(c);
			float t = vector.Dot(cp - beg, dir);
			if (t < 0.2 || t > reach)
				continue;
			float off = vector.Distance(cp, beg + dir * t);
			if (off > 0.35 + t * 0.06 + Radius(c) * 0.5)
				continue;
			float score = off / t;
			if (score >= bestScore)
				continue;
			// in plain view: nothing fixed in between
			vector h2, n2;
			int comp;
			set<Object> res = new set<Object>;
			if (DayZPhysics.RaycastRV(beg, cp, h2, n2, comp, res, c, p, false, false, ObjIntersectGeom, 0.0))
			{
				bool blocked = false;
				foreach (Object r : res)
					if (r != c && r != hands && KindOf(r) == PG_Kind.NONE)
						blocked = true;
				if (blocked || (res.Count() == 0 && vector.Distance(h2, cp) > Radius(c) + 0.3))
					continue;
			}
			bestScore = score;
			best = c;
		}
		return best;
	}

	// ---- what it can move -------------------------------------------------------------------------------------
	static int KindOf(Object o)
	{
		if (!o || o.IsInherited(House) || o.IsInherited(PG_Portal))
			return PG_Kind.NONE;
		EntityAI e = EntityAI.Cast(o);
		if (e && (e.IsInherited(DayZPlayer) || e.IsInherited(DayZInfected) || e.IsInherited(DayZAnimal)))
		{
			if (e.IsAlive() && e.IsInherited(DayZPlayer))
				return PG_Kind.CREATURE;
			if (e.IsAlive())
				return PG_Kind.CORPSE;
			return PG_Kind.NONE;		// the dead: the server moving a body shows on no client
		}
		if (e && e.IsInherited(Transport))
			return PG_Kind.RIGID;
		ItemBase item = ItemBase.Cast(o);
		if (item)
		{
			if (item.GetHierarchyParent())
				return PG_Kind.NONE;		// in someone's inventory, or attached to something
			if (item.IsInherited(TentBase) || item.IsInherited(BaseBuildingBase) || item.IsInherited(GardenBase))
				return PG_Kind.NONE;		// built, pitched, planted
			return PG_Kind.RIGID;
		}
		if (dBodyIsSet(o) && dBodyIsDynamic(o))
			return PG_Kind.RIGID;
		return PG_Kind.NONE;
	}

	static float MassOf(Object o, int kind)
	{
		if (kind == PG_Kind.CREATURE || kind == PG_Kind.CORPSE || (EntityAI.Cast(o) && !EntityAI.Cast(o).IsInherited(ItemBase) && !o.IsInherited(Transport)))
		{
			if (o.IsInherited(DayZAnimal))
				return Math.Clamp(o.ConfigGetFloat("weight") / 1000.0, 20, 300);
			return 80;
		}
		if (dBodyIsSet(o))
		{
			float m = dBodyGetMass(o);
			if (m > 0)
				return m;
		}
		return Math.Max(0.1, o.ConfigGetFloat("weight") / 1000.0);
	}

	static float Radius(Object o)
	{
		vector mm[2];
		o.ClippingInfo(mm);
		return Math.Max(0.2, vector.Distance(mm[0], mm[1]) * 0.5);
	}

	static void MakeDynamic(Object o)
	{
		if (dBodyIsSet(o) && dBodyIsDynamic(o))
		{
			dBodyActive(o, ActiveState.ACTIVE);
			return;
		}
		ItemBase item = ItemBase.Cast(o);
		if (item)
		{
			item.ThrowPhysically(null, vector.Zero, true);
			item.SetDynamicPhysicsLifeTime(30);		// the default runs out mid-flight on a long throw
			return;
		}
		o.CreateDynamicPhysics(PhxInteractionLayers.DYNAMICITEM);
		o.EnableDynamicCCD(true);
	}

	//! the middle of its collision box (the drawn box can be much bigger)
	static vector Centre(Object o)
	{
		vector mm[2];
		if (!o.GetCollisionBox(mm))
			o.ClippingInfo(mm);
		return o.ModelToWorld((mm[0] + mm[1]) * 0.5);
	}

	// ---- punt / launch ----------------------------------------------------------------------------------------
	static void Launch(Object o, int kind, float mass, vector dir, float impulse, float creatureSpeed)
	{
		if (!o)
			return;
		if (kind == PG_Kind.RIGID)
		{
			MakeDynamic(o);
			float sp = Math.Min(MAX_SPEED, impulse / Math.Max(mass, 0.1));
			if (o.IsInherited(Transport))
				sp = Math.Max(sp, 6.0);		// a parked vehicle's brakes eat a small push
			SetVelocity(o, GetVelocity(o) + dir * sp);
			vector spin = Vector(Math.RandomFloat(-1, 1), Math.RandomFloat(-1, 1), Math.RandomFloat(-1, 1)) * Math.Min(6.0, sp * 0.3);
			dBodySetAngularVelocity(o, dBodyGetAngularVelocity(o) + spin);
			Loose(o);
		}
		else if (kind == PG_Kind.CREATURE)
		{
			vector v = dir * creatureSpeed;
			v[1] = Math.Max(v[1], 0) + CREATURE_LIFT;
			PG_Motion.Set(EntityAI.Cast(o), PG_Mode.FLY, vector.Zero, v);
		}
		else if (kind == PG_Kind.CORPSE)
		{
			// already flying (punted again in the air): just faster
			foreach (PG_Thrown f : s_Thrown)
			{
				if (f.Obj == o)
				{
					f.Vel = f.Vel + dir * creatureSpeed;
					f.Vel[1] = Math.Max(f.Vel[1], 0) + CREATURE_LIFT;
					return;
				}
			}
			PG_Thrown t = new PG_Thrown();
			t.Obj = o;
			t.Vel = dir * creatureSpeed;
			t.Vel[1] = Math.Max(t.Vel[1], 0) + CREATURE_LIFT;
			s_Thrown.Insert(t);
			Run();
		}
	}

	static void Release(PlayerBase p, bool fx = true)
	{
		PG_Grab g = s_Grabs.Get(p);
		if (!g)
			return;
		s_Grabs.Remove(p);
		if (fx)
			Fx(p, PG_FxEvent.DROP, g.Obj);
		if (!g.Obj)
			return;
		if (g.Kind == PG_Kind.CREATURE)
		{
			// let go: it keeps the velocity it had and falls
			PG_MoveState s = PG_Motion.Get(g.Obj);
			vector v = vector.Zero;
			if (s)
				v = s.Vel;
			PG_Motion.Set(EntityAI.Cast(g.Obj), PG_Mode.FLY, vector.Zero, v);
		}
		else if (g.Kind == PG_Kind.RIGID)
		{
			dBodyEnableGravity(g.Obj, true);
			Loose(g.Obj);
		}
		else if (g.Kind == PG_Kind.CORPSE)
		{
			PG_Thrown t = new PG_Thrown();
			t.Obj = g.Obj;
			t.Vel = vector.Zero;
			s_Thrown.Insert(t);
		}
	}

	//! keep a thrown or dropped body physical until it lies on something: an item's physics times out after a
	//! while and would leave it hanging where it was
	static void Loose(Object o)
	{
		foreach (PG_Thrown l : s_Loose)
			if (l.Obj == o)
			{
				l.Age = 0;
				return;
			}
		PG_Thrown n = new PG_Thrown();
		n.Obj = o;
		s_Loose.Insert(n);
		Run();
	}

	//! false once it has landed (or after a while)
	static bool Settle(PG_Thrown l, float dt)
	{
		Object o = l.Obj;
		if (!o || o.IsInherited(Transport))
			return false;
		l.Age += dt;
		if (l.Age > 30)
			return false;
		vector mm[2];
		o.ClippingInfo(mm);
		vector c = Centre(o);
		float bottom = c[1] - (mm[1][1] - mm[0][1]) * 0.5;
		float floor = PG_Motion.Floor(c, o);
		bool resting = bottom - floor < 0.25;
		bool dyn = dBodyIsSet(o) && dBodyIsDynamic(o);
		if (resting && l.Age > 0.5 && (!dyn || GetVelocity(o).Length() < 0.3))
			return false;
		if (!dyn)
			MakeDynamic(o);
		else if (!dBodyIsActive(o))
			dBodyActive(o, ActiveState.ACTIVE);
		return true;
	}

	// ---- the per-frame part (server) ------------------------------------------------------------------------
	static void Run()
	{
		if (s_Running)
			return;
		s_Running = true;
		g_Game.GetUpdateQueue(CALL_CATEGORY_SYSTEM).Insert(PG_Tick);
	}

	static void Heading(PlayerBase p, out vector fwd, out vector right)
	{
		fwd = p.GetDirection();
		fwd[1] = 0;
		fwd.Normalize();
		right = Vector(fwd[2], 0, -fwd[0]);
	}

	static vector Cross(vector a, vector b)
	{
		return Vector(a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]);
	}

	static void StoreAxes(PG_Grab g)
	{
		vector m[4];
		g.Obj.GetTransform(m);
		vector fwd, right;
		Heading(g.Holder, fwd, right);
		for (int i = 0; i < 3; i++)
			g.Axes[i] = Vector(vector.Dot(m[i], right), m[i][1], vector.Dot(m[i], fwd));
	}

	static void PG_Tick(float dt)
	{
		if (dt <= 0)
			return;
		dt = Math.Min(dt, 0.1);
		array<PlayerBase> drop = new array<PlayerBase>;
		foreach (PlayerBase p, PG_Grab g : s_Grabs)
		{
			if (!p || !g.Obj || !p.IsAlive() || !PG_GravityGun.Cast(p.GetHumanInventory().GetEntityInHands()) || KindOf(g.Obj) != g.Kind)
			{
				drop.Insert(p);
				continue;
			}
			PG_GunBase gun = PG_GunBase.Cast(p.GetHumanInventory().GetEntityInHands());
			if (!gun.PG_Use(p, POWER_HOLD * dt) || !Hold(g, dt))
				drop.Insert(p);
		}
		foreach (PlayerBase d : drop)
		{
			if (d)
				Release(d);
			else
				s_Grabs.Remove(d);
		}
		for (int i = s_Thrown.Count() - 1; i >= 0; i--)
		{
			if (!Fly(s_Thrown[i], dt))
				s_Thrown.Remove(i);
		}
		for (int j = s_Loose.Count() - 1; j >= 0; j--)
		{
			if (!Settle(s_Loose[j], dt))
				s_Loose.Remove(j);
		}
		if (s_Grabs.Count() == 0 && s_Thrown.Count() == 0 && s_Loose.Count() == 0)
		{
			g_Game.GetUpdateQueue(CALL_CATEGORY_SYSTEM).Remove(PG_Tick);
			s_Running = false;
		}
	}

	//! one frame of holding / pulling; false to let go
	static bool Hold(PG_Grab g, float dt)
	{
		vector eye = Tip(g);
		vector target = HoldPoint(g);
		vector pos = Centre(g.Obj);
		if (g.Pulling)
		{
			float d = vector.Distance(pos, target);
			if (d > PULL_RANGE + 5)
				return false;
			if (d <= 1.2)
			{
				g.Pulling = false;
				StoreAxes(g);
				Fx(g.Holder, PG_FxEvent.CATCH, g.Obj);
			}
		}
		if (g.Kind == PG_Kind.RIGID)
			return HoldRigid(g, target, pos, eye, dt);
		if (g.Kind == PG_Kind.CREATURE)
		{
			g.SendTimer -= dt;
			if (g.SendTimer <= 0)
			{
				g.SendTimer = 0.05;
				PG_Motion.Set(EntityAI.Cast(g.Obj), PG_Mode.HELD, target - (pos - g.Obj.GetPosition()), vector.Zero);
			}
			return true;
		}
		// an infected, an animal: moved straight, a little behind; pulled in at a steady pace
		vector cur = g.Obj.GetPosition();
		vector want = target - (pos - cur);
		vector step = (want - cur) * Math.Min(1.0, 16.0 * dt);
		if (g.Pulling)
		{
			step = want - cur;
			float sl = step.Length();
			if (sl > 14.0 * dt)
				step = step * (14.0 * dt / sl);
		}
		vector next = cur + step;
		float floor = PG_Motion.Floor(next + "0 1 0", g.Obj);
		if (next[1] < floor)
			next[1] = floor;			// not through the ground
		g.Obj.SetPosition(next);
		return true;
	}

	static bool HoldRigid(PG_Grab g, vector target, vector pos, vector eye, float dt)
	{
		Object o = g.Obj;
		if (!dBodyIsSet(o))
			return false;
		if (!dBodyIsDynamic(o))
			MakeDynamic(o);
		dBodyActive(o, ActiveState.ACTIVE);
		vector v = GetVelocity(o);
		float maxAcc;
		vector acc;
		if (g.Pulling)
		{
			vector toEye = eye - pos;
			toEye.Normalize();
			maxAcc = PULL_FORCE / Math.Max(g.Mass, 0.1);
			acc = toEye * Math.Min(maxAcc, 40.0) - v * 1.5;		// drawn in, a little damped against orbiting
			acc[1] = acc[1] + 9.81;
		}
		else
		{
			vector wantV = (target - pos) * SPRING;
			float ws = wantV.Length();
			if (ws > MAX_SPEED)
				wantV = wantV * (MAX_SPEED / ws);
			acc = (wantV - v) * 12.0;
			acc[1] = acc[1] + 9.81;							// holding up against gravity
			maxAcc = HOLD_FORCE / Math.Max(g.Mass, 0.1);
		}
		float al = acc.Length();
		if (al > maxAcc)
			acc = acc * (maxAcc / al);
		SetVelocity(o, v + acc * dt);
		if (!g.Pulling)
		{
			// turn it back to the attitude it was picked up in, relative to the holder's heading
			vector m[4];
			o.GetTransform(m);
			vector fwd, right;
			Heading(g.Holder, fwd, right);
			vector w = vector.Zero;
			for (int i = 0; i < 3; i++)
			{
				vector ax = g.Axes[i];
				vector want = right * ax[0] + Vector(0, ax[1], 0) + fwd * ax[2];
				w = w + Cross(m[i], want);
			}
			w = w * (0.5 * SPIN);
			vector cur = dBodyGetAngularVelocity(o);
			dBodySetAngularVelocity(o, cur + (w - cur) * Math.Min(1.0, 10.0 * dt));
		}
		// stuck too far off (behind a wall): let go
		return vector.Distance(pos, target) < 6.0 || g.Pulling;
	}

	//! a thrown infected or animal: flies straight under gravity until it hits something
	static bool Fly(PG_Thrown t, float dt)
	{
		if (!t.Obj)
			return false;
		t.Age += dt;
		t.Vel[1] = t.Vel[1] - 9.81 * dt;
		vector cur = t.Obj.GetPosition();
		vector next = cur + t.Vel * dt;
		vector hit, normal;
		int comp;
		set<Object> res = new set<Object>;
		vector up = "0 0.3 0";
		if (DayZPhysics.RaycastRV(cur + up, next + up, hit, normal, comp, res, null, t.Obj, false, false, ObjIntersectGeom, 0.0) || t.Age > 15)
		{
			// back off the surface, so it does not end up inside it
			vector back = t.Vel.Normalized();
			vector land = hit - up - back * 0.5;
			// the ray's normal comes short (and sometimes wrong way round, or nothing at all)
			if (normal.Length() < 0.001)
				normal = -back;
			normal.Normalize();
			if (vector.Dot(t.Vel, normal) > 0)
				normal = -normal;
			float gy = g_Game.SurfaceY(land[0], land[2]);
			if (land[1] < gy)
				land[1] = gy;
			t.Obj.SetPosition(land);
			// a wall, a tree: stopped dead; a floor (it faces up): landed
			float speed = t.Vel.Length();
			if (normal[1] > 0.7)
				speed = PG_Motion.LandingSpeed(t.Vel);
			PG_Motion.Impact(EntityAI.Cast(t.Obj), speed);
			return false;
		}
		float ground = g_Game.SurfaceY(next[0], next[2]);
		if (next[1] <= ground)
		{
			next[1] = ground;
			t.Obj.SetPosition(next);
			PG_Motion.Impact(EntityAI.Cast(t.Obj), PG_Motion.LandingSpeed(t.Vel));
			return false;
		}
		t.Obj.SetPosition(next);
		return true;
	}
}
