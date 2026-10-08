#include "KSMenuGameMode.h"
#include "KSArenaBuilder.h"
#include "KSArenaData.h"
#include "KSMenuHUD.h"
#include "KSMenuPlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"

AKSMenuGameMode::AKSMenuGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = AKSMenuPlayerController::StaticClass();
	HUDClass = AKSMenuHUD::StaticClass();
	GameStateClass = AGameStateBase::StaticClass();
	PlayerStateClass = APlayerState::StaticClass();
}

void AKSMenuGameMode::StartPlay()
{
	Super::StartPlay();
	AKSArenaBuilder::SpawnArena(GetWorld(), FMath::RandRange(0, KSArena::Num() - 1));
}

void AKSMenuGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// No pawn in the menu; the controller flies the camera.
}
