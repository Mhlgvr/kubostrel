#include "KSHUD.h"
#include "KSArenaData.h"
#include "KSAssets.h"
#include "KSAudio.h"
#include "KSCharacter.h"
#include "KSGameInstance.h"
#include "KSGameState.h"
#include "KSPlayerController.h"
#include "KSPlayerState.h"
#include "KSTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"

namespace
{
	FLinearColor KSAlpha(const FLinearColor& Color, float Alpha)
	{
		FLinearColor Result = Color;
		Result.A = Alpha;
		return Result;
	}

	// 1 while Age < Hold, then fades to 0 over Fade seconds.
	float KSFade(float Age, float Hold, float Fade)
	{
		return Age < Hold ? 1.f : FMath::Clamp(1.f - (Age - Hold) / Fade, 0.f, 1.f);
	}

	const FLinearColor KSDark(0.02f, 0.03f, 0.07f, 0.6f);
}

void AKSHUD::DrawHUD()
{
	Super::DrawHUD();
	AKSPlayerController* PC = Cast<AKSPlayerController>(PlayerOwner);
	if (!PC || !Canvas)
	{
		return;
	}
	UI.Begin(Canvas, PC->GetPointers(), UKSAssets::Get(this));
	UKSGameInstance* GI = Cast<UKSGameInstance>(GetGameInstance());
	AKSGameState* GS = GetWorld()->GetGameState<AKSGameState>();
	AKSCharacter* Robot = PC->GetKSCharacter();
	const float Now = PC->GetLocalTime();
	const bool bEnded = GS && !GS->IsPlaying();
	const bool bAlive = Robot && Robot->IsAlive();

	DrawDamage(PC, Robot, Now);
	if (bAlive && !bEnded && !PC->IsMenuOpen())
	{
		DrawCrosshair(PC, Robot, Now);
	}
	DrawTopBar(PC, GS);
	DrawKillFeed(PC, GS, Now);
	if (!bEnded)
	{
		if (bAlive)
		{
			DrawStatus(PC, Robot, Now);
		}
		if (PC->IsTouchMode() && !PC->IsMenuOpen())
		{
			DrawTouchControls(PC, Robot, Now);
		}
		DrawMessages(PC, Now);
		DrawDeath(PC, Robot, GS);
		if (PC->bScoreHeld && GS && !PC->IsMenuOpen())
		{
			DrawScoreboard(PC, GS, 170.f);
		}
	}
	else if (GS)
	{
		DrawMatchEnd(PC, GS);
	}

	if (PC->IsMenuOpen())
	{
		DrawMenu(PC, GI);
	}
	else if (PC->IsTouchMode())
	{
		const FBox2D& Pause = PC->GetLayout().PauseButton;
		const bool bHeld = UI.IsHeld(Pause);
		UI.Rect(Pause, bHeld ? KSAlpha(KSColors::Cyan * 0.4f, 0.9f) : KSDark);
		UI.GlowFrame(Pause, KSAlpha(KSColors::Cyan, 0.7f), 2.f);
		const FVector2D C = Pause.GetCenter();
		UI.Rect(FKSUI::BoxCentered(C - FVector2D(10.f, 0.f), 10.f, 38.f), KSColors::Text);
		UI.Rect(FKSUI::BoxCentered(C + FVector2D(10.f, 0.f), 10.f, 38.f), KSColors::Text);
		if (UI.Tapped(Pause))
		{
			KSAudio::Play2D(this, KSSound::UIClick);
			PC->SetMenuOpen(true);
		}
	}

	if (GI && GI->GetSettings().bShowFPS)
	{
		const float X = PC->GetLayout().SafeX - 30.f;
		UI.Text(FString::Printf(TEXT("%d FPS"), FMath::RoundToInt(PC->GetFPS())), FVector2D(X, 136.f), 24.f, KSColors::Dim);
	}
}

// ---------------------------------------------------------------------------------------------
// Aim
// ---------------------------------------------------------------------------------------------

void AKSHUD::DrawCrosshair(AKSPlayerController* PC, AKSCharacter* Robot, float Now)
{
	const FVector2D C(UI.Width * 0.5f, UI.Height * 0.5f);
	const int32 Weapon = Robot->GetWeapon();
	const float Fov = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetFOVAngle() : 100.f;
	// Distance on screen at which a ray this many degrees off-center lands.
	const float Focal = (UI.Width * 0.5f) / FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(Fov, 30.f, 150.f) * 0.5f));
	const float Gap = FMath::Clamp(Focal * FMath::Tan(FMath::DegreesToRadians(Robot->GetSpreadDegrees())), 7.f, 170.f);
	const bool bOnTarget = PC->bAimOnTarget || PC->bAutoFiring;
	const FLinearColor Color = bOnTarget ? FLinearColor(1.f, 0.32f, 0.3f, 0.95f) : FLinearColor(1.f, 1.f, 1.f, 0.9f);
	const FLinearColor Shadow(0.f, 0.f, 0.f, 0.45f);
	auto Bar = [this, &Color, &Shadow](const FBox2D& Box)
	{
		UI.Rect(Box.ExpandBy(1.5f), Shadow);
		UI.Rect(Box, Color);
	};

	switch (Weapon)
	{
	case KSWeapon::Shotgun:
		UI.Ring(C, Gap + 10.f, KSAlpha(Color, 0.85f));
		break;
	case KSWeapon::Rail:
		Bar(FBox2D(C + FVector2D(-46.f, -1.f), C + FVector2D(-12.f, 1.f)));
		Bar(FBox2D(C + FVector2D(12.f, -1.f), C + FVector2D(46.f, 1.f)));
		UI.Ring(C, 7.f, KSAlpha(Color, 0.9f));
		break;
	case KSWeapon::Rocket:
		UI.Ring(C, 22.f, KSAlpha(Color, 0.85f));
		Bar(FBox2D(C + FVector2D(-1.5f, 26.f), C + FVector2D(1.5f, 40.f)));
		break;
	default:
		Bar(FBox2D(C + FVector2D(-1.5f, -Gap - 14.f), C + FVector2D(1.5f, -Gap)));
		Bar(FBox2D(C + FVector2D(-1.5f, Gap), C + FVector2D(1.5f, Gap + 14.f)));
		Bar(FBox2D(C + FVector2D(-Gap - 14.f, -1.5f), C + FVector2D(-Gap, 1.5f)));
		Bar(FBox2D(C + FVector2D(Gap, -1.5f), C + FVector2D(Gap + 14.f, 1.5f)));
		break;
	}
	Bar(FKSUI::BoxCentered(C, 3.f, 3.f));

	// Hit marker: a small X, red for headshots. A bigger X for a kill.
	const float HitAge = Now - PC->HitMarkerTime;
	if (HitAge < 0.25f)
	{
		const FLinearColor HitColor = KSAlpha(PC->bHitMarkerHead ? KSColors::Red : KSColors::Text, 1.f - HitAge / 0.25f);
		for (const FVector2D& D : { FVector2D(1.f, 1.f), FVector2D(1.f, -1.f), FVector2D(-1.f, 1.f), FVector2D(-1.f, -1.f) })
		{
			UI.Line(C + D * 9.f, C + D * 19.f, HitColor, 3.f);
		}
	}
	const float KillAge = Now - PC->KillMarkerTime;
	if (KillAge < 0.5f)
	{
		const FLinearColor KillColor = KSAlpha(KSColors::Red, 1.f - KillAge / 0.5f);
		const float Grow = 1.f + KillAge * 0.6f;
		for (const FVector2D& D : { FVector2D(1.f, 1.f), FVector2D(1.f, -1.f), FVector2D(-1.f, 1.f), FVector2D(-1.f, -1.f) })
		{
			UI.Line(C + D * 14.f * Grow, C + D * 32.f * Grow, KillColor, 4.f);
		}
	}

	if (Robot->IsReloading())
	{
		const FBox2D Track = FKSUI::BoxCentered(C + FVector2D(0.f, 92.f), 160.f, 8.f);
		UI.Rect(Track, FLinearColor(0.f, 0.f, 0.f, 0.5f));
		UI.Rect(FBox2D(Track.Min, FVector2D(Track.Min.X + 160.f * Robot->GetReloadProgress(), Track.Max.Y)), KSAlpha(KSColors::Cyan, 0.9f));
		UI.Text(TEXT("ПЕРЕЗАРЯДКА"), C + FVector2D(0.f, 68.f), 24.f, KSAlpha(KSColors::Text, 0.85f), EKSAlign::Center, true, true);
	}
}

void AKSHUD::DrawDamage(AKSPlayerController* PC, AKSCharacter* Robot, float Now)
{
	UKSAssets* Assets = UI.Assets;
	if (!Assets)
	{
		return;
	}
	float Alpha = 0.f;
	const float Age = Now - PC->DamageTime;
	if (Age < 0.5f)
	{
		Alpha = 0.6f * (1.f - Age / 0.5f);
	}
	if (Robot && Robot->IsAlive() && Robot->GetHealth() < 35.f)
	{
		Alpha = FMath::Max(Alpha, (0.28f + 0.14f * FMath::Sin(Now * 5.f)) * (1.f - Robot->GetHealth() / 35.f));
	}
	if (Alpha > 0.01f)
	{
		UI.Image(Assets->VignetteTexture, FKSUI::BoxAt(0.f, 0.f, UI.Width, UI.Height), FLinearColor(1.f, 0.04f, 0.04f, Alpha));
	}

	// Arrows around the crosshair pointing at whoever hit us.
	const FVector Here = Robot ? Robot->GetActorLocation() : (PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector);
	const float ViewYaw = PC->GetControlRotation().Yaw;
	const FVector2D C(UI.Width * 0.5f, UI.Height * 0.5f);
	for (const FKSDamageMark& Mark : PC->DamageMarks)
	{
		const float MarkAge = Now - Mark.Time;
		if (MarkAge > 1.2f)
		{
			continue;
		}
		const FVector To = Mark.From - Here;
		if (To.SizeSquared2D() < 1.f)
		{
			continue;
		}
		const float Angle = FRotator::NormalizeAxis(To.Rotation().Yaw - ViewYaw);
		const float Rad = FMath::DegreesToRadians(Angle);
		const FVector2D Pos = C + FVector2D(FMath::Sin(Rad), -FMath::Cos(Rad)) * 230.f;
		UI.Image(Assets->ArrowTexture, FKSUI::BoxCentered(Pos, 70.f, 70.f), FLinearColor(1.f, 0.15f, 0.1f, 0.9f * (1.f - MarkAge / 1.2f)), Angle);
	}
}

// ---------------------------------------------------------------------------------------------
// Score, feed and status
// ---------------------------------------------------------------------------------------------

void AKSHUD::DrawTopBar(AKSPlayerController* PC, AKSGameState* GS)
{
	if (!GS)
	{
		return;
	}
	const FBox2D Box = PC->GetLayout().ScoreButton;
	const FVector2D C = Box.GetCenter();
	UI.Rect(Box, KSAlpha(KSColors::PanelFill, 0.6f));
	UI.GlowFrame(Box, KSAlpha(KSColors::Cyan, 0.45f), 2.f);

	const float TimeLeft = GS->IsPlaying() ? GS->GetTimeLeft() : 0.f;
	const FLinearColor TimeColor = (GS->IsPlaying() && TimeLeft < 30.f) ? KSColors::Orange : KSColors::Text;
	UI.Text(KS::FormatTime(TimeLeft), FVector2D(C.X, Box.Min.Y + 46.f), 50.f, TimeColor, EKSAlign::Center, true, true);
	UI.Text(FString::Printf(TEXT("до %d"), GS->FragLimit), FVector2D(C.X, Box.Max.Y - 26.f), 24.f, KSColors::Dim, EKSAlign::Center, false, true);

	const AKSPlayerState* Mine = PC->GetPlayerState<AKSPlayerState>();
	if (!Mine)
	{
		return;
	}
	const TArray<AKSPlayerState*> Ranking = GS->GetRanking();
	const int32 Place = Ranking.IndexOfByKey(Mine) + 1;
	const float SideX = 150.f;
	UI.Text(FString::FromInt(Mine->Kills), FVector2D(C.X - SideX, Box.Min.Y + 48.f), 48.f, Mine->GetColor(), EKSAlign::Center, true, true);
	UI.Text(TEXT("фраги"), FVector2D(C.X - SideX, Box.Max.Y - 26.f), 22.f, KSColors::Dim, EKSAlign::Center, false, true);
	UI.Text(FString::Printf(TEXT("#%d"), FMath::Max(Place, 1)), FVector2D(C.X + SideX, Box.Min.Y + 48.f), 48.f, Place == 1 ? KSColors::Yellow : KSColors::Text, EKSAlign::Center, true, true);
	UI.Text(TEXT("место"), FVector2D(C.X + SideX, Box.Max.Y - 26.f), 22.f, KSColors::Dim, EKSAlign::Center, false, true);
}

void AKSHUD::DrawKillFeed(AKSPlayerController* PC, AKSGameState* GS, float Now)
{
	if (!GS)
	{
		return;
	}
	const float Right = UI.Width - PC->GetLayout().SafeX + 30.f;
	float Y = 30.f;
	const TArray<FKSFeedEntry>& Feed = GS->GetFeed();
	for (int32 i = Feed.Num() - 1; i >= 0; --i)
	{
		const FKSFeedEntry& Entry = Feed[i];
		const float Age = Now - Entry.Time;
		if (Age > 6.f)
		{
			continue;
		}
		const float A = KSFade(Age, 5.f, 1.f);
		const float NameSize = 30.f;
		const float MiddleSize = 25.f;
		const float Space = 14.f;
		const float RightW = UI.MeasureText(Entry.Right, NameSize).X;
		const float MiddleW = UI.MeasureText(Entry.Middle, MiddleSize).X;
		const float LeftW = UI.MeasureText(Entry.Left, NameSize).X;
		const float HeadW = Entry.bHeadshot ? 26.f : 0.f;
		float Total = MiddleW + HeadW;
		Total += Entry.Right.IsEmpty() ? 0.f : RightW + Space;
		Total += Entry.Left.IsEmpty() ? 0.f : LeftW + Space;

		UI.Rect(FBox2D(FVector2D(Right - Total - 16.f, Y - 4.f), FVector2D(Right + 10.f, Y + 40.f)), FLinearColor(0.01f, 0.01f, 0.03f, 0.5f * A));
		float X = Right;
		if (!Entry.Right.IsEmpty())
		{
			UI.Text(Entry.Right, FVector2D(X, Y + 18.f), NameSize, KSAlpha(Entry.RightColor, A), EKSAlign::Right, true, true);
			X -= RightW + Space;
		}
		if (Entry.bHeadshot)
		{
			UI.Ring(FVector2D(X - 9.f, Y + 18.f), 9.f, KSAlpha(KSColors::Red, A));
			UI.Rect(FKSUI::BoxCentered(FVector2D(X - 9.f, Y + 18.f), 3.f, 3.f), KSAlpha(KSColors::Red, A));
			X -= HeadW;
		}
		UI.Text(Entry.Middle, FVector2D(X, Y + 18.f), MiddleSize, KSAlpha(Entry.MiddleColor, A), EKSAlign::Right, false, true);
		X -= MiddleW + Space;
		if (!Entry.Left.IsEmpty())
		{
			UI.Text(Entry.Left, FVector2D(X, Y + 18.f), NameSize, KSAlpha(Entry.LeftColor, A), EKSAlign::Right, true, true);
		}
		Y += 48.f;
	}
}

void AKSHUD::DrawStatus(AKSPlayerController* PC, AKSCharacter* Robot, float Now)
{
	const FKSTouchLayout& L = PC->GetLayout();

	// Health bar above the weapon slots.
	const float Health = Robot->GetHealth();
	const float BarW = 440.f;
	const float BarY = L.WeaponSlots[0].Min.Y - 50.f;
	const FBox2D Bar(FVector2D(UI.Width * 0.5f - BarW * 0.5f + 60.f, BarY), FVector2D(UI.Width * 0.5f + BarW * 0.5f, BarY + 24.f));
	const float Fraction = FMath::Clamp(Health / KS::MaxHealth, 0.f, 1.f);
	const FLinearColor HealthColor = Fraction > 0.5f ? KSColors::Green : (Fraction > 0.25f ? KSColors::Yellow : KSColors::Red);
	UI.Rect(Bar.ExpandBy(3.f), FLinearColor(0.f, 0.f, 0.f, 0.55f));
	UI.Rect(FBox2D(Bar.Min, FVector2D(Bar.Min.X + (Bar.Max.X - Bar.Min.X) * Fraction, Bar.Max.Y)), KSAlpha(HealthColor, 0.9f));
	UI.Frame(Bar.ExpandBy(3.f), KSAlpha(HealthColor, 0.6f), 2.f);
	const FVector2D Cross(Bar.Min.X - 86.f, Bar.GetCenter().Y);
	UI.Rect(FKSUI::BoxCentered(Cross, 22.f, 7.f), HealthColor);
	UI.Rect(FKSUI::BoxCentered(Cross, 7.f, 22.f), HealthColor);
	UI.Text(FString::FromInt(FMath::CeilToInt(Health)), FVector2D(Bar.Min.X - 14.f, Bar.GetCenter().Y), 40.f, KSColors::Text, EKSAlign::Right, true, true);

	if (Robot->IsSpawnProtected())
	{
		UI.Text(TEXT("ЗАЩИТА"), FVector2D(UI.Width * 0.5f, BarY - 30.f), 26.f, KSAlpha(KSColors::Cyan, 0.6f + 0.4f * FMath::Sin(Now * 10.f)), EKSAlign::Center, true, true);
	}

	for (int32 i = 0; i < KSWeapon::Count; ++i)
	{
		const FBox2D& Slot = L.WeaponSlots[i];
		const FKSWeaponInfo& Info = KS::Weapon(i);
		const bool bOwned = Robot->HasWeapon(i);
		const int32 Ammo = Robot->GetAmmo(i);
		const bool bUsable = bOwned && (i == KSWeapon::Rifle || Ammo > 0);
		const bool bCurrent = Robot->GetWeapon() == i;
		UI.Rect(Slot, bCurrent ? KSAlpha(Info.Color * 0.3f, 0.85f) : FLinearColor(0.02f, 0.025f, 0.05f, 0.55f));
		if (bCurrent)
		{
			UI.GlowFrame(Slot, Info.Color, 3.f);
		}
		else
		{
			UI.Frame(Slot, KSAlpha(bUsable ? Info.Color : KSColors::Dim, bUsable ? 0.55f : 0.25f), 2.f);
		}
		const FLinearColor NameColor = bUsable ? (bCurrent ? KSColors::Text : Info.Color) : KSAlpha(KSColors::Dim, 0.6f);
		UI.Text(Info.ShortName, FVector2D(Slot.GetCenter().X, Slot.Min.Y + 28.f), 26.f, NameColor, EKSAlign::Center, true, true);
		if (bOwned)
		{
			UI.Text(FString::FromInt(Ammo), FVector2D(Slot.GetCenter().X, Slot.Max.Y - 28.f), 32.f, bUsable ? KSColors::Text : KSColors::Red, EKSAlign::Center, true, true);
		}
		if (i == KSWeapon::Rifle && Robot->IsReloading())
		{
			const float P = Robot->GetReloadProgress();
			UI.Rect(FBox2D(FVector2D(Slot.Min.X + 6.f, Slot.Max.Y - 9.f), FVector2D(Slot.Min.X + 6.f + (Slot.Max.X - Slot.Min.X - 12.f) * P, Slot.Max.Y - 5.f)), KSColors::Cyan);
		}
		if (!PC->IsTouchMode())
		{
			UI.Text(FString::FromInt(i + 1), Slot.Min + FVector2D(8.f, 4.f), 20.f, KSColors::Dim, EKSAlign::Left, false, false, false);
		}
	}
}

void AKSHUD::DrawTouchControls(AKSPlayerController* PC, AKSCharacter* Robot, float Now)
{
	const FKSTouchLayout& L = PC->GetLayout();
	const bool bAlive = Robot && Robot->IsAlive();

	// Movement stick: shown where the thumb landed, or faintly in its usual place.
	const FVector2D Base = PC->bMoveActive ? PC->MoveBase : L.MoveCenter;
	FVector2D Knob = PC->bMoveActive ? PC->MoveKnob : Base;
	if ((Knob - Base).Size() > L.MoveRadius)
	{
		Knob = Base + (Knob - Base).GetSafeNormal() * L.MoveRadius;
	}
	const float StickA = PC->bMoveActive ? 0.6f : 0.25f;
	UI.Circle(Base, L.MoveRadius, FLinearColor(0.02f, 0.03f, 0.06f, StickA * 0.6f));
	UI.Ring(Base, L.MoveRadius, KSAlpha(KSColors::Cyan, StickA));
	UI.Circle(Knob, 54.f, KSAlpha(FLinearColor(0.6f, 0.85f, 1.f, 1.f), StickA * 0.7f));
	UI.Ring(Knob, 54.f, KSAlpha(KSColors::Cyan, FMath::Min(1.f, StickA + 0.2f)));

	if (!bAlive)
	{
		return;
	}

	// Fire button with a crosshair, glowing while it shoots.
	const FLinearColor WeaponColor = KS::Weapon(Robot->GetWeapon()).Color;
	const bool bShooting = PC->bFireHeld || PC->bAutoFiring;
	if (bShooting)
	{
		UI.Glow(L.FireCenter, L.FireRadius * 1.6f, KSAlpha(WeaponColor, 0.3f + 0.1f * FMath::Sin(Now * 20.f)));
	}
	UI.Circle(L.FireCenter, L.FireRadius, FLinearColor(0.03f, 0.02f, 0.05f, PC->bFireHeld ? 0.6f : 0.4f));
	UI.Ring(L.FireCenter, L.FireRadius, KSAlpha(WeaponColor, PC->bFireHeld ? 1.f : 0.65f));
	UI.Ring(L.FireCenter, 34.f, KSAlpha(KSColors::Text, 0.85f));
	const FVector2D F = L.FireCenter;
	UI.Rect(FBox2D(F + FVector2D(-2.f, -52.f), F + FVector2D(2.f, -22.f)), KSAlpha(KSColors::Text, 0.85f));
	UI.Rect(FBox2D(F + FVector2D(-2.f, 22.f), F + FVector2D(2.f, 52.f)), KSAlpha(KSColors::Text, 0.85f));
	UI.Rect(FBox2D(F + FVector2D(-52.f, -2.f), F + FVector2D(-22.f, 2.f)), KSAlpha(KSColors::Text, 0.85f));
	UI.Rect(FBox2D(F + FVector2D(22.f, -2.f), F + FVector2D(52.f, 2.f)), KSAlpha(KSColors::Text, 0.85f));
	if (PC->bAutoFiring && !PC->bFireHeld)
	{
		UI.Text(TEXT("АВТО"), F + FVector2D(0.f, L.FireRadius + 26.f), 22.f, KSAlpha(WeaponColor, 0.9f), EKSAlign::Center, true, true);
	}

	// Jump button.
	UI.Circle(L.JumpCenter, L.JumpRadius, FLinearColor(0.02f, 0.03f, 0.06f, PC->bJumpHeld ? 0.6f : 0.4f));
	UI.Ring(L.JumpCenter, L.JumpRadius, KSAlpha(KSColors::Cyan, PC->bJumpHeld ? 1.f : 0.6f));
	if (UI.Assets)
	{
		UI.Image(UI.Assets->ArrowTexture, FKSUI::BoxCentered(L.JumpCenter, 58.f, 58.f), KSAlpha(KSColors::Text, 0.9f));
	}

	// Reload button, only for the rifle (the other weapons don't use magazines).
	if (Robot->GetWeapon() == KSWeapon::Rifle)
	{
		const FVector2D R = L.ReloadCenter;
		UI.Circle(R, L.ReloadRadius, FLinearColor(0.02f, 0.03f, 0.06f, 0.4f));
		UI.Ring(R, L.ReloadRadius, KSAlpha(KSColors::Cyan, 0.55f));
		if (Robot->IsReloading())
		{
			UI.Text(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Robot->GetReloadProgress() * 100.f)), R, 24.f, KSColors::Cyan, EKSAlign::Center, true, true);
		}
		else
		{
			UI.Ring(R, 22.f, KSAlpha(KSColors::Text, 0.85f));
			if (UI.Assets)
			{
				UI.Image(UI.Assets->ArrowTexture, FKSUI::BoxCentered(R + FVector2D(16.f, -16.f), 22.f, 22.f), KSAlpha(KSColors::Text, 0.9f), 135.f);
			}
			UI.Text(FString::FromInt(Robot->GetAmmo(KSWeapon::Rifle)), R + FVector2D(0.f, L.ReloadRadius + 20.f), 22.f, KSColors::Dim, EKSAlign::Center, true, true);
		}
	}
}

void AKSHUD::DrawMessages(AKSPlayerController* PC, float Now)
{
	const float W = UI.Width;
	const float CenterAge = Now - PC->CenterTime;
	if (CenterAge < 1.8f && !PC->CenterText.IsEmpty())
	{
		const float A = KSFade(CenterAge, 1.3f, 0.5f);
		const float Pop = 1.f + 0.25f * FMath::Max(0.f, 1.f - CenterAge / 0.15f);
		UI.Text(PC->CenterText, FVector2D(W * 0.5f, 290.f), 56.f * Pop, KSAlpha(PC->CenterColor, A), EKSAlign::Center, true, true);
		if (!PC->CenterSubText.IsEmpty())
		{
			UI.Text(PC->CenterSubText, FVector2D(W * 0.5f, 348.f), 32.f, KSAlpha(KSColors::Text, A), EKSAlign::Center, false, true);
		}
	}
	const float PickupAge = Now - PC->PickupTime;
	if (PickupAge < 1.6f && !PC->PickupText.IsEmpty())
	{
		const float A = KSFade(PickupAge, 1.1f, 0.5f);
		UI.Text(PC->PickupText, FVector2D(W * 0.5f, 760.f - PickupAge * 20.f), 36.f, KSAlpha(PC->PickupColor, A), EKSAlign::Center, true, true);
	}
}

void AKSHUD::DrawDeath(AKSPlayerController* PC, AKSCharacter* Robot, AKSGameState* GS)
{
	if (!Robot || Robot->IsAlive())
	{
		return;
	}
	const float W = UI.Width;
	const float A = FMath::Clamp((Robot->GetTimeSinceDeath() - 0.2f) / 0.4f, 0.f, 1.f);
	UI.Rect(FKSUI::BoxAt(0.f, 0.f, W, UI.Height), FLinearColor(0.08f, 0.f, 0.02f, 0.35f * A));
	UI.Text(TEXT("РОБОТ УНИЧТОЖЕН"), FVector2D(W * 0.5f, 320.f), 64.f, KSAlpha(KSColors::Red, A), EKSAlign::Center, true, true);

	FString Line;
	FLinearColor LineColor = KSColors::Text;
	if (!PC->KillerName.IsEmpty())
	{
		UI.Text(TEXT("Противник"), FVector2D(W * 0.5f, 382.f), 24.f, KSAlpha(KSColors::Dim, A), EKSAlign::Center, false, true);
		Line = FString::Printf(TEXT("%s  ·  %s"), *PC->KillerName, KS::Weapon(PC->KillerWeapon).Name);
		LineColor = PC->KillerColor;
	}
	else
	{
		Line = PC->KillerWeapon == KSWeapon::Rocket ? TEXT("Свой взрыв") : TEXT("Падение с арены");
	}
	UI.Text(Line, FVector2D(W * 0.5f, 420.f), 40.f, KSAlpha(LineColor, A), EKSAlign::Center, true, true);

	const AKSPlayerState* PS = PC->GetPlayerState<AKSPlayerState>();
	if (PS && GS && PS->RespawnTime > 0.f)
	{
		const int32 Seconds = FMath::Max(1, FMath::CeilToInt(PS->RespawnTime - GS->GetServerTime()));
		UI.Text(FString::Printf(TEXT("Возрождение через %d"), Seconds), FVector2D(W * 0.5f, 510.f), 36.f, KSAlpha(KSColors::Text, A), EKSAlign::Center, false, true);
	}
}

// ---------------------------------------------------------------------------------------------
// Scoreboard and results
// ---------------------------------------------------------------------------------------------

void AKSHUD::DrawScoreboard(AKSPlayerController* PC, AKSGameState* GS, float Top)
{
	const TArray<AKSPlayerState*> Ranking = GS->GetRanking();
	const float PanelW = 1040.f;
	const float RowH = 64.f;
	const float X0 = UI.Width * 0.5f - PanelW * 0.5f;
	const float PanelH = 84.f + Ranking.Num() * RowH + 14.f;
	UI.Panel(FKSUI::BoxAt(X0, Top, PanelW, PanelH), KSColors::Cyan);

	const float KillsX = X0 + 680.f;
	const float DeathsX = X0 + 820.f;
	const float PingX = X0 + 950.f;
	const float HeaderY = Top + 40.f;
	UI.Text(TEXT("ИГРОК"), FVector2D(X0 + 110.f, HeaderY), 24.f, KSColors::Dim, EKSAlign::Left, true, true);
	UI.Text(TEXT("ФРАГИ"), FVector2D(KillsX, HeaderY), 24.f, KSColors::Dim, EKSAlign::Center, true, true);
	UI.Text(TEXT("ПОТЕРИ"), FVector2D(DeathsX, HeaderY), 24.f, KSColors::Dim, EKSAlign::Center, true, true);
	UI.Text(TEXT("ПИНГ"), FVector2D(PingX, HeaderY), 24.f, KSColors::Dim, EKSAlign::Center, true, true);

	const APlayerState* Mine = PC->PlayerState;
	for (int32 i = 0; i < Ranking.Num(); ++i)
	{
		const AKSPlayerState* PS = Ranking[i];
		const float Y = Top + 84.f + i * RowH;
		const float MidY = Y + RowH * 0.5f - 4.f;
		const FLinearColor Color = PS->GetColor();
		if (PS == Mine)
		{
			UI.Rect(FBox2D(FVector2D(X0 + 12.f, Y), FVector2D(X0 + PanelW - 12.f, Y + RowH - 8.f)), KSAlpha(Color * 0.25f, 0.8f));
		}
		UI.Text(FString::FromInt(i + 1), FVector2D(X0 + 50.f, MidY), 32.f, i == 0 ? KSColors::Yellow : KSColors::Dim, EKSAlign::Center, true, true);
		UI.Rect(FKSUI::BoxCentered(FVector2D(X0 + 90.f, MidY), 16.f, 16.f), Color);
		UI.Text(PS->GetPlayerName(), FVector2D(X0 + 110.f, MidY), 32.f, KSColors::Text, EKSAlign::Left, PS == Mine, true);
		UI.Text(FString::FromInt(PS->Kills), FVector2D(KillsX, MidY), 34.f, Color, EKSAlign::Center, true, true);
		UI.Text(FString::FromInt(PS->Deaths), FVector2D(DeathsX, MidY), 30.f, KSColors::Text, EKSAlign::Center, false, true);
		const FString Ping = PS->IsABot() ? FString(TEXT("бот")) : FString::FromInt(FMath::RoundToInt(PS->GetPingInMilliseconds()));
		UI.Text(Ping, FVector2D(PingX, MidY), 26.f, KSColors::Dim, EKSAlign::Center, false, true);
	}
}

void AKSHUD::DrawMatchEnd(AKSPlayerController* PC, AKSGameState* GS)
{
	const float W = UI.Width;
	UI.Rect(FKSUI::BoxAt(0.f, 0.f, W, UI.Height), FLinearColor(0.01f, 0.01f, 0.03f, 0.6f));
	const APlayerState* Mine = PC->PlayerState;
	const bool bWon = GS->Winner && GS->Winner == Mine;
	UI.Text(bWon ? TEXT("ПОБЕДА!") : TEXT("МАТЧ ОКОНЧЕН"), FVector2D(W * 0.5f, 100.f), 76.f, bWon ? KSColors::Yellow : KSColors::Cyan, EKSAlign::Center, true, true);
	if (GS->Winner && !bWon)
	{
		UI.Text(FString::Printf(TEXT("Лучший результат: %s"), *GS->Winner->GetPlayerName()), FVector2D(W * 0.5f, 168.f), 34.f, KSColors::Text, EKSAlign::Center, false, true);
	}
	DrawScoreboard(PC, GS, 214.f);

	const int32 Seconds = FMath::Max(0, FMath::CeilToInt(GS->GetTimeLeft()));
	const FString NextArena = KSArena::GetName((FMath::Max(GS->ArenaIndex, 0) + 1) % KSArena::Num());
	UI.Text(FString::Printf(TEXT("Следующая арена «%s» через %d с"), *NextArena, Seconds), FVector2D(W * 0.5f, UI.Height - 176.f), 32.f, KSColors::Text, EKSAlign::Center, false, true);
	if (!PC->IsMenuOpen() && UI.Button(FKSUI::BoxCentered(FVector2D(W * 0.5f, UI.Height - 100.f), 380.f, 84.f), TEXT("Выйти в меню"), KSColors::Magenta))
	{
		KSAudio::Play2D(this, KSSound::UIClick);
		PC->LeaveMatch();
	}
}

// ---------------------------------------------------------------------------------------------
// In-match menu
// ---------------------------------------------------------------------------------------------

int32 AKSHUD::Stepper(float Y, float Left, float Right, const FString& Label, const FString& Value)
{
	UI.Text(Label, FVector2D(Left, Y), 32.f, KSColors::Text, EKSAlign::Left, false, true);
	const float ButtonW = 80.f;
	const FBox2D Plus = FKSUI::BoxCentered(FVector2D(Right - ButtonW * 0.5f, Y), ButtonW, 70.f);
	const FBox2D Minus = FKSUI::BoxCentered(FVector2D(Right - ButtonW * 1.5f - 150.f, Y), ButtonW, 70.f);
	UI.Text(Value, FVector2D(Right - ButtonW - 75.f, Y), 32.f, KSColors::Cyan, EKSAlign::Center, true, true);
	int32 Result = 0;
	if (UI.Button(Minus, TEXT("–"), KSColors::Cyan, true, 44.f))
	{
		Result = -1;
	}
	if (UI.Button(Plus, TEXT("+"), KSColors::Cyan, true, 44.f))
	{
		Result = 1;
	}
	if (Result != 0)
	{
		KSAudio::Play2D(this, KSSound::UIClick);
	}
	return Result;
}

bool AKSHUD::Toggle(float Y, float Left, float Right, const FString& Label, const FString& Value, bool bOn)
{
	UI.Text(Label, FVector2D(Left, Y), 32.f, KSColors::Text, EKSAlign::Left, false, true);
	const FBox2D Box = FKSUI::BoxCentered(FVector2D(Right - 155.f, Y), 310.f, 70.f);
	const bool bTapped = UI.Button(Box, Value, bOn ? KSColors::Green : KSColors::Dim, true, 32.f);
	if (bTapped)
	{
		KSAudio::Play2D(this, KSSound::UIClick);
	}
	return bTapped;
}

void AKSHUD::DrawMenu(AKSPlayerController* PC, UKSGameInstance* GI)
{
	const float W = UI.Width;
	UI.Rect(FKSUI::BoxAt(0.f, 0.f, W, UI.Height), FLinearColor(0.f, 0.f, 0.02f, 0.65f));
	const float PanelW = 980.f;
	const FBox2D Panel = FKSUI::BoxAt(W * 0.5f - PanelW * 0.5f, 60.f, PanelW, 960.f);
	UI.Panel(Panel, KSColors::Cyan);
	const float Left = Panel.Min.X + 60.f;
	const float Right = Panel.Max.X - 60.f;
	UI.Text(TEXT("МЕНЮ"), FVector2D(W * 0.5f, 120.f), 52.f, KSColors::Cyan, EKSAlign::Center, true, true);

	if (UI.Button(FBox2D(FVector2D(Left, 170.f), FVector2D(Right, 254.f)), TEXT("Продолжить"), KSColors::Green))
	{
		KSAudio::Play2D(this, KSSound::UIClick);
		PC->SetMenuOpen(false);
		return;
	}
	if (!GI)
	{
		return;
	}

	FKSSettings& S = GI->EditSettings();
	bool bChanged = false;
	float Y = 320.f;
	const float Step = 92.f;
	if (const int32 D = Stepper(Y, Left, Right, TEXT("Чувствительность"), FString::Printf(TEXT("%.1f"), S.Sensitivity)))
	{
		S.Sensitivity = FMath::Clamp(FMath::RoundToFloat((S.Sensitivity + D * 0.1f) * 10.f) / 10.f, 0.3f, 3.f);
		bChanged = true;
	}
	Y += Step;
	if (const int32 D = Stepper(Y, Left, Right, TEXT("Громкость"), FString::Printf(TEXT("%d%%"), FMath::RoundToInt(S.Volume * 100.f))))
	{
		S.Volume = FMath::Clamp(FMath::RoundToFloat((S.Volume + D * 0.1f) * 10.f) / 10.f, 0.f, 1.f);
		bChanged = true;
	}
	Y += Step;
	if (Toggle(Y, Left, Right, TEXT("Помощь в прицеливании"), S.bAimAssist ? TEXT("Включена") : TEXT("Выключена"), S.bAimAssist))
	{
		S.bAimAssist = !S.bAimAssist;
		bChanged = true;
	}
	Y += Step;
	if (Toggle(Y, Left, Right, TEXT("Автоогонь"), S.bAutoFire ? TEXT("Включён") : TEXT("Выключен"), S.bAutoFire))
	{
		S.bAutoFire = !S.bAutoFire;
		bChanged = true;
	}
	Y += Step;
	if (Toggle(Y, Left, Right, TEXT("Графика"), S.Quality > 0 ? TEXT("Высокая") : TEXT("Экономная"), S.Quality > 0))
	{
		S.Quality = S.Quality > 0 ? 0 : 1;
		GI->ApplyGraphicsSettings();
		bChanged = true;
	}
	if (bChanged)
	{
		GI->SaveSettings();
	}

	const bool bHost = GetNetMode() == NM_ListenServer;
	float InfoY = 790.f;
	if (bHost)
	{
		const TArray<FString> Addresses = GI->GetLocalAddressStrings();
		const FString Address = Addresses.Num() > 0 ? FString::Join(Addresses, TEXT(", ")) : FString(TEXT("нет сети"));
		UI.Text(FString::Printf(TEXT("Адрес для друзей: %s"), *Address), FVector2D(W * 0.5f, InfoY), 28.f, KSColors::Text, EKSAlign::Center, false, true);
		UI.Text(TEXT("Ты хост: выход закончит матч для всех."), FVector2D(W * 0.5f, InfoY + 38.f), 24.f, KSColors::Dim, EKSAlign::Center, false, true);
	}
	if (UI.Button(FBox2D(FVector2D(Left, 880.f), FVector2D(Right, 964.f)), TEXT("Выйти в меню"), KSColors::Magenta))
	{
		KSAudio::Play2D(this, KSSound::UIClick);
		PC->LeaveMatch();
	}
}
