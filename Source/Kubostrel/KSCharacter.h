#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "KSTypes.h"
#include "KSCharacter.generated.h"

class UCameraComponent;
class USceneComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class AKSPlayerState;
class AKSPlayerController;

// The player robot. Shots are traced on the shooter's machine and checked by the server,
// so hits feel instant on a phone even with some network lag.
UCLASS()
class AKSCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AKSCharacter(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	virtual void Landed(const FHitResult& Hit) override;
	virtual void FellOutOfWorld(const UDamageType& DamageType) override;

	// ---- Controls, called on the machine that controls this robot ----
	void SetWantsToFire(bool bFire) { bWantsToFire = bFire; }
	bool WantsToFire() const { return bWantsToFire; }
	void SelectWeapon(int32 Weapon);
	void CycleWeapon(int32 Direction);
	void StartReload();
	void DoJump();

	// ---- State ----
	bool IsAlive() const { return !bDead; }
	bool IsFrozen() const { return bFrozen; }
	float GetHealth() const { return Health; }
	int32 GetWeapon() const { return CurrentWeapon; }
	bool HasWeapon(int32 Weapon) const;
	// Rifle: rounds in the magazine. Other weapons: carried ammo.
	int32 GetAmmo(int32 Weapon) const;
	bool IsReloading() const { return bReloading; }
	float GetReloadProgress() const;
	bool IsSwitching() const;
	float GetSpreadDegrees() const;
	bool IsSpawnProtected() const;
	float GetTimeSinceDeath() const;
	float GetTimeSinceFire() const;
	FLinearColor GetPlayerColor() const;
	FVector GetEyeLocation() const;
	FRotator GetAimRotation() const;
	AKSPlayerState* GetKSPlayerState() const;
	// A human player whose camera is this robot (first person view on this machine).
	bool IsLocalHuman() const;

	// ---- Server ----
	void ApplyDamageFrom(float Amount, AController* InstigatorController, uint8 Weapon, bool bHeadshot, const FVector& FromLocation);
	void ApplyKnockback(const FVector& Velocity);
	bool TryTakePickup(uint8 Type);
	void SetFrozen(bool bFreeze);

protected:
	UFUNCTION(Server, Reliable)
	void ServerFire(uint8 Weapon, FVector_NetQuantize Origin, FVector_NetQuantizeNormal Direction, const TArray<FKSShotHit>& Hits, const TArray<FVector_NetQuantize>& Ends, uint16 HitMask);

	UFUNCTION(Server, Reliable)
	void ServerFireRocket(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Direction);

	UFUNCTION(Server, Reliable)
	void ServerSelectWeapon(uint8 Weapon);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastShotFX(uint8 Weapon, FVector_NetQuantize Origin, const TArray<FVector_NetQuantize>& Ends, uint16 HitMask);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastHurt(FVector_NetQuantize FromLocation, bool bHeadshot);

	UFUNCTION(Client, Reliable)
	void ClientKnockback(FVector_NetQuantize10 Velocity);

	UFUNCTION()
	void OnRep_Dead();

	UFUNCTION()
	void OnRep_Weapon();

	UFUNCTION()
	void OnRep_WeaponMask(uint8 OldMask);

private:
	void BuildBody();
	void BuildViewModels();
	UStaticMeshComponent* AddPart(USceneComponent* Parent, int32 Shape, const FVector& Location, const FVector& Size, UMaterialInterface* Material,
		const FRotator& Rotation = FRotator::ZeroRotator, bool bFirstPerson = false, bool bShadow = true);
	void UpdateColors();
	void ApplyWeaponVisuals();
	void OnWeaponGained(int32 Weapon);

	void TickWeapon(float DeltaSeconds);
	void FireOnce();
	void FireHitscan(const FVector& Origin, const FVector& Direction);
	void ProcessShot(uint8 Weapon, const FVector& Origin, const TArray<FKSShotHit>& Hits);
	void PlayShotFX(uint8 Weapon, const FVector& Origin, const TArray<FVector_NetQuantize>& Ends, uint16 HitMask);
	void SpawnRocket(const FVector& Origin, const FVector& Direction);
	bool ConsumeServerShot(uint8 Weapon);
	void EndSpawnProtection();
	void Die(AController* Killer, uint8 Weapon, bool bHeadshot);
	int32 BestWeapon() const;

	void TickBody(float DeltaSeconds);
	void TickViewModel(float DeltaSeconds);
	void TickCamera(float DeltaSeconds);
	void TickJumpPads();
	void TickFootsteps(float DeltaSeconds);
	FVector GetMuzzleLocation() const;
	float GetWorldTime() const;
	float GetServerTime() const;

	// ---- Replicated ----
	UPROPERTY(Replicated)
	float Health = 100.f;

	UPROPERTY(ReplicatedUsing = OnRep_Dead)
	bool bDead = false;

	UPROPERTY(Replicated)
	bool bFrozen = false;

	UPROPERTY(ReplicatedUsing = OnRep_Weapon)
	uint8 CurrentWeapon = 0;

	UPROPERTY(ReplicatedUsing = OnRep_WeaponMask)
	uint8 WeaponMask = 1;

	// Ammo for shotgun, rail and rocket launcher.
	UPROPERTY(Replicated)
	uint8 SpecialAmmo[3];

	// Server time until which damage is ignored.
	UPROPERTY(Replicated)
	float SpawnProtectedUntil = 0.f;

	// ---- Local state ----
	bool bWantsToFire = false;
	int32 RifleMag = 30;
	bool bReloading = false;
	float ReloadEndTime = 0.f;
	float SwitchEndTime = 0.f;
	float NextFireTime = 0.f;
	float LastFireTime = -100.f;
	float RecentShots = 0.f;
	float DeathTime = 0.f;
	float NextPadTime = 0.f;
	float StepDistance = 0.f;
	float WalkCycle = 0.f;
	float LandingDip = 0.f;
	float ViewKick = 0.f;
	float HitFlash = 0.f;
	float MuzzleFlashTime = -100.f;
	float JumpReleaseTime = 0.f;
	FRotator DeathView = FRotator::ZeroRotator;
	FRotator LastViewRotation = FRotator::ZeroRotator;
	FVector2D ViewSway = FVector2D::ZeroVector;
	int32 AppliedColorIndex = -1;
	bool bViewModelsBuilt = false;
	bool bSpawnFxDone = false;

	// Server-side fire rate check: one credit per weapon interval, a few can be banked.
	float ServerFireCredit = 1.f;
	float ServerLastFireTime = -100.f;

	// ---- Components ----
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY()
	TObjectPtr<USceneComponent> BodyRoot;

	UPROPERTY()
	TObjectPtr<USceneComponent> HeadPivot;

	UPROPERTY()
	TObjectPtr<USceneComponent> ArmsPivot;

	UPROPERTY()
	TObjectPtr<USceneComponent> LegLeft;

	UPROPERTY()
	TObjectPtr<USceneComponent> LegRight;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> GunBody;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> GunGlow;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> MuzzleThird;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Shield;

	UPROPERTY()
	TObjectPtr<USceneComponent> ViewRoot;

	UPROPERTY()
	TArray<TObjectPtr<USceneComponent>> ViewModels;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> MuzzleFirst;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> ArmorMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> NeonMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> GunGlowMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> ShieldMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> MuzzleMaterial;
};
