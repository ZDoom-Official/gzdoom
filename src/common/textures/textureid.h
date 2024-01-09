#pragma once
#define TEXTUREID_H

#include <cstddef>

enum class ETextureType : uint8_t
{
	Any,
	Wall,
	Flat,
	Sprite,
	WallPatch,
	Build,		// no longer used but needs to remain for ZScript
	SkinSprite,
	Decal,
	MiscPatch,
	FontChar,
	Override,	// For patches between TX_START/TX_END
	Autopage,	// Automap background - used to enable the use of FAutomapTexture
	SkinGraphic,
	Null,
	FirstDefined,
	Special,
	SWCanvas,
};

class FTextureID
{
	friend class FTextureManager;
	friend void R_InitSpriteDefs();

public:
	FTextureID() = default;
	FTextureID(std::nullptr_t) : texnum(0) {}
	bool isNull() const { return texnum == 0; }
	bool isValid() const { return texnum > 0; }
	bool Exists() const { return texnum >= 0; }
	void SetInvalid() { texnum = -1; }
	void SetNull() { texnum = 0; }
	bool operator ==(const FTextureID &other) const { return texnum == other.texnum; }
	bool operator !=(const FTextureID &other) const { return texnum != other.texnum; }
	FTextureID operator +(int offset) const noexcept(true);
	int GetIndex() const { return texnum; }	// Use this only if you absolutely need the index!
	void SetIndex(int index) { texnum = index; }	// Use this only if you absolutely need the index!

											// The switch list needs these to sort the switches by texture index
	int operator -(FTextureID other) const { return texnum - other.texnum; }
	bool operator < (FTextureID other) const { return texnum < other.texnum; }
	bool operator > (FTextureID other) const { return texnum > other.texnum; }

protected:
	constexpr FTextureID(int num) : texnum(num) { }
private:
	int texnum;
};

class FNullTextureID : public FTextureID
{
public:
	constexpr FNullTextureID() : FTextureID(0) {}
};

// This is for the script interface which needs to do casts from int to texture.
class FSetTextureID : public FTextureID
{
public:
	constexpr FSetTextureID(int v) : FTextureID(v) {}
};

#ifdef TARRAY_H
template<> struct THashTraits<FTextureID>
{

	hash_t Hash(const FTextureID key) { return (hash_t)key.GetIndex(); }

	// Compares two keys, returning zero if they are the same.
	int Compare(const FTextureID left, const FTextureID right) { return left != right; }
};
#endif