#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "KSGameState.generated.h"

class AKSPlayerState;

UENUM()
enum class EKSMatchPhase : uint8
{
	Playing,
	Ended
};

// One line of the kill feed: "Left  Middle  Right", for example "Neo  [Rail]  Bot Volt".
struct FKSFeedEntry
{
	FString Left;
	FString Middle;
	FString Right;
	FLinearColor LeftColor = FLinearColor::White;
	FLinearColor MiddleColor = FLinearColor::White;
	FLinearColor RightColor = FLinearColor::White;
	bool bHeadshot = false;
	float Time = 0.f;
};

UCLASS()
class AKSGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AKSGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Server: builds the arena here and on every client.
	void SetArena(int32 Index);
	// Server: switches the match phase and plays its effects on the host too.
	void SetPhase(EKSMatchPhase Phase, float EndTime, APlayerState* InWinner);

	// Weapon is 255 when nobody shot the victim (a fall). Streak is the killer's new kill streak.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastKill(APlayerState* Killer, APlayerState* Victim, uint8 Weapon, bool bHeadshot, uint8 Streak);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastNotice(const FString& Name, int32 ColorIndex, bool bJoined);

	float GetServerTime() const { return (float)GetServerWorldTimeSeconds(); }
	// Seconds until the match ends (while playing) or until the next match (after it ended).
	float GetTimeLeft() const;
	bool IsPlaying() const { return MatchPhase == EKSMatchPhase::Playing; }
	const TArray<FKSFeedEntry>& GetFeed() const { return Feed; }
	// Players sorted by kills, then by fewer deaths.
	TArray<AKSPlayerState*> GetRanking() const;

	UPROPERTY(ReplicatedUsing = OnRep_ArenaIndex)
	int32 ArenaIndex = INDEX_NONE;

	UPROPERTY(ReplicatedUsing = OnRep_MatchPhase)
	EKSMatchPhase MatchPhase = EKSMatchPhase::Playing;

	UPROPERTY(Replicated)
	float PhaseEndTime = 0.f;

	UPROPERTY(Replicated)
	int32 FragLimit = 20;

	UPROPERTY(Replicated)
	TObjectPtr<APlayerState> Winner;

	// Local time of the last phase change, for HUD animations.
	float PhaseChangeTime = -100.f;

protected:
	UFUNCTION()
	void OnRep_ArenaIndex();

	UFUNCTION()
	void OnRep_MatchPhase();

private:
	void AddFeed(const FKSFeedEntry& Entry);

	TArray<FKSFeedEntry> Feed;
};
