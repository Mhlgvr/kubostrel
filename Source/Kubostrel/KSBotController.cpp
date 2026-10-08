#include "KSBotController.h"
#include "KSArenaData.h"
#include "KSCharacter.h"
#include "KSGameMode.h"
#include "KSPickup.h"
#include "KSTypes.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"

AKSBotController::AKSBotController()
{
	bWantsPlayerState = true;
	bSetControlRotationFromPawnOrientation = false;
	PrimaryActorTick.bCanEverTick = true;
}

AKSCharacter* AKSBotController::GetBot() const
{
	return Cast<AKSCharacter>(GetPawn());
}

float AKSBotController::GetTime() const
{
	return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}

const AKSBotController::FKSBotSkill& AKSBotController::GetSkill() const
{
	// Reaction time (s), aim wobble (degrees), turn speed (degrees per second), how often it dodges.
	static const FKSBotSkill Skills[3] =
	{
		{ 0.70f, 4.5f, 200.f, 0.45f },
		{ 0.45f, 2.6f, 320.f, 0.75f },
		{ 0.25f, 1.3f, 540.f, 1.00f },
	};
	const AKSGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AKSGameMode>() : nullptr;
	return Skills[FMath::Clamp(GM ? GM->GetBotSkill() : 1, 0, 2)];
}

void AKSBotController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	Enemy = nullptr;
	Path.Reset();
	PathIndex = 0;
	bHasGoal = false;
	StuckTimer = 0.f;
	StuckCount = 0;
	FirstSeenTime = -100.f;
	LastSeenTime = -100.f;
	if (InPawn)
	{
		DesiredAim = FRotator(0.f, InPawn->GetActorRotation().Yaw, 0.f);
		SetControlRotation(DesiredAim);
		StuckCheckLocation = InPawn->GetActorLocation();
	}
}

void AKSBotController::Tick(float DeltaSeconds)
{
	AKSCharacter* Bot = GetBot();
	if (Bot && Bot->IsAlive() && !Bot->IsFrozen())
	{
		UpdateTarget(Bot);
		AKSCharacter* Target = Enemy.Get();
		if (Target && Target->IsAlive() && GetTime() - LastSeenTime < 0.35f)
		{
			Fight(Bot, Target, DeltaSeconds);
		}
		else
		{
			Bot->SetWantsToFire(false);
			Roam(Bot, DeltaSeconds);
		}
		CheckStuck(Bot, DeltaSeconds);
	}
	else if (Bot)
	{
		Bot->SetWantsToFire(false);
	}
	// Turns toward DesiredAim through UpdateControlRotation.
	Super::Tick(DeltaSeconds);
}

void AKSBotController::UpdateControlRotation(float DeltaTime, bool bUpdatePawn)
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn)
	{
		return;
	}
	const FKSBotSkill& Skill = GetSkill();
	FRotator Current = GetControlRotation();
	Current.Pitch = FRotator::NormalizeAxis(Current.Pitch);
	const FRotator Delta = (DesiredAim - Current).GetNormalized();
	// Eases in near the target and is capped by the bot's turn speed, like a thumb on a screen.
	const float Blend = FMath::Min(1.f, DeltaTime * 7.f);
	const float MaxStep = Skill.TurnRate * DeltaTime;
	Current.Yaw = FRotator::NormalizeAxis(Current.Yaw + FMath::Clamp(Delta.Yaw * Blend, -MaxStep, MaxStep));
	Current.Pitch = FMath::Clamp(Current.Pitch + FMath::Clamp(Delta.Pitch * Blend, -MaxStep, MaxStep), -80.f, 80.f);
	Current.Roll = 0.f;
	SetControlRotation(Current);
	if (bUpdatePawn)
	{
		MyPawn->FaceRotation(Current, DeltaTime);
	}
}

// ---------------------------------------------------------------------------------------------
// Seeing
// ---------------------------------------------------------------------------------------------

bool AKSBotController::CanSee(AKSCharacter* Bot, AKSCharacter* Other) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(KSBotSight), false, Bot);
	const FCollisionObjectQueryParams WorldOnly(ECC_WorldStatic);
	const FVector Eye = Bot->GetEyeLocation();
	if (!GetWorld()->LineTraceTestByObjectType(Eye, Other->GetEyeLocation(), WorldOnly, Params))
	{
		return true;
	}
	return !GetWorld()->LineTraceTestByObjectType(Eye, Other->GetActorLocation(), WorldOnly, Params);
}

void AKSBotController::UpdateTarget(AKSCharacter* Bot)
{
	const float Now = GetTime();
	if (Now < NextTargetCheck)
	{
		return;
	}
	NextTargetCheck = Now + 0.2f;

	const FVector BotLocation = Bot->GetActorLocation();
	const FVector Facing = GetControlRotation().Vector();
	AKSCharacter* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	for (TActorIterator<AKSCharacter> It(GetWorld()); It; ++It)
	{
		AKSCharacter* Other = *It;
		if (Other == Bot || !Other->IsAlive() || Other->IsSpawnProtected())
		{
			continue;
		}
		const FVector ToOther = Other->GetActorLocation() - BotLocation;
		const float Distance = ToOther.Size();
		if (Distance > 6000.f)
		{
			continue;
		}
		// Bots notice what is in front of them, or very close.
		if (Distance > 900.f && FVector::DotProduct(ToOther / FMath::Max(Distance, 1.f), Facing) < -0.2f)
		{
			continue;
		}
		if (!CanSee(Bot, Other))
		{
			continue;
		}
		const float Score = Distance * (Other == Enemy.Get() ? 0.6f : 1.f);
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Other;
		}
	}

	if (Best)
	{
		if (Best != Enemy.Get() || Now - LastSeenTime > 1.5f)
		{
			// A new target: the bot needs a moment to react.
			FirstSeenTime = Now;
		}
		Enemy = Best;
		LastSeenTime = Now;
		LastSeenLocation = Best->GetActorLocation();
	}
	else if (Enemy.IsValid() && !Enemy->IsAlive())
	{
		Enemy = nullptr;
	}
}

// ---------------------------------------------------------------------------------------------
// Fighting
// ---------------------------------------------------------------------------------------------

void AKSBotController::ChooseWeapon(AKSCharacter* Bot, float Distance)
{
	const float Now = GetTime();
	if (Now < NextWeaponCheck)
	{
		return;
	}
	NextWeaponCheck = Now + 0.8f;
	auto Ready = [Bot](int32 Weapon)
	{
		return Bot->HasWeapon(Weapon) && (Weapon == KSWeapon::Rifle || Bot->GetAmmo(Weapon) > 0);
	};
	int32 Want = KSWeapon::Rifle;
	if (Distance < 800.f && Ready(KSWeapon::Shotgun))
	{
		Want = KSWeapon::Shotgun;
	}
	else if (Distance > 700.f && Ready(KSWeapon::Rail))
	{
		Want = KSWeapon::Rail;
	}
	else if (Distance > 500.f && Distance < 2600.f && Ready(KSWeapon::Rocket))
	{
		Want = KSWeapon::Rocket;
	}
	if (Want != Bot->GetWeapon())
	{
		Bot->SelectWeapon(Want);
	}
}

void AKSBotController::Fight(AKSCharacter* Bot, AKSCharacter* Target, float DeltaSeconds)
{
	const float Now = GetTime();
	const FKSBotSkill& Skill = GetSkill();
	const FVector BotLocation = Bot->GetActorLocation();
	const FVector TargetLocation = Target->GetActorLocation();
	const float Distance = FVector::Dist(BotLocation, TargetLocation);
	ChooseWeapon(Bot, Distance);
	const int32 Weapon = Bot->GetWeapon();

	FVector AimPoint = TargetLocation + FVector(0.f, 0.f, 20.f);
	if (Weapon == KSWeapon::Rocket)
	{
		// Lead the target and aim low so the blast catches it.
		const float Flight = Distance / KS::RocketSpeed;
		AimPoint = TargetLocation + Target->GetVelocity() * Flight * 0.8f - FVector(0.f, 0.f, 70.f);
	}

	if (Now >= NextWobble)
	{
		NextWobble = Now + FMath::FRandRange(0.25f, 0.6f);
		AimWobbleTarget = FRotator(FMath::FRandRange(-0.6f, 0.6f) * Skill.AimError, FMath::FRandRange(-1.f, 1.f) * Skill.AimError, 0.f);
	}
	AimWobble = FMath::RInterpTo(AimWobble, AimWobbleTarget, DeltaSeconds, 4.f);
	FRotator Aim = (AimPoint - Bot->GetEyeLocation()).Rotation() + AimWobble;
	Aim.Pitch = FRotator::NormalizeAxis(Aim.Pitch);
	// Path following below points DesiredAim along the path; the gun stays on the target.
	ON_SCOPE_EXIT
	{
		DesiredAim = Aim;
	};

	const FRotator Current = GetControlRotation();
	const float YawError = FMath::Abs(FRotator::NormalizeAxis(Aim.Yaw - Current.Yaw));
	const float PitchError = FMath::Abs(FRotator::NormalizeAxis(Aim.Pitch - FRotator::NormalizeAxis(Current.Pitch)));
	const float Tolerance = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(70.f, Distance)) + Skill.AimError, 2.f, 15.f);
	bool bFire = Now - FirstSeenTime >= Skill.Reaction && YawError < Tolerance && PitchError < Tolerance;
	if (Weapon == KSWeapon::Rocket && Distance < 450.f)
	{
		bFire = false;
	}
	Bot->SetWantsToFire(bFire);

	if (Now >= NextStrafeChange)
	{
		NextStrafeChange = Now + FMath::FRandRange(0.5f, 1.6f);
		if (FMath::FRand() < Skill.StrafeChance)
		{
			StrafeDir = -StrafeDir;
		}
	}
	const float Ideal = Weapon == KSWeapon::Shotgun ? 450.f : Weapon == KSWeapon::Rail ? 2200.f : Weapon == KSWeapon::Rocket ? 1100.f : 1300.f;
	const float Height = TargetLocation.Z - BotLocation.Z;
	if (Distance > Ideal * 1.25f && FMath::Abs(Height) > 150.f)
	{
		// The target is on another level: take the proper way up or down while shooting.
		if (PathTo(Bot, TargetLocation - FVector(0.f, 0.f, KS::CapsuleHalfHeight)))
		{
			FollowPath(Bot);
		}
		return;
	}
	bHasGoal = false;

	FVector ToTarget = TargetLocation - BotLocation;
	ToTarget.Z = 0.f;
	ToTarget = ToTarget.GetSafeNormal();
	const FVector Side = FVector::CrossProduct(FVector::UpVector, ToTarget);
	float Forward = 0.f;
	if (Distance > Ideal * 1.25f)
	{
		Forward = 1.f;
	}
	else if (Distance < Ideal * 0.7f)
	{
		Forward = -0.7f;
	}
	FVector MoveDir = (ToTarget * Forward + Side * (StrafeDir * 0.8f)).GetSafeNormal();
	if (!MoveDir.IsNearlyZero() && !IsSafeToMove(Bot, MoveDir))
	{
		StrafeDir = -StrafeDir;
		MoveDir = (ToTarget * Forward + Side * (StrafeDir * 0.8f)).GetSafeNormal();
		if (!IsSafeToMove(Bot, MoveDir))
		{
			MoveDir = FVector::ZeroVector;
		}
	}
	if (!MoveDir.IsNearlyZero() && Bot->GetCharacterMovement()->IsMovingOnGround())
	{
		Bot->AddMovementInput(MoveDir, 1.f);
	}
	if (Now >= NextJumpCheck)
	{
		NextJumpCheck = Now + 1.f;
		if (FMath::FRand() < 0.15f * Skill.StrafeChance)
		{
			Bot->DoJump();
		}
	}
}

bool AKSBotController::IsSafeToMove(AKSCharacter* Bot, const FVector& Direction) const
{
	// Is there floor ahead? Bots should not strafe off a platform by accident.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(KSBotLedge), false, Bot);
	const FCollisionObjectQueryParams WorldOnly(ECC_WorldStatic);
	const FVector Start = Bot->GetActorLocation() + Direction * 120.f;
	const FVector End = Start - FVector(0.f, 0.f, KS::CapsuleHalfHeight + 260.f);
	return GetWorld()->LineTraceTestByObjectType(Start, End, WorldOnly, Params);
}

// ---------------------------------------------------------------------------------------------
// Moving around
// ---------------------------------------------------------------------------------------------

void AKSBotController::Roam(AKSCharacter* Bot, float DeltaSeconds)
{
	const float Now = GetTime();
	if (!bHasGoal || !Path.IsValidIndex(PathIndex) || Now - GoalTime > 25.f)
	{
		PickGoal(Bot);
	}
	FollowPath(Bot);

	if (Enemy.IsValid() && Now - LastSeenTime < 3.f)
	{
		// Keep looking where the enemy disappeared.
		DesiredAim = (LastSeenLocation - Bot->GetEyeLocation()).Rotation();
		DesiredAim.Pitch = FMath::Clamp(FRotator::NormalizeAxis(DesiredAim.Pitch), -30.f, 30.f);
	}
}

void AKSBotController::PickGoal(AKSCharacter* Bot)
{
	const AKSGameMode* GM = GetWorld()->GetAuthGameMode<AKSGameMode>();
	if (!GM)
	{
		return;
	}
	const float Now = GetTime();
	const FVector BotLocation = Bot->GetActorLocation();

	if (Enemy.IsValid() && Enemy->IsAlive() && Now - LastSeenTime < 4.f)
	{
		if (PathTo(Bot, LastSeenLocation - FVector(0.f, 0.f, KS::CapsuleHalfHeight)))
		{
			return;
		}
	}

	const AKSPickup* BestPickup = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	for (const AKSPickup* Pickup : GM->GetPickups())
	{
		if (!IsValid(Pickup) || !Pickup->IsAvailable())
		{
			continue;
		}
		const int32 Type = Pickup->GetType();
		float Want = 0.f;
		if (Type == KSPickupType::Health)
		{
			Want = Bot->GetHealth() < 70.f ? (KS::MaxHealth - Bot->GetHealth()) / 30.f : 0.f;
		}
		else if (!Bot->HasWeapon(Type))
		{
			Want = 2.5f;
		}
		else if (Bot->GetAmmo(Type) < KS::Weapon(Type).MaxAmmo / 2)
		{
			Want = 1.f;
		}
		if (Want <= 0.f)
		{
			continue;
		}
		const float Score = FVector::Dist(BotLocation, Pickup->GetActorLocation()) / Want;
		if (Score < BestScore)
		{
			BestScore = Score;
			BestPickup = Pickup;
		}
	}
	if (BestPickup && PathTo(Bot, BestPickup->GetActorLocation()))
	{
		return;
	}

	const FKSNavGraph& Graph = GM->GetNavGraph();
	if (!Graph.IsValid())
	{
		return;
	}
	for (int32 Try = 0; Try < 6; ++Try)
	{
		const FVector Node = Graph.Nodes[FMath::RandRange(0, Graph.Nodes.Num() - 1)];
		if (FVector::Dist2D(Node, BotLocation) > 1200.f && PathTo(Bot, Node))
		{
			return;
		}
	}
}

bool AKSBotController::PathTo(AKSCharacter* Bot, const FVector& Goal)
{
	const AKSGameMode* GM = GetWorld()->GetAuthGameMode<AKSGameMode>();
	if (!GM || !GM->GetNavGraph().IsValid())
	{
		return false;
	}
	// Re-planning every frame toward a moving target would be wasteful.
	if (bHasGoal && Path.IsValidIndex(PathIndex) && FVector::Dist(GoalLocation, Goal) < 400.f)
	{
		return true;
	}
	const FKSNavGraph& Graph = GM->GetNavGraph();
	const FVector Feet = Bot->GetActorLocation() - FVector(0.f, 0.f, KS::CapsuleHalfHeight);
	TArray<int32> NewPath;
	if (!Graph.FindPath(Graph.FindNearest(Feet), Graph.FindNearest(Goal), NewPath))
	{
		return false;
	}
	Path = MoveTemp(NewPath);
	PathIndex = 0;
	// Skip the first waypoint when it is behind us on the way to the second.
	if (Path.Num() > 1)
	{
		const FVector First = Graph.Nodes[Path[0]];
		const FVector Second = Graph.Nodes[Path[1]];
		if (FMath::Abs(First.Z - Second.Z) < 30.f && FVector::Dist2D(Feet, Second) < FVector::Dist2D(First, Second))
		{
			PathIndex = 1;
		}
	}
	GoalLocation = Goal;
	bHasGoal = true;
	GoalTime = GetTime();
	return true;
}

void AKSBotController::FollowPath(AKSCharacter* Bot)
{
	const AKSGameMode* GM = GetWorld()->GetAuthGameMode<AKSGameMode>();
	if (!GM || !bHasGoal || !Path.IsValidIndex(PathIndex))
	{
		bHasGoal = false;
		return;
	}
	const FKSNavGraph& Graph = GM->GetNavGraph();
	const FVector Feet = Bot->GetActorLocation() - FVector(0.f, 0.f, KS::CapsuleHalfHeight);
	const bool bOnGround = Bot->GetCharacterMovement()->IsMovingOnGround();

	FVector Target = Graph.Nodes[Path[PathIndex]];
	// In the air (thrown by a jump pad) a waypoint counts as reached a bit further out.
	const float Reach = bOnGround ? 80.f : 130.f;
	if (FVector::Dist2D(Feet, Target) < Reach && FMath::Abs(Feet.Z - Target.Z) < 160.f)
	{
		++PathIndex;
		if (!Path.IsValidIndex(PathIndex))
		{
			bHasGoal = false;
			Path.Reset();
			return;
		}
		Target = Graph.Nodes[Path[PathIndex]];
	}
	FVector Dir = Target - Feet;
	Dir.Z = 0.f;
	if (Dir.SizeSquared() < 1.f)
	{
		return;
	}
	Dir.Normalize();
	// Flights from jump pads are exact; steering in the air would spoil them.
	if (bOnGround)
	{
		Bot->AddMovementInput(Dir, 1.f);
	}
	DesiredAim = FRotator(0.f, Dir.Rotation().Yaw, 0.f);
}

void AKSBotController::CheckStuck(AKSCharacter* Bot, float DeltaSeconds)
{
	StuckTimer += DeltaSeconds;
	if (StuckTimer < 1.5f)
	{
		return;
	}
	const float Moved = FVector::Dist2D(Bot->GetActorLocation(), StuckCheckLocation);
	if (bHasGoal && Moved < 60.f)
	{
		++StuckCount;
		Bot->DoJump();
		if (StuckCount >= 2)
		{
			bHasGoal = false;
			Path.Reset();
			StuckCount = 0;
		}
	}
	else
	{
		StuckCount = 0;
	}
	StuckTimer = 0.f;
	StuckCheckLocation = Bot->GetActorLocation();
}
