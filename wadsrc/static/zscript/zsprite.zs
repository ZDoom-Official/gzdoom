struct FSpawnZSpriteParams native
{
	native Vector3		Pos, Vel;
	native Vector2		Scale, Offset;
	native double		Roll, Alpha;
	native TextureID	Texture;
	native int			Style;
	native uint			Translation;
	native uint			Flags;
};

Class ZSprite : Thinker native
{
	native Vector3		Pos, Vel, Prev;
	native Vector2		Scale, Offset;
	native double		Roll, Alpha;
	native TextureID	Texture;
	native int			Style;
	native uint			Translation;
	native uint			Flags;

	native void SetTranslation(Name trans);
	native bool IsFrozen();

	static ZSprite Spawn(Class<ZSprite> type, TextureID tex, Vector3 pos, Vector3 vel, double alpha = 1.0, double roll = 0.0, 
						 Vector2 scale = (1,1), Vector2 offset = (0,0), int style = STYLE_Normal, int trans = 0, int flags = 0)
	{
		if (!Level)	return null;

		FSpawnZSpriteParams p;
		p.Texture = tex;
		p.Pos = pos;
		p.Vel = vel;
		p.Alpha = alpha;
		p.Roll = roll;
		p.Scale = scale;
		p.Offset = offset;
		p.Style = style;
		p.Translation = trans;
		p.Flags = flags;

		return level.SpawnZSprite(type, p);
	}
}