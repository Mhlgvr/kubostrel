#include "KSPlayerController.h"
#include "KSAudio.h"
#include "KSCharacter.h"
#include "KSGameInstance.h"
#include "KSGameMode.h"
#include "KSGameState.h"
#include "KSPlayerState.h"
#include "KSTypes.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"

namespace
{
	// Degrees of turn per UI unit of finger movement (the screen is 1080 units tall).
	constexpr float KSTouchLookScale = 0.13f;
	// Degrees per unit of the engine's mouse axis (same feel as the engine's default pawn).
	constexpr float KSMouseLookScale = 2.5f;
	constexpr float KSMoveDeadZone = 0.12f;
	// Past this much stick travel (after the dead zone) the robot runs at full speed.
	constexpr float KSMoveFullTravel = 0.5f;
	constexpr float KSMaxPitch = 84.f;
}

AKSPlayerController::AKSPlayerController()
{
}

void AKSPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalController())
	{
		SetMenuOpen(false);
	}
}

float AKSPlayerController::GetLocalTime() const
{
	return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}

AKSCharacter* AKSPlayerController::GetKSCharacter() const
{
	return Cast<AKSCharacter>(GetPawn());
}

void AKSPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	const UKSGameInstance* GI = Cast<UKSGameInstance>(GetGameInstance());
	// The player state arriving means the connection to the host is fully set up.
	if (!bNameSent && PlayerState && GI)
	{
		bNameSent = true;
		ServerSetPlayerName(GI->GetSettings().PlayerName);
	}

	const float Now = GetLocalTime();
	Layout = FKSTouchLayout::Make(GetScreenSize().X / FMath::Max(GetUIScale(), 0.01f));

	AKSCharacter* Robot = GetKSCharacter();
	if (Robot != LastPawn.Get())
	{
		LastPawn = Robot;
		PointerRoles.Reset();
		RecoilToApply = 0.f;
		RecoilToRecover = 0.f;
		RecoilYaw = 0.f;
		AimTarget = nullptr;
	}

	const AKSGameState* GS = GetWorld()->GetGameState<AKSGameState>();
	const bool bPlaying = !GS || GS->IsPlaying();
	const bool bCanAct = Robot && Robot->IsAlive() && !Robot->IsFrozen() && bPlaying && !bMenuOpen;

	FVector2D Move = FVector2D::ZeroVector;
	FVector2D Look = FVector2D::ZeroVector;
	bool bFire = false;
	if (IsTouchMode())
	{
		HandleTouch(DeltaTime, Robot, bCanAct, Move, Look, bFire);
	}
	else
	{
		HandleDesktop(DeltaTime, Robot, bCanAct, Move, Look, bFire);
	}

	LookFriction = 1.f;
	bAimOnTarget = false;
	if (bCanAct)
	{
		const FKSSettings* Settings = GI ? &GI->GetSettings() : nullptr;
		const bool bAssist = Settings && Settings->bAimAssist && IsTouchMode();
		if (bAssist)
		{
			UpdateAimAssist(DeltaTime, Robot, !Look.IsNearlyZero() || !Move.IsNearlyZero() || bFire);
		}
		ApplyLook(Look.X * LookFriction, Look.Y * LookFriction);

		const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
		const FRotationMatrix YawMatrix(Yaw);
		Robot->AddMovementInput(YawMatrix.GetUnitAxis(EAxis::X), Move.Y);
		Robot->AddMovementInput(YawMatrix.GetUnitAxis(EAxis::Y), Move.X);

		const bool bAutoSetting = Settings && Settings->bAutoFire && IsTouchMode();
		if (bAutoSetting && IsCrosshairOnEnemy(Robot))
		{
			// A short hold-over stops the gun from stuttering at the edge of a target.
			AutoFireUntil = Now + 0.15f;
		}
		bAutoFiring = bAutoSetting && Now < AutoFireUntil;
		Robot->SetWantsToFire(bFire || bAutoFiring);

		UpdateRecoil(DeltaTime, Robot);
		// Turn the body right away instead of on the next frame.
		Robot->FaceRotation(GetControlRotation(), DeltaTime);
	}
	else
	{
		bAutoFiring = false;
		AutoFireUntil = 0.f;
		RecoilToApply = 0.f;
		RecoilYaw = 0.f;
		if (Robot)
		{
			Robot->SetWantsToFire(false);
		}
	}

	DamageMarks.RemoveAll([Now](const FKSDamageMark& Mark) { return Now - Mark.Time > 1.5f; });
}

// ---------------------------------------------------------------------------------------------
// Touch controls
// ---------------------------------------------------------------------------------------------

AKSPlayerController::EKSTouchRole AKSPlayerController::ClaimPointer(const FKSPointer& Pointer, AKSCharacter* Robot, bool bCanAct)
{
	if (bMenuOpen)
	{
		return EKSTouchRole::Ignored;
	}
	// Fingers that were already down (for example while respawning) never trigger buttons.
	const bool bFresh = Pointer.bPressed;
	const FVector2D Pos = Pointer.StartPos;
	if (Layout.PauseButton.ExpandBy(12.f).IsInside(Pos))
	{
		// The HUD handles the tap.
		return EKSTouchRole::Ignored;
	}
	if (FKSTouchLayout::InCircle(Pos, Layout.FireCenter, Layout.FireRadius * 1.15f))
	{
		return EKSTouchRole::Fire;
	}
	if (FKSTouchLayout::InCircle(Pos, Layout.JumpCenter, Layout.JumpRadius * 1.15f))
	{
		if (bFresh && bCanAct && Robot)
		{
			Robot->DoJump();
		}
		return EKSTouchRole::Jump;
	}
	if (FKSTouchLayout::InCircle(Pos, Layout.ReloadCenter, Layout.ReloadRadius * 1.15f))
	{
		if (bFresh && bCanAct && Robot)
		{
			Robot->StartReload();
		}
		return EKSTouchRole::Ignored;
	}
	for (int32 i = 0; i < KSWeapon::Count; ++i)
	{
		if (Layout.WeaponSlots[i].ExpandBy(6.f).IsInside(Pos))
		{
			if (bFresh && Robot && Robot->IsAlive())
			{
				Robot->SelectWeapon(i);
			}
			return EKSTouchRole::Ignored;
		}
	}
	if (Layout.ScoreButton.IsInside(Pos))
	{
		return EKSTouchRole::Score;
	}
	if (Pos.X < Layout.MoveZoneRight)
	{
		for (const TPair<int32, EKSTouchRole>& Pair : PointerRoles)
		{
			if (Pair.Value == EKSTouchRole::Move && Pair.Key != Pointer.Id)
			{
				return EKSTouchRole::Look;
			}
		}
		// The stick appears under the finger, kept fully on screen.
		const float R = Layout.MoveRadius;
		MoveBase.X = FMath::Clamp(Pos.X, Layout.SafeX + R * 0.6f, Layout.MoveZoneRight - R * 0.5f);
		MoveBase.Y = FMath::Clamp(Pos.Y, 380.f, Layout.Height - R * 0.75f);
		return EKSTouchRole::Move;
	}
	return EKSTouchRole::Look;
}

void AKSPlayerController::HandleTouch(float DeltaTime, AKSCharacter* Robot, bool bCanAct, FVector2D& OutMove, FVector2D& OutLook, bool& bOutFire)
{
	const float Scale = FMath::Max(GetUIScale(), 0.01f);
	const UKSGameInstance* GI = Cast<UKSGameInstance>(GetGameInstance());
	const float LookScale = KSTouchLookScale * (GI ? GI->GetSettings().Sensitivity : 1.f);

	bFireHeld = false;
	bJumpHeld = false;
	bScoreHeld = false;
	bMoveActive = false;

	TSet<int32> Seen;
	for (const FKSPointer& PixelPointer : GetPointers())
	{
		FKSPointer P = PixelPointer;
		P.Pos /= Scale;
		P.StartPos /= Scale;
		P.Delta /= Scale;
		Seen.Add(P.Id);

		if (P.bReleased)
		{
			PointerRoles.Remove(P.Id);
			continue;
		}
		EKSTouchRole* Existing = PointerRoles.Find(P.Id);
		EKSTouchRole Role = Existing ? *Existing : EKSTouchRole::None;
		if (P.bPressed || !Existing)
		{
			Role = ClaimPointer(P, Robot, bCanAct);
			PointerRoles.Add(P.Id, Role);
		}

		switch (Role)
		{
		case EKSTouchRole::Move:
		{
			bMoveActive = true;
			const float R = Layout.MoveRadius;
			FVector2D Offset = P.Pos - MoveBase;
			// Dragging past the edge pulls the stick along with the finger.
			if (Offset.Size() > R)
			{
				MoveBase = P.Pos - Offset.GetSafeNormal() * R;
				Offset = P.Pos - MoveBase;
			}
			MoveKnob = P.Pos;
			const FVector2D Stick = Offset / R;
			const float Length = Stick.Size();
			if (Length > KSMoveDeadZone)
			{
				const float Amount = FMath::Clamp((Length - KSMoveDeadZone) / KSMoveFullTravel, 0.f, 1.f);
				const FVector2D Dir = Stick / Length;
				OutMove = FVector2D(Dir.X, -Dir.Y) * Amount;
			}
			break;
		}
		case EKSTouchRole::Fire:
			bFireHeld = true;
			bOutFire = true;
			OutLook += FVector2D(P.Delta.X, -P.Delta.Y) * LookScale;
			break;
		case EKSTouchRole::Look:
			OutLook += FVector2D(P.Delta.X, -P.Delta.Y) * LookScale;
			break;
		case EKSTouchRole::Jump:
			bJumpHeld = true;
			break;
		case EKSTouchRole::Score:
			bScoreHeld = true;
			break;
		default:
			break;
		}
	}

	for (auto It = PointerRoles.CreateIterator(); It; ++It)
	{
		if (!Seen.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
	if (!bCanAct)
	{
		OutMove = FVector2D::ZeroVector;
		OutLook = FVector2D::ZeroVector;
		bOutFire = false;
	}
}

void AKSPlayerController::HandleDesktop(float DeltaTime, AKSCharacter* Robot, bool bCanAct, FVector2D& OutMove, FVector2D& OutLook, bool& bOutFire)
{
	bFireHeld = false;
	bJumpHeld = IsInputKeyDown(EKeys::SpaceBar);
	bScoreHeld = IsInputKeyDown(EKeys::Tab);
	bMoveActive = false;

	if (WasInputKeyJustPressed(EKeys::Escape))
	{
		SetMenuOpen(!bMenuOpen);
	}
	if (!bCanAct)
	{
		return;
	}

	const UKSGameInstance* GI = Cast<UKSGameInstance>(GetGameInstance());
	const float Sensitivity = GI ? GI->GetSettings().Sensitivity : 1.f;
	float MouseX = 0.f;
	float MouseY = 0.f;
	GetInputMouseDelta(MouseX, MouseY);
	OutLook = FVector2D(MouseX, MouseY) * KSMouseLookScale * Sensitivity;

	FVector2D Move = FVector2D::ZeroVector;
	if (IsInputKeyDown(EKeys::W) || IsInputKeyDown(EKeys::Up))
	{
		Move.Y += 1.f;
	}
	if (IsInputKeyDown(EKeys::S) || IsInputKeyDown(EKeys::Down))
	{
		Move.Y -= 1.f;
	}
	if (IsInputKeyDown(EKeys::D) || IsInputKeyDown(EKeys::Right))
	{
		Move.X += 1.f;
	}
	if (IsInputKeyDown(EKeys::A) || IsInputKeyDown(EKeys::Left))
	{
		Move.X -= 1.f;
	}
	OutMove = Move.GetSafeNormal();

	bOutFire = IsInputKeyDown(EKeys::LeftMouseButton);
	bFireHeld = bOutFire;

	if (WasInputKeyJustPressed(EKeys::SpaceBar))
	{
		Robot->DoJump();
	}
	if (WasInputKeyJustPressed(EKeys::R))
	{
		Robot->StartReload();
	}
	const FKey Slots[KSWeapon::Count] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four };
	for (int32 i = 0; i < KSWeapon::Count; ++i)
	{
		if (WasInputKeyJustPressed(Slots[i]))
		{
			Robot->SelectWeapon(i);
		}
	}
	if (WasInputKeyJustPressed(EKeys::MouseScrollUp))
	{
		Robot->CycleWeapon(-1);
	}
	if (WasInputKeyJustPressed(EKeys::MouseScrollDown))
	{
		Robot->CycleWeapon(1);
	}
}

// ---------------------------------------------------------------------------------------------
// Aiming
// ---------------------------------------------------------------------------------------------

void AKSPlayerController::ApplyLook(float DeltaYaw, float DeltaPitch)
{
	if (DeltaYaw == 0.f && DeltaPitch == 0.f)
	{
		return;
	}
	FRotator Rotation = GetControlRotation();
	Rotation.Yaw = FRotator::NormalizeAxis(Rotation.Yaw + DeltaYaw);
	Rotation.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Rotation.Pitch) + DeltaPitch, -KSMaxPitch, KSMaxPitch);
	Rotation.Roll = 0.f;
	SetControlRotation(Rotation);
}

void AKSPlayerController::UpdateAimAssist(float DeltaTime, AKSCharacter* Robot, bool bActive)
{
	const float Now = GetLocalTime();
	const FVector Eye = Robot->GetEyeLocation();
	const FVector Forward = GetControlRotation().Vector();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(KSAimAssist), false, Robot);
	const FCollisionObjectQueryParams WorldOnly(ECC_WorldStatic);

	if (Now >= NextAimScan)
	{
		NextAimScan = Now + 0.1f;
		AimTarget = nullptr;
		float BestAngle = 8.f;
		for (TActorIterator<AKSCharacter> It(GetWorld()); It; ++It)
		{
			AKSCharacter* Other = *It;
			if (Other == Robot || !Other->IsAlive())
			{
				continue;
			}
			const FVector Point = Other->GetActorLocation() + FVector(0.f, 0.f, 25.f);
			const FVector ToPoint = Point - Eye;
			const float Distance = ToPoint.Size();
			if (Distance < 1.f || Distance > 6000.f)
			{
				continue;
			}
			const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Forward, ToPoint / Distance), -1.f, 1.f)));
			if (Angle >= BestAngle)
			{
				continue;
			}
			if (GetWorld()->LineTraceTestByObjectType(Eye, Point, WorldOnly, Params))
			{
				continue;
			}
			BestAngle = Angle;
			AimTarget = Other;
		}
	}

	AKSCharacter* Target = AimTarget.Get();
	if (!Target || !Target->IsAlive())
	{
		return;
	}
	const FVector ToTarget = Target->GetActorLocation() + FVector(0.f, 0.f, 25.f) - Eye;
	const float Distance = FMath::Max(ToTarget.Size(), 100.f);
	const FRotator Want = ToTarget.Rotation();
	const FRotator Have = GetControlRotation();
	const float YawError = FRotator::NormalizeAxis(Want.Yaw - Have.Yaw);
	const float PitchError = FRotator::NormalizeAxis(Want.Pitch) - FRotator::NormalizeAxis(Have.Pitch);
	const float Angle = FMath::Sqrt(YawError * YawError + PitchError * PitchError);

	// A robot is about 60 cm wide, so the help zone shrinks with distance.
	const float TargetSize = FMath::RadiansToDegrees(FMath::Atan2(60.f, Distance));
	const float FrictionZone = FMath::Clamp(TargetSize * 1.5f, 1.2f, 4.f);
	const float MagnetZone = FMath::Clamp(TargetSize * 2.5f, 2.f, 6.f);
	if (Angle < FrictionZone)
	{
		bAimOnTarget = true;
		LookFriction = 0.55f;
	}
	if (bActive && Angle < MagnetZone)
	{
		const float Step = 8.f * DeltaTime;
		ApplyLook(FMath::Clamp(YawError, -Step, Step), FMath::Clamp(PitchError, -Step, Step) * 0.6f);
	}
}

bool AKSPlayerController::IsCrosshairOnEnemy(AKSCharacter* Robot) const
{
	const int32 Weapon = Robot->GetWeapon();
	if (Weapon != KSWeapon::Rifle && Robot->GetAmmo(Weapon) <= 0)
	{
		return false;
	}
	const float Range = Weapon == KSWeapon::Rocket ? 5000.f : FMath::Min(KS::Weapon(Weapon).Range, 9000.f);
	const FVector Start = Robot->GetEyeLocation();
	const FVector End = Start + GetControlRotation().Vector() * Range;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(KSAutoFire), false, Robot);
	FHitResult Hit;
	if (!GetWorld()->SweepSingleByObjectType(Hit, Start, End, FQuat::Identity, Objects, FCollisionShape::MakeSphere(12.f), Params))
	{
		return false;
	}
	const AKSCharacter* Other = Cast<AKSCharacter>(Hit.GetActor());
	if (!Other || !Other->IsAlive() || Other->IsSpawnProtected())
	{
		return false;
	}
	if (Weapon == KSWeapon::Rocket && Hit.Distance < 450.f)
	{
		return false;
	}
	if (Weapon == KSWeapon::Shotgun && Hit.Distance > 1800.f)
	{
		return false;
	}
	return true;
}

void AKSPlayerController::AddRecoil(float Pitch)
{
	RecoilToApply += Pitch * 0.6f;
	RecoilYaw += FMath::FRandRange(-0.25f, 0.25f) * Pitch;
}

void AKSPlayerController::UpdateRecoil(float DeltaTime, AKSCharacter* Robot)
{
	// The kick lands within about 60 ms, then half of it drifts back once the shooting stops.
	if (RecoilToApply > 0.001f || FMath::Abs(RecoilYaw) > 0.001f)
	{
		const float Alpha = FMath::Min(1.f, DeltaTime / 0.06f);
		const float Up = RecoilToApply * Alpha;
		const float Side = RecoilYaw * Alpha;
		RecoilToApply -= Up;
		RecoilYaw -= Side;
		RecoilToRecover += Up * 0.5f;
		ApplyLook(Side, Up);
	}
	if (RecoilToRecover > 0.001f && Robot->GetTimeSinceFire() > 0.12f)
	{
		const float Back = RecoilToRecover * FMath::Min(1.f, DeltaTime * 10.f);
		RecoilToRecover -= Back;
		ApplyLook(0.f, -Back);
	}
}

// ---------------------------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------------------------

void AKSPlayerController::ShowCenter(const FString& Text, const FString& SubText, const FLinearColor& Color)
{
	CenterText = Text;
	CenterSubText = SubText;
	CenterColor = Color;
	CenterTime = GetLocalTime();
}

void AKSPlayerController::NotifyLocalHit(bool bHeadshot)
{
	HitMarkerTime = GetLocalTime();
	bHitMarkerHead = bHeadshot;
	KSAudio::Play2D(this, bHeadshot ? KSSound::Headshot : KSSound::HitMarker, 0.8f);
}

void AKSPlayerController::ClientHitConfirmed_Implementation(bool bHeadshot)
{
	NotifyLocalHit(bHeadshot);
}

void AKSPlayerController::ClientDamaged_Implementation(FVector_NetQuantize FromLocation, float Amount)
{
	FKSDamageMark& Mark = DamageMarks.AddDefaulted_GetRef();
	Mark.From = FromLocation;
	Mark.Amount = Amount;
	Mark.Time = GetLocalTime();
	if (DamageMarks.Num() > 4)
	{
		DamageMarks.RemoveAt(0);
	}
	DamageTime = GetLocalTime();
	KSAudio::Play2D(this, KSSound::Hurt, FMath::Clamp(Amount / 40.f, 0.4f, 1.f));
}

void AKSPlayerController::ClientPickupNotice_Implementation(uint8 Type)
{
	PickupText = Type == KSPickupType::Health ? FString(TEXT("+50 здоровья")) : FString::Printf(TEXT("+ %s"), KS::PickupName(Type));
	PickupColor = KS::PickupColor(Type);
	PickupTime = GetLocalTime();
}

void AKSPlayerController::OnKillFeed(APlayerState* Killer, APlayerState* Victim, uint8 Weapon, bool bHeadshot, int32 Streak)
{
	const APlayerState* Mine = PlayerState;
	if (!Mine)
	{
		return;
	}
	const AKSPlayerState* KillerPS = Cast<AKSPlayerState>(Killer);
	const AKSPlayerState* VictimPS = Cast<AKSPlayerState>(Victim);
	if (Killer == Mine && Victim != Mine)
	{
		KillMarkerTime = GetLocalTime();
		KSAudio::Play2D(this, KSSound::KillConfirm);
		FString Sub = VictimPS ? VictimPS->GetPlayerName() : FString();
		if (Streak >= 3)
		{
			Sub += FString::Printf(TEXT("  ·  серия %d"), Streak);
		}
		ShowCenter(bHeadshot ? TEXT("В ГОЛОВУ!") : TEXT("УНИЧТОЖЕН"), Sub, VictimPS ? VictimPS->GetColor() : FLinearColor::White);
	}
	else if (Victim == Mine)
	{
		const bool bByOther = KillerPS && KillerPS != VictimPS;
		KillerName = bByOther ? KillerPS->GetPlayerName() : FString();
		KillerColor = bByOther ? KillerPS->GetColor() : FLinearColor::White;
		KillerWeapon = Weapon;
	}
}

// ---------------------------------------------------------------------------------------------
// Menu and server calls
// ---------------------------------------------------------------------------------------------

void AKSPlayerController::SetMenuOpen(bool bOpen)
{
	bMenuOpen = bOpen;
	PointerRoles.Reset();
	if (AKSCharacter* Robot = GetKSCharacter())
	{
		Robot->SetWantsToFire(false);
	}
#if !(PLATFORM_IOS || PLATFORM_ANDROID)
	bShowMouseCursor = bOpen || IsTouchMode();
	if (bShowMouseCursor)
	{
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		SetInputMode(Mode);
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
	}
#endif
}

void AKSPlayerController::LeaveMatch()
{
	if (UKSGameInstance* GI = Cast<UKSGameInstance>(GetGameInstance()))
	{
		GI->ReturnToMenu();
	}
}

void AKSPlayerController::ServerSetPlayerName_Implementation(const FString& Name)
{
	if (AKSGameMode* GM = GetWorld()->GetAuthGameMode<AKSGameMode>())
	{
		GM->SetPlayerName(this, Name.Left(64));
	}
}
