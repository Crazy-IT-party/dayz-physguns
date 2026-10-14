// Moving players by script. They have no rigid body to push: while held or
// thrown they run a scripted animation command (one class per kind, below) that moves them each frame by a
// translation the character controller then applies, so they still collide with the world. The state lives in
// PG_Motion; for a player it has to run where that player's movement is simulated - its own client - as well as
// on the server, so the server sends it there (PG_RPC.MOTION).
class PG_MoveState
{
	int Mode;
	vector Target;		// HELD: where to be
	vector Vel;			// current velocity, world
	float Age;			// seconds in this mode
	vector LastPos;		// where it was before this frame's translation
	vector Intended;	// this frame's translation, world
}

class PG_Motion
{
	static const float GRAVITY = 9.81;
	static const float HOLD_GAIN = 10.0;		// 1/s: velocity towards the hold point per metre off it
	static const float HOLD_MAX_SPEED = 18.0;
	static const float HOLD_RESPONSE = 14.0;	// 1/s: how fast the velocity follows

	static ref map<Object, ref PG_MoveState> s_States = new map<Object, ref PG_MoveState>;

	static PG_MoveState Get(Object o)
	{
		if (!o)
			return null;
		return s_States.Get(o);
	}

	static bool IsActive(Object o)
	{
		PG_MoveState s = Get(o);
		return s && s.Mode != PG_Mode.NONE;
	}

	//! set how a creature moves (on the server; players get it sent to their client too). teleport: where it
	//! has just been put (the owner's client moves its player itself)
	static void Set(EntityAI e, int mode, vector target, vector vel, vector teleport = "0 0 0")
	{
		if (!e)
			return;
		Apply(e, mode, target, vel);
		if (g_Game.IsServer())
		{
			PlayerBase p = PlayerBase.Cast(e);
			if (p && p.GetIdentity())
			{
				Param4<int, vector, vector, vector> prm = new Param4<int, vector, vector, vector>(mode, target, vel, teleport);
				// a target update while held may be lost; a change of mode may not
				g_Game.RPCSingleParam(p, PG_RPC.MOTION, prm, mode != PG_Mode.HELD || teleport != vector.Zero, p.GetIdentity());
			}
		}
	}

	static void Apply(EntityAI e, int mode, vector target, vector vel)
	{
		PG_MoveState s = Get(e);
		if (mode == PG_Mode.NONE)
		{
			if (s)
				s.Mode = PG_Mode.NONE;
			return;
		}
		if (!s)
		{
			s = new PG_MoveState();
			s_States.Set(e, s);
		}
		if (s.Mode != mode)
		{
			s.Age = 0;
			if (mode == PG_Mode.FLY || s.Mode == PG_Mode.NONE)
				s.Vel = vel;
		}
		s.Mode = mode;
		s.Target = target;
		if (mode == PG_Mode.FLY)
			s.Vel = vel;
	}

	//! PrePhys: the velocity for this frame
	static void Step(Object o, PG_MoveState s, float dt)
	{
		vector pos = o.GetPosition();
		s.Age += dt;
		if (s.Mode == PG_Mode.HELD)
		{
			vector want = (s.Target - pos) * HOLD_GAIN;
			float sp = want.Length();
			if (sp > HOLD_MAX_SPEED)
				want = want * (HOLD_MAX_SPEED / sp);
			float k = Math.Min(1.0, HOLD_RESPONSE * dt);
			s.Vel = s.Vel + (want - s.Vel) * k;
		}
		else if (s.Mode == PG_Mode.FLY)
		{
			s.Vel[1] = s.Vel[1] - GRAVITY * dt;
		}
		s.LastPos = pos;
	}

	// ---- hitting things -----------------------------------------------------------------------------------------
	static const float SAFE_SPEED = 8.0;		// m/s: slower than this a thrown body is unhurt (a 3 m fall)
	static const float LETHAL_SPEED = 20.0;		// m/s: this fast it is dead (a 20 m fall)
	static const float LEGS_LOW = 5.0;			// m: falls that start breaking legs, as vanilla fall damage ...
	static const float LEGS_HIGH = 9.0;			// ... and that surely break them
	static const float LEGS_DAMAGE = 125.0;

	//! how hard a landing hits: all of the fall, a little of the speed along the ground
	static float LandingSpeed(vector vel)
	{
		float h = Math.Sqrt(vel[0] * vel[0] + vel[2] * vel[2]) * 0.4;
		return Math.Sqrt(vel[1] * vel[1] + h * h);
	}

	//! (server) a thrown player, infected or animal hits a wall or the ground at this speed: hurt it as a fall
	//! from the same speed would - a player is knocked out and breaks legs too
	static void Impact(EntityAI e, float speed)
	{
		if (!g_Game.IsServer() || !e || !e.IsAlive() || speed <= SAFE_SPEED)
			return;
		float k = Math.InverseLerp(SAFE_SPEED, LETHAL_SPEED, speed);
		PlayerBase p = PlayerBase.Cast(e);
		if (p)
		{
			float height = speed * speed / (2.0 * GRAVITY);
			float legs = Math.Clamp(Math.InverseLerp(LEGS_LOW, LEGS_HIGH, height), 0, 1);
			if (legs > 0)
				p.DamageAllLegs(LEGS_DAMAGE * legs);
			p.DecreaseHealth("", "Shock", 150.0 * k);
		}
		e.DecreaseHealth("", "Health", e.GetMaxHealth("", "Health") * k);
	}

	//! the floor under a point: the ground or whatever stands on it (a roof, a floor), below "from"
	static float Floor(vector from, Object self)
	{
		vector hit, normal;
		int comp;
		set<Object> res = new set<Object>;
		float gy = g_Game.SurfaceY(from[0], from[2]);
		if (DayZPhysics.RaycastRV(from, Vector(from[0], gy - 0.5, from[2]), hit, normal, comp, res, null, self, false, false, ObjIntersectGeom, 0.0))
			return Math.Max(hit[1], gy);
		return gy;
	}

	//! PostPhys: where it goes this frame, its own collision with the world; false when the flight is over
	static bool After(Object o, PG_MoveState s, float dt, out vector outPos)
	{
		outPos = s.LastPos;
		if (s.Mode == PG_Mode.NONE)
			return false;
		vector from = s.LastPos;
		vector to = from + s.Vel * dt;
		vector waist = "0 0.9 0";
		// a wall in the way: stop at it, the velocity into it gone
		vector hit, normal;
		int comp;
		set<Object> res = new set<Object>;
		vector d = to - from;
		if (d.Length() > 0.001 && DayZPhysics.RaycastRV(from + waist, to + waist + d.Normalized() * 0.3, hit, normal, comp, res, null, o, false, false, ObjIntersectGeom, 0.0))
		{
			to = hit - waist - d.Normalized() * 0.35;
			to[1] = Math.Max(to[1], from[1] + d[1]);
			// the engine's hit normal comes unnormalised (or zero): unnormalised, taking the speed off along it
			// would throw the body back faster than it came, a bit more on every frame against the wall
			if (normal.Length() > 0.01)
			{
				normal.Normalize();
				float into = vector.Dot(s.Vel, normal);
				if (into < 0)
					s.Vel = s.Vel - normal * into;
			}
			else
				s.Vel = vector.Zero;
		}
		float floor = Floor(to + waist, o);
		bool landed = false;
		if (to[1] <= floor)
		{
			to[1] = floor;
			if (s.Vel[1] < 0)
				s.Vel[1] = 0;
			landed = true;
		}
		outPos = to;
		if (s.Mode == PG_Mode.FLY)
		{
			if ((landed && s.Age > 0.1) || s.Age > 20)
			{
				s.Mode = PG_Mode.NONE;
				return false;
			}
		}
		return true;
	}

}

class PG_HumanCmd extends HumanCommandScript
{
	protected DayZPlayer m_Who;
	protected vector m_Intended;

	void PG_HumanCmd(DayZPlayer pHuman)
	{
		m_Who = pHuman;
	}

	// a player is moved through its character controller (so it still collides), with the controller's own
	// gravity off - it would pin a held player to the ground; PG_Motion applies gravity when it flies
	override void OnActivate()
	{
		m_Who.PhysicsEnableGravity(false);
	}

	override void OnDeactivate()
	{
		m_Who.PhysicsEnableGravity(true);
	}

	override void PreAnimUpdate(float pDt)
	{
	}

	override void PrePhysUpdate(float pDt)
	{
		PG_MoveState s = PG_Motion.Get(m_Who);
		if (!s || s.Mode == PG_Mode.NONE)
		{
			PrePhys_SetTranslation(vector.Zero);
			return;
		}
		PG_Motion.Step(m_Who, s, pDt);
		m_Intended = s.Vel * pDt;
		PrePhys_SetTranslation(m_Who.VectorToLocal(m_Intended));
	}

	override bool PostPhysUpdate(float pDt)
	{
		PG_MoveState s = PG_Motion.Get(m_Who);
		if (!s || s.Mode == PG_Mode.NONE)
			return false;
		// the controller keeps a character on the ground: put it where it should be ourselves, stopped by walls,
		// floors and ceilings (PG_Motion.After)
		vector pos;
		vector before = s.Vel;
		int mode = s.Mode;
		bool go = PG_Motion.After(m_Who, s, pDt, pos);
		PostPhys_SetPosition(pos);
		// what a wall or the ground took off its speed is how hard it hit
		if (mode == PG_Mode.FLY)
		{
			vector lost = before - s.Vel;
			float hit = lost.Length();
			if (!go)
				hit = PG_Motion.LandingSpeed(before);		// landed: it skids on, the fall is what hurts
			PG_Motion.Impact(m_Who, hit);
		}
		return go;
	}
}

// a player starts the command from its own command handler (the only place a command may be started), which
// keeps its normal handling out of the way while it runs
modded class PlayerBase
{
	override bool ModCommandHandlerBefore(float pDt, int pCurrentCommandID, bool pCurrentCommandFinished)
	{
		if (PG_Motion.IsActive(this) && IsAlive())
		{
			if (pCurrentCommandID != DayZPlayerConstants.COMMANDID_SCRIPT || pCurrentCommandFinished)
				StartCommand_ScriptInst(PG_HumanCmd);
			return true;
		}
		return super.ModCommandHandlerBefore(pDt, pCurrentCommandID, pCurrentCommandFinished);
	}
}

// infected and animals: no scripted command (starting one on an infected crashes the server) - while the gravity
// gun holds or throws them they are moved directly (PG_GravityGun, as the dead), their AI paused
modded class ZombieBase
{
	override bool ModCommandHandlerBefore(float pDt, int pCurrentCommandID, bool pCurrentCommandFinished)
	{
		if (PG_GravityGun.IsPuppet(this))
			return true;
		return super.ModCommandHandlerBefore(pDt, pCurrentCommandID, pCurrentCommandFinished);
	}
}

modded class AnimalBase
{
	override bool ModCommandHandlerBefore(float pDt, int pCurrentCommandID, bool pCurrentCommandFinished)
	{
		if (PG_GravityGun.IsPuppet(this))
			return true;
		return super.ModCommandHandlerBefore(pDt, pCurrentCommandID, pCurrentCommandFinished);
	}
}
