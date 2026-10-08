#include "KSPickup.h"
#include "KSArenaData.h"
#include "KSAssets.h"
#include "KSAudio.h"
#include "KSCharacter.h"
#include "KSFXManager.h"
#include "KSGameState.h"
#include "KSTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"

namespace
{
	constexpr float KSItemHeight = 62.f;
	constexpr float KSTakeRadius = 85.f;
	constexpr float KSTakeHeight = 130.f;
	const FRotator KSAlongX(90.f, 0.f, 0.f);
}

AKSPickup::AKSPickup()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetNetUpdateFrequency(5.f);
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Root->SetMobility(EComponentMobility::Movable);
}

void AKSPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AKSPickup, Type, COND_InitialOnly);
	DOREPLIFETIME(AKSPickup, bAvailable);
}

float AKSPickup::GetRespawnDelay(uint8 InType)
{
	switch (InType)
	{
	case KSPickupType::Health: return 15.f;
	case KSPickupType::Shotgun: return 20.f;
	case KSPickupType::Rail: return 25.f;
	default: return 30.f;
	}
}

void AKSPickup::BeginPlay()
{
	Super::BeginPlay();
	BuildVisuals();
	OnRep_Available();
}

UStaticMeshComponent* AKSPickup::AddPart(USceneComponent* Parent, int32 Shape, const FVector& Location, const FVector& Size, UMaterialInterface* Material, const FRotator& Rotation)
{
	UKSAssets* Assets = UKSAssets::Get(this);
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
	Part->SetupAttachment(Parent);
	Part->SetStaticMesh(Assets ? Assets->GetMesh((EKSShape)Shape) : nullptr);
	Part->SetMaterial(0, Material);
	Part->SetRelativeLocationAndRotation(Location, Rotation);
	Part->SetRelativeScale3D(Size / 100.f);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetGenerateOverlapEvents(false);
	Part->SetCanEverAffectNavigation(false);
	Part->SetCastShadow(false);
	Part->RegisterComponent();
	return Part;
}

void AKSPickup::BuildVisuals()
{
	UKSAssets* Assets = UKSAssets::Get(this);
	if (!Assets || bVisualsBuilt)
	{
		return;
	}
	bVisualsBuilt = true;
	const FLinearColor Color = KS::PickupColor(Type);
	const int32 Cube = (int32)EKSShape::Cube;
	const int32 Cylinder = (int32)EKSShape::Cylinder;
	const int32 Sphere = (int32)EKSShape::Sphere;
	UMaterialInterface* Metal = Assets->SharedSurface(FLinearColor(0.05f, 0.055f, 0.065f), 0.3f, 0.7f);
	UMaterialInterface* Neon = Assets->SharedNeon(Color, 7.f);
	UMaterialInterface* White = Assets->SharedNeon(FLinearColor(1.f, 1.f, 1.f), 4.f);
	RingMaterial = Assets->MakeNeon(this, Color, 6.f);
	HaloMaterial = Assets->MakeGlow(this, Color, 1.5f, 0.35f);

	// The pad: a dark disc with a neon rim.
	AddPart(Root, Cylinder, FVector(0.f, 0.f, 4.f), FVector(84.f, 84.f, 8.f), Metal);
	AddPart(Root, Cylinder, FVector(0.f, 0.f, 2.5f), FVector(98.f, 98.f, 5.f), RingMaterial);

	ItemRoot = NewObject<USceneComponent>(this);
	ItemRoot->SetupAttachment(Root);
	ItemRoot->SetRelativeLocation(FVector(0.f, 0.f, KSItemHeight));
	ItemRoot->RegisterComponent();

	switch (Type)
	{
	case KSPickupType::Health:
		AddPart(ItemRoot, Cube, FVector::ZeroVector, FVector(14.f, 40.f, 14.f), Neon);
		AddPart(ItemRoot, Cube, FVector::ZeroVector, FVector(14.f, 14.f, 40.f), Neon);
		AddPart(ItemRoot, Cube, FVector::ZeroVector, FVector(16.f, 10.f, 10.f), White);
		break;
	case KSPickupType::Shotgun:
		AddPart(ItemRoot, Cube, FVector(-6.f, 0.f, 0.f), FVector(30.f, 9.f, 10.f), Metal);
		AddPart(ItemRoot, Cylinder, FVector(18.f, 2.2f, 2.f), FVector(4.f, 4.f, 30.f), Metal, KSAlongX);
		AddPart(ItemRoot, Cylinder, FVector(18.f, -2.2f, 2.f), FVector(4.f, 4.f, 30.f), Metal, KSAlongX);
		AddPart(ItemRoot, Cube, FVector(-24.f, 0.f, -2.f), FVector(16.f, 7.f, 9.f), Metal);
		AddPart(ItemRoot, Cube, FVector(0.f, 4.8f, 2.f), FVector(26.f, 1.f, 2.f), Neon);
		AddPart(ItemRoot, Cube, FVector(0.f, -4.8f, 2.f), FVector(26.f, 1.f, 2.f), Neon);
		break;
	case KSPickupType::Rail:
		AddPart(ItemRoot, Cube, FVector(-4.f, 0.f, 0.f), FVector(40.f, 6.f, 8.f), Metal);
		AddPart(ItemRoot, Cube, FVector(28.f, 0.f, 1.f), FVector(30.f, 3.5f, 3.5f), Metal);
		for (const float X : { 18.f, 26.f, 34.f })
		{
			AddPart(ItemRoot, Cylinder, FVector(X, 0.f, 1.f), FVector(9.f, 9.f, 2.f), Neon, KSAlongX);
		}
		AddPart(ItemRoot, Cube, FVector(-28.f, 0.f, -1.f), FVector(12.f, 5.f, 9.f), Metal);
		break;
	default:
		AddPart(ItemRoot, Cylinder, FVector(4.f, 0.f, 0.f), FVector(13.f, 13.f, 56.f), Metal, KSAlongX);
		AddPart(ItemRoot, Cylinder, FVector(32.f, 0.f, 0.f), FVector(15.f, 15.f, 4.f), Neon, KSAlongX);
		AddPart(ItemRoot, Cylinder, FVector(-24.f, 0.f, 0.f), FVector(15.f, 15.f, 4.f), Neon, KSAlongX);
		AddPart(ItemRoot, Sphere, FVector(33.f, 0.f, 0.f), FVector(9.f, 9.f, 9.f), Neon);
		break;
	}

	Halo = AddPart(Root, Sphere, FVector(0.f, 0.f, KSItemHeight), FVector(95.f, 95.f, 95.f), HaloMaterial);
	// Each pickup starts its spin at a different angle.
	Age = FMath::FRandRange(0.f, 10.f);
}

void AKSPickup::OnRep_Available()
{
	if (ItemRoot)
	{
		ItemRoot->SetVisibility(bAvailable, true);
	}
	if (Halo)
	{
		Halo->SetVisibility(bAvailable);
	}
	if (RingMaterial)
	{
		RingMaterial->SetScalarParameterValue(TEXT("Intensity"), bAvailable ? 6.f : 0.8f);
	}
	if (!HasActorBegunPlay())
	{
		return;
	}
	const FLinearColor Color = KS::PickupColor(Type);
	AKSFXManager* FX = AKSFXManager::Get(this);
	if (bAvailable)
	{
		if (FX)
		{
			FX->Flash(GetActorLocation() + FVector(0.f, 0.f, KSItemHeight), Color, 20.f, 120.f, 0.35f, 5.f);
		}
	}
	else
	{
		if (FX)
		{
			FX->Pillar(GetActorLocation(), Color, 260.f);
		}
		KSAudio::PlayAt(this, KSSound::Pickup, GetActorLocation(), 1.f);
	}
}

void AKSPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	if (ItemRoot && bAvailable)
	{
		ItemRoot->SetRelativeLocationAndRotation(FVector(0.f, 0.f, KSItemHeight + FMath::Sin(Age * 2.2f) * 6.f), FRotator(0.f, Age * 90.f, 0.f));
	}
	if (HaloMaterial && bAvailable)
	{
		HaloMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.28f + 0.1f * FMath::Sin(Age * 3.f));
	}

	if (!HasAuthority())
	{
		return;
	}
	const AKSGameState* GS = GetWorld()->GetGameState<AKSGameState>();
	const float Now = GS ? GS->GetServerTime() : GetWorld()->GetTimeSeconds();
	if (!bAvailable)
	{
		if (Now >= RespawnAt)
		{
			bAvailable = true;
			ForceNetUpdate();
			OnRep_Available();
		}
		return;
	}
	const FVector Location = GetActorLocation();
	for (TActorIterator<AKSCharacter> It(GetWorld()); It; ++It)
	{
		AKSCharacter* Character = *It;
		if (!Character->IsAlive())
		{
			continue;
		}
		const FVector Center = Character->GetActorLocation();
		if (FVector::Dist2D(Center, Location) > KSTakeRadius || FMath::Abs(Center.Z - Location.Z) > KSTakeHeight)
		{
			continue;
		}
		if (Character->TryTakePickup(Type))
		{
			bAvailable = false;
			RespawnAt = Now + GetRespawnDelay(Type);
			ForceNetUpdate();
			OnRep_Available();
			break;
		}
	}
}
