#pragma once

#include "CoreMinimal.h"
#include "KSBasePlayerController.h"
#include "KSUI.h"
#include "KSPlayerController.generated.h"

class AKSCharacter;
class APlayerState;

struct FKSDamageMark
{
	FVector From = FVector::ZeroVector;
	float Amount = 0.f;
	float Time = 0.f;
};

// The match controller: on-screen touch controls (or mouse and keyboard), aim help, recoil
// and the short-lived messages the HUD shows.
UCLASS()
class AKSPlayerController : public AKSBasePlayerController
{
	GENERATED_BODY()

public:
	AKSPlayerController();

	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;

	UFUNCTION(Client, Reliable)
	void ClientDamaged(FVector_NetQuantize FromLocation, float Amount);

	// Only rockets: bullets show their hit marker on the shooter's side right away.
	UFUNCTION(Client, Reliable)
	void ClientHitConfirmed(bool bHeadshot);

	UFUNCTION(Client, Reliable)
	void ClientPickupNotice(uint8 Type);

	UFUNCTION(Server, Reliable)
	void ServerSetPlayerName(const FString& Name);

	void AddRecoil(float Pitch);
	void NotifyLocalHit(bool bHeadshot);
	void OnKillFeed(APlayerState* Killer, APlayerState* Victim, uint8 Weapon, bool bHeadshot, int32 Streak);

	void SetMenuOpen(bool bOpen);
	bool IsMenuOpen() const { return bMenuOpen; }
	void LeaveMatch();

	AKSCharacter* GetKSCharacter() const;
	const FKSTouchLayout& GetLayout() const { return Layout; }
	float GetLocalTime() const;

	// ---- State shown by the HUD ----
	bool bMoveActive = false;
	FVector2D MoveBase = FVector2D::ZeroVector;
	FVector2D MoveKnob = FVector2D::ZeroVector;
	bool bFireHeld = false;
	bool bJumpHeld = false;
	bool bScoreHeld = false;
	bool bAutoFiring = false;
	bool bAimOnTarget = false;

	float HitMarkerTime = -100.f;
	bool bHitMarkerHead = false;
	float KillMarkerTime = -100.f;
	float DamageTime = -100.f;
	TArray<FKSDamageMark> DamageMarks;

	FString CenterText;
	FString CenterSubText;
	FLinearColor CenterColor = FLinearColor::White;
	float CenterTime = -100.f;

	FString PickupText;
	FLinearColor PickupColor = FLinearColor::White;
	float PickupTime = -100.f;

	// Who destroyed us last, for the death screen.
	FString KillerName;
	FLinearColor KillerColor = FLinearColor::White;
	uint8 KillerWeapon = 255;

private:
	enum class EKSTouchRole : uint8
	{
		None,
		Move,
		Look,
		Fire,
		Jump,
		Score,
		Ignored
	};

	void HandleTouch(float DeltaTime, AKSCharacter* Robot, bool bCanAct, FVector2D& OutMove, FVector2D& OutLook, bool& bOutFire);
	void HandleDesktop(float DeltaTime, AKSCharacter* Robot, bool bCanAct, FVector2D& OutMove, FVector2D& OutLook, bool& bOutFire);
	EKSTouchRole ClaimPointer(const FKSPointer& Pointer, AKSCharacter* Robot, bool bCanAct);
	void ApplyLook(float DeltaYaw, float DeltaPitch);
	void UpdateAimAssist(float DeltaTime, AKSCharacter* Robot, bool bActive);
	void UpdateRecoil(float DeltaTime, AKSCharacter* Robot);
	bool IsCrosshairOnEnemy(AKSCharacter* Robot) const;
	void ShowCenter(const FString& Text, const FString& SubText, const FLinearColor& Color);

	FKSTouchLayout Layout;
	TMap<int32, EKSTouchRole> PointerRoles;
	bool bMenuOpen = false;
	bool bNameSent = false;
	TWeakObjectPtr<APawn> LastPawn;

	float RecoilToApply = 0.f;
	float RecoilToRecover = 0.f;
	float RecoilYaw = 0.f;
	float AutoFireUntil = 0.f;
	float NextAimScan = 0.f;
	float LookFriction = 1.f;
	TWeakObjectPtr<AKSCharacter> AimTarget;
};
