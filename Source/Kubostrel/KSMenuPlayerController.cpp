#include "KSMenuPlayerController.h"
#include "KSArenaBuilder.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"

AKSMenuPlayerController::AKSMenuPlayerController()
{
	bShowMouseCursor = true;
}

void AKSMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	SetInputMode(Mode);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	MenuCamera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Params);
	if (MenuCamera)
	{
		// The default camera actor letterboxes to 16:9; phones are wider.
		MenuCamera->GetCameraComponent()->SetConstraintAspectRatio(false);
		MenuCamera->GetCameraComponent()->SetFieldOfView(80.f);
		SetViewTarget(MenuCamera);
	}
	OrbitAngle = FMath::FRandRange(0.f, 360.f);
}

void AKSMenuPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	const AKSArenaBuilder* Arena = AKSArenaBuilder::Find(this);
	if (!MenuCamera || !Arena || Arena->GetArenaIndex() == INDEX_NONE)
	{
		return;
	}
	const FKSArenaDef& Def = Arena->GetDef();
	OrbitAngle = FMath::Fmod(OrbitAngle + DeltaTime * 4.f, 360.f);
	const float Rad = FMath::DegreesToRadians(OrbitAngle);
	const FVector Location = Def.MenuCameraTarget + FVector(FMath::Cos(Rad) * Def.MenuCameraRadius, FMath::Sin(Rad) * Def.MenuCameraRadius, Def.MenuCameraHeight);
	MenuCamera->SetActorLocationAndRotation(Location, (Def.MenuCameraTarget - Location).Rotation());
	if (GetViewTarget() != MenuCamera)
	{
		SetViewTarget(MenuCamera);
	}
}
