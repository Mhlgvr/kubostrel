#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "KSArenaData.h"
#include "KSGameMode.generated.h"

class AKSCharacter;
class AKSPickup;
class AKSBotController;
class AKSPlayerState;
class AKSGameState;

// Free-for-all deathmatch on a listen server (or offline training with bots).
UCLASS()
class AKSGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AKSGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void InitGameState() override;
	virtual void StartPlay() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual bool PlayerCanRestart_Implementation(APlayerController* Player) override;
	virtual void Tick(float DeltaSeconds) override;

	void CharacterKilled(AKSCharacter* Victim, AController* Killer, uint8 Weapon, bool bHeadshot);
	void SetPlayerName(AController* Controller, const FString& RequestedName);

	bool IsMatchPlaying() const;
	const FKSNavGraph& GetNavGraph() const { return NavGraph; }
	const TArray<TObjectPtr<AKSPickup>>& GetPickups() const { return Pickups; }
	int32 GetBotSkill() const { return BotSkill; }

private:
	void StartMatch(int32 Arena);
	void EndMatch();
	void SpawnPlayer(AController* Controller);
	FTransform ChooseSpawn(AController* Controller) const;
	void UpdateBots(AController* Leaving = nullptr);
	void AddBot();
	void RemoveBot();
	void AssignColor(AKSPlayerState* PlayerState);
	void SpawnPickups();
	void ClearPickups();
	int32 CountHumans(AController* Leaving) const;
	TArray<AController*> GetAllControllers() const;
	AKSGameState* GetKSGameState() const;
	float Now() const;

	UPROPERTY()
	TArray<TObjectPtr<AKSPickup>> Pickups;

	UPROPERTY()
	TArray<TObjectPtr<AKSBotController>> Bots;

	FKSNavGraph NavGraph;
	int32 ArenaIndex = 0;
	int32 BotCount = 2;
	int32 FragLimit = 20;
	float TimeLimit = 480.f;
	int32 BotSkill = 1;
	int32 BotNameCounter = 0;
	bool bMatchStarted = false;
	bool bUpdatingBots = false;
};
