#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KSRocket.generated.h"

class AKSCharacter;
class USceneComponent;
class UStaticMeshComponent;

// A rocket flies in a straight line at a fixed speed, so every machine can work out where it is
// from where and when it was launched. Only the server decides when and where it explodes.
UCLASS()
class AKSRocket : public AActor
{
	GENERATED_BODY()

public:
	AKSRocket();

	// Server, before FinishSpawning.
	void InitRocket(AKSCharacter* Shooter, const FVector& InOrigin, const FVector& InDirection);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION(NetMulticast, Reliable)
	void MulticastExplode(FVector_NetQuantize Location);

private:
	FVector GetPositionAt(float ServerTime) const;
	float GetServerTime() const;
	void Explode(const FVector& Location);
	void BuildVisuals();
	void HideVisuals();

	UPROPERTY(Replicated)
	FVector_NetQuantize Origin;

	UPROPERTY(Replicated)
	FVector_NetQuantizeNormal Direction;

	UPROPERTY(Replicated)
	float LaunchTime = 0.f;

	TWeakObjectPtr<AController> ShooterController;
	TWeakObjectPtr<AKSCharacter> ShooterPawn;
	FVector LastPosition = FVector::ZeroVector;
	FVector LastTrailPosition = FVector::ZeroVector;
	float TrailTimer = 0.f;
	bool bExploded = false;
	bool bHiddenByWall = false;

	UPROPERTY()
	TObjectPtr<USceneComponent> Root;

	UPROPERTY()
	TObjectPtr<USceneComponent> Visual;
};
