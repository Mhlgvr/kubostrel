#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KSPickup.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

// A floating health pack or weapon on a glowing pad. The server hands it out and brings it back later.
UCLASS()
class AKSPickup : public AActor
{
	GENERATED_BODY()

public:
	AKSPickup();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Before FinishSpawning.
	void SetType(uint8 InType) { Type = InType; }
	uint8 GetType() const { return Type; }
	bool IsAvailable() const { return bAvailable; }

	static float GetRespawnDelay(uint8 Type);

protected:
	UFUNCTION()
	void OnRep_Available();

private:
	void BuildVisuals();
	UStaticMeshComponent* AddPart(USceneComponent* Parent, int32 Shape, const FVector& Location, const FVector& Size, UMaterialInterface* Material, const FRotator& Rotation = FRotator::ZeroRotator);

	UPROPERTY(Replicated)
	uint8 Type = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Available)
	bool bAvailable = true;

	// Server time when the item comes back.
	float RespawnAt = 0.f;
	float Age = 0.f;
	bool bVisualsBuilt = false;

	UPROPERTY()
	TObjectPtr<USceneComponent> Root;

	UPROPERTY()
	TObjectPtr<USceneComponent> ItemRoot;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Halo;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> RingMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> HaloMaterial;
};
