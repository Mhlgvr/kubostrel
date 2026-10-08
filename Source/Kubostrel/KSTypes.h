#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "KSTypes.generated.h"

class AActor;

namespace KSWeapon
{
	enum Type : uint8
	{
		Rifle = 0,
		Shotgun = 1,
		Rail = 2,
		Rocket = 3,
		Count = 4
	};
}

namespace KSPickupType
{
	enum Type : uint8
	{
		Health = 0,
		Shotgun = 1,
		Rail = 2,
		Rocket = 3,
		Count = 4
	};
}

namespace KSSound
{
	enum Type : uint8
	{
		RifleShot,
		ShotgunShot,
		RailShot,
		RocketLaunch,
		Explosion,
		HitMarker,
		KillConfirm,
		Headshot,
		Pickup,
		Jump,
		JumpPad,
		Footstep,
		Hurt,
		Reload,
		UIClick,
		Empty,
		Spawn,
		MatchEnd,
		Count
	};
}

struct FKSWeaponInfo
{
	const TCHAR* Name;
	const TCHAR* ShortName;
	float FireInterval;
	float Damage;
	int32 Pellets;
	float SpreadDeg;
	float MoveSpreadDeg;
	float Range;
	float HeadshotMult;
	// Rifle: magazine size (reserve ammo is unlimited). Other weapons: maximum carried ammo.
	int32 MaxAmmo;
	int32 PickupAmmo;
	float RecoilPitch;
	uint8 Sound;
	FLinearColor Color;
};

namespace KS
{
	constexpr int32 MaxPlayers = 6;
	constexpr int32 GamePort = 7777;
	constexpr int32 DiscoveryPort = 7787;
	constexpr float RespawnDelay = 3.0f;
	constexpr float SpawnProtectionTime = 2.0f;
	constexpr float MaxHealth = 100.f;
	constexpr float RifleReloadTime = 1.6f;
	constexpr float WeaponSwitchTime = 0.3f;
	constexpr float CapsuleRadius = 38.f;
	constexpr float CapsuleHalfHeight = 90.f;
	constexpr float EyeHeight = 68.f;
	constexpr float HeadshotHeight = 50.f;
	constexpr float RocketRadius = 380.f;
	constexpr float RocketSpeed = 2600.f;
	constexpr float MatchEndPause = 10.f;
	constexpr float GravityScale = 1.3f;
	constexpr float Gravity = 980.f * GravityScale;

	extern const TCHAR* GameVersion;
	extern const TCHAR* EntryMap;
	extern const TCHAR* GameModePath;

	const FKSWeaponInfo& Weapon(int32 Index);
	const FLinearColor& PlayerColor(int32 Index);
	int32 NumPlayerColors();
	const TCHAR* PickupName(int32 Type);
	FLinearColor PickupColor(int32 Type);
	FString FormatTime(float Seconds);
	// Simple quantized hash for caching colored materials.
	uint32 ColorKey(const FLinearColor& Color, float Extra = 0.f);
}

USTRUCT()
struct FKSShotHit
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<AActor> Actor;

	UPROPERTY()
	FVector_NetQuantize Location;

	UPROPERTY()
	FVector_NetQuantizeNormal Normal;

	FKSShotHit()
		: Actor(nullptr)
		, Location(FVector::ZeroVector)
		, Normal(FVector::UpVector)
	{
	}
};

// One pointer (finger or mouse) as seen by the UI this frame.
struct FKSPointer
{
	int32 Id = 0;
	FVector2D Pos = FVector2D::ZeroVector;
	FVector2D StartPos = FVector2D::ZeroVector;
	FVector2D Delta = FVector2D::ZeroVector;
	bool bDown = false;
	bool bPressed = false;
	bool bReleased = false;
};
