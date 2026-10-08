#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KSArenaData.h"
#include "KSArenaBuilder.generated.h"

class USceneComponent;
class UPrimitiveComponent;
class UMaterialInstanceDynamic;
class UDirectionalLightComponent;
class USkyLightComponent;
class USkyAtmosphereComponent;
class UExponentialHeightFogComponent;
class UPostProcessComponent;

// Builds the arena described by KSArena::Get() out of basic shapes, plus sun, sky, fog and post-processing.
// Not replicated: the server and every client build the same arena locally from its index.
UCLASS()
class AKSArenaBuilder : public AActor
{
	GENERATED_BODY()

public:
	AKSArenaBuilder();

	static AKSArenaBuilder* SpawnArena(UWorld* World, int32 ArenaIndex);
	// The arena builder in this world, if there is one.
	static AKSArenaBuilder* Find(const UObject* WorldContext);

	void Build(int32 InArenaIndex);
	int32 GetArenaIndex() const { return ArenaIndex; }
	const FKSArenaDef& GetDef() const { return KSArena::Get(ArenaIndex); }

	// 0 = economy, 1 = high. Changes shadows and post-processing cost.
	void ApplyQuality(int32 Quality);

	virtual void Tick(float DeltaSeconds) override;

private:
	void Clear();
	void BuildLighting(const FKSArenaTheme& Theme);
	void BuildGeometry(const FKSArenaDef& Def);
	void RecaptureSky();

	UPROPERTY()
	TObjectPtr<USceneComponent> Root;

	UPROPERTY()
	TObjectPtr<UDirectionalLightComponent> Sun;

	UPROPERTY()
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY()
	TObjectPtr<USkyAtmosphereComponent> Atmosphere;

	UPROPERTY()
	TObjectPtr<UExponentialHeightFogComponent> Fog;

	UPROPERTY()
	TObjectPtr<UPostProcessComponent> PostProcess;

	UPROPERTY()
	TArray<TObjectPtr<UPrimitiveComponent>> Pieces;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;

	// Pad glow pieces, animated every frame.
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> PadMaterial;

	int32 ArenaIndex = INDEX_NONE;
	float PadIntensity = 8.f;
	float RecaptureTime = -1.f;
	int32 RecapturesLeft = 0;
	float Age = 0.f;
};
