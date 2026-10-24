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

// How screen pixels, where the engine reports fingers and the mouse, map to UI units on the HUD canvas.
struct FKSScreenToUI
{
	// UI = Pixel * Scale + Offset.
	FVector2D Scale = FVector2D(1.f, 1.f);
	FVector2D Offset = FVector2D::ZeroVector;
	// The canvas in UI units; it is always 1080 units tall.
	FVector2D Size = FVector2D(1920.f, 1080.f);

	// The mapping for a canvas that covers the whole screen.
	static FKSScreenToUI ForScreen(const FVector2D& ScreenSize)
	{
		FKSScreenToUI Map;
		const float UIPerPixel = 1080.f / FMath::Max(ScreenSize.Y, 1.f);
		Map.Scale = FVector2D(UIPerPixel, UIPerPixel);
		Map.Size = FVector2D(ScreenSize.X * UIPerPixel, 1080.f);
		return Map;
	}

	FKSPointer Convert(const FKSPointer& Pixel) const
	{
		FKSPointer P = Pixel;
		P.Pos = Pixel.Pos * Scale + Offset;
		P.StartPos = Pixel.StartPos * Scale + Offset;
		P.Delta = Pixel.Delta * Scale;
		return P;
	}
};
