#include "KSGameMode.h"
#include "Kubostrel.h"
#include "KSBotController.h"
#include "KSCharacter.h"
#include "KSGameInstance.h"
#include "KSGameState.h"
#include "KSHUD.h"
#include "KSPickup.h"
#include "KSPlayerController.h"
#include "KSPlayerState.h"
#include "KSTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

AKSGameMode::AKSGameMode()
{
	GameStateClass = AKSGameState::StaticClass();
	PlayerStateClass = AKSPlayerState::StaticClass();
	PlayerControllerClass = AKSPlayerController::StaticClass();
	HUDClass = AKSHUD::StaticClass();
	DefaultPawnClass = AKSCharacter::StaticClass();
	PrimaryActorTick.bCanEverTick = true;
	bUseSeamlessTravel = false;
}

void AKSGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	ArenaIndex = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("arena"), 0), 0, KSArena::Num() - 1);
	BotCount = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("bots"), 2), 0, KS::MaxPlayers - 1);
	FragLimit = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("frags"), 20), 5, 100);
	TimeLimit = (float)FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("time"), 480), 60, 3600);
	BotSkill = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("skill"), 1), 0, 2);
	UE_LOG(LogKS, Log, TEXT("Match: arena %d, bots %d, frags %d, time %.0f s, skill %d"), ArenaIndex, BotCount, FragLimit, TimeLimit, BotSkill);
}

void AKSGameMode::InitGameState()
{
	Super::InitGameState();
	if (AKSGameState* GS = GetKSGameState())
	{
		GS->FragLimit = FragLimit;
	}
}

void AKSGameMode::StartPlay()
{
	Super::StartPlay();
	StartMatch(ArenaIndex);
}

AKSGameState* AKSGameMode::GetKSGameState() const
{
	return GetGameState<AKSGameState>();
}

float AKSGameMode::Now() const
{
	const AKSGameState* GS = GetKSGameState();
	return GS ? GS->GetServerTime() : GetWorld()->GetTimeSeconds();
}

bool AKSGameMode::IsMatchPlaying() const
{
	const AKSGameState* GS = GetKSGameState();
	return bMatchStarted && GS && GS->IsPlaying();
}

TArray<AController*> AKSGameMode::GetAllControllers() const
{
	TArray<AController*> Result;
	for (FConstControllerIterator It = GetWorld()->GetControllerIterator(); It; ++It)
	{
		if (AController* C = It->Get())
		{
			Result.Add(C);
		}
	}
	return Result;
}

// ---------------------------------------------------------------------------------------------
// Match flow
// ---------------------------------------------------------------------------------------------

void AKSGameMode::StartMatch(int32 Arena)
{
	AKSGameState* GS = GetKSGameState();
	if (!GS)
	{
		return;
	}
	ArenaIndex = Arena;
	GS->FragLimit = FragLimit;
	GS->SetArena(Arena);
	NavGraph.Build(KSArena::Get(Arena));

	for (APlayerState* PS : GS->PlayerArray)
	{
		if (AKSPlayerState* KSPS = Cast<AKSPlayerState>(PS))
		{
			KSPS->ResetScore();
		}
	}
	GS->SetPhase(EKSMatchPhase::Playing, Now() + TimeLimit, nullptr);

	ClearPickups();
	SpawnPickups();

	bMatchStarted = true;
	UpdateBots();
	for (AController* C : GetAllControllers())
	{
		SpawnPlayer(C);
	}
	UE_LOG(LogKS, Log, TEXT("Match started on %s"), *KSArena::GetName(Arena));
}

void AKSGameMode::EndMatch()
{
	AKSGameState* GS = GetKSGameState();
	if (!GS || !GS->IsPlaying())
	{
		return;
	}
	const TArray<AKSPlayerState*> Ranking = GS->GetRanking();
	GS->SetPhase(EKSMatchPhase::Ended, Now() + KS::MatchEndPause, Ranking.Num() > 0 ? Ranking[0] : nullptr);
	for (TActorIterator<AKSCharacter> It(GetWorld()); It; ++It)
	{
		It->SetFrozen(true);
	}
	UE_LOG(LogKS, Log, TEXT("Match ended"));
}

void AKSGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AKSGameState* GS = GetKSGameState();
	if (!bMatchStarted || !GS)
	{
		return;
	}
	const float T = Now();
	if (!GS->IsPlaying())
	{
		if (T >= GS->PhaseEndTime)
		{
			StartMatch((ArenaIndex + 1) % KSArena::Num());
		}
		return;
	}
	if (T >= GS->PhaseEndTime)
	{
		EndMatch();
		return;
	}

	for (AController* C : GetAllControllers())
	{
		AKSPlayerState* PS = C->GetPlayerState<AKSPlayerState>();
		if (!PS)
		{
			continue;
		}
		const bool bWaiting = PS->RespawnTime > 0.f;
		if ((bWaiting && T >= PS->RespawnTime) || (!bWaiting && !C->GetPawn()))
		{
			SpawnPlayer(C);
		}
	}
}

void AKSGameMode::CharacterKilled(AKSCharacter* Victim, AController* Killer, uint8 Weapon, bool bHeadshot)
{
	AKSGameState* GS = GetKSGameState();
	if (!Victim || !GS)
	{
		return;
	}
	AKSPlayerState* VictimPS = Victim->GetKSPlayerState();
	AKSPlayerState* KillerPS = Killer ? Killer->GetPlayerState<AKSPlayerState>() : nullptr;
	if (VictimPS)
	{
		VictimPS->Deaths++;
		VictimPS->Streak = 0;
		VictimPS->RespawnTime = Now() + KS::RespawnDelay;
		VictimPS->ForceNetUpdate();
	}
	if (!GS->IsPlaying())
	{
		return;
	}

	int32 Streak = 0;
	if (KillerPS && KillerPS != VictimPS)
	{
		KillerPS->Kills++;
		KillerPS->Streak++;
		Streak = KillerPS->Streak;
		KillerPS->ForceNetUpdate();
	}
	else if (VictimPS)
	{
		// Falling off the arena or blowing yourself up costs a point.
		VictimPS->Kills = FMath::Max(0, VictimPS->Kills - 1);
	}
	GS->MulticastKill(KillerPS, VictimPS, Weapon, bHeadshot, (uint8)FMath::Min(Streak, 255));

	if (KillerPS && KillerPS != VictimPS && KillerPS->Kills >= FragLimit)
	{
		EndMatch();
	}
}

// ---------------------------------------------------------------------------------------------
// Players
// ---------------------------------------------------------------------------------------------

void AKSGameMode::PostLogin(APlayerController* NewPlayer)
{
	if (AKSPlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<AKSPlayerState>() : nullptr)
	{
		AssignColor(PS);
	}
	// Make room before the newcomer's robot appears.
	UpdateBots();
	Super::PostLogin(NewPlayer);
}

void AKSGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// Before the first match starts StartMatch spawns everyone.
	if (bMatchStarted)
	{
		SpawnPlayer(NewPlayer);
	}
}

bool AKSGameMode::PlayerCanRestart_Implementation(APlayerController* Player)
{
	// Respawns are timed by the game mode only.
	return false;
}

void AKSGameMode::Logout(AController* Exiting)
{
	AKSBotController* Bot = Cast<AKSBotController>(Exiting);
	if (Bot)
	{
		Bots.Remove(Bot);
	}
	else if (const AKSPlayerState* PS = Exiting ? Exiting->GetPlayerState<AKSPlayerState>() : nullptr)
	{
		if (PS->bAnnounced)
		{
			if (AKSGameState* GS = GetKSGameState())
			{
				GS->MulticastNotice(PS->GetPlayerName(), PS->ColorIndex, false);
			}
		}
	}
	if (Exiting)
	{
		if (APawn* Pawn = Exiting->GetPawn())
		{
			Pawn->Destroy();
		}
	}
	Super::Logout(Exiting);
	if (!Bot)
	{
		UpdateBots(Exiting);
	}
}

void AKSGameMode::SetPlayerName(AController* Controller, const FString& RequestedName)
{
	AKSPlayerState* PS = Controller ? Controller->GetPlayerState<AKSPlayerState>() : nullptr;
	AKSGameState* GS = GetKSGameState();
	if (!PS || !GS)
	{
		return;
	}
	FString Base = UKSGameInstance::SanitizeName(RequestedName);
	if (Base.IsEmpty())
	{
		Base = TEXT("Игрок");
	}
	auto IsTaken = [GS, PS](const FString& Name)
	{
		for (const APlayerState* Other : GS->PlayerArray)
		{
			if (Other && Other != PS && Other->GetPlayerName().Equals(Name, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	};
	FString Name = Base;
	for (int32 Suffix = 2; IsTaken(Name) && Suffix < 100; ++Suffix)
	{
		Name = FString::Printf(TEXT("%s %d"), *Base.Left(11), Suffix);
	}
	PS->SetPlayerName(Name);
	if (!PS->bAnnounced)
	{
		PS->bAnnounced = true;
		GS->MulticastNotice(Name, PS->ColorIndex, true);
	}
}

void AKSGameMode::AssignColor(AKSPlayerState* PlayerState)
{
	AKSGameState* GS = GetKSGameState();
	if (!PlayerState || !GS)
	{
		return;
	}
	for (int32 Index = 0; Index < KS::NumPlayerColors(); ++Index)
	{
		bool bUsed = false;
		for (const APlayerState* Other : GS->PlayerArray)
		{
			const AKSPlayerState* KSOther = Cast<AKSPlayerState>(Other);
			if (KSOther && KSOther != PlayerState && KSOther->ColorIndex == Index)
			{
				bUsed = true;
				break;
			}
		}
		if (!bUsed)
		{
			PlayerState->ColorIndex = Index;
			PlayerState->ForceNetUpdate();
			return;
		}
	}
	PlayerState->ColorIndex = GS->PlayerArray.Num() % KS::NumPlayerColors();
}

int32 AKSGameMode::CountHumans(AController* Leaving) const
{
	int32 Count = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (PC && PC != Leaving)
		{
			++Count;
		}
	}
	return Count;
}

FTransform AKSGameMode::ChooseSpawn(AController* Controller) const
{
	const FKSArenaDef& Def = KSArena::Get(ArenaIndex);
	if (Def.Spawns.Num() == 0)
	{
		return FTransform(FVector(0.f, 0.f, 200.f));
	}
	TArray<FVector> Others;
	for (TActorIterator<AKSCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsAlive() && It->GetController() != Controller)
		{
			Others.Add(It->GetActorLocation());
		}
	}
	// The spot farthest from everyone else, with some randomness so spawns are not predictable.
	int32 Best = 0;
	float BestScore = -1.f;
	for (int32 i = 0; i < Def.Spawns.Num(); ++i)
	{
		const FVector Location = Def.Spawns[i].GetLocation();
		float Nearest = 5000.f;
		for (const FVector& Other : Others)
		{
			Nearest = FMath::Min(Nearest, FVector::Dist(Location, Other));
		}
		const float Score = Nearest + FMath::FRandRange(0.f, 400.f);
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = i;
		}
	}
	return Def.Spawns[Best];
}

void AKSGameMode::SpawnPlayer(AController* Controller)
{
	if (!Controller || !bMatchStarted)
	{
		return;
	}
	AKSPlayerState* PS = Controller->GetPlayerState<AKSPlayerState>();
	if (APawn* OldPawn = Controller->GetPawn())
	{
		Controller->UnPossess();
		OldPawn->Destroy();
	}

	const FTransform Spawn = ChooseSpawn(Controller);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	AKSCharacter* Character = GetWorld()->SpawnActor<AKSCharacter>(AKSCharacter::StaticClass(), Spawn, Params);
	if (!Character)
	{
		UE_LOG(LogKS, Warning, TEXT("Could not spawn a robot"));
		return;
	}
	Controller->Possess(Character);
	const FRotator Facing(0.f, Spawn.Rotator().Yaw, 0.f);
	Controller->SetControlRotation(Facing);
	if (APlayerController* PC = Cast<APlayerController>(Controller))
	{
		PC->ClientSetRotation(Facing, true);
	}
	if (PS)
	{
		PS->RespawnTime = 0.f;
		PS->ForceNetUpdate();
	}
	if (!IsMatchPlaying())
	{
		Character->SetFrozen(true);
	}
}

// ---------------------------------------------------------------------------------------------
// Bots
// ---------------------------------------------------------------------------------------------

void AKSGameMode::UpdateBots(AController* Leaving)
{
	if (!bMatchStarted || bUpdatingBots)
	{
		return;
	}
	TGuardValue<bool> Guard(bUpdatingBots, true);
	Bots.RemoveAll([](const TObjectPtr<AKSBotController>& Bot) { return !IsValid(Bot); });
	const int32 Humans = CountHumans(Leaving);
	const int32 Desired = FMath::Clamp(BotCount, 0, FMath::Max(0, KS::MaxPlayers - Humans));
	while (Bots.Num() < Desired)
	{
		const int32 Before = Bots.Num();
		AddBot();
		if (Bots.Num() == Before)
		{
			break;
		}
	}
	while (Bots.Num() > Desired)
	{
		RemoveBot();
	}
}

void AKSGameMode::AddBot()
{
	static const TCHAR* Names[] = { TEXT("Вольт"), TEXT("Искра"), TEXT("Гром"), TEXT("Неон"), TEXT("Кварц"), TEXT("Бит") };
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AKSBotController* Bot = GetWorld()->SpawnActor<AKSBotController>(AKSBotController::StaticClass(), FTransform::Identity, Params);
	if (!Bot)
	{
		return;
	}
	Bots.Add(Bot);
	if (AKSPlayerState* PS = Bot->GetPlayerState<AKSPlayerState>())
	{
		PS->SetIsABot(true);
		PS->SetPlayerName(FString::Printf(TEXT("Бот %s"), Names[BotNameCounter % UE_ARRAY_COUNT(Names)]));
		AssignColor(PS);
	}
	++BotNameCounter;
	// Tick spawns the robot (StartMatch spawns everyone itself).
}

void AKSGameMode::RemoveBot()
{
	if (Bots.Num() == 0)
	{
		return;
	}
	AKSBotController* Bot = Bots.Pop();
	if (IsValid(Bot))
	{
		if (APawn* Pawn = Bot->GetPawn())
		{
			Pawn->Destroy();
		}
		Bot->Destroy();
	}
}

// ---------------------------------------------------------------------------------------------
// Pickups
// ---------------------------------------------------------------------------------------------

void AKSGameMode::SpawnPickups()
{
	const FKSArenaDef& Def = KSArena::Get(ArenaIndex);
	for (const FKSPickupSpot& Spot : Def.Pickups)
	{
		const FTransform Transform(Spot.Location);
		AKSPickup* Pickup = GetWorld()->SpawnActorDeferred<AKSPickup>(AKSPickup::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Pickup)
		{
			Pickup->SetType(Spot.Type);
			Pickup->FinishSpawning(Transform);
			Pickups.Add(Pickup);
		}
	}
}

void AKSGameMode::ClearPickups()
{
	for (AKSPickup* Pickup : Pickups)
	{
		if (IsValid(Pickup))
		{
			Pickup->Destroy();
		}
	}
	Pickups.Reset();
}
