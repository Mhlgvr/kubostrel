#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Templates/Function.h"
#include "KSArenaData.h"
#include "KSAssets.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture2D;

// Meshes, materials and UI textures shared by the whole game. Lives on the game instance.
UCLASS()
class UKSAssets : public UObject
{
	GENERATED_BODY()

public:
	void Initialize();

	static UKSAssets* Get(const UObject* WorldContext);

	UStaticMesh* GetMesh(EKSShape Shape) const;

	// True when the neon materials made by Content/Python/ks_content.py are present.
	bool HasGeneratedMaterials() const { return bGenerated; }

	UMaterialInstanceDynamic* MakeSurface(UObject* Outer, const FKSSurfaceStyle& Style);
	UMaterialInstanceDynamic* MakeNeon(UObject* Outer, const FLinearColor& Color, float Intensity);
	UMaterialInstanceDynamic* MakeGlow(UObject* Outer, const FLinearColor& Color, float Intensity, float Opacity);
	UMaterialInstanceDynamic* MakeCharacter(UObject* Outer, const FLinearColor& Color, float Rim);

	// Shared instances for effects that never change their parameters.
	UMaterialInstanceDynamic* SharedNeon(const FLinearColor& Color, float Intensity);
	UMaterialInstanceDynamic* SharedSurface(const FLinearColor& Color, float Roughness, float Metallic);

	UPROPERTY()
	TObjectPtr<UTexture2D> CircleTexture;

	UPROPERTY()
	TObjectPtr<UTexture2D> RingTexture;

	UPROPERTY()
	TObjectPtr<UTexture2D> SoftTexture;

	UPROPERTY()
	TObjectPtr<UTexture2D> VignetteTexture;

	UPROPERTY()
	TObjectPtr<UTexture2D> ArrowTexture;

private:
	UTexture2D* MakeTexture(int32 Size, TFunctionRef<float(float, float)> AlphaFn);

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> SurfaceMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> NeonMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> GlowMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> CharacterMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> FallbackMaterial;

	UPROPERTY()
	TMap<uint32, TObjectPtr<UMaterialInstanceDynamic>> SharedCache;

	bool bGenerated = false;
};
