#include "KSRocket.h"
#include "KSAssets.h"
#include "KSAudio.h"
#include "KSCharacter.h"
#include "KSFXManager.h"
#include "KSGameState.h"
#include "KSArenaData.h"
#include "KSTypes.h"
#include "CollisionQueryParams.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

namespace
{
	constexpr float KSRocketLife = 4.f;
	constexpr float KSRocketDamage = 90.f;
	constexpr float KSSelfDamageScale = 0.5f;
	constexpr float KSSelfKnockback = 1100.f;
	constexpr float KSOtherKnockback = 800.f;
}

AKSRocket::AKSRocket()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	bReplicates = true;
	SetReplicatingMovement(false);
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(10.f);
	Origin = FVector::ZeroVector;
	Direction = FVector::ForwardVector;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void AKSRocket::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AKSRocket, Origin, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AKSRocket, Direction, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AKSRocket, LaunchTime, COND_InitialOnly);
}

void AKSRocket::InitRocket(AKSCharacter* Shooter, const FVector& InOrigin, const FVector& InDirection)
{
	ShooterPawn = Shooter;
	ShooterController = Shooter ? Shooter->GetController() : nullptr;
	Origin = InOrigin;
	Direction = InDirection.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	LaunchTime = GetServerTime();
}

float AKSRocket::GetServerTime() const
{
	const AKSGameState* GS = GetWorld() ? GetWorld()->GetGameState<AKSGameState>() : nullptr;
	return GS ? GS->GetServerTime() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f);
}

FVector AKSRocket::GetPositionAt(float ServerTime) const
{
	return FVector(Origin) + FVector(Direction) * (KS::RocketSpeed * FMath::Max(0.f, ServerTime - LaunchTime));
}

void AKSRocket::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		// Clients only learn the start; the server keeps its own clock for the whole flight.
		LastPosition = Origin;
	}
	else
	{
		LastPosition = GetPositionAt(GetServerTime());
	}
	LastTrailPosition = LastPosition;
	SetActorLocationAndRotation(LastPosition, FVector(Direction).Rotation());
	BuildVisuals();
}

void AKSRocket::BuildVisuals()
{
	UKSAssets* Assets = UKSAssets::Get(this);
	if (!Assets)
	{
		return;
	}
	const FLinearColor Color = KS::Weapon(KSWeapon::Rocket).Color;
	Visual = NewObject<USceneComponent>(this);
	Visual->SetupAttachment(Root);
	Visual->RegisterComponent();
	auto Add = [this, Assets](EKSShape Shape, const FVector& Location, const FVector& Size, UMaterialInterface* Material, const FRotator& Rotation)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
		Part->SetupAttachment(Visual);
		Part->SetStaticMesh(Assets->GetMesh(Shape));
		Part->SetMaterial(0, Material);
		Part->SetRelativeLocationAndRotation(Location, Rotation);
		Part->SetRelativeScale3D(Size / 100.f);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetGenerateOverlapEvents(false);
		Part->SetCanEverAffectNavigation(false);
		Part->SetCastShadow(false);
		Part->RegisterComponent();
	};
	const FRotator AlongX(90.f, 0.f, 0.f);
	Add(EKSShape::Cylinder, FVector::ZeroVector, FVector(10.f, 10.f, 34.f), Assets->SharedSurface(FLinearColor(0.08f, 0.08f, 0.09f), 0.3f, 0.8f), AlongX);
	Add(EKSShape::Sphere, FVector(17.f, 0.f, 0.f), FVector(10.f, 10.f, 14.f), Assets->SharedNeon(Color, 6.f), AlongX);
	Add(EKSShape::Sphere, FVector(-20.f, 0.f, 0.f), FVector(16.f, 16.f, 16.f), Assets->SharedNeon(FLinearColor(1.f, 0.7f, 0.3f), 14.f), FRotator::ZeroRotator);
	Add(EKSShape::Sphere, FVector(-22.f, 0.f, 0.f), FVector(70.f, 70.f, 70.f), Assets->MakeGlow(this, Color, 2.f, 0.5f), FRotator::ZeroRotator);
}

void AKSRocket::HideVisuals()
{
	bHiddenByWall = true;
	if (Visual)
	{
		Visual->SetVisibility(false, true);
	}
}

void AKSRocket::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bExploded)
	{
		return;
	}
	const float Now = GetServerTime();
	const FVector Position = GetPositionAt(Now);

	if (HasAuthority())
	{
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_Pawn);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(KSRocket), false, this);
		if (AKSCharacter* Shooter = ShooterPawn.Get())
		{
			Params.AddIgnoredActor(Shooter);
		}
		FHitResult Hit;
		if (GetWorld()->SweepSingleByObjectType(Hit, LastPosition, Position, FQuat::Identity, Objects, FCollisionShape::MakeSphere(10.f), Params))
		{
			// Explode slightly in front of the surface so the blast isn't blocked by it.
			Explode(Hit.Location + Hit.ImpactNormal * 4.f);
			return;
		}
		if (Now - LaunchTime > KSRocketLife)
		{
			Explode(Position);
			return;
		}
	}
	else if (!bHiddenByWall)
	{
		// The explosion message is on its way; don't let the rocket fly through the wall meanwhile.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(KSRocketClient), false, this);
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByObjectType(Hit, LastPosition, Position, FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			SetActorLocation(Hit.Location);
			HideVisuals();
		}
	}

	if (!bHiddenByWall)
	{
		SetActorLocation(Position);
		TrailTimer += DeltaSeconds;
		if (TrailTimer >= 0.03f)
		{
			TrailTimer = 0.f;
			if (AKSFXManager* FX = AKSFXManager::Get(this))
			{
				const FVector Tail = Position - FVector(Direction) * 22.f;
				FX->Tracer(LastTrailPosition, Tail, FLinearColor(1.f, 0.45f, 0.15f), 7.f, 0.35f, 5.f);
				LastTrailPosition = Tail;
			}
		}
	}
	LastPosition = Position;
}

void AKSRocket::Explode(const FVector& Location)
{
	if (bExploded)
	{
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(KSRocketBlast), false, this);
	const FCollisionObjectQueryParams WorldOnly(ECC_WorldStatic);
	AController* ShooterPC = ShooterController.Get();
	for (TActorIterator<AKSCharacter> It(GetWorld()); It; ++It)
	{
		AKSCharacter* Character = *It;
		if (!Character->IsAlive())
		{
			continue;
		}
		// Closest point of the capsule to the blast, so a hit at the feet still counts in full.
		const FVector Center = Character->GetActorLocation();
		const FVector Closest(Center.X, Center.Y, FMath::Clamp(Location.Z, Center.Z - KS::CapsuleHalfHeight, Center.Z + KS::CapsuleHalfHeight));
		const float Distance = FMath::Max(0.f, FVector::Dist(Location, Closest) - KS::CapsuleRadius);
		if (Distance > KS::RocketRadius)
		{
			continue;
		}
		if (GetWorld()->LineTraceTestByObjectType(Location, Center, WorldOnly, Params)
			&& GetWorld()->LineTraceTestByObjectType(Location, Closest, WorldOnly, Params))
		{
			continue;
		}
		const float Falloff = 1.f - Distance / KS::RocketRadius;
		const bool bSelf = Character == ShooterPawn.Get();
		float Damage = KSRocketDamage * (0.3f + 0.7f * Falloff);
		if (bSelf)
		{
			Damage *= KSSelfDamageScale;
		}
		FVector Push = Center - Location;
		Push.Z = FMath::Max(Push.Z, 0.f) + 60.f;
		Push = Push.GetSafeNormal() * (bSelf ? KSSelfKnockback : KSOtherKnockback) * (0.35f + 0.65f * Falloff);
		Character->ApplyKnockback(Push);
		Character->ApplyDamageFrom(Damage, ShooterPC, KSWeapon::Rocket, false, Location);
	}

	MulticastExplode(Location);
	bExploded = true;
	SetActorTickEnabled(false);
	// Live a moment longer so the explosion message reaches everyone.
	SetLifeSpan(0.3f);
}

void AKSRocket::MulticastExplode_Implementation(FVector_NetQuantize Location)
{
	bExploded = true;
	HideVisuals();
	if (AKSFXManager* FX = AKSFXManager::Get(this))
	{
		FX->Explosion(Location, KS::Weapon(KSWeapon::Rocket).Color);
	}
	KSAudio::PlayAt(this, KSSound::Explosion, Location, 1.f, FMath::FRandRange(0.92f, 1.08f));
}
