// RPC ids (on the player entity). Client -> server: what the gun in hand should do, with the camera ray it was
// aimed along. Server -> one client: how its own character is being moved (only the owner can move a player).
class PG_RPC
{
	static const int PRIMARY      = 0x50470101;	// Param2<vector, vector> camera position, direction
	static const int SECONDARY    = 0x50470102;	// Param3<vector, vector, bool> ..., pressed (true) / released
	static const int AIM          = 0x50470103;	// Param2<vector, vector>, streamed while the gravity gun is in hand
	static const int HOLD         = 0x50470104;	// Param3<vector, vector, bool> gravity gun's trigger held down (true) / let go
	static const int MODE         = 0x50470106;	// Param2<vector, vector> the fire mode button: the portal gun swaps blue / orange
	static const int RESET        = 0x50470105;	// Param2<vector, vector> the reload button: the gravity gun throws, the portal gun closes its portals
	static const int MOTION       = 0x50470110;	// Param4<int, vector, vector, vector> PG_Mode, target, velocity, teleported to
	static const int FX           = 0x50470120;	// Param2<int, Object> PG_FxEvent, what it is about: to every client
}

// what a gun just did, for every client to show and play (PG_Fx.c)
enum PG_FxEvent
{
	GRAB,		// caught something, held at once
	PULL,		// caught something far off: pulling it in
	CATCH,		// what it pulled has arrived
	DROP,		// let go
	PUNT,		// punted something
	LAUNCH,		// threw what it held
	DRY,		// fired at nothing
	PORTAL_BLUE,	// the portal gun opened its blue portal ...
	PORTAL_ORANGE	// ... its orange one
}

// how a creature (player, infected, animal) is being moved by script
enum PG_Mode
{
	NONE,
	HELD,		// pulled towards a point in front of the gravity gun
	FLY			// thrown: flies under gravity until it lands
}
