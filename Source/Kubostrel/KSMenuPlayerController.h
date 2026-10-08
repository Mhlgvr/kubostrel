#pragma once

#include "CoreMinimal.h"
#include "KSBasePlayerController.h"
#include "KSMenuPlayerController.generated.h"

class ACameraActor;

UCLASS()
class AKSMenuPlayerController : public AKSBasePlayerController
{
	GENERATED_BODY()

public:
	AKSMenuPlayerController();

	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;

private:
	UPROPERTY()
	TObjectPtr<ACameraActor> MenuCamera;

	float OrbitAngle = 0.f;
};
