#pragma once

#include "CoreMinimal.h"

// Surface styles used by arena geometry. Each arena theme gives every style its own look.
namespace KSMat
{
	enum Type : uint8
	{
		Floor,
		Wall,
		Platform,
		CoverA,
		CoverB,
		Metal,
		Skyline,
		NeonA,
		NeonB,
		NeonC,
		NeonPad,
		Count
	};

	inline bool IsNeon(uint8 Mat) { return Mat >= NeonA && Mat < Count; }
}

enum class EKSShape : uint8
{
	Cube,
	Cylinder,
	Sphere
};

struct FKSBlock
{
	EKSShape Shape = EKSShape::Cube;
	FVector Center = FVector::ZeroVector;
	// Full extents in cm (cylinders: diameter, diameter, height).
	FVector Size = FVector(100.f, 100.f, 100.f);
	FRotator Rotation = FRotator::ZeroRotator;
	uint8 Mat = KSMat::Wall;
	bool bCollision = true;
	bool bCastShadow = true;
	// Blocks bot movement on the level it stands on. Floors and ramps are walkable, not obstacles.
	bool bObstacle = true;

	FBox GetBounds() const;
};

struct FKSSurfaceStyle
{
	FLinearColor Color = FLinearColor::White;
	float Roughness = 0.6f;
	float Metallic = 0.f;
	float GridScale = 200.f;
	float GridStrength = 0.3f;
	FLinearColor GlowColor = FLinearColor::Black;
	float GridGlow = 0.f;
	// Brightness for neon styles.
	float Intensity = 6.f;
};

struct FKSArenaTheme
{
	FRotator SunRotation = FRotator(-14.f, 135.f, 0.f);
	FLinearColor SunColor = FLinearColor(1.f, 0.7f, 0.45f);
	float SunIntensity = 8.f;
	float SkyLightIntensity = 1.2f;
	FLinearColor FogColor = FLinearColor(0.4f, 0.3f, 0.45f);
	float FogDensity = 0.02f;
	float FogFalloff = 0.2f;
	float FogStart = 1200.f;
	float Bloom = 1.0f;
	FKSSurfaceStyle Mats[KSMat::Count];
};

struct FKSPickupSpot
{
	FVector Location = FVector::ZeroVector;
	uint8 Type = 0;
};

struct FKSJumpPad
{
	FVector Location = FVector::ZeroVector;
	FVector Target = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
};

struct FKSNavLink
{
	FVector From = FVector::ZeroVector;
	FVector To = FVector::ZeroVector;
	bool bTwoWay = true;
};

struct FKSArenaDef
{
	FString Name;
	// Inner playable half size (walls start right outside it).
	FVector2D HalfSize = FVector2D(2800.f, 2800.f);
	TArray<FKSBlock> Blocks;
	TArray<FTransform> Spawns;
	TArray<FKSPickupSpot> Pickups;
	TArray<FKSJumpPad> JumpPads;
	// Extra bot waypoints on raised surfaces.
	TArray<FVector> NavPoints;
	// Ramps, drops and other connections the automatic linking can't see.
	TArray<FKSNavLink> NavLinks;
	FVector MenuCameraTarget = FVector(0.f, 0.f, 200.f);
	float MenuCameraRadius = 2600.f;
	float MenuCameraHeight = 900.f;
	FKSArenaTheme Theme;
};

namespace KSArena
{
	int32 Num();
	const FKSArenaDef& Get(int32 Index);
	FString GetName(int32 Index);
	FVector ComputeLaunchVelocity(const FVector& From, const FVector& To, float ApexAboveFrom);
}

// Waypoint graph for bots, built from an arena description on the server.
struct FKSNavGraph
{
	TArray<FVector> Nodes;
	TArray<TArray<int32>> Links;

	void Build(const FKSArenaDef& Def);
	int32 FindNearest(const FVector& Location) const;
	bool FindPath(int32 From, int32 To, TArray<int32>& OutPath) const;
	bool IsValid() const { return Nodes.Num() > 0; }

private:
	int32 AddNode(const FVector& Location);
	void AddLink(int32 From, int32 To);
};
