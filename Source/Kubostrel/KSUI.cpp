#include "KSUI.h"
#include "KSAssets.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "CanvasItem.h"
#include "TextureResource.h"
#include "Fonts/SlateFontInfo.h"

FKSTouchLayout FKSTouchLayout::Make(float ScreenWidthUnits)
{
	FKSTouchLayout L;
	const float W = ScreenWidthUnits;
	// Room for the notch and the rounded corners of the phone.
	const float SafeX = 150.f;
	// Keeps buttons clear of the home indicator at the bottom edge.
	const float SafeBottom = 64.f;
	L.Width = W;
	L.Height = 1080.f;
	L.SafeX = SafeX;
	L.SafeBottom = SafeBottom;
	L.MoveZoneRight = W * 0.42f;
	L.MoveCenter = FVector2D(SafeX + 230.f, 790.f);
	L.FireCenter = FVector2D(W - SafeX - 230.f, 790.f);
	L.JumpCenter = FVector2D(W - SafeX - 60.f, 590.f);
	L.ReloadCenter = FVector2D(W - SafeX - 440.f, 660.f);

	const float SlotW = 150.f;
	const float SlotH = 92.f;
	const float Gap = 14.f;
	const float Total = float(KSWeapon::Count) * SlotW + float(KSWeapon::Count - 1) * Gap;
	const float X0 = W * 0.5f - Total * 0.5f;
	for (int32 i = 0; i < KSWeapon::Count; ++i)
	{
		const float X = X0 + i * (SlotW + Gap);
		L.WeaponSlots[i] = FBox2D(FVector2D(X, 1080.f - SafeBottom - SlotH), FVector2D(X + SlotW, 1080.f - SafeBottom));
	}
	L.PauseButton = FBox2D(FVector2D(SafeX - 30.f, 26.f), FVector2D(SafeX + 66.f, 122.f));
	L.ScoreButton = FBox2D(FVector2D(W * 0.5f - 230.f, 16.f), FVector2D(W * 0.5f + 230.f, 136.f));
	return L;
}

void FKSUI::Begin(UCanvas* InCanvas, const TArray<FKSPointer>& PixelPointers, UKSAssets* InAssets)
{
	Canvas = InCanvas;
	Assets = InAssets;
	Font = GEngine ? GEngine->GetLargeFont() : nullptr;
	const float PixelHeight = Canvas ? FMath::Max(Canvas->ClipY, 1.f) : 1080.f;
	const float PixelWidth = Canvas ? FMath::Max(Canvas->ClipX, 1.f) : 1920.f;
	Scale = PixelHeight / 1080.f;
	Height = 1080.f;
	Width = PixelWidth / Scale;

	Pointers = PixelPointers;
	for (FKSPointer& P : Pointers)
	{
		P.Pos /= Scale;
		P.StartPos /= Scale;
		P.Delta /= Scale;
	}
}

void FKSUI::Rect(const FBox2D& Box, const FLinearColor& Color, bool bAdditive)
{
	if (!Canvas)
	{
		return;
	}
	FCanvasTileItem Tile(Box.Min * Scale, (Box.Max - Box.Min) * Scale, bAdditive ? Color * Color.A : Color);
	Tile.BlendMode = bAdditive ? SE_BLEND_Additive : SE_BLEND_Translucent;
	Canvas->DrawItem(Tile);
}

void FKSUI::Frame(const FBox2D& Box, const FLinearColor& Color, float Thickness)
{
	const FVector2D Min = Box.Min;
	const FVector2D Max = Box.Max;
	Rect(FBox2D(Min, FVector2D(Max.X, Min.Y + Thickness)), Color);
	Rect(FBox2D(FVector2D(Min.X, Max.Y - Thickness), Max), Color);
	Rect(FBox2D(FVector2D(Min.X, Min.Y + Thickness), FVector2D(Min.X + Thickness, Max.Y - Thickness)), Color);
	Rect(FBox2D(FVector2D(Max.X - Thickness, Min.Y + Thickness), FVector2D(Max.X, Max.Y - Thickness)), Color);
}

void FKSUI::GlowFrame(const FBox2D& Box, const FLinearColor& Color, float Thickness)
{
	// A wide faint frame behind a thin bright one looks like a neon tube.
	const float Spread = Thickness * 3.f;
	FLinearColor Soft = Color;
	Soft.A = 0.18f * Color.A;
	Frame(FBox2D(Box.Min - FVector2D(Spread), Box.Max + FVector2D(Spread)), Soft, Thickness + Spread * 2.f);
	Frame(Box, Color, Thickness);
}

void FKSUI::Line(const FVector2D& A, const FVector2D& B, const FLinearColor& Color, float Thickness)
{
	if (!Canvas)
	{
		return;
	}
	FCanvasLineItem Item(A * Scale, B * Scale);
	Item.SetColor(Color);
	Item.LineThickness = Thickness * Scale;
	Canvas->DrawItem(Item);
}

void FKSUI::Image(UTexture2D* Texture, const FBox2D& Box, const FLinearColor& Color, float RotationDegrees, bool bAdditive)
{
	if (!Canvas || !Texture || !Texture->GetResource())
	{
		return;
	}
	FCanvasTileItem Tile(Box.Min * Scale, Texture->GetResource(), (Box.Max - Box.Min) * Scale, bAdditive ? Color * Color.A : Color);
	Tile.BlendMode = bAdditive ? SE_BLEND_Additive : SE_BLEND_Translucent;
	if (RotationDegrees != 0.f)
	{
		Tile.Rotation = FRotator(0.f, RotationDegrees, 0.f);
		Tile.PivotPoint = FVector2D(0.5f, 0.5f);
	}
	Canvas->DrawItem(Tile);
}

void FKSUI::Circle(const FVector2D& Center, float Radius, const FLinearColor& Color)
{
	if (Assets)
	{
		Image(Assets->CircleTexture, BoxCentered(Center, Radius * 2.f, Radius * 2.f), Color);
	}
}

void FKSUI::Ring(const FVector2D& Center, float Radius, const FLinearColor& Color)
{
	if (Assets)
	{
		Image(Assets->RingTexture, BoxCentered(Center, Radius * 2.f, Radius * 2.f), Color);
	}
}

void FKSUI::Glow(const FVector2D& Center, float Radius, const FLinearColor& Color)
{
	if (Assets)
	{
		Image(Assets->SoftTexture, BoxCentered(Center, Radius * 2.f, Radius * 2.f), Color, 0.f, true);
	}
}

FVector2D FKSUI::MeasureText(const FString& Str, float Size) const
{
	if (!Canvas || !Font || Str.IsEmpty())
	{
		return FVector2D::ZeroVector;
	}
	const float FontScale = Size * Scale / FMath::Max(1.f, (float)Font->LegacyFontSize);
	double W = 0.0;
	double H = 0.0;
	Canvas->TextSize(Font, Str, W, H, FontScale, FontScale);
	return FVector2D(W, H) / Scale;
}

FVector2D FKSUI::Text(const FString& Str, const FVector2D& Pos, float Size, const FLinearColor& Color, EKSAlign Align, bool bBold, bool bCenterY, bool bShadow)
{
	if (!Canvas || !Font || Str.IsEmpty())
	{
		return FVector2D::ZeroVector;
	}
	const FVector2D Measured = MeasureText(Str, Size);
	FVector2D TopLeft = Pos;
	if (Align == EKSAlign::Center)
	{
		TopLeft.X -= Measured.X * 0.5f;
	}
	else if (Align == EKSAlign::Right)
	{
		TopLeft.X -= Measured.X;
	}
	if (bCenterY)
	{
		TopLeft.Y -= Measured.Y * 0.5f;
	}

	FCanvasTextItem Item(TopLeft * Scale, FText::FromString(Str), Font, Color);
	// Render the glyphs at the final size instead of stretching the small default font.
	FSlateFontInfo Info = Font->GetLegacySlateFontInfo();
	Info.Size = Size * Scale;
	if (bBold)
	{
		Info.TypefaceFontName = FName(TEXT("Bold"));
	}
	Item.SlateFontInfo = Info;
	if (bShadow)
	{
		Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.75f * Color.A), FVector2D(2.f, 2.f) * Scale);
	}
	Canvas->DrawItem(Item);
	return Measured;
}

bool FKSUI::IsHeld(const FBox2D& Box) const
{
	for (const FKSPointer& P : Pointers)
	{
		if (P.bDown && Box.IsInside(P.Pos) && Box.IsInside(P.StartPos))
		{
			return true;
		}
	}
	return false;
}

bool FKSUI::Tapped(const FBox2D& Box) const
{
	for (const FKSPointer& P : Pointers)
	{
		if (P.bReleased && Box.IsInside(P.Pos) && Box.IsInside(P.StartPos))
		{
			return true;
		}
	}
	return false;
}

bool FKSUI::PressedIn(const FBox2D& Box) const
{
	for (const FKSPointer& P : Pointers)
	{
		if (P.bPressed && Box.IsInside(P.Pos))
		{
			return true;
		}
	}
	return false;
}

bool FKSUI::AnyTap() const
{
	for (const FKSPointer& P : Pointers)
	{
		if (P.bReleased)
		{
			return true;
		}
	}
	return false;
}

void FKSUI::Panel(const FBox2D& Box, const FLinearColor& Accent)
{
	Rect(Box, KSColors::PanelFill);
	GlowFrame(Box, Accent, 3.f);
}

bool FKSUI::Button(const FBox2D& Box, const FString& Label, const FLinearColor& Accent, bool bEnabled, float TextSize)
{
	const bool bHeld = bEnabled && IsHeld(Box);
	FLinearColor Fill = bHeld ? Accent * 0.5f : FLinearColor(0.02f, 0.03f, 0.07f, 1.f);
	Fill.A = bHeld ? 0.95f : 0.8f;
	Rect(Box, Fill);
	const FLinearColor Edge = bEnabled ? Accent : FLinearColor(0.3f, 0.32f, 0.38f, 1.f);
	GlowFrame(Box, Edge, bHeld ? 4.f : 3.f);
	Text(Label, FVector2D(Box.GetCenter().X, Box.GetCenter().Y), TextSize, bEnabled ? KSColors::Text : KSColors::Dim, EKSAlign::Center, true, true);
	return bEnabled && Tapped(Box);
}
