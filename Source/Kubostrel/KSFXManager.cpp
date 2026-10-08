#include "KSFXManager.h"
#include "Kubostrel.h"
#include "KSAssets.h"
#include "KSArenaData.h"
#include "KSTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "CollisionQueryParams.h"

static TWeakObjectPtr<AKSFXManager> GKSFXManager;
static constexpr int32 KSMaxGlowFx = 192;
static constexpr int32 KSMaxSolidFx = 64;

AKSFXManager::AKSFXManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	bReplicates = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

AKSFXManager* AKSFXManager::Get(const UObject* WorldContext)
{
	UWorld* World = (GEngine && WorldContext) ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World || World->bIsTearingDown || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	if (GKSFXManager.IsValid() && GKSFXManager->GetWorld() == World)
	{
		return GKSFXManager.Get();
	}
	for (TActorIterator<AKSFXManager> It(World); It; ++It)
	{
		GKSFXManager = *It;
		return *It;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AKSFXManager* Manager = World->SpawnActor<AKSFXManager>(AKSFXManager::StaticClass(), FTransform::Identity, Params);
	GKSFXManager = Manager;
	return Manager;
}

int32 AKSFXManager::Acquire(bool bSolid, bool bSphereMesh, const FLinearColor& Color, float Soft)
{
	TArray<FKSFx>& Fx = bSolid ? SolidFx : GlowFx;
	TArray<TObjectPtr<UStaticMeshComponent>>& Meshes = bSolid ? SolidMeshes : GlowMeshes;
	TArray<TObjectPtr<UMaterialInstanceDynamic>>& Mats = bSolid ? SolidMaterials : GlowMaterials;
	const int32 MaxCount = bSolid ? KSMaxSolidFx : KSMaxGlowFx;

	UKSAssets* Assets = UKSAssets::Get(this);
	if (!Assets)
	{
		return INDEX_NONE;
	}

	// A free slot that already has the right mesh, then a new slot, then any free slot, then the oldest effect.
	int32 Index = INDEX_NONE;
	for (int32 i = 0; i < Fx.Num(); ++i)
	{
		if (!Fx[i].bActive && Fx[i].bSphereMesh == bSphereMesh)
		{
			Index = i;
			break;
		}
	}
	if (Index == INDEX_NONE && Fx.Num() < MaxCount)
	{
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this);
		Mesh->SetupAttachment(RootComponent);
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->SetCastShadow(false);
		Mesh->SetStaticMesh(Assets->GetMesh(bSphereMesh ? EKSShape::Sphere : EKSShape::Cube));
		UMaterialInstanceDynamic* Mat = bSolid ? Assets->MakeCharacter(this, Color, 1.2f) : Assets->MakeGlow(this, Color, 1.f, 1.f);
		Mesh->SetMaterial(0, Mat);
		Mesh->SetVisibility(false);
		Mesh->RegisterComponent();
		Meshes.Add(Mesh);
		Mats.Add(Mat);
		FKSFx& New = Fx.AddDefaulted_GetRef();
		New.bSphereMesh = bSphereMesh;
		Index = Fx.Num() - 1;
	}
	if (Index == INDEX_NONE)
	{
		for (int32 i = 0; i < Fx.Num(); ++i)
		{
			if (!Fx[i].bActive)
			{
				Index = i;
				break;
			}
		}
	}
	if (Index == INDEX_NONE)
	{
		float Oldest = -1.f;
		for (int32 i = 0; i < Fx.Num(); ++i)
		{
			const float Progress = Fx[i].Age / FMath::Max(Fx[i].Life, 0.01f);
			if (Progress > Oldest)
			{
				Oldest = Progress;
				Index = i;
			}
		}
		if (Index != INDEX_NONE)
		{
			Release(Index, bSolid);
		}
	}
	if (Index == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	FKSFx& Item = Fx[Index];
	if (Item.bSphereMesh != bSphereMesh)
	{
		Meshes[Index]->SetStaticMesh(Assets->GetMesh(bSphereMesh ? EKSShape::Sphere : EKSShape::Cube));
	}
	Item = FKSFx();
	Item.bActive = true;
	Item.bSphereMesh = bSphereMesh;
	if (UMaterialInstanceDynamic* Mat = Mats[Index])
	{
		Mat->SetVectorParameterValue(TEXT("Color"), Color);
		if (!bSolid)
		{
			Mat->SetScalarParameterValue(TEXT("Soft"), Soft);
		}
	}
	++ActiveCount;
	SetActorTickEnabled(true);
	return Index;
}

void AKSFXManager::Release(int32 Index, bool bSolid)
{
	TArray<FKSFx>& Fx = bSolid ? SolidFx : GlowFx;
	TArray<TObjectPtr<UStaticMeshComponent>>& Meshes = bSolid ? SolidMeshes : GlowMeshes;
	if (Fx.IsValidIndex(Index) && Fx[Index].bActive)
	{
		Fx[Index].bActive = false;
		Meshes[Index]->SetVisibility(false);
		ActiveCount = FMath::Max(0, ActiveCount - 1);
	}
}

void AKSFXManager::Tracer(const FVector& Start, const FVector& End, const FLinearColor& Color, float Width, float Life, float Intensity)
{
	const int32 Index = Acquire(false, false, Color, 0.f);
	if (Index == INDEX_NONE)
	{
		return;
	}
	FKSFx& Item = GlowFx[Index];
	Item.Kind = EKSFxKind::Line;
	Item.Location = Start;
	Item.End = End;
	Item.Life = Life;
	Item.Scale0 = FVector(1.f, Width, Width);
	Item.Scale1 = FVector(1.f, Width * 0.3f, Width * 0.3f);
	Item.Intensity = Intensity;
	UpdateGlow(Index, 0.f);
	GlowMeshes[Index]->SetVisibility(true);
}

void AKSFXManager::Beam(const FVector& Start, const FVector& End, const FLinearColor& Color)
{
	Tracer(Start, End, Color, 9.f, 0.65f, 5.f);
	Tracer(Start, End, FMath::Lerp(Color, FLinearColor::White, 0.6f), 2.5f, 0.28f, 16.f);
	Flash(End, Color, 10.f, 70.f, 0.3f, 8.f);
}

void AKSFXManager::Flash(const FVector& Location, const FLinearColor& Color, float StartSize, float EndSize, float Life, float Intensity)
{
	const int32 Index = Acquire(false, true, Color, 1.f);
	if (Index == INDEX_NONE)
	{
		return;
	}
	FKSFx& Item = GlowFx[Index];
	Item.Kind = EKSFxKind::Sphere;
	Item.Location = Location;
	Item.Life = Life;
	Item.Scale0 = FVector(StartSize);
	Item.Scale1 = FVector(EndSize);
	Item.Intensity = Intensity;
	UpdateGlow(Index, 0.f);
	GlowMeshes[Index]->SetVisibility(true);
}

void AKSFXManager::Spark(const FVector& Location, const FVector& Velocity, const FLinearColor& Color, float Size, float Life)
{
	const int32 Index = Acquire(false, false, Color, 0.f);
	if (Index == INDEX_NONE)
	{
		return;
	}
	FKSFx& Item = GlowFx[Index];
	Item.Kind = EKSFxKind::Spark;
	Item.Location = Location;
	Item.Velocity = Velocity;
	Item.Life = Life;
	Item.Scale0 = FVector(Size);
	Item.Intensity = 10.f;
	Item.Gravity = KS::Gravity * 0.6f;
	UpdateGlow(Index, 0.f);
	GlowMeshes[Index]->SetVisibility(true);
}

void AKSFXManager::Impact(const FVector& Location, const FVector& Normal, const FLinearColor& Color, float Scale)
{
	const FVector N = Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	Flash(Location + N * 2.f, Color, 6.f * Scale, 45.f * Scale, 0.14f, 9.f);
	const int32 Count = FMath::RoundToInt(4 * Scale);
	for (int32 i = 0; i < Count; ++i)
	{
		const FVector Dir = (N + FMath::VRand() * 0.85f).GetSafeNormal();
		Spark(Location + N * 3.f, Dir * FMath::FRandRange(350.f, 900.f) * Scale, Color, 2.5f, FMath::FRandRange(0.2f, 0.38f));
	}
}

void AKSFXManager::Explosion(const FVector& Location, const FLinearColor& Color)
{
	Flash(Location, FMath::Lerp(Color, FLinearColor::White, 0.5f), 40.f, 440.f, 0.24f, 12.f);
	Flash(Location, Color, 90.f, KS::RocketRadius * 2.f, 0.5f, 4.f);

	// Flat shock wave along the ground.
	const int32 Ring = Acquire(false, true, Color, 1.f);
	if (Ring != INDEX_NONE)
	{
		FKSFx& Item = GlowFx[Ring];
		Item.Kind = EKSFxKind::Sphere;
		Item.Location = Location;
		Item.Life = 0.45f;
		Item.Scale0 = FVector(60.f, 60.f, 6.f);
		Item.Scale1 = FVector(KS::RocketRadius * 2.6f, KS::RocketRadius * 2.6f, 14.f);
		Item.Intensity = 3.f;
		UpdateGlow(Ring, 0.f);
		GlowMeshes[Ring]->SetVisibility(true);
	}

	for (int32 i = 0; i < 14; ++i)
	{
		const FVector Dir = (FMath::VRand() + FVector(0.f, 0.f, 0.5f)).GetSafeNormal();
		Spark(Location, Dir * FMath::FRandRange(1100.f, 2300.f), Color, 4.f, FMath::FRandRange(0.45f, 0.85f));
	}
}

void AKSFXManager::Pillar(const FVector& FloorLocation, const FLinearColor& Color, float Height)
{
	Tracer(FloorLocation, FloorLocation + FVector(0.f, 0.f, Height), Color, 30.f, 0.7f, 4.f);
	Flash(FloorLocation + FVector(0.f, 0.f, 10.f), Color, 20.f, 170.f, 0.45f, 5.f);
}

void AKSFXManager::Debris(const FVector& Location, const FVector& BaseVelocity, const FLinearColor& Color, int32 Count)
{
	for (int32 i = 0; i < Count; ++i)
	{
		const int32 Index = Acquire(true, false, Color, 0.f);
		if (Index == INDEX_NONE)
		{
			return;
		}
		FKSFx& Item = SolidFx[Index];
		Item.Kind = EKSFxKind::Chunk;
		Item.Location = Location + FMath::VRand() * FMath::FRandRange(0.f, 50.f);
		Item.Velocity = BaseVelocity * 0.5f + FMath::VRand() * FMath::FRandRange(250.f, 650.f) + FVector(0.f, 0.f, 380.f);
		Item.Rotation = FRotator(FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f));
		Item.Spin = FRotator(FMath::FRandRange(-720.f, 720.f), FMath::FRandRange(-720.f, 720.f), FMath::FRandRange(-720.f, 720.f));
		const float Size = FMath::FRandRange(12.f, 24.f);
		Item.Scale0 = FVector(Size, Size * FMath::FRandRange(0.6f, 1.2f), Size * FMath::FRandRange(0.5f, 1.f));
		Item.Life = FMath::FRandRange(2.0f, 2.8f);
		Item.Gravity = KS::Gravity;
		UpdateChunk(Index, 0.f);
		SolidMeshes[Index]->SetVisibility(true);
	}
	Flash(Location, Color, 30.f, 260.f, 0.35f, 7.f);
}

void AKSFXManager::UpdateGlow(int32 Index, float DeltaSeconds)
{
	FKSFx& Item = GlowFx[Index];
	UStaticMeshComponent* Mesh = GlowMeshes[Index];
	UMaterialInstanceDynamic* Mat = GlowMaterials[Index];
	const float T = FMath::Clamp(Item.Age / FMath::Max(Item.Life, 0.01f), 0.f, 1.f);
	const float Fade = FMath::Square(1.f - T);

	switch (Item.Kind)
	{
	case EKSFxKind::Line:
	{
		const FVector Delta = Item.End - Item.Location;
		const float Len = FMath::Max(Delta.Size(), 1.f);
		const FVector Thickness = FMath::Lerp(Item.Scale0, Item.Scale1, T);
		Mesh->SetWorldTransform(FTransform(Delta.Rotation(), (Item.Location + Item.End) * 0.5f, FVector(Len, Thickness.Y, Thickness.Z) / 100.f));
		break;
	}
	case EKSFxKind::Sphere:
	{
		const float Grow = 1.f - FMath::Square(1.f - T);
		Mesh->SetWorldTransform(FTransform(FRotator::ZeroRotator, Item.Location, FMath::Lerp(Item.Scale0, Item.Scale1, Grow) / 100.f));
		break;
	}
	case EKSFxKind::Spark:
	{
		Item.Velocity.Z -= Item.Gravity * DeltaSeconds;
		Item.Velocity *= FMath::Max(0.f, 1.f - 2.5f * DeltaSeconds);
		Item.Location += Item.Velocity * DeltaSeconds;
		const float Speed = Item.Velocity.Size();
		const float Stretch = FMath::Max(Item.Scale0.X, Speed * 0.025f);
		const FRotator Rot = Speed > 1.f ? Item.Velocity.Rotation() : FRotator::ZeroRotator;
		Mesh->SetWorldTransform(FTransform(Rot, Item.Location, FVector(Stretch, Item.Scale0.Y, Item.Scale0.Z) * (1.f - T * 0.6f) / 100.f));
		break;
	}
	default:
		break;
	}

	if (Mat)
	{
		Mat->SetScalarParameterValue(TEXT("Intensity"), Item.Intensity);
		Mat->SetScalarParameterValue(TEXT("Opacity"), Fade);
	}
}

void AKSFXManager::UpdateChunk(int32 Index, float DeltaSeconds)
{
	FKSFx& Item = SolidFx[Index];
	UStaticMeshComponent* Mesh = SolidMeshes[Index];

	if (DeltaSeconds > 0.f)
	{
		Item.Velocity.Z -= Item.Gravity * DeltaSeconds;
		const FVector Next = Item.Location + Item.Velocity * DeltaSeconds;
		FHitResult Hit;
		FCollisionObjectQueryParams Objects(ECC_WorldStatic);
		if (GetWorld()->LineTraceSingleByObjectType(Hit, Item.Location, Next, Objects))
		{
			// Bounce, losing most of the energy.
			Item.Location = Hit.ImpactPoint + Hit.ImpactNormal * 6.f;
			Item.Velocity = Item.Velocity.MirrorByVector(Hit.ImpactNormal) * 0.38f;
			Item.Spin *= 0.5f;
		}
		else
		{
			Item.Location = Next;
		}
		Item.Rotation += Item.Spin * DeltaSeconds;
	}

	const float Remaining = Item.Life - Item.Age;
	const float Shrink = FMath::Clamp(Remaining / 0.4f, 0.f, 1.f);
	Mesh->SetWorldTransform(FTransform(Item.Rotation, Item.Location, Item.Scale0 * Shrink / 100.f));
}

void AKSFXManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	for (int32 i = 0; i < GlowFx.Num(); ++i)
	{
		if (!GlowFx[i].bActive)
		{
			continue;
		}
		GlowFx[i].Age += DeltaSeconds;
		if (GlowFx[i].Age >= GlowFx[i].Life)
		{
			Release(i, false);
			continue;
		}
		UpdateGlow(i, DeltaSeconds);
	}

	for (int32 i = 0; i < SolidFx.Num(); ++i)
	{
		if (!SolidFx[i].bActive)
		{
			continue;
		}
		SolidFx[i].Age += DeltaSeconds;
		if (SolidFx[i].Age >= SolidFx[i].Life)
		{
			Release(i, true);
			continue;
		}
		UpdateChunk(i, DeltaSeconds);
	}

	if (ActiveCount == 0)
	{
		SetActorTickEnabled(false);
	}
}
