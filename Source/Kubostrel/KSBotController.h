#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "KSBotController.generated.h"

class AKSCharacter;
class AKSPickup;

// A simple deathmatch bot: walks the arena's waypoint graph to pickups, fights whoever it sees
// and aims like a person would, with reaction time, limited turn speed and some aim wobble.
UCLASS()
class AKSBotController : public AAIController
{
	GENERATED_BODY()

public:
	AKSBotController();

	virtual void Tick(float DeltaSeconds) override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void UpdateControlRotation(float DeltaTime, bool bUpdatePawn = true) override;

private:
	struct FKSBotSkill
	{
		float Reaction;
		float AimError;
		float TurnRate;
		float StrafeChance;
	};

	AKSCharacter* GetBot() const;
	const FKSBotSkill& GetSkill() const;
	float GetTime() const;

	void UpdateTarget(AKSCharacter* Bot);
	bool CanSee(AKSCharacter* Bot, AKSCharacter* Other) const;
	void ChooseWeapon(AKSCharacter* Bot, float Distance);
	void Fight(AKSCharacter* Bot, AKSCharacter* Target, float DeltaSeconds);
	void Roam(AKSCharacter* Bot, float DeltaSeconds);
	void PickGoal(AKSCharacter* Bot);
	bool PathTo(AKSCharacter* Bot, const FVector& Goal);
	void FollowPath(AKSCharacter* Bot);
	bool IsSafeToMove(AKSCharacter* Bot, const FVector& Direction) const;
	void CheckStuck(AKSCharacter* Bot, float DeltaSeconds);

	TWeakObjectPtr<AKSCharacter> Enemy;
	FVector LastSeenLocation = FVector::ZeroVector;
	float LastSeenTime = -100.f;
	float FirstSeenTime = -100.f;
	float NextTargetCheck = 0.f;
	float NextWeaponCheck = 0.f;

	FRotator DesiredAim = FRotator::ZeroRotator;
	FRotator AimWobble = FRotator::ZeroRotator;
	FRotator AimWobbleTarget = FRotator::ZeroRotator;
	float NextWobble = 0.f;

	TArray<int32> Path;
	int32 PathIndex = 0;
	FVector GoalLocation = FVector::ZeroVector;
	bool bHasGoal = false;
	float GoalTime = 0.f;

	float StrafeDir = 1.f;
	float NextStrafeChange = 0.f;
	float NextJumpCheck = 0.f;

	FVector StuckCheckLocation = FVector::ZeroVector;
	float StuckTimer = 0.f;
	int32 StuckCount = 0;
};
