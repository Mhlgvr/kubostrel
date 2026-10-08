#include "KSTypes.h"

namespace KS
{
	const TCHAR* GameVersion = TEXT("1.0");
	const TCHAR* EntryMap = TEXT("/Engine/Maps/Entry");
	const TCHAR* GameModePath = TEXT("/Script/Kubostrel.KSGameMode");

	static const FKSWeaponInfo GWeapons[KSWeapon::Count] =
	{
		// Name, Short, Interval, Damage, Pellets, Spread, MoveSpread, Range, Head, MaxAmmo, Pickup, Recoil, Sound, Color
		{ TEXT("Автомат"),   TEXT("АВТ"),  0.105f, 11.f, 1, 1.2f, 2.8f, 12000.f, 1.6f, 30, 0,  0.55f, KSSound::RifleShot,    FLinearColor(0.10f, 0.80f, 1.00f) },
		{ TEXT("Дробовик"),  TEXT("ДРОБ"), 0.85f,  9.f,  9, 6.5f, 7.5f, 3500.f,  1.0f, 24, 12, 3.0f,  KSSound::ShotgunShot,  FLinearColor(1.00f, 0.45f, 0.05f) },
		{ TEXT("Рельса"),    TEXT("РЕЛ"),  1.15f,  80.f, 1, 0.0f, 0.6f, 25000.f, 1.5f, 12, 6,  2.2f,  KSSound::RailShot,     FLinearColor(0.90f, 0.15f, 1.00f) },
		{ TEXT("Ракетница"), TEXT("РАК"),  0.90f,  100.f,1, 0.0f, 0.0f, 0.f,     1.0f, 12, 6,  2.5f,  KSSound::RocketLaunch, FLinearColor(1.00f, 0.22f, 0.08f) },
	};

	static const FLinearColor GPlayerColors[] =
	{
		FLinearColor(0.00f, 0.75f, 1.00f),
		FLinearColor(1.00f, 0.10f, 0.65f),
		FLinearColor(0.35f, 1.00f, 0.15f),
		FLinearColor(1.00f, 0.45f, 0.02f),
		FLinearColor(1.00f, 0.85f, 0.05f),
		FLinearColor(0.55f, 0.25f, 1.00f),
	};

	const FKSWeaponInfo& Weapon(int32 Index)
	{
		return GWeapons[FMath::Clamp(Index, 0, (int32)KSWeapon::Count - 1)];
	}

	int32 NumPlayerColors()
	{
		return UE_ARRAY_COUNT(GPlayerColors);
	}

	const FLinearColor& PlayerColor(int32 Index)
	{
		const int32 Num = NumPlayerColors();
		return GPlayerColors[((Index % Num) + Num) % Num];
	}

	const TCHAR* PickupName(int32 Type)
	{
		switch (Type)
		{
		case KSPickupType::Health: return TEXT("Аптечка");
		case KSPickupType::Shotgun: return GWeapons[KSWeapon::Shotgun].Name;
		case KSPickupType::Rail: return GWeapons[KSWeapon::Rail].Name;
		case KSPickupType::Rocket: return GWeapons[KSWeapon::Rocket].Name;
		default: return TEXT("");
		}
	}

	FLinearColor PickupColor(int32 Type)
	{
		switch (Type)
		{
		case KSPickupType::Health: return FLinearColor(0.15f, 1.0f, 0.35f);
		case KSPickupType::Shotgun: return GWeapons[KSWeapon::Shotgun].Color;
		case KSPickupType::Rail: return GWeapons[KSWeapon::Rail].Color;
		case KSPickupType::Rocket: return GWeapons[KSWeapon::Rocket].Color;
		default: return FLinearColor::White;
		}
	}

	FString FormatTime(float Seconds)
	{
		const int32 Total = FMath::Max(0, FMath::CeilToInt(Seconds));
		return FString::Printf(TEXT("%d:%02d"), Total / 60, Total % 60);
	}

	uint32 ColorKey(const FLinearColor& Color, float Extra)
	{
		const uint32 R = (uint32)FMath::Clamp(FMath::RoundToInt(Color.R * 63.f), 0, 255);
		const uint32 G = (uint32)FMath::Clamp(FMath::RoundToInt(Color.G * 63.f), 0, 255);
		const uint32 B = (uint32)FMath::Clamp(FMath::RoundToInt(Color.B * 63.f), 0, 255);
		const uint32 E = (uint32)FMath::Clamp(FMath::RoundToInt(Extra * 8.f), 0, 255);
		return (R << 24) | (G << 16) | (B << 8) | E;
	}
}
