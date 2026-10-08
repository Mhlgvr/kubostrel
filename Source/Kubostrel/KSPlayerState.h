#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "KSPlayerState.generated.h"

UCLASS()
class AKSPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AKSPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	FLinearColor GetColor() const;
	void ResetScore();

	UPROPERTY(Replicated)
	int32 Kills = 0;

	UPROPERTY(Replicated)
	int32 Deaths = 0;

	UPROPERTY(Replicated)
	int32 ColorIndex = 0;

	// Kills in a row without dying.
	UPROPERTY(Replicated)
	int32 Streak = 0;

	// Server time when a dead player comes back; 0 while alive.
	UPROPERTY(Replicated)
	float RespawnTime = 0.f;

	// Server only: the "joined the game" notice was already shown.
	bool bAnnounced = false;
};
