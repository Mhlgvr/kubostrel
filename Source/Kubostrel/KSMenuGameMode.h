#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "KSMenuGameMode.generated.h"

// The main menu: no robot, just an arena slowly flying by behind the buttons.
UCLASS()
class AKSMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AKSMenuGameMode();

	virtual void StartPlay() override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
};
