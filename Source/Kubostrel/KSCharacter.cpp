#include "KSCharacter.h"
#include "Kubostrel.h"
#include "KSArenaBuilder.h"
#include "KSArenaData.h"
#include "KSAssets.h"
#include "KSAudio.h"
#include "KSFXManager.h"
#include "KSGameInstance.h"
#include "KSGameMode.h"
#include "KSGameState.h"
#include "KSPlayerController.h"
#include "KSPlayerState.h"
#include "KSRocket.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

namespace
{
	// First person weapon positions relative to the camera, and muzzle positions relative to each weapon.
	const FVector KSViewOffset[KSWeapon::Count] =
	{
		FVector(30.f, 11.f, -11.f),
		FVector(30.f, 12.f, -12.f),
		FVector(28.f, 11.f, -11.f),
		FVector(24.f, 14.f, -14.f),
	};
	const FVector KSMuzzleOffset[KSWeapon::Count] =
	{
		FVector(34.f, 0.f, 1.f),
		FVector(37.f, 0.f, 2.f),
		FVector(46.f, 0.f, 1.f),
		FVector(36.f, 0.f, 2.f),
	};
	constexpr int32 KSCube = (int32)EKSShape::Cube;
	constexpr int32 KSCylinder = (int32)EKSShape::Cylinder;
	constexpr int32 KSSphere = (int32)EKSShape::Sphere;
	// Cylinders stand along Z; this lays them along X.
	const FRotator KSAlongX(90.f, 0.f, 0.f);
}

AKSCharacter::AKSCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	SpecialAmmo[0] = 0;
	SpecialAmmo[1] = 0;
	SpecialAmmo[2] = 0;

	GetCapsuleComponent()->InitCapsuleSize(KS::CapsuleRadius, KS::CapsuleHalfHeight);
	BaseEyeHeight = KS::EyeHeight;
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	JumpMaxHoldTime = 0.f;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->MaxWalkSpeed = 640.f;
	Move->MaxAcceleration = 3200.f;
	Move->BrakingDecelerationWalking = 2800.f;
	Move->GroundFriction = 9.f;
	Move->JumpZVelocity = 640.f;
	Move->AirControl = 0.45f;
	Move->GravityScale = KS::GravityScale;
	Move->MaxStepHeight = 50.f;
	Move->SetWalkableFloorAngle(46.f);
	Move->bOrientRotationToMovement = false;
	Move->bCanWalkOffLedges = true;
	// Jump pads compute an exact arc, so nothing should slow players down in the air.
	Move->FallingLateralFriction = 0.f;
	Move->BrakingDecelerationFalling = 0.f;
	Move->NavAgentProps.bCanCrouch = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(0.f, 0.f, KS::EyeHeight));
	Camera->bUsePawnControlRotation = true;
	Camera->SetFieldOfView(100.f);

	// The robot is built from simple shapes at runtime; the skeletal mesh slot stays empty.
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->PrimaryComponentTick.bStartWithTickEnabled = false;
	GetMesh()->SetVisibility(false);
}

void AKSCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AKSCharacter, Health, COND_OwnerOnly);
	DOREPLIFETIME(AKSCharacter, bDead);
	DOREPLIFETIME(AKSCharacter, bFrozen);
	// The owner switches weapons instantly on its own; everyone else follows the server.
	DOREPLIFETIME_CONDITION(AKSCharacter, CurrentWeapon, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AKSCharacter, WeaponMask, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AKSCharacter, SpecialAmmo, COND_OwnerOnly);
	DOREPLIFETIME(AKSCharacter, SpawnProtectedUntil);
}

float AKSCharacter::GetWorldTime() const
{
	return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}

float AKSCharacter::GetServerTime() const
{
	const AKSGameState* GS = GetWorld() ? GetWorld()->GetGameState<AKSGameState>() : nullptr;
	return GS ? GS->GetServerTime() : GetWorldTime();
}

AKSPlayerState* AKSCharacter::GetKSPlayerState() const
{
	return GetPlayerState<AKSPlayerState>();
}

bool AKSCharacter::IsLocalHuman() const
{
	return IsLocallyControlled() && IsPlayerControlled();
}

FLinearColor AKSCharacter::GetPlayerColor() const
{
	const AKSPlayerState* PS = GetKSPlayerState();
	return PS ? PS->GetColor() : FLinearColor(0.8f, 0.85f, 0.9f);
}

FVector AKSCharacter::GetEyeLocation() const
{
	return GetActorLocation() + FVector(0.f, 0.f, BaseEyeHeight);
}

FRotator AKSCharacter::GetAimRotation() const
{
	return Controller ? Controller->GetControlRotation() : GetBaseAimRotation();
}

bool AKSCharacter::HasWeapon(int32 Weapon) const
{
	return Weapon == KSWeapon::Rifle || (Weapon > 0 && Weapon < KSWeapon::Count && (WeaponMask & (1 << Weapon)) != 0);
}

int32 AKSCharacter::GetAmmo(int32 Weapon) const
{
	if (Weapon == KSWeapon::Rifle)
	{
		return RifleMag;
	}
	return (Weapon > 0 && Weapon < KSWeapon::Count) ? SpecialAmmo[Weapon - 1] : 0;
}

float AKSCharacter::GetReloadProgress() const
{
	if (!bReloading)
	{
		return 0.f;
	}
	return FMath::Clamp(1.f - (ReloadEndTime - GetWorldTime()) / KS::RifleReloadTime, 0.f, 1.f);
}

bool AKSCharacter::IsSwitching() const
{
	return GetWorldTime() < SwitchEndTime;
}

bool AKSCharacter::IsSpawnProtected() const
{
	return !bDead && GetServerTime() < SpawnProtectedUntil;
}

float AKSCharacter::GetTimeSinceDeath() const
{
	return bDead ? GetWorldTime() - DeathTime : 0.f;
}

float AKSCharacter::GetTimeSinceFire() const
{
	return GetWorldTime() - LastFireTime;
}

float AKSCharacter::GetSpreadDegrees() const
{
	const FKSWeaponInfo& Info = KS::Weapon(CurrentWeapon);
	const float Move = FMath::Clamp(GetVelocity().Size2D() / 640.f, 0.f, 1.f);
	float Spread = FMath::Lerp(Info.SpreadDeg, Info.MoveSpreadDeg, Move);
	if (!GetCharacterMovement()->IsMovingOnGround())
	{
		Spread += 1.5f;
	}
	if (CurrentWeapon == KSWeapon::Rifle)
	{
		Spread += FMath::Min(RecentShots, 8.f) * 0.18f;
	}
	return Spread;
}

// ---------------------------------------------------------------------------------------------
// Construction of the robot and the first person weapons
// ---------------------------------------------------------------------------------------------

static USceneComponent* KSNewPivot(AActor* Owner, USceneComponent* Parent, const FVector& Location)
{
	USceneComponent* Pivot = NewObject<USceneComponent>(Owner);
	Pivot->SetupAttachment(Parent);
	Pivot->SetRelativeLocation(Location);
	Pivot->RegisterComponent();
	return Pivot;
}

UStaticMeshComponent* AKSCharacter::AddPart(USceneComponent* Parent, int32 Shape, const FVector& Location, const FVector& Size, UMaterialInterface* Material,
	const FRotator& Rotation, bool bFirstPerson, bool bShadow)
{
	UKSAssets* Assets = UKSAssets::Get(this);
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
	Part->SetupAttachment(Parent);
	Part->SetStaticMesh(Assets ? Assets->GetMesh((EKSShape)Shape) : nullptr);
	Part->SetMaterial(0, Material);
	Part->SetRelativeLocationAndRotation(Location, Rotation);
	Part->SetRelativeScale3D(Size / 100.f);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetGenerateOverlapEvents(false);
	Part->SetCanEverAffectNavigation(false);
	if (bFirstPerson)
	{
		Part->SetOnlyOwnerSee(true);
		Part->SetCastShadow(false);
	}
	else
	{
		// Hidden from our own camera, but we still see our shadow.
		Part->SetOwnerNoSee(true);
		Part->SetCastShadow(bShadow);
		Part->SetCastHiddenShadow(bShadow);
	}
	Part->RegisterComponent();
	return Part;
}

void AKSCharacter::BuildBody()
{
	UKSAssets* Assets = UKSAssets::Get(this);
	if (!Assets || BodyRoot)
	{
		return;
	}
	const FLinearColor Color = GetPlayerColor();
	ArmorMaterial = Assets->MakeCharacter(this, Color, 1.6f);
	NeonMaterial = Assets->MakeNeon(this, Color, 5.f);
	GunGlowMaterial = Assets->MakeNeon(this, KS::Weapon(CurrentWeapon).Color, 6.f);
	ShieldMaterial = Assets->MakeGlow(this, Color, 1.2f, 0.3f);
	MuzzleMaterial = Assets->MakeGlow(this, FLinearColor(1.f, 0.85f, 0.55f), 10.f, 1.f);
	UMaterialInterface* Metal = Assets->SharedSurface(FLinearColor(0.05f, 0.055f, 0.065f), 0.3f, 0.7f);
	const FRotator Zero = FRotator::ZeroRotator;

	BodyRoot = KSNewPivot(this, GetCapsuleComponent(), FVector::ZeroVector);
	AddPart(BodyRoot, KSCube, FVector(0.f, 0.f, -6.f), FVector(30.f, 40.f, 18.f), ArmorMaterial);

	for (const float Side : { -1.f, 1.f })
	{
		USceneComponent* Leg = KSNewPivot(this, BodyRoot, FVector(0.f, Side * 12.f, -12.f));
		if (Side < 0.f)
		{
			LegLeft = Leg;
		}
		else
		{
			LegRight = Leg;
		}
		AddPart(Leg, KSCube, FVector(0.f, 0.f, -38.f), FVector(16.f, 16.f, 72.f), ArmorMaterial);
		AddPart(Leg, KSCube, FVector(5.f, 0.f, -72.f), FVector(26.f, 17.f, 9.f), ArmorMaterial);
		AddPart(Leg, KSCube, FVector(0.f, 0.f, -36.f), FVector(17.5f, 17.5f, 4.f), NeonMaterial, Zero, false, false);
	}

	AddPart(BodyRoot, KSCube, FVector(0.f, 0.f, 26.f), FVector(32.f, 50.f, 46.f), ArmorMaterial);
	AddPart(BodyRoot, KSCube, FVector(16.5f, 0.f, 34.f), FVector(2.f, 34.f, 5.f), NeonMaterial, Zero, false, false);
	AddPart(BodyRoot, KSCube, FVector(-20.f, 0.f, 26.f), FVector(14.f, 34.f, 30.f), ArmorMaterial);
	AddPart(BodyRoot, KSCube, FVector(-27.5f, 0.f, 30.f), FVector(2.f, 26.f, 4.f), NeonMaterial, Zero, false, false);
	for (const float Side : { -1.f, 1.f })
	{
		AddPart(BodyRoot, KSCube, FVector(0.f, Side * 32.f, 40.f), FVector(20.f, 18.f, 18.f), ArmorMaterial);
		AddPart(BodyRoot, KSCube, FVector(0.f, Side * 32.f, 46.f), FVector(21.f, 19.f, 3.f), NeonMaterial, Zero, false, false);
	}

	HeadPivot = KSNewPivot(this, BodyRoot, FVector(0.f, 0.f, 52.f));
	AddPart(HeadPivot, KSCube, FVector(0.f, 0.f, 14.f), FVector(28.f, 26.f, 26.f), ArmorMaterial);
	AddPart(HeadPivot, KSCube, FVector(14.5f, 0.f, 16.f), FVector(3.f, 22.f, 7.f), NeonMaterial, Zero, false, false);
	AddPart(HeadPivot, KSCylinder, FVector(-8.f, 9.f, 33.f), FVector(3.f, 3.f, 14.f), ArmorMaterial);
	AddPart(HeadPivot, KSSphere, FVector(-8.f, 9.f, 41.f), FVector(6.f, 6.f, 6.f), NeonMaterial, Zero, false, false);

	ArmsPivot = KSNewPivot(this, BodyRoot, FVector(0.f, 0.f, 38.f));
	AddPart(ArmsPivot, KSCube, FVector(14.f, 28.f, -8.f), FVector(34.f, 11.f, 11.f), ArmorMaterial, FRotator(0.f, -12.f, 0.f));
	AddPart(ArmsPivot, KSCube, FVector(20.f, -14.f, -10.f), FVector(40.f, 11.f, 11.f), ArmorMaterial, FRotator(0.f, 28.f, 0.f));
	GunBody = AddPart(ArmsPivot, KSCube, FVector(36.f, 14.f, -10.f), FVector(52.f, 9.f, 12.f), Metal);
	GunGlow = AddPart(ArmsPivot, KSCube, FVector(36.f, 19.f, -8.f), FVector(42.f, 1.5f, 2.5f), GunGlowMaterial, Zero, false, false);
	MuzzleThird = AddPart(ArmsPivot, KSSphere, FVector(64.f, 14.f, -9.f), FVector(18.f), MuzzleMaterial, Zero, false, false);
	MuzzleThird->SetVisibility(false);

	Shield = AddPart(BodyRoot, KSSphere, FVector::ZeroVector, FVector(115.f, 115.f, 205.f), ShieldMaterial, Zero, false, false);
	Shield->SetVisibility(false);

	AppliedColorIndex = INDEX_NONE;
	UpdateColors();
	ApplyWeaponVisuals();
}

void AKSCharacter::BuildViewModels()
{
	bViewModelsBuilt = true;
	UKSAssets* Assets = UKSAssets::Get(this);
	if (!Assets || !Camera)
	{
		return;
	}
	UMaterialInterface* Metal = Assets->SharedSurface(FLinearColor(0.06f, 0.065f, 0.075f), 0.28f, 0.75f);
	UMaterialInterface* Grip = Assets->SharedSurface(FLinearColor(0.015f, 0.015f, 0.02f), 0.6f, 0.1f);
	const FRotator Zero = FRotator::ZeroRotator;

	ViewRoot = KSNewPivot(this, Camera, FVector::ZeroVector);
	for (int32 W = 0; W < KSWeapon::Count; ++W)
	{
		UMaterialInterface* Neon = Assets->SharedNeon(KS::Weapon(W).Color, 6.f);
		USceneComponent* Model = KSNewPivot(this, ViewRoot, KSViewOffset[W]);
		ViewModels.Add(Model);
		auto Part = [this, Model](int32 Shape, const FVector& Location, const FVector& Size, UMaterialInterface* Material, const FRotator& Rotation)
		{
			AddPart(Model, Shape, Location, Size, Material, Rotation, true, false);
		};

		switch (W)
		{
		case KSWeapon::Rifle:
			Part(KSCube, FVector(0.f, 0.f, 0.f), FVector(30.f, 6.f, 8.f), Metal, Zero);
			Part(KSCylinder, FVector(24.f, 0.f, 1.f), FVector(3.f, 3.f, 18.f), Metal, KSAlongX);
			Part(KSCube, FVector(4.f, 0.f, -8.f), FVector(5.f, 4.5f, 11.f), Grip, FRotator(-12.f, 0.f, 0.f));
			Part(KSCube, FVector(-19.f, 0.f, -1.f), FVector(12.f, 5.f, 7.f), Grip, Zero);
			Part(KSCube, FVector(2.f, 0.f, 5.5f), FVector(7.f, 2.5f, 3.f), Metal, Zero);
			Part(KSCube, FVector(2.f, 3.2f, 1.f), FVector(22.f, 0.6f, 1.2f), Neon, Zero);
			Part(KSCube, FVector(2.f, -3.2f, 1.f), FVector(22.f, 0.6f, 1.2f), Neon, Zero);
			Part(KSCylinder, FVector(30.f, 0.f, 1.f), FVector(4.4f, 4.4f, 1.6f), Neon, KSAlongX);
			break;
		case KSWeapon::Shotgun:
			Part(KSCube, FVector(0.f, 0.f, 0.f), FVector(26.f, 8.f, 9.f), Metal, Zero);
			Part(KSCylinder, FVector(22.f, 1.9f, 2.f), FVector(3.6f, 3.6f, 26.f), Metal, KSAlongX);
			Part(KSCylinder, FVector(22.f, -1.9f, 2.f), FVector(3.6f, 3.6f, 26.f), Metal, KSAlongX);
			Part(KSCube, FVector(15.f, 0.f, -3.5f), FVector(12.f, 7.f, 5.f), Grip, Zero);
			Part(KSCube, FVector(-20.f, 0.f, -2.f), FVector(16.f, 6.f, 8.f), Grip, Zero);
			Part(KSCube, FVector(0.f, 4.3f, 2.f), FVector(20.f, 0.6f, 1.4f), Neon, Zero);
			Part(KSCube, FVector(0.f, -4.3f, 2.f), FVector(20.f, 0.6f, 1.4f), Neon, Zero);
			Part(KSCube, FVector(15.f, 0.f, -0.5f), FVector(12.5f, 7.5f, 1.f), Neon, Zero);
			break;
		case KSWeapon::Rail:
			Part(KSCube, FVector(0.f, 0.f, 0.f), FVector(36.f, 5.f, 7.f), Metal, Zero);
			Part(KSCube, FVector(28.f, 0.f, 1.f), FVector(30.f, 3.f, 3.f), Metal, Zero);
			for (const float X : { 20.f, 27.f, 34.f })
			{
				Part(KSCylinder, FVector(X, 0.f, 1.f), FVector(7.5f, 7.5f, 1.6f), Neon, KSAlongX);
			}
			Part(KSCube, FVector(26.f, 0.f, 3.2f), FVector(30.f, 1.f, 1.f), Neon, Zero);
			Part(KSCube, FVector(-22.f, 0.f, -1.f), FVector(12.f, 4.5f, 8.f), Grip, Zero);
			Part(KSCube, FVector(2.f, 0.f, -6.f), FVector(5.f, 4.f, 9.f), Grip, Zero);
			break;
		default:
			Part(KSCylinder, FVector(8.f, 0.f, 2.f), FVector(11.f, 11.f, 52.f), Metal, KSAlongX);
			Part(KSCylinder, FVector(33.f, 0.f, 2.f), FVector(12.6f, 12.6f, 3.f), Neon, KSAlongX);
			Part(KSCylinder, FVector(-17.f, 0.f, 2.f), FVector(12.6f, 12.6f, 3.f), Neon, KSAlongX);
			Part(KSCube, FVector(0.f, 0.f, -7.f), FVector(6.f, 4.f, 10.f), Grip, Zero);
			Part(KSCube, FVector(4.f, -5.5f, 8.f), FVector(8.f, 3.f, 4.f), Metal, Zero);
			Part(KSSphere, FVector(35.f, 0.f, 2.f), FVector(8.f, 8.f, 8.f), Neon, Zero);
			break;
		}
	}
	MuzzleFirst = AddPart(ViewRoot, KSSphere, FVector::ZeroVector, FVector(9.f), MuzzleMaterial, Zero, true, false);
	MuzzleFirst->SetVisibility(false);
	LastViewRotation = GetAimRotation();
	ApplyWeaponVisuals();
}

void AKSCharacter::UpdateColors()
{
	const AKSPlayerState* PS = GetKSPlayerState();
	const int32 Index = PS ? PS->ColorIndex : INDEX_NONE;
	if (Index == AppliedColorIndex)
	{
		return;
	}
	AppliedColorIndex = Index;
	const FLinearColor Color = GetPlayerColor();
	if (ArmorMaterial)
	{
		ArmorMaterial->SetVectorParameterValue(TEXT("Color"), Color);
	}
	if (NeonMaterial)
	{
		NeonMaterial->SetVectorParameterValue(TEXT("Color"), Color);
	}
	if (ShieldMaterial)
	{
		ShieldMaterial->SetVectorParameterValue(TEXT("Color"), Color);
	}
}

void AKSCharacter::ApplyWeaponVisuals()
{
	const FKSWeaponInfo& Info = KS::Weapon(CurrentWeapon);
	if (GunGlowMaterial)
	{
		GunGlowMaterial->SetVectorParameterValue(TEXT("Color"), Info.Color);
	}
	if (GunBody)
	{
		const FVector Size = CurrentWeapon == KSWeapon::Rocket ? FVector(58.f, 16.f, 16.f)
			: CurrentWeapon == KSWeapon::Rail ? FVector(64.f, 8.f, 10.f)
			: CurrentWeapon == KSWeapon::Shotgun ? FVector(50.f, 11.f, 12.f)
			: FVector(52.f, 9.f, 12.f);
		GunBody->SetRelativeScale3D(Size / 100.f);
	}
	for (int32 W = 0; W < ViewModels.Num(); ++W)
	{
		if (ViewModels[W])
		{
			ViewModels[W]->SetVisibility(W == CurrentWeapon, true);
		}
	}
	if (MuzzleFirst && CurrentWeapon < KSWeapon::Count)
	{
		MuzzleFirst->SetRelativeLocation(KSViewOffset[CurrentWeapon] + KSMuzzleOffset[CurrentWeapon]);
		MuzzleFirst->SetVisibility(false);
	}
}

// ---------------------------------------------------------------------------------------------
// Life cycle
// ---------------------------------------------------------------------------------------------

void AKSCharacter::BeginPlay()
{
	Super::BeginPlay();
	BuildBody();
	if (HasAuthority())
	{
		SpawnProtectedUntil = GetServerTime() + KS::SpawnProtectionTime;
	}
	LastViewRotation = GetActorRotation();
}

void AKSCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	UpdateColors();
}

void AKSCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	UpdateColors();
}

void AKSCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Now = GetWorldTime();

	if (GetCharacterMovement()->IsMovingOnGround())
	{
		WalkCycle += DeltaSeconds * GetVelocity().Size2D() / 95.f;
	}
	RecentShots = FMath::Max(0.f, RecentShots - DeltaSeconds * 6.f);
	LandingDip = FMath::Max(0.f, LandingDip - DeltaSeconds * 4.f);
	HitFlash = FMath::Max(0.f, HitFlash - DeltaSeconds * 5.f);
	if (JumpReleaseTime > 0.f && Now >= JumpReleaseTime)
	{
		StopJumping();
		JumpReleaseTime = 0.f;
	}
	UpdateColors();

	// Wait for the player state so the column of light has the player's color.
	if (!bSpawnFxDone && (GetKSPlayerState() || GetGameTimeSinceCreation() > 0.5f))
	{
		bSpawnFxDone = true;
		// Someone joining mid-match should not see effects for robots that spawned long ago.
		if (IsSpawnProtected())
		{
			if (AKSFXManager* FX = AKSFXManager::Get(this))
			{
				FX->Pillar(GetActorLocation() - FVector(0.f, 0.f, KS::CapsuleHalfHeight), GetPlayerColor(), 420.f);
			}
			KSAudio::PlayAt(this, KSSound::Spawn, GetActorLocation(), 0.8f);
		}
	}

	if (IsLocallyControlled())
	{
		TickWeapon(DeltaSeconds);
	}
	if (HasAuthority() || GetLocalRole() == ROLE_AutonomousProxy)
	{
		TickJumpPads();
	}
	TickFootsteps(DeltaSeconds);
	TickBody(DeltaSeconds);
	if (IsLocalHuman())
	{
		if (!bViewModelsBuilt)
		{
			BuildViewModels();
		}
		TickViewModel(DeltaSeconds);
		TickCamera(DeltaSeconds);
	}

	if (HasAuthority() && !bDead && GetActorLocation().Z < -1500.f)
	{
		Die(nullptr, 255, false);
	}
}

void AKSCharacter::FellOutOfWorld(const UDamageType& DamageType)
{
	if (HasAuthority())
	{
		Die(nullptr, 255, false);
	}
}

void AKSCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	LandingDip = 1.f;
	if (IsLocalHuman())
	{
		KSAudio::Play2D(this, KSSound::Footstep, 1.f, 0.8f);
	}
}

void AKSCharacter::DoJump()
{
	if (bDead || bFrozen || !CanJump())
	{
		return;
	}
	Jump();
	JumpReleaseTime = GetWorldTime() + 0.15f;
	if (IsLocalHuman())
	{
		KSAudio::Play2D(this, KSSound::Jump);
	}
}

// ---------------------------------------------------------------------------------------------
// Animation
// ---------------------------------------------------------------------------------------------

void AKSCharacter::TickBody(float DeltaSeconds)
{
	if (!BodyRoot || bDead)
	{
		return;
	}
	const float Now = GetWorldTime();
	const FVector Velocity = GetVelocity();
	const float Speed = Velocity.Size2D();
	const float MoveAmount = FMath::Clamp(Speed / 600.f, 0.f, 1.f);
	const bool bGround = GetCharacterMovement()->IsMovingOnGround();

	const float Swing = FMath::Sin(WalkCycle) * 38.f * MoveAmount;
	if (LegLeft && LegRight)
	{
		LegLeft->SetRelativeRotation(FRotator(bGround ? Swing : 25.f, 0.f, 0.f));
		LegRight->SetRelativeRotation(FRotator(bGround ? -Swing : -15.f, 0.f, 0.f));
	}

	// Lean a little into the movement.
	const FVector LocalVelocity = GetActorRotation().UnrotateVector(Velocity);
	const float LeanSide = FMath::Clamp(LocalVelocity.Y / 640.f, -1.f, 1.f) * 6.f;
	const float LeanFront = FMath::Clamp(LocalVelocity.X / 640.f, -1.f, 1.f) * -5.f;
	const float Bob = bGround ? FMath::Abs(FMath::Sin(WalkCycle)) * 3.f * MoveAmount : 0.f;
	BodyRoot->SetRelativeLocationAndRotation(FVector(0.f, 0.f, Bob), FRotator(LeanFront, 0.f, LeanSide));

	const float Pitch = FRotator::NormalizeAxis(GetAimRotation().Pitch);
	if (HeadPivot)
	{
		HeadPivot->SetRelativeRotation(FRotator(Pitch * 0.6f, 0.f, 0.f));
	}
	if (ArmsPivot)
	{
		ArmsPivot->SetRelativeRotation(FRotator(Pitch, 0.f, 0.f));
	}

	if (MuzzleThird)
	{
		const bool bFlash = Now - MuzzleFlashTime < 0.06f;
		if (MuzzleThird->IsVisible() != bFlash)
		{
			MuzzleThird->SetVisibility(bFlash);
		}
	}
	if (ArmorMaterial)
	{
		ArmorMaterial->SetScalarParameterValue(TEXT("Flash"), HitFlash);
	}
	if (Shield)
	{
		const bool bProtected = IsSpawnProtected();
		if (Shield->IsVisible() != bProtected)
		{
			Shield->SetVisibility(bProtected);
		}
		if (bProtected && ShieldMaterial)
		{
			ShieldMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.22f + 0.1f * FMath::Sin(Now * 12.f));
		}
	}
}

void AKSCharacter::TickViewModel(float DeltaSeconds)
{
	if (!ViewRoot)
	{
		return;
	}
	const float Now = GetWorldTime();
	const bool bGround = GetCharacterMovement()->IsMovingOnGround();
	const float MoveAmount = bGround ? FMath::Clamp(GetVelocity().Size2D() / 640.f, 0.f, 1.f) : 0.f;

	// The gun lags behind fast turns a little.
	const FRotator View = GetAimRotation();
	const FRotator Delta = (View - LastViewRotation).GetNormalized();
	LastViewRotation = View;
	const FVector2D TargetSway(FMath::Clamp(Delta.Yaw * 0.5f, -3.f, 3.f), FMath::Clamp(Delta.Pitch * 0.5f, -3.f, 3.f));
	ViewSway = FMath::Vector2DInterpTo(ViewSway, TargetSway, DeltaSeconds, 10.f);
	ViewKick = FMath::FInterpTo(ViewKick, 0.f, DeltaSeconds, 14.f);

	const float BobSide = FMath::Sin(WalkCycle) * 0.9f * MoveAmount;
	const float BobUp = FMath::Abs(FMath::Cos(WalkCycle)) * 0.8f * MoveAmount;
	FVector Offset(-ViewKick * 4.f, -ViewSway.X * 0.6f + BobSide, -BobUp - ViewSway.Y * 0.4f - LandingDip * 3.f);
	FRotator Rotation(ViewKick * 5.f + ViewSway.Y * 1.5f, -ViewSway.X * 1.5f, -ViewSway.X * 1.2f);

	if (bReloading)
	{
		const float Dip = FMath::Sin(GetReloadProgress() * UE_PI);
		Offset.Z -= Dip * 8.f;
		Rotation.Roll += Dip * 25.f;
		Rotation.Pitch -= Dip * 12.f;
	}
	if (Now < SwitchEndTime)
	{
		const float Lowered = FMath::Clamp((SwitchEndTime - Now) / KS::WeaponSwitchTime, 0.f, 1.f);
		Offset.Z -= Lowered * 12.f;
		Rotation.Pitch -= Lowered * 20.f;
	}
	ViewRoot->SetRelativeLocationAndRotation(Offset, Rotation);

	if (MuzzleFirst)
	{
		const bool bFlash = Now - MuzzleFlashTime < 0.05f;
		if (MuzzleFirst->IsVisible() != bFlash)
		{
			MuzzleFirst->SetVisibility(bFlash);
		}
		if (bFlash)
		{
			MuzzleFirst->SetRelativeScale3D(FVector(FMath::FRandRange(0.07f, 0.12f)));
		}
	}
}

void AKSCharacter::TickCamera(float DeltaSeconds)
{
	if (!Camera)
	{
		return;
	}
	if (const UKSGameInstance* GI = Cast<UKSGameInstance>(GetGameInstance()))
	{
		Camera->SetFieldOfView(GI->GetSettings().FieldOfView);
	}
	if (bDead)
	{
		// Sink to the floor and tip over.
		const float T = FMath::Clamp(GetTimeSinceDeath() / 0.7f, 0.f, 1.f);
		const float Ease = 1.f - FMath::Square(1.f - T);
		Camera->SetRelativeLocation(FVector(0.f, 0.f, FMath::Lerp(KS::EyeHeight, -60.f, Ease)));
		Camera->SetWorldRotation(FRotator(FMath::Lerp(DeathView.Pitch, -10.f, Ease), DeathView.Yaw, FMath::Lerp(0.f, 28.f, Ease)));
		return;
	}
	Camera->SetRelativeLocation(FVector(0.f, 0.f, KS::EyeHeight - LandingDip * 6.f));
}

void AKSCharacter::TickFootsteps(float DeltaSeconds)
{
	if (bDead || !GetCharacterMovement()->IsMovingOnGround())
	{
		return;
	}
	const float Speed = GetVelocity().Size2D();
	if (Speed < 150.f)
	{
		return;
	}
	StepDistance += Speed * DeltaSeconds;
	if (StepDistance > 210.f)
	{
		StepDistance = 0.f;
		const float Pitch = FMath::FRandRange(0.85f, 1.15f);
		if (IsLocalHuman())
		{
			KSAudio::Play2D(this, KSSound::Footstep, 0.7f, Pitch);
		}
		else
		{
			KSAudio::PlayAt(this, KSSound::Footstep, GetActorLocation() - FVector(0.f, 0.f, KS::CapsuleHalfHeight), 1.f, Pitch);
		}
	}
}

void AKSCharacter::TickJumpPads()
{
	if (bDead || bFrozen)
	{
		return;
	}
	const float Now = GetWorldTime();
	if (Now < NextPadTime)
	{
		return;
	}
	const AKSArenaBuilder* Arena = AKSArenaBuilder::Find(this);
	if (!Arena || Arena->GetArenaIndex() == INDEX_NONE)
	{
		return;
	}
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, KS::CapsuleHalfHeight);
	for (const FKSJumpPad& Pad : Arena->GetDef().JumpPads)
	{
		if (FVector::Dist2D(Feet, Pad.Location) < 115.f && FMath::Abs(Feet.Z - Pad.Location.Z) < 45.f)
		{
			NextPadTime = Now + 0.6f;
			LaunchCharacter(Pad.Velocity, true, true);
			if (IsLocalHuman())
			{
				KSAudio::Play2D(this, KSSound::JumpPad);
			}
			if (AKSFXManager* FX = AKSFXManager::Get(this))
			{
				FX->Flash(Pad.Location + FVector(0.f, 0.f, 20.f), Arena->GetDef().Theme.Mats[KSMat::NeonPad].Color, 40.f, 260.f, 0.35f, 5.f);
			}
			break;
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Weapons
// ---------------------------------------------------------------------------------------------

int32 AKSCharacter::BestWeapon() const
{
	const int32 Order[KSWeapon::Count] = { KSWeapon::Rail, KSWeapon::Rocket, KSWeapon::Shotgun, KSWeapon::Rifle };
	for (const int32 W : Order)
	{
		if (HasWeapon(W) && (W == KSWeapon::Rifle || GetAmmo(W) > 0))
		{
			return W;
		}
	}
	return KSWeapon::Rifle;
}

void AKSCharacter::SelectWeapon(int32 Weapon)
{
	if (bDead || Weapon < 0 || Weapon >= KSWeapon::Count || Weapon == CurrentWeapon || !HasWeapon(Weapon))
	{
		return;
	}
	if (Weapon != KSWeapon::Rifle && GetAmmo(Weapon) <= 0)
	{
		return;
	}
	CurrentWeapon = (uint8)Weapon;
	bReloading = false;
	SwitchEndTime = GetWorldTime() + KS::WeaponSwitchTime;
	NextFireTime = FMath::Max(NextFireTime, SwitchEndTime);
	ApplyWeaponVisuals();
	if (!HasAuthority())
	{
		ServerSelectWeapon(CurrentWeapon);
	}
	if (IsLocalHuman())
	{
		KSAudio::Play2D(this, KSSound::Reload, 0.5f, 1.4f);
	}
}

void AKSCharacter::CycleWeapon(int32 Direction)
{
	const int32 Step = Direction >= 0 ? 1 : KSWeapon::Count - 1;
	for (int32 i = 1; i < KSWeapon::Count; ++i)
	{
		const int32 W = (CurrentWeapon + Step * i) % KSWeapon::Count;
		if (HasWeapon(W) && (W == KSWeapon::Rifle || GetAmmo(W) > 0))
		{
			SelectWeapon(W);
			return;
		}
	}
}

void AKSCharacter::ServerSelectWeapon_Implementation(uint8 Weapon)
{
	if (Weapon < KSWeapon::Count && HasWeapon(Weapon))
	{
		CurrentWeapon = Weapon;
		ApplyWeaponVisuals();
	}
}

void AKSCharacter::OnRep_Weapon()
{
	ApplyWeaponVisuals();
}

void AKSCharacter::OnRep_WeaponMask(uint8 OldMask)
{
	for (int32 W = 1; W < KSWeapon::Count; ++W)
	{
		if ((WeaponMask & (1 << W)) && !(OldMask & (1 << W)))
		{
			OnWeaponGained(W);
		}
	}
}

void AKSCharacter::OnWeaponGained(int32 Weapon)
{
	if (IsLocallyControlled() && !bDead)
	{
		SelectWeapon(Weapon);
	}
}

void AKSCharacter::StartReload()
{
	if (bDead || bFrozen || CurrentWeapon != KSWeapon::Rifle || bReloading || RifleMag >= KS::Weapon(KSWeapon::Rifle).MaxAmmo)
	{
		return;
	}
	bReloading = true;
	ReloadEndTime = GetWorldTime() + KS::RifleReloadTime;
	if (IsLocalHuman())
	{
		KSAudio::Play2D(this, KSSound::Reload);
	}
}

void AKSCharacter::TickWeapon(float DeltaSeconds)
{
	const float Now = GetWorldTime();
	if (bReloading && Now >= ReloadEndTime)
	{
		bReloading = false;
		RifleMag = KS::Weapon(KSWeapon::Rifle).MaxAmmo;
	}
	if (bDead || bFrozen || !bWantsToFire || Now < SwitchEndTime || Now < NextFireTime)
	{
		return;
	}

	if (CurrentWeapon == KSWeapon::Rifle)
	{
		if (bReloading)
		{
			return;
		}
		if (RifleMag <= 0)
		{
			StartReload();
			return;
		}
	}
	else if (GetAmmo(CurrentWeapon) <= 0)
	{
		if (IsLocalHuman())
		{
			KSAudio::Play2D(this, KSSound::Empty);
		}
		NextFireTime = Now + 0.3f;
		SelectWeapon(BestWeapon());
		return;
	}

	const float Interval = KS::Weapon(CurrentWeapon).FireInterval;
	// Keep a steady rate of fire independent of the frame rate.
	NextFireTime = (Now - NextFireTime > Interval) ? Now + Interval : NextFireTime + Interval;
	FireOnce();

	if (CurrentWeapon == KSWeapon::Rifle && RifleMag <= 0)
	{
		StartReload();
	}
}

void AKSCharacter::FireOnce()
{
	const float Now = GetWorldTime();
	const uint8 Weapon = CurrentWeapon;
	const FKSWeaponInfo& Info = KS::Weapon(Weapon);
	LastFireTime = Now;
	MuzzleFlashTime = Now;
	ViewKick = FMath::Min(ViewKick + 1.f, 2.f);
	RecentShots += 1.f;
	if (Weapon == KSWeapon::Rifle)
	{
		--RifleMag;
	}
	else if (SpecialAmmo[Weapon - 1] > 0)
	{
		--SpecialAmmo[Weapon - 1];
	}

	const FVector Origin = GetEyeLocation();
	const FVector Direction = GetAimRotation().Vector();
	const float Pitch = FMath::FRandRange(0.96f, 1.04f);
	if (IsLocalHuman())
	{
		KSAudio::Play2D(this, Info.Sound, 1.f, Pitch);
		if (AKSPlayerController* PC = Cast<AKSPlayerController>(Controller))
		{
			PC->AddRecoil(Info.RecoilPitch);
		}
	}
	else
	{
		// A bot on the host: the host's player hears it here.
		KSAudio::PlayAt(this, Info.Sound, GetMuzzleLocation(), 1.f, Pitch);
	}

	if (Weapon == KSWeapon::Rocket)
	{
		if (AKSFXManager* FX = AKSFXManager::Get(this))
		{
			FX->Flash(GetMuzzleLocation(), Info.Color, 10.f, 70.f, 0.15f, 6.f);
		}
		if (HasAuthority())
		{
			SpawnRocket(Origin, Direction);
		}
		else
		{
			ServerFireRocket(Origin, Direction);
		}
		return;
	}
	FireHitscan(Origin, Direction);
}

void AKSCharacter::FireHitscan(const FVector& Origin, const FVector& Direction)
{
	const uint8 Weapon = CurrentWeapon;
	const FKSWeaponInfo& Info = KS::Weapon(Weapon);
	FRandomStream Random(FMath::Rand());
	const float SpreadRad = FMath::DegreesToRadians(GetSpreadDegrees());

	FCollisionQueryParams Params(SCENE_QUERY_STAT(KSShot), false, this);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_Pawn);

	TArray<FKSShotHit> Hits;
	TArray<FVector_NetQuantize> Ends;
	uint16 HitMask = 0;
	bool bHeadshot = false;
	for (int32 i = 0; i < Info.Pellets; ++i)
	{
		const FVector Dir = SpreadRad > 0.f ? Random.VRandCone(Direction, SpreadRad) : Direction;
		const FVector TraceEnd = Origin + Dir * Info.Range;
		FVector End = TraceEnd;
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByObjectType(Hit, Origin, TraceEnd, Objects, Params))
		{
			End = Hit.ImpactPoint;
			AKSCharacter* Victim = Cast<AKSCharacter>(Hit.GetActor());
			if (Victim && Victim->IsAlive())
			{
				FKSShotHit& Shot = Hits.AddDefaulted_GetRef();
				Shot.Actor = Victim;
				Shot.Location = Hit.ImpactPoint;
				Shot.Normal = Hit.ImpactNormal;
				HitMask |= uint16(1u << i);
				bHeadshot |= Hit.ImpactPoint.Z > Victim->GetActorLocation().Z + KS::HeadshotHeight;
			}
		}
		Ends.Add(End);
	}

	PlayShotFX(Weapon, Origin, Ends, HitMask);
	if (Hits.Num() > 0 && IsLocalHuman())
	{
		if (AKSPlayerController* PC = Cast<AKSPlayerController>(Controller))
		{
			PC->NotifyLocalHit(bHeadshot);
		}
	}

	if (HasAuthority())
	{
		EndSpawnProtection();
		ProcessShot(Weapon, Origin, Hits);
		MulticastShotFX(Weapon, Origin, Ends, HitMask);
	}
	else
	{
		ServerFire(Weapon, Origin, Direction, Hits, Ends, HitMask);
	}
}

bool AKSCharacter::ConsumeServerShot(uint8 Weapon)
{
	if (bDead || bFrozen || Weapon >= KSWeapon::Count || !HasWeapon(Weapon))
	{
		return false;
	}
	const float Now = GetWorldTime();
	const float Interval = KS::Weapon(Weapon).FireInterval;
	// Network jitter can bunch shots together, so allow a small bank of shots but keep the average rate.
	ServerFireCredit = FMath::Min(3.f, ServerFireCredit + (Now - ServerLastFireTime) / Interval);
	ServerLastFireTime = Now;
	if (ServerFireCredit < 0.75f)
	{
		return false;
	}
	ServerFireCredit -= 1.f;
	if (Weapon != KSWeapon::Rifle)
	{
		uint8& Ammo = SpecialAmmo[Weapon - 1];
		if (Ammo == 0)
		{
			return false;
		}
		--Ammo;
	}
	EndSpawnProtection();
	return true;
}

void AKSCharacter::ServerFire_Implementation(uint8 Weapon, FVector_NetQuantize Origin, FVector_NetQuantizeNormal Direction,
	const TArray<FKSShotHit>& Hits, const TArray<FVector_NetQuantize>& Ends, uint16 HitMask)
{
	if (Weapon == KSWeapon::Rocket || !ConsumeServerShot(Weapon))
	{
		return;
	}
	FVector SafeOrigin = Origin;
	if (FVector::DistSquared(SafeOrigin, GetEyeLocation()) > FMath::Square(300.f))
	{
		SafeOrigin = GetEyeLocation();
	}
	const int32 MaxShots = KS::Weapon(Weapon).Pellets;
	const TArray<FKSShotHit> Checked(Hits.GetData(), FMath::Min(Hits.Num(), MaxShots));
	ProcessShot(Weapon, SafeOrigin, Checked);
	const TArray<FVector_NetQuantize> FxEnds(Ends.GetData(), FMath::Min(Ends.Num(), MaxShots));
	MuzzleFlashTime = GetWorldTime();
	MulticastShotFX(Weapon, SafeOrigin, FxEnds, HitMask);
}

void AKSCharacter::ServerFireRocket_Implementation(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Direction)
{
	if (!ConsumeServerShot(KSWeapon::Rocket))
	{
		return;
	}
	FVector SafeOrigin = Origin;
	if (FVector::DistSquared(SafeOrigin, GetEyeLocation()) > FMath::Square(300.f))
	{
		SafeOrigin = GetEyeLocation();
	}
	SpawnRocket(SafeOrigin, FVector(Direction).GetSafeNormal(UE_SMALL_NUMBER, GetActorForwardVector()));
}

void AKSCharacter::SpawnRocket(const FVector& Origin, const FVector& Direction)
{
	EndSpawnProtection();
	const FTransform Transform(Direction.Rotation(), Origin);
	AKSRocket* Rocket = GetWorld()->SpawnActorDeferred<AKSRocket>(AKSRocket::StaticClass(), Transform, this, this, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Rocket)
	{
		Rocket->InitRocket(this, Origin, Direction);
		Rocket->FinishSpawning(Transform);
	}
	const TArray<FVector_NetQuantize> NoEnds;
	MulticastShotFX(KSWeapon::Rocket, Origin, NoEnds, 0);
}

void AKSCharacter::ProcessShot(uint8 Weapon, const FVector& Origin, const TArray<FKSShotHit>& Hits)
{
	const FKSWeaponInfo& Info = KS::Weapon(Weapon);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(KSShotCheck), false, this);
	const FCollisionObjectQueryParams WorldOnly(ECC_WorldStatic);

	struct FKSDamage
	{
		float Amount = 0.f;
		bool bHeadshot = false;
	};
	TMap<AKSCharacter*, FKSDamage> Damage;

	for (const FKSShotHit& Hit : Hits)
	{
		AKSCharacter* Victim = Cast<AKSCharacter>(Hit.Actor);
		if (!Victim || Victim == this || !Victim->IsAlive())
		{
			continue;
		}
		const FVector HitLocation = Hit.Location;
		// The victim may have moved a bit on the server since the shooter saw it.
		if (FVector::DistSquared(HitLocation, Victim->GetActorLocation()) > FMath::Square(260.f))
		{
			continue;
		}
		if (FVector::DistSquared(Origin, HitLocation) > FMath::Square(Info.Range + 300.f))
		{
			continue;
		}
		// No shooting through walls.
		FHitResult Blocker;
		const FVector CheckEnd = HitLocation - (HitLocation - Origin).GetSafeNormal() * 15.f;
		if (GetWorld()->LineTraceSingleByObjectType(Blocker, Origin, CheckEnd, WorldOnly, Params))
		{
			continue;
		}
		const bool bHead = HitLocation.Z > Victim->GetActorLocation().Z + KS::HeadshotHeight;
		FKSDamage& Entry = Damage.FindOrAdd(Victim);
		Entry.Amount += Info.Damage * (bHead ? Info.HeadshotMult : 1.f);
		Entry.bHeadshot |= bHead;
	}

	for (const TPair<AKSCharacter*, FKSDamage>& Pair : Damage)
	{
		Pair.Key->ApplyDamageFrom(Pair.Value.Amount, GetController(), Weapon, Pair.Value.bHeadshot, Origin);
	}
}

void AKSCharacter::MulticastShotFX_Implementation(uint8 Weapon, FVector_NetQuantize Origin, const TArray<FVector_NetQuantize>& Ends, uint16 HitMask)
{
	// The machine that fired (the owner, or the server for bots) already played everything.
	if (IsLocallyControlled() || Weapon >= KSWeapon::Count)
	{
		return;
	}
	MuzzleFlashTime = GetWorldTime();
	const FKSWeaponInfo& Info = KS::Weapon(Weapon);
	KSAudio::PlayAt(this, Info.Sound, GetMuzzleLocation(), 1.f, FMath::FRandRange(0.96f, 1.04f));
	if (Weapon == KSWeapon::Rocket)
	{
		if (AKSFXManager* FX = AKSFXManager::Get(this))
		{
			FX->Flash(GetMuzzleLocation(), Info.Color, 10.f, 70.f, 0.15f, 6.f);
		}
		return;
	}
	PlayShotFX(Weapon, Origin, Ends, HitMask);
}

void AKSCharacter::PlayShotFX(uint8 Weapon, const FVector& Origin, const TArray<FVector_NetQuantize>& Ends, uint16 HitMask)
{
	AKSFXManager* FX = AKSFXManager::Get(this);
	if (!FX)
	{
		return;
	}
	const FKSWeaponInfo& Info = KS::Weapon(Weapon);
	const FVector Muzzle = GetMuzzleLocation();
	FX->Flash(Muzzle, Info.Color, 6.f, 32.f, 0.06f, 8.f);

	for (int32 i = 0; i < Ends.Num(); ++i)
	{
		const FVector End = Ends[i];
		const FVector Dir = (End - Origin).GetSafeNormal();
		const bool bHitPlayer = (HitMask & (1u << i)) != 0;
		const bool bHitWorld = FVector::DistSquared(Origin, End) < FMath::Square(Info.Range - 20.f);
		switch (Weapon)
		{
		case KSWeapon::Rail:
			FX->Beam(Muzzle, End, Info.Color);
			break;
		case KSWeapon::Shotgun:
			if (i % 2 == 0)
			{
				FX->Tracer(Muzzle, End, Info.Color, 1.2f, 0.07f, 9.f);
			}
			break;
		default:
			FX->Tracer(Muzzle, End, Info.Color, 1.8f, 0.06f, 10.f);
			break;
		}
		const float Scale = Weapon == KSWeapon::Shotgun ? 0.55f : (Weapon == KSWeapon::Rail ? 1.5f : 0.8f);
		if (bHitPlayer)
		{
			FX->Impact(End, -Dir, FLinearColor(1.f, 0.35f, 0.2f), Scale * 1.2f);
		}
		else if (bHitWorld)
		{
			FX->Impact(End, -Dir, Info.Color, Scale);
		}
	}
}

FVector AKSCharacter::GetMuzzleLocation() const
{
	if (IsLocalHuman() && MuzzleFirst)
	{
		return MuzzleFirst->GetComponentLocation();
	}
	if (MuzzleThird)
	{
		return MuzzleThird->GetComponentLocation();
	}
	return GetEyeLocation();
}

// ---------------------------------------------------------------------------------------------
// Damage, death and pickups (server)
// ---------------------------------------------------------------------------------------------

void AKSCharacter::EndSpawnProtection()
{
	if (HasAuthority() && SpawnProtectedUntil > 0.f)
	{
		SpawnProtectedUntil = 0.f;
	}
}

void AKSCharacter::ApplyDamageFrom(float Amount, AController* InstigatorController, uint8 Weapon, bool bHeadshot, const FVector& FromLocation)
{
	if (!HasAuthority() || bDead || bFrozen || Amount <= 0.f)
	{
		return;
	}
	const bool bSelf = InstigatorController && InstigatorController == GetController();
	if (IsSpawnProtected() && !bSelf)
	{
		return;
	}
	Health = FMath::Max(0.f, Health - Amount);
	MulticastHurt(FromLocation, bHeadshot);
	if (AKSPlayerController* VictimPC = Cast<AKSPlayerController>(GetController()))
	{
		VictimPC->ClientDamaged(FromLocation, Amount);
	}
	if (Health <= 0.f)
	{
		Die(InstigatorController, Weapon, bHeadshot);
		return;
	}
	// Bullets already showed a hit marker on the shooter's phone; rockets are only known here.
	if (Weapon == KSWeapon::Rocket && !bSelf)
	{
		if (AKSPlayerController* AttackerPC = Cast<AKSPlayerController>(InstigatorController))
		{
			AttackerPC->ClientHitConfirmed(bHeadshot);
		}
	}
}

void AKSCharacter::MulticastHurt_Implementation(FVector_NetQuantize FromLocation, bool bHeadshot)
{
	HitFlash = 1.f;
	if (!IsLocalHuman())
	{
		KSAudio::PlayAt(this, KSSound::Hurt, GetActorLocation(), 0.7f, bHeadshot ? 1.3f : 1.f);
	}
}

void AKSCharacter::ApplyKnockback(const FVector& Velocity)
{
	if (bDead || !HasAuthority())
	{
		return;
	}
	LaunchCharacter(Velocity, false, false);
	// A remote player's phone predicts its own movement, so it has to be launched there too.
	if (!IsLocallyControlled() && IsPlayerControlled())
	{
		ClientKnockback(Velocity);
	}
}

void AKSCharacter::ClientKnockback_Implementation(FVector_NetQuantize10 Velocity)
{
	LaunchCharacter(Velocity, false, false);
}

void AKSCharacter::Die(AController* Killer, uint8 Weapon, bool bHeadshot)
{
	if (bDead)
	{
		return;
	}
	bDead = true;
	Health = 0.f;
	bWantsToFire = false;
	OnRep_Dead();
	ForceNetUpdate();
	if (AKSGameMode* GM = GetWorld()->GetAuthGameMode<AKSGameMode>())
	{
		GM->CharacterKilled(this, Killer, Weapon, bHeadshot);
	}
	SetLifeSpan(KS::RespawnDelay + 6.f);
}

void AKSCharacter::OnRep_Dead()
{
	if (!bDead)
	{
		return;
	}
	DeathTime = GetWorldTime();
	bWantsToFire = false;
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	if (BodyRoot)
	{
		BodyRoot->SetVisibility(false, true);
	}
	if (ViewRoot)
	{
		ViewRoot->SetVisibility(false, true);
	}
	if (AKSFXManager* FX = AKSFXManager::Get(this))
	{
		FX->Debris(GetActorLocation(), GetVelocity(), GetPlayerColor(), 10);
	}
	KSAudio::PlayAt(this, KSSound::Explosion, GetActorLocation(), 0.55f, 1.6f);
	if (IsLocalHuman() && Camera)
	{
		DeathView = GetAimRotation();
		Camera->bUsePawnControlRotation = false;
	}
}

void AKSCharacter::SetFrozen(bool bFreeze)
{
	if (!HasAuthority())
	{
		return;
	}
	bFrozen = bFreeze;
	bWantsToFire = false;
	if (bFreeze)
	{
		GetCharacterMovement()->StopMovementImmediately();
	}
}

bool AKSCharacter::TryTakePickup(uint8 Type)
{
	if (!HasAuthority() || bDead)
	{
		return false;
	}
	if (Type == KSPickupType::Health)
	{
		if (Health >= KS::MaxHealth)
		{
			return false;
		}
		Health = FMath::Min(KS::MaxHealth, Health + 50.f);
	}
	else if (Type > 0 && Type < KSWeapon::Count)
	{
		const int32 Weapon = Type;
		const FKSWeaponInfo& Info = KS::Weapon(Weapon);
		uint8& Ammo = SpecialAmmo[Weapon - 1];
		const bool bHad = HasWeapon(Weapon);
		if (bHad && Ammo >= Info.MaxAmmo)
		{
			return false;
		}
		WeaponMask |= uint8(1 << Weapon);
		Ammo = (uint8)FMath::Min(Info.MaxAmmo, Ammo + Info.PickupAmmo);
		// Remote owners learn about new weapons through OnRep_WeaponMask.
		if (!bHad && IsLocallyControlled())
		{
			OnWeaponGained(Weapon);
		}
	}
	else
	{
		return false;
	}
	if (AKSPlayerController* PC = Cast<AKSPlayerController>(GetController()))
	{
		PC->ClientPickupNotice(Type);
	}
	return true;
}
