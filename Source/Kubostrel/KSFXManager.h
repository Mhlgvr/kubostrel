#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KSFXManager.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;

// Local visual effects made of pooled glowing shapes: tracers, beams, sparks, explosions and debris.
// One per world, never replicated; every machine plays the effects it is told about.
UCLASS()
class AKSFXManager : public AActor
{
	GENERATED_BODY()

public:
	AKSFXManager();

	static AKSFXManager* Get(const UObject* WorldContext);

	// Straight glowing line that fades out.
	void Tracer(const FVector& Start, const FVector& End, const FLinearColor& Color, float Width, float Life, float Intensity = 8.f);
	// Railgun beam: wide fading glow plus a bright core.
	void Beam(const FVector& Start, const FVector& End, const FLinearColor& Color);
	// Glowing sphere that grows from StartSize to EndSize and fades.
	void Flash(const FVector& Location, const FLinearColor& Color, float StartSize, float EndSize, float Life, float Intensity = 6.f);
	void Impact(const FVector& Location, const FVector& Normal, const FLinearColor& Color, float Scale = 1.f);
	void Explosion(const FVector& Location, const FLinearColor& Color);
	// Bouncing chunks, used when a robot is destroyed.
	void Debris(const FVector& Location, const FVector& BaseVelocity, const FLinearColor& Color, int32 Count);
	// Column of light, used for spawns and pickups.
	void Pillar(const FVector& FloorLocation, const FLinearColor& Color, float Height);

	virtual void Tick(float DeltaSeconds) override;

private:
	enum class EKSFxKind : uint8
	{
		Line,
		Sphere,
		Spark,
		Chunk
	};

	struct FKSFx
	{
		bool bActive = false;
		EKSFxKind Kind = EKSFxKind::Line;
		float Age = 0.f;
		float Life = 1.f;
		FVector Location = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		FRotator Spin = FRotator::ZeroRotator;
		FVector Scale0 = FVector::OneVector;
		FVector Scale1 = FVector::OneVector;
		float Intensity = 1.f;
		float Gravity = 0.f;
		bool bSphereMesh = false;
	};

	int32 Acquire(bool bSolid, bool bSphereMesh, const FLinearColor& Color, float Soft);
	void Release(int32 Index, bool bSolid);
	void UpdateGlow(int32 Index, float DeltaSeconds);
	void UpdateChunk(int32 Index, float DeltaSeconds);
	void Spark(const FVector& Location, const FVector& Velocity, const FLinearColor& Color, float Size, float Life);

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> GlowMeshes;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> GlowMaterials;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> SolidMeshes;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SolidMaterials;

	TArray<FKSFx> GlowFx;
	TArray<FKSFx> SolidFx;
	int32 ActiveCount = 0;
};
