#ifndef __RES_VOXEL_H
#define __RES_VOXEL_H

#include <stdint.h>
// [RH] Voxels from Build

enum
{
	MAXVOXMIPS = 5,
};

struct kvxslab_t
{
	uint8_t		ztop;			// starting z coordinate of top of slab
	uint8_t		zleng;			// # of bytes in the color array - slab height
	uint8_t		backfacecull;	// low 6 bits tell which of 6 faces are exposed
	uint8_t		col[1/*zleng*/];// color data from top to bottom
};

struct kvxslab_bgra_t
{
	uint32_t	ztop;			// starting z coordinate of top of slab
	uint32_t	zleng;			// # of bytes in the color array - slab height
	uint32_t	backfacecull;	// low 6 bits tell which of 6 faces are exposed
	uint32_t	col[1/*zleng*/];// color data from top to bottom
};

struct FVoxel;

struct FVoxelMipLevel
{
	FVoxelMipLevel();
	~FVoxelMipLevel();

	int			SizeX;
	int			SizeY;
	int			SizeZ;
	DVector3	Pivot;
	int			*OffsetX;
	short		*OffsetXY;
private:
	uint8_t	*SlabData;
	TArray<uint8_t> SlabDataRemapped;
public:
	TArray<uint32_t> SlabDataBgra;

	uint8_t *GetSlabData(bool wantpaletted) const;

	friend FVoxel *R_LoadKVX(int lumpnum);
	friend struct FVoxel;
};

struct FVoxel
{
	TArray<uint8_t> Palette;
	int LumpNum;
	int NumMips;
	int VoxelIndex;
	FVoxelMipLevel Mips[MAXVOXMIPS];
	bool Remapped = false;
	bool Bgramade = false;

	void CreateBgraSlabData();
	void Remap();
	void RemovePalette();
};

struct FVoxelDef
{
	FVoxel *Voxel;
	int PlacedSpin;			// degrees/sec to spin actors without MF_DROPPED set
	int DroppedSpin;		// degrees/sec to spin actors with MF_DROPPED set
	int VoxeldefIndex;		// Needed by GZDoom
	double		Scale;
	DAngle		AngleOffset;// added to actor's angle to compensate for wrong-facing voxels
	DAngle		PitchOffset;
	DAngle		RollOffset;
	double		xoffset;
	double		yoffset;
	double		zoffset;
	bool		PitchFromMomentum;
	bool		UseActorPitch;
	bool		UseActorRoll;
};

extern TDeletingArray<FVoxel *> Voxels;	// used only to auto-delete voxels on exit.
extern TDeletingArray<FVoxelDef *> VoxelDefs;

FVoxel* VOX_GetVoxel(int lumpnum);

FVoxel *R_LoadKVX(int lumpnum);
FVoxelDef *R_LoadVoxelDef(int lumpnum, int spin);

#endif
