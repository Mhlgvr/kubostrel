#include "KSGameInstance.h"
#include "Kubostrel.h"
#include "KSAssets.h"
#include "KSTypes.h"
#include "KSArenaBuilder.h"
#include "KSGameState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "UObject/UObjectGlobals.h"

static const TCHAR* KSSaveSlot = TEXT("KubostrelSettings");
static constexpr double KSJoinTimeout = 25.0;

static void KSSetCVar(const TCHAR* Name, float Value)
{
	if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		Var->Set(Value, ECVF_SetByCode);
	}
}

void UKSGameInstance::Init()
{
	Super::Init();

	Assets = NewObject<UKSAssets>(this);
	Assets->Initialize();

	LoadSettings();
	ApplyGraphicsSettings();

	Discovery = MakeUnique<FKSLanDiscovery>();

	if (GEngine)
	{
		GEngine->OnNetworkFailure().AddUObject(this, &UKSGameInstance::HandleNetworkFailure);
		GEngine->OnTravelFailure().AddUObject(this, &UKSGameInstance::HandleTravelFailure);
	}
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UKSGameInstance::HandlePostLoadMap);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UKSGameInstance::TickInstance), 0.f);
}

void UKSGameInstance::Shutdown()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
	if (GEngine)
	{
		GEngine->OnNetworkFailure().RemoveAll(this);
		GEngine->OnTravelFailure().RemoveAll(this);
	}
	Discovery.Reset();
	Super::Shutdown();
}

FString UKSGameInstance::SanitizeName(const FString& Name)
{
	FString Result;
	for (const TCHAR C : Name)
	{
		const bool bLatin = (C >= TEXT('A') && C <= TEXT('Z')) || (C >= TEXT('a') && C <= TEXT('z'));
		const bool bDigit = C >= TEXT('0') && C <= TEXT('9');
		const bool bCyrillic = C >= 0x0400 && C <= 0x04FF;
		const bool bExtra = C == TEXT(' ') || C == TEXT('-') || C == TEXT('_');
		if (bLatin || bDigit || bCyrillic || bExtra)
		{
			Result.AppendChar(C);
		}
	}
	Result = Result.TrimStartAndEnd().Left(14);
	return Result;
}

void UKSGameInstance::LoadSettings()
{
	if (UGameplayStatics::DoesSaveGameExist(KSSaveSlot, 0))
	{
		if (UKSSaveGame* Save = Cast<UKSSaveGame>(UGameplayStatics::LoadGameFromSlot(KSSaveSlot, 0)))
		{
			Settings = Save->Settings;
		}
	}
	Settings.PlayerName = SanitizeName(Settings.PlayerName);
	if (Settings.PlayerName.IsEmpty())
	{
		Settings.PlayerName = FString::Printf(TEXT("Игрок %d"), FMath::RandRange(100, 999));
	}
	Settings.Sensitivity = FMath::Clamp(Settings.Sensitivity, 0.2f, 3.f);
	Settings.Volume = FMath::Clamp(Settings.Volume, 0.f, 1.f);
	Settings.FieldOfView = FMath::Clamp(Settings.FieldOfView, 80.f, 115.f);
	Settings.Quality = FMath::Clamp(Settings.Quality, 0, 1);
	Settings.HostArena = FMath::Clamp(Settings.HostArena, 0, KSArena::Num() - 1);
	Settings.TrainingArena = FMath::Clamp(Settings.TrainingArena, 0, KSArena::Num() - 1);
	Settings.HostBots = FMath::Clamp(Settings.HostBots, 0, KS::MaxPlayers - 1);
	Settings.TrainingBots = FMath::Clamp(Settings.TrainingBots, 1, KS::MaxPlayers - 1);
	Settings.HostFrags = FMath::Clamp(Settings.HostFrags, 5, 50);
	Settings.HostMinutes = FMath::Clamp(Settings.HostMinutes, 3, 20);
	Settings.BotSkill = FMath::Clamp(Settings.BotSkill, 0, 2);
}

void UKSGameInstance::SaveSettings()
{
	if (UKSSaveGame* Save = Cast<UKSSaveGame>(UGameplayStatics::CreateSaveGameObject(UKSSaveGame::StaticClass())))
	{
		Save->Settings = Settings;
		UGameplayStatics::SaveGameToSlot(Save, KSSaveSlot, 0);
	}
}

void UKSGameInstance::ApplyGraphicsSettings()
{
	const bool bHigh = Settings.Quality > 0;
	KSSetCVar(TEXT("r.ScreenPercentage"), bHigh ? 100.f : 75.f);
	KSSetCVar(TEXT("r.Mobile.AmbientOcclusionQuality"), bHigh ? 1.f : 0.f);
	KSSetCVar(TEXT("r.Shadow.CSM.MaxMobileCascades"), bHigh ? 2.f : 1.f);
	KSSetCVar(TEXT("r.DepthOfFieldQuality"), 0.f);
	KSSetCVar(TEXT("r.MotionBlurQuality"), 0.f);
	if (AKSArenaBuilder* Builder = AKSArenaBuilder::Find(this))
	{
		Builder->ApplyQuality(Settings.Quality);
	}
}

void UKSGameInstance::HostGame(const FKSMatchOptions& O)
{
	StopLanSearch();
	bJoining = false;
	const FString Options = FString::Printf(TEXT("%sgame=%s?arena=%d?bots=%d?frags=%d?time=%d?skill=%d"),
		O.bOnline ? TEXT("listen?") : TEXT(""), KS::GameModePath, O.Arena, O.Bots, O.FragLimit, O.Minutes * 60, O.BotSkill);
	UE_LOG(LogKS, Log, TEXT("Starting match: %s"), *Options);
	UGameplayStatics::OpenLevel(this, FName(KS::EntryMap), true, Options);
}

void UKSGameInstance::JoinGame(const FString& InAddress)
{
	FString Address = InAddress.TrimStartAndEnd();
	if (Address.IsEmpty())
	{
		return;
	}
	if (!Address.Contains(TEXT(":")))
	{
		Address += FString::Printf(TEXT(":%d"), KS::GamePort);
	}
	APlayerController* PC = GetFirstLocalPlayerController();
	if (!PC)
	{
		return;
	}
	StopLanSearch();
	JoinAddress = Address;
	JoinStartTime = FPlatformTime::Seconds();
	bJoining = true;
	MenuMessage.Reset();
	UE_LOG(LogKS, Log, TEXT("Joining %s"), *Address);
	PC->ClientTravel(Address, TRAVEL_Absolute);
}

void UKSGameInstance::CancelJoin()
{
	if (!bJoining)
	{
		return;
	}
	bJoining = false;
	// The engine drops a connection attempt when it travels to a "?closed" URL, then loads the menu map again.
	// UEngine::CancelPending would stay on the current map, but it is not public in UE 5.8.
	if (GEngine && GetWorld())
	{
		GEngine->SetClientTravel(GetWorld(), TEXT("?closed"), TRAVEL_Absolute);
	}
}

void UKSGameInstance::ReturnToMenu()
{
	bJoining = false;
	UGameplayStatics::OpenLevel(this, FName(KS::EntryMap), true, TEXT("game=/Script/Kubostrel.KSMenuGameMode"));
}

FString UKSGameInstance::TakeMenuMessage()
{
	FString Message = MenuMessage;
	MenuMessage.Reset();
	return Message;
}

void UKSGameInstance::StartLanSearch()
{
	if (Discovery)
	{
		Discovery->StartSearch();
	}
}

void UKSGameInstance::StopLanSearch()
{
	if (Discovery)
	{
		Discovery->StopSearch();
	}
}

bool UKSGameInstance::IsSearching() const
{
	return Discovery && Discovery->IsSearching();
}

const TArray<FKSFoundGame>& UKSGameInstance::GetFoundGames() const
{
	static const TArray<FKSFoundGame> Empty;
	return Discovery ? Discovery->GetGames() : Empty;
}

TArray<FString> UKSGameInstance::GetLocalAddressStrings() const
{
	const double Now = FPlatformTime::Seconds();
	if (Now - AddressCacheTime > 3.0)
	{
		AddressCacheTime = Now;
		CachedAddresses.Reset();
		for (const uint32 Ip : FKSLanDiscovery::GetLocalAddresses())
		{
			CachedAddresses.Add(FKSLanDiscovery::AddressToString(Ip));
		}
	}
	return CachedAddresses;
}

void UKSGameInstance::UpdateResponder()
{
	if (!Discovery)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() != NM_ListenServer)
	{
		Discovery->StopResponder();
		return;
	}
	Discovery->StartResponder();
	int32 Humans = 0;
	int32 Arena = 0;
	if (AGameStateBase* GameState = World->GetGameState())
	{
		for (APlayerState* PS : GameState->PlayerArray)
		{
			if (PS && !PS->IsABot())
			{
				++Humans;
			}
		}
		if (AKSGameState* KSGameState = Cast<AKSGameState>(GameState))
		{
			Arena = KSGameState->ArenaIndex;
		}
	}
	const int32 Port = World->URL.Port > 0 ? World->URL.Port : KS::GamePort;
	Discovery->SetHostInfo(Settings.PlayerName, Arena, Humans, KS::MaxPlayers, Port);
}

bool UKSGameInstance::TickInstance(float DeltaTime)
{
	const double Now = FPlatformTime::Seconds();
	UpdateResponder();
	if (Discovery)
	{
		Discovery->Tick(Now);
	}
	if (bJoining && Now - JoinStartTime > KSJoinTimeout)
	{
		CancelJoin();
		MenuMessage = TEXT("Хост не отвечает. Проверь адрес и что вы в одной сети Wi-Fi.");
	}
	return true;
}

void UKSGameInstance::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	UE_LOG(LogKS, Warning, TEXT("Network failure %s: %s"), ENetworkFailure::ToString(FailureType), *ErrorString);
	switch (FailureType)
	{
	case ENetworkFailure::ConnectionLost:
		MenuMessage = bJoining ? TEXT("Не удалось подключиться к игре.") : TEXT("Связь с хостом потеряна. Похоже, он вышел из игры.");
		break;
	case ENetworkFailure::ConnectionTimeout:
		MenuMessage = bJoining ? TEXT("Хост не отвечает. Проверь адрес и что вы в одной сети Wi-Fi.") : TEXT("Хост перестал отвечать.");
		break;
	case ENetworkFailure::PendingConnectionFailure:
		MenuMessage = TEXT("Не удалось подключиться к игре.");
		break;
	case ENetworkFailure::OutdatedClient:
	case ENetworkFailure::OutdatedServer:
		MenuMessage = TEXT("У вас разные версии игры. Установите одну и ту же сборку.");
		break;
	case ENetworkFailure::NetDriverListenFailure:
		MenuMessage = TEXT("Не удалось создать игру: сетевой порт занят.");
		break;
	case ENetworkFailure::FailureReceived:
		MenuMessage = ErrorString.Contains(TEXT("full")) ? TEXT("В этой игре уже 6 игроков.") : TEXT("Хост отклонил подключение.");
		break;
	default:
		MenuMessage = TEXT("Ошибка сети.");
		break;
	}
	bJoining = false;
}

void UKSGameInstance::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	UE_LOG(LogKS, Warning, TEXT("Travel failure %s: %s"), ETravelFailure::ToString(FailureType), *ErrorString);
	MenuMessage = TEXT("Не удалось подключиться к игре.");
	bJoining = false;
}

void UKSGameInstance::HandlePostLoadMap(UWorld* World)
{
	bJoining = false;
	ApplyGraphicsSettings();
}
