#pragma once

#include "CoreMinimal.h"
#include "KSTypes.h"

class UCanvas;
class UFont;
class UTexture2D;
class UKSAssets;

// Where the touch controls sit, in UI units (the screen is 1080 units tall).
struct FKSTouchLayout
{
	float Width = 1920.f;
	float Height = 1080.f;
	// Margins kept free for the notch, the rounded corners and the home indicator.
	float SafeX = 150.f;
	float SafeBottom = 64.f;
	// Touches that start left of this line steer; to the right they look around.
	float MoveZoneRight = 800.f;
	FVector2D MoveCenter = FVector2D::ZeroVector;
	float MoveRadius = 130.f;
	FVector2D FireCenter = FVector2D::ZeroVector;
	float FireRadius = 108.f;
	FVector2D JumpCenter = FVector2D::ZeroVector;
	float JumpRadius = 78.f;
	FVector2D ReloadCenter = FVector2D::ZeroVector;
	float ReloadRadius = 60.f;
	FBox2D WeaponSlots[KSWeapon::Count];
	FBox2D PauseButton;
	FBox2D ScoreButton;

	static FKSTouchLayout Make(float ScreenWidthUnits);
	static bool InCircle(const FVector2D& Point, const FVector2D& Center, float Radius)
	{
		return FVector2D::DistSquared(Point, Center) <= Radius * Radius;
	}
};

enum class EKSAlign : uint8
{
	Left,
	Center,
	Right
};

// Immediate-mode drawing and buttons on top of UCanvas, in UI units.
class FKSUI
{
public:
	void Begin(UCanvas* InCanvas, const TArray<FKSPointer>& PixelPointers, UKSAssets* InAssets);

	void Rect(const FBox2D& Box, const FLinearColor& Color, bool bAdditive = false);
	void Frame(const FBox2D& Box, const FLinearColor& Color, float Thickness = 3.f);
	void GlowFrame(const FBox2D& Box, const FLinearColor& Color, float Thickness = 3.f);
	void Line(const FVector2D& A, const FVector2D& B, const FLinearColor& Color, float Thickness = 2.f);
	void Circle(const FVector2D& Center, float Radius, const FLinearColor& Color);
	void Ring(const FVector2D& Center, float Radius, const FLinearColor& Color);
	void Glow(const FVector2D& Center, float Radius, const FLinearColor& Color);
	void Image(UTexture2D* Texture, const FBox2D& Box, const FLinearColor& Color, float RotationDegrees = 0.f, bool bAdditive = false);

	// Draws text with its top edge at Pos.Y (or centered on it). Returns the size in UI units.
	FVector2D Text(const FString& Text, const FVector2D& Pos, float Size, const FLinearColor& Color,
		EKSAlign Align = EKSAlign::Left, bool bBold = false, bool bCenterY = false, bool bShadow = true);
	FVector2D MeasureText(const FString& Text, float Size) const;

	// A neon button. Returns true when it was tapped (pressed and released inside).
	bool Button(const FBox2D& Box, const FString& Label, const FLinearColor& Accent, bool bEnabled = true, float TextSize = 40.f);
	void Panel(const FBox2D& Box, const FLinearColor& Accent);

	bool IsHeld(const FBox2D& Box) const;
	bool Tapped(const FBox2D& Box) const;
	bool PressedIn(const FBox2D& Box) const;
	bool AnyTap() const;

	static FBox2D BoxAt(float X, float Y, float W, float H) { return FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H)); }
	static FBox2D BoxCentered(const FVector2D& Center, float W, float H)
	{
		return FBox2D(Center - FVector2D(W, H) * 0.5f, Center + FVector2D(W, H) * 0.5f);
	}

	UCanvas* Canvas = nullptr;
	UKSAssets* Assets = nullptr;
	// Pixels per UI unit.
	float Scale = 1.f;
	float Width = 1920.f;
	float Height = 1080.f;
	// Pointers in UI units.
	TArray<FKSPointer> Pointers;

private:
	UFont* Font = nullptr;
};

namespace KSColors
{
	const FLinearColor Cyan(0.f, 0.85f, 1.f, 1.f);
	const FLinearColor Magenta(1.f, 0.15f, 0.7f, 1.f);
	const FLinearColor Orange(1.f, 0.55f, 0.1f, 1.f);
	const FLinearColor Green(0.3f, 1.f, 0.4f, 1.f);
	const FLinearColor Red(1.f, 0.2f, 0.2f, 1.f);
	const FLinearColor Yellow(1.f, 0.9f, 0.2f, 1.f);
	const FLinearColor Text(0.95f, 0.97f, 1.f, 1.f);
	const FLinearColor Dim(0.55f, 0.6f, 0.7f, 1.f);
	const FLinearColor PanelFill(0.02f, 0.025f, 0.05f, 0.82f);
}
