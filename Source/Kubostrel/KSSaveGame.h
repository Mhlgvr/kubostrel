#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "KSSaveGame.generated.h"

USTRUCT()
struct FKSSettings
{
	GENERATED_BODY()

	UPROPERTY()
	FString PlayerName;

	UPROPERTY()
	float Sensitivity = 1.f;

	UPROPERTY()
	bool bAutoFire = true;

	UPROPERTY()
	bool bAimAssist = true;

	// 0 = economy, 1 = high.
	UPROPERTY()
	int32 Quality = 1;

	UPROPERTY()
	float Volume = 0.8f;

	UPROPERTY()
	float FieldOfView = 100.f;

	UPROPERTY()
	bool bShowFPS = false;

	UPROPERTY()
	FString LastAddress;

	UPROPERTY()
	int32 HostArena = 0;

	UPROPERTY()
	int32 HostBots = 2;

	UPROPERTY()
	int32 HostFrags = 20;

	UPROPERTY()
	int32 HostMinutes = 8;

	UPROPERTY()
	int32 TrainingArena = 0;

	UPROPERTY()
	int32 TrainingBots = 3;

	// 0 = easy, 1 = normal, 2 = hard.
	UPROPERTY()
	int32 BotSkill = 1;
};

UCLASS()
class UKSSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 SaveVersion = 1;

	UPROPERTY()
	FKSSettings Settings;
};
