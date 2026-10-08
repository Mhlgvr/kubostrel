#include "KSArenaBuilder.h"
#include "KSGameInstance.h"
#include "Kubostrel.h"
#include "KSAssets.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"

static TWeakObjectPtr<AKSArenaBuilder> GKSArenaBuilder;

static void KSSetupPiece(UStaticMeshComponent* Piece, USceneComponent* Parent, UStaticMesh* Mesh, UMaterialInterface* Material, bool bCollision, bool bShadow)
{
	Piece->SetupAttachment(Parent);
	Piece->SetMobility(EComponentMobility::Static);
	Piece->SetStaticMesh(Mesh);
	Piece->SetMaterial(0, Material);
	Piece->SetCastShadow(bShadow);
	Piece->SetGenerateOverlapEvents(false);
	Piece->SetCanEverAffectNavigation(false);
	if (bCollision)
	{
		Piece->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	}
	else
	{
		Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

AKSArenaBuilder::AKSArenaBuilder()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	RootComponent = Root;
}

AKSArenaBuilder* AKSArenaBuilder::Find(const UObject* WorldContext)
{
	UWorld* World = (GEngine && WorldContext) ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return nullptr;
	}
	if (GKSArenaBuilder.IsValid() && GKSArenaBuilder->GetWorld() == World)
	{
		return GKSArenaBuilder.Get();
	}
	for (TActorIterator<AKSArenaBuilder> It(World); It; ++It)
	{
		GKSArenaBuilder = *It;
		return *It;
	}
	return nullptr;
}

AKSArenaBuilder* AKSArenaBuilder::SpawnArena(UWorld* World, int32 InArenaIndex)
{
	if (!World)
	{
		return nullptr;
	}
	AKSArenaBuilder* Builder = Find(World);
	if (!Builder)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Builder = World->SpawnActor<AKSArenaBuilder>(AKSArenaBuilder::StaticClass(), FTransform::Identity, Params);
	}
	if (Builder && Builder->GetArenaIndex() != InArenaIndex)
	{
		Builder->Build(InArenaIndex);
	}
	return Builder;
}

void AKSArenaBuilder::Build(int32 InArenaIndex)
{
	Clear();
	ArenaIndex = FMath::Clamp(InArenaIndex, 0, KSArena::Num() - 1);
	const FKSArenaDef& Def = KSArena::Get(ArenaIndex);
	BuildLighting(Def.Theme);
	BuildGeometry(Def);
	GKSArenaBuilder = this;
	if (const UKSGameInstance* GI = Cast<UKSGameInstance>(GetGameInstance()))
	{
		ApplyQuality(GI->GetSettings().Quality);
	}
	UE_LOG(LogKS, Log, TEXT("Built arena %d (%s): %d blocks, %d components"), ArenaIndex, *Def.Name, Def.Blocks.Num(), Pieces.Num());
}

void AKSArenaBuilder::Clear()
{
	for (UPrimitiveComponent* Piece : Pieces)
	{
		if (Piece)
		{
			Piece->DestroyComponent();
		}
	}
	Pieces.Reset();
	Materials.Reset();
	PadMaterial = nullptr;

	USceneComponent* Others[] = { Sun.Get(), SkyLight.Get(), Atmosphere.Get(), Fog.Get(), PostProcess.Get() };
	for (USceneComponent* Comp : Others)
	{
		if (Comp)
		{
			Comp->DestroyComponent();
		}
	}
	Sun = nullptr;
	SkyLight = nullptr;
	Atmosphere = nullptr;
	Fog = nullptr;
	PostProcess = nullptr;
}

void AKSArenaBuilder::BuildLighting(const FKSArenaTheme& Theme)
{
	Sun = NewObject<UDirectionalLightComponent>(this);
	Sun->SetupAttachment(Root);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetRelativeRotation(Theme.SunRotation);
	Sun->SetIntensity(Theme.SunIntensity);
	Sun->SetLightColor(Theme.SunColor);
	Sun->SetAtmosphereSunLight(true);
	Sun->SetCastShadows(true);
	Sun->SetDynamicShadowDistanceMovableLight(6500.f);
	Sun->SetDynamicShadowCascades(2);
	Sun->SetCascadeDistributionExponent(2.5f);
	Sun->RegisterComponent();

	Atmosphere = NewObject<USkyAtmosphereComponent>(this);
	Atmosphere->SetupAttachment(Root);
	// A bit more haze than the defaults gives a warmer glow around the low sun.
	Atmosphere->SetMieScatteringScale(0.006f);
	Atmosphere->SetMieAnisotropy(0.85f);
	Atmosphere->RegisterComponent();

	SkyLight = NewObject<USkyLightComponent>(this);
	SkyLight->SetupAttachment(Root);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->SourceType = ESkyLightSourceType::SLS_CapturedScene;
	SkyLight->bRealTimeCapture = false;
	SkyLight->bLowerHemisphereIsBlack = false;
	SkyLight->SetLowerHemisphereColor(Theme.FogColor * 0.25f);
	SkyLight->SetIntensity(Theme.SkyLightIntensity);
	SkyLight->RegisterComponent();
	// The first capture can happen before the atmosphere is ready, so capture again shortly after.
	RecaptureTime = Age + 0.25f;
	RecapturesLeft = 2;

	Fog = NewObject<UExponentialHeightFogComponent>(this);
	Fog->SetupAttachment(Root);
	Fog->SetFogDensity(Theme.FogDensity);
	Fog->SetFogHeightFalloff(Theme.FogFalloff);
	Fog->SetFogInscatteringColor(Theme.FogColor);
	Fog->SetStartDistance(Theme.FogStart);
	Fog->SetFogMaxOpacity(0.92f);
	Fog->RegisterComponent();

	PostProcess = NewObject<UPostProcessComponent>(this);
	PostProcess->SetupAttachment(Root);
	PostProcess->bUnbound = true;
	FPostProcessSettings& S = PostProcess->Settings;
	// Fixed exposure: the scene always looks the same, and mobile skips the eye adaptation passes.
	S.bOverride_AutoExposureMethod = true;
	S.AutoExposureMethod = AEM_Manual;
	S.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	S.AutoExposureApplyPhysicalCameraExposure = 0;
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = 0.f;
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = Theme.Bloom;
	S.bOverride_BloomThreshold = true;
	S.BloomThreshold = 0.9f;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = 0.45f;
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(1.12f, 1.12f, 1.12f, 1.f);
	S.bOverride_ColorContrast = true;
	S.ColorContrast = FVector4(1.06f, 1.06f, 1.06f, 1.f);
	PostProcess->RegisterComponent();
}

void AKSArenaBuilder::BuildGeometry(const FKSArenaDef& Def)
{
	UKSAssets* Assets = UKSAssets::Get(this);
	if (!Assets)
	{
		UE_LOG(LogKS, Error, TEXT("Arena builder: assets are not available"));
		return;
	}

	const FKSArenaTheme& Theme = Def.Theme;
	Materials.SetNum(KSMat::Count);
	for (int32 i = 0; i < KSMat::Count; ++i)
	{
		const FKSSurfaceStyle& Style = Theme.Mats[i];
		Materials[i] = KSMat::IsNeon((uint8)i) ? Assets->MakeNeon(this, Style.Color, Style.Intensity) : Assets->MakeSurface(this, Style);
	}
	PadMaterial = Materials[KSMat::NeonPad];
	PadIntensity = Theme.Mats[KSMat::NeonPad].Intensity;

	// Generated materials support instancing, so each (shape, material, collision, shadow) set is one draw call.
	// The engine's fallback material doesn't, so without them every block is its own component.
	const bool bInstanced = Assets->HasGeneratedMaterials();
	TMap<uint32, UInstancedStaticMeshComponent*> Batches;

	for (const FKSBlock& Block : Def.Blocks)
	{
		UStaticMesh* Mesh = Assets->GetMesh(Block.Shape);
		UMaterialInterface* Material = Materials.IsValidIndex(Block.Mat) ? Materials[Block.Mat].Get() : nullptr;
		const FTransform Transform(Block.Rotation, Block.Center, Block.Size / 100.f);

		if (bInstanced)
		{
			const uint32 Key = uint32(Block.Shape) | (uint32(Block.Mat) << 8) | (Block.bCollision ? (1u << 16) : 0u) | (Block.bCastShadow ? (1u << 17) : 0u);
			UInstancedStaticMeshComponent*& Batch = Batches.FindOrAdd(Key);
			if (!Batch)
			{
				Batch = NewObject<UInstancedStaticMeshComponent>(this);
				KSSetupPiece(Batch, Root, Mesh, Material, Block.bCollision, Block.bCastShadow);
			}
			Batch->AddInstance(Transform);
		}
		else
		{
			UStaticMeshComponent* Piece = NewObject<UStaticMeshComponent>(this);
			KSSetupPiece(Piece, Root, Mesh, Material, Block.bCollision, Block.bCastShadow);
			Piece->SetRelativeTransform(Transform);
			Piece->RegisterComponent();
			Pieces.Add(Piece);
		}
	}

	for (TPair<uint32, UInstancedStaticMeshComponent*>& Pair : Batches)
	{
		Pair.Value->RegisterComponent();
		Pieces.Add(Pair.Value);
	}
}

void AKSArenaBuilder::ApplyQuality(int32 Quality)
{
	if (Sun)
	{
		Sun->SetDynamicShadowCascades(Quality > 0 ? 2 : 1);
		Sun->SetDynamicShadowDistanceMovableLight(Quality > 0 ? 6500.f : 4000.f);
	}
	if (PostProcess)
	{
		PostProcess->Settings.VignetteIntensity = Quality > 0 ? 0.45f : 0.f;
	}
}

void AKSArenaBuilder::RecaptureSky()
{
	if (SkyLight)
	{
		SkyLight->RecaptureSky();
	}
}

void AKSArenaBuilder::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;

	if (RecaptureTime >= 0.f && Age >= RecaptureTime)
	{
		RecaptureSky();
		// One more capture a little later covers slow first frames on phones.
		RecaptureTime = (--RecapturesLeft > 0) ? Age + 1.25f : -1.f;
	}

	if (PadMaterial)
	{
		PadMaterial->SetScalarParameterValue(TEXT("Intensity"), PadIntensity * (0.7f + 0.3f * FMath::Sin(Age * 5.f)));
	}
}
