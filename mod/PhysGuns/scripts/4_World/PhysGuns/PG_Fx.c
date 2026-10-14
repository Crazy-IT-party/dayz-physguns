// The gravity gun's looks and sounds, on every client. While it holds or pulls something: a thick blue beam from
// the muzzle to it, like a live cable - a bright core in a glowing sheath, wavering, with thin arcs crackling round
// it - a blue light where it grips, a low electric hum, and a howl that rises as the beam swings. A punt: a flash
// of beam, a thump and a snap. The server tells every client what each gun does (PG_RPC.FX); the rest is drawn
// here, out of local objects (PG_BeamSeg / PG_BeamArc: pieces of tube laid end to end along the beam) and the
// sounds in data/sound (tools/build_sounds.py).
class PG_BeamSeg extends House
{
}

class PG_BeamArc extends House
{
}

class PG_BeamLight extends PointLightBase
{
	void PG_BeamLight()
	{
		SetVisibleDuringDaylight(true);
		SetRadiusTo(4);
		SetBrightnessTo(2.0);
		SetCastShadow(false);
		SetFlareVisible(false);
		SetDiffuseColor(0.25, 0.55, 1.0);
		SetAmbientColor(0.25, 0.55, 1.0);
	}
}

// one gun's beam and sounds
class PG_Beam
{
	PlayerBase Holder;
	Object Target;
	bool Held;				// holding or pulling: the steady beam
	bool Pulling;
	float Flash;			// a punt's flash of beam: seconds left
	Object FlashTarget;
	float FlashLen;			// fired at nothing: how far the flash reaches
	float Time;
	float ArcTimer;
	float Swing;			// how fast the beam swings, rad/s, smoothed
	vector LastDir;
	ref array<Object> Segs = new array<Object>;
	ref array<Object> Arcs = new array<Object>;
	ref array<vector> Jitter = new array<vector>;
	ScriptedLightBase Grip;
	ref array<ref array<Object>> Rings = new array<ref array<Object>>;	// the cage of glowing rings round what it holds
	ref array<ref array<Object>> Sparks = new array<ref array<Object>>;	// arcs crawling over it
	ref array<ref array<vector>> SparkPath = new array<ref array<vector>>;	// ... on the unit sphere
	float SparkTimer;
	AbstractWave Hum;
	AbstractWave Howl;
	ref SoundObject HumObj;
	ref SoundObjectBuilder HumBuilder;
	ref SoundObject HowlObj;
	ref SoundObjectBuilder HowlBuilder;
}

class PG_Fx
{
	static const float SEG_LEN = 0.2;		// the beam models' length (tools/build_models.py)
	static const float PITCH = 0.17;		// m between the points the beam is laid through
	static const int MAX_POINTS = 90;

	static ref map<PlayerBase, ref PG_Beam> s_Beams = new map<PlayerBase, ref PG_Beam>;

	// ---- what the server says -----------------------------------------------------------------------------------
	static void Event(PlayerBase holder, int ev, Object o)
	{
		if (!holder || g_Game.IsDedicatedServer())
			return;
		vector dir;
		vector at = Muzzle(holder, dir);
		if (ev == PG_FxEvent.PORTAL_BLUE || ev == PG_FxEvent.PORTAL_ORANGE)
		{
			if (ev == PG_FxEvent.PORTAL_BLUE)
				Play("PG_PortalBlue_SoundSet", at);
			else
				Play("PG_PortalOrange_SoundSet", at);
			MuzzleFlash(at + dir * 0.2, 3.0);
			return;
		}
		PG_Beam b = s_Beams.Get(holder);
		if (!b)
		{
			b = new PG_Beam();
			b.Holder = holder;
			s_Beams.Set(holder, b);
		}
		if (ev == PG_FxEvent.GRAB || ev == PG_FxEvent.PULL)
		{
			b.Target = o;
			b.Held = true;
			b.Pulling = ev == PG_FxEvent.PULL;
			b.LastDir = vector.Zero;
			b.Swing = 0;
			Play("PG_Grab_SoundSet", at);
			StartLoops(b, at);
		}
		else if (ev == PG_FxEvent.CATCH)
		{
			b.Pulling = false;
			Play("PG_Grab_SoundSet", at);
		}
		else if (ev == PG_FxEvent.DROP)
		{
			StopHold(b);
			Play("PG_Drop_SoundSet", at);
		}
		else if (ev == PG_FxEvent.PUNT || ev == PG_FxEvent.LAUNCH)
		{
			StopHold(b);
			b.Flash = 0.15;
			b.FlashTarget = o;
			b.FlashLen = 3.0;
			if (ev == PG_FxEvent.LAUNCH)
				Play("PG_Launch_SoundSet", at);
			else
				Play("PG_Punt_SoundSet", at);
			MuzzleFlash(at + dir * 0.2, 6.0);
		}
		else if (ev == PG_FxEvent.DRY)
		{
			b.Flash = 0.08;
			b.FlashTarget = null;
			b.FlashLen = 1.6;
			Play("PG_Dry_SoundSet", at);
			MuzzleFlash(at + dir * 0.2, 2.5);
		}
	}

	// ---- every frame ----------------------------------------------------------------------------------------------
	static void Update(float dt)
	{
		if (s_Beams.Count() == 0 || dt <= 0)
			return;
		for (int i = s_Beams.Count() - 1; i >= 0; i--)
		{
			PlayerBase p = s_Beams.GetKey(i);
			PG_Beam b = s_Beams.GetElement(i);
			if (!p || !p.IsAlive() || !PG_GravityGun.Cast(p.GetHumanInventory().GetEntityInHands()))
			{
				Clear(b);
				s_Beams.RemoveElement(i);
				continue;
			}
			if (b.Held && !b.Target)
				StopHold(b);
			Tick(b, dt);
		}
	}

	static void Tick(PG_Beam b, float dt)
	{
		b.Time += dt;
		vector dir;
		vector p0 = Muzzle(b.Holder, dir);
		vector end;
		bool show = false;
		if (b.Held && b.Target)
		{
			end = PG_GravityGun.Centre(b.Target);
			show = true;
		}
		else if (b.Flash > 0)
		{
			b.Flash -= dt;
			end = p0 + dir * b.FlashLen;
			if (b.FlashTarget)
				end = PG_GravityGun.Centre(b.FlashTarget);
			show = true;
		}
		if (!show)
		{
			Lay(b.Segs, "PG_BeamSeg", null);
			Lay(b.Arcs, "PG_BeamArc", null);
			return;
		}

		// the path: straight from the muzzle to what it grips, with a slight live waver
		vector chord = end - p0;
		float len = chord.Length();
		if (len < 0.05)
			return;
		int n = Math.Ceil(len / PITCH);
		n = Math.Clamp(n, 2, MAX_POINTS);
		vector c = chord * (1.0 / len);
		vector u = PG_GravityGun.Cross(c, "0 1 0");
		if (u.Length() < 0.1)
			u = PG_GravityGun.Cross(c, "1 0 0");
		u.Normalize();
		vector v = PG_GravityGun.Cross(c, u);
		float amp = 0.012;
		if (b.Pulling)
			amp = 0.02;
		b.ArcTimer -= dt;
		bool newArc = b.ArcTimer <= 0 || b.Jitter.Count() != n + 1;
		if (newArc)
		{
			b.ArcTimer = 0.045;
			b.Jitter.Clear();
		}
		array<vector> cable = new array<vector>;
		array<vector> arc = new array<vector>;
		for (int i = 0; i <= n; i++)
		{
			float t = i / (n * 1.0);
			float s = 1.0 - t;
			vector pt = p0 * s + end * t;
			float e = Math.Sin(Math.PI * t);
			float w1 = Math.Sin(t * 17.0 + b.Time * 11.0);
			float w2 = Math.Cos(t * 13.0 - b.Time * 9.0);
			cable.Insert(pt + (u * w1 + v * w2) * (amp * e));
			if (newArc)
				b.Jitter.Insert(Vector(Math.RandomFloat(-1, 1), Math.RandomFloat(-1, 1), 0));
			vector j = b.Jitter[i];
			arc.Insert(pt + (u * j[0] + v * j[1]) * (0.035 * e));
		}
		Lay(b.Segs, "PG_BeamSeg", cable);
		Lay(b.Arcs, "PG_BeamArc", arc);

		if (!b.Held)
			return;
		DrawField(b, end, dt);
		// the grip's light, flickering
		if (!b.Grip)
			b.Grip = ScriptedLightBase.CreateLight(PG_BeamLight, end);
		if (b.Grip)
		{
			b.Grip.SetPosition(end);
			b.Grip.SetBrightnessTo(1.6 + Math.RandomFloat(0, 1.2));
		}
		// the sounds follow the muzzle; the howl comes with the beam's swing
		vector bd = c;
		if (b.LastDir != vector.Zero)
		{
			float dot = Math.Clamp(vector.Dot(bd, b.LastDir), -1.0, 1.0);
			float rate = Math.Acos(dot) / dt;
			b.Swing += (rate - b.Swing) * Math.Min(1.0, 6.0 * dt);
		}
		b.LastDir = bd;
		float pull = 1.0;
		if (b.Pulling)
			pull = 1.2;
		if (b.Hum)
		{
			b.Hum.SetPosition(p0);
			b.Hum.SetFrequency(pull + 0.03 * Math.Sin(b.Time * 7.0));
		}
		if (b.Howl)
		{
			float k = Math.Clamp((b.Swing - 0.2) / 2.0, 0.0, 1.0);
			b.Howl.SetPosition(p0);
			b.Howl.SetVolumeRelative(k);
			b.Howl.SetFrequency(0.75 + 0.6 * k);
		}
	}

	//! lay a chain of tube pieces through pts (none: take them all away)
	static void Lay(array<Object> pool, string type, array<vector> pts)
	{
		int n = 0;
		if (pts)
			n = pts.Count() - 1;
		while (pool.Count() > n)
		{
			int last = pool.Count() - 1;
			Object old = pool[last];
			pool.Remove(last);
			if (old)
				g_Game.ObjectDelete(old);
		}
		while (pool.Count() < n)
		{
			Object made = g_Game.CreateObjectEx(type, pts[pool.Count()], ECE_LOCAL);
			if (!made)
				return;
			pool.Insert(made);
		}
		for (int i = 0; i < n; i++)
		{
			vector a = pts[i];
			vector d = pts[i + 1] - a;
			float l = d.Length();
			if (l < 0.001 || !pool[i])
				continue;
			d = d * (1.0 / l);
			vector up = "0 1 0";
			if (Math.AbsFloat(d[1]) > 0.95)
				up = "1 0 0";
			vector m[4];
			Math3D.DirectionAndUpMatrix(d, up, m);
			m[3] = a;
			pool[i].SetTransform(m);
			pool[i].SetScale(l / SEG_LEN);
		}
	}

	// ---- helpers ------------------------------------------------------------------------------------------------
	//! the muzzle of the gun in this player's hands and the way the barrel points
	static vector Muzzle(PlayerBase p, out vector dir)
	{
		dir = p.GetDirection();
		EntityAI gun = p.GetHumanInventory().GetEntityInHands();
		if (gun && gun.MemoryPointExists("usti hlavne"))
		{
			vector m = gun.ModelToWorld(gun.GetMemoryPointPos("usti hlavne"));
			if (gun.MemoryPointExists("konec hlavne"))
			{
				vector d = m - gun.ModelToWorld(gun.GetMemoryPointPos("konec hlavne"));
				if (d.Length() > 0.01)
					dir = d.Normalized();
			}
			return m;
		}
		return p.GetPosition() + "0 1.4 0" + dir * 0.6;
	}

	static void StartLoops(PG_Beam b, vector at)
	{
		StopLoops(b);
		b.Hum = PlayLoop("PG_Hum_SoundSet", at, b.HumObj, b.HumBuilder);
		b.Howl = PlayLoop("PG_Howl_SoundSet", at, b.HowlObj, b.HowlBuilder);
		if (b.Howl)
			b.Howl.SetVolumeRelative(0);
	}

	static void StopLoops(PG_Beam b)
	{
		if (b.Hum)
			b.Hum.Stop();
		if (b.Howl)
			b.Howl.Stop();
		b.Hum = null;
		b.Howl = null;
		b.HumObj = null;
		b.HumBuilder = null;
		b.HowlObj = null;
		b.HowlBuilder = null;
	}

	static void StopHold(PG_Beam b)
	{
		b.Held = false;
		b.Pulling = false;
		b.Target = null;
		StopLoops(b);
		if (b.Grip)
			b.Grip.Destroy();
		b.Grip = null;
		ClearField(b);
	}

	// ---- the field round what it holds ----------------------------------------------------------------------
	static const int SPARKS = 4;
	static const int SPARK_POINTS = 9;

	static const int RINGS = 4;
	static const int RING_POINTS = 29;

	//! a sphere a little bigger than it - a turning cage of thin glowing rings, open enough to see it inside - with
	//! arcs crawling over it
	static void DrawField(PG_Beam b, vector centre, float dt)
	{
		float r = FieldRadius(b.Target) * (1.0 + 0.02 * Math.Sin(b.Time * 23.0));
		while (b.Rings.Count() < RINGS)
			b.Rings.Insert(new array<Object>);
		float spin = b.Time * 0.9;
		for (int k = 0; k < RINGS; k++)
		{
			// great circles about axes spread round the sphere, the whole cage turning
			float az = spin + Math.PI * k / RINGS;
			float tilt = 0.5 + 0.35 * Math.Sin(spin * 0.7 + k);
			vector axis = Vector(Math.Cos(az) * Math.Sin(tilt), Math.Cos(tilt), Math.Sin(az) * Math.Sin(tilt));
			vector a = PG_GravityGun.Cross(axis, "0 1 0");
			if (a.Length() < 0.1)
				a = "1 0 0";
			a.Normalize();
			vector c = PG_GravityGun.Cross(axis, a);
			array<vector> ring = new array<vector>;
			for (int q = 0; q < RING_POINTS; q++)
			{
				float t = Math.PI2 * q / (RING_POINTS - 1.0);
				ring.Insert(centre + (a * Math.Cos(t) + c * Math.Sin(t)) * r);
			}
			Lay(b.Rings[k], "PG_BeamArc", ring);
		}
		b.SparkTimer -= dt;
		if (b.SparkTimer <= 0 || b.SparkPath.Count() != SPARKS)
		{
			b.SparkTimer = 0.06;
			b.SparkPath.Clear();
			for (int s = 0; s < SPARKS; s++)
				b.SparkPath.Insert(SparkPath());
		}
		while (b.Sparks.Count() < SPARKS)
			b.Sparks.Insert(new array<Object>);
		for (int i = 0; i < SPARKS; i++)
		{
			array<vector> unit = b.SparkPath[i];
			array<vector> pts = new array<vector>;
			foreach (vector u : unit)
				pts.Insert(centre + u * r);
			Lay(b.Sparks[i], "PG_BeamArc", pts);
		}
	}

	//! a jagged arc along a random great circle of the unit sphere
	static array<vector> SparkPath()
	{
		vector axis = Vector(Math.RandomFloat(-1, 1), Math.RandomFloat(-1, 1), Math.RandomFloat(-1, 1));
		if (axis.Length() < 0.1)
			axis = "0 1 0";
		axis.Normalize();
		vector a = PG_GravityGun.Cross(axis, "0 1 0");
		if (a.Length() < 0.1)
			a = PG_GravityGun.Cross(axis, "1 0 0");
		a.Normalize();
		vector c = PG_GravityGun.Cross(axis, a);
		float start = Math.RandomFloat(0, Math.PI2);
		float span = Math.RandomFloat(0.8, 2.0);
		array<vector> pts = new array<vector>;
		for (int i = 0; i < SPARK_POINTS; i++)
		{
			float t = start + span * i / (SPARK_POINTS - 1.0);
			vector p = a * Math.Cos(t) + c * Math.Sin(t) + axis * Math.RandomFloat(-0.08, 0.08);
			p.Normalize();
			pts.Insert(p * Math.RandomFloat(0.97, 1.06));
		}
		return pts;
	}

	static float FieldRadius(Object o)
	{
		if (!o)
			return 0.3;
		vector mm[2];
		if (!o.GetCollisionBox(mm))
			o.ClippingInfo(mm);
		return Math.Clamp(vector.Distance(mm[0], mm[1]) * 0.5 * 1.1 + 0.05, 0.25, 1.4);
	}

	static void ClearField(PG_Beam b)
	{
		foreach (array<Object> ringPool : b.Rings)
			Lay(ringPool, "PG_BeamArc", null);
		b.Rings.Clear();
		foreach (array<Object> pool : b.Sparks)
			Lay(pool, "PG_BeamArc", null);
		b.Sparks.Clear();
		b.SparkPath.Clear();
	}

	static void Clear(PG_Beam b)
	{
		StopHold(b);
		Lay(b.Segs, "PG_BeamSeg", null);
		Lay(b.Arcs, "PG_BeamArc", null);
	}

	static void MuzzleFlash(vector at, float brightness)
	{
		ScriptedLightBase l = ScriptedLightBase.CreateLight(PG_BeamLight, at);
		if (!l)
			return;
		l.SetBrightnessTo(brightness);
		l.SetRadiusTo(6);
		l.SetFadeOutTime(0.12);
		l.SetLifetime(0.1);
	}

	static AbstractWave PlayLoop(string soundSet, vector at, out SoundObject obj, out SoundObjectBuilder builder)
	{
		SoundParams sp = new SoundParams(soundSet);
		if (!sp.IsValid())
			return null;
		builder = new SoundObjectBuilder(sp);
		obj = builder.BuildSoundObject();
		if (!obj)
			return null;
		obj.SetKind(WaveKind.WAVEEFFECTEX);
		obj.SetPosition(at);
		AbstractWave w = g_Game.GetSoundScene().Play3D(obj, builder);
		if (w)
			w.Loop(true);
		return w;
	}

	static void Play(string soundSet, vector at)
	{
		SoundParams sp = new SoundParams(soundSet);
		if (!sp.IsValid())
			return;
		SoundObjectBuilder builder = new SoundObjectBuilder(sp);
		SoundObject obj = builder.BuildSoundObject();
		if (!obj)
			return;
		obj.SetKind(WaveKind.WAVEEFFECTEX);
		obj.SetPosition(at);
		g_Game.GetSoundScene().Play3D(obj, builder);
	}
}
