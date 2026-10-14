modded class MissionGameplay
{
	override void OnUpdate(float timeslice)
	{
		super.OnUpdate(timeslice);
		PG_Client.Update(timeslice);
		PG_Fx.Update(timeslice);
	}
}
