#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Engine/EngineBaseTypes.h"
#include "Containers/Ticker.h"
#include "KSSaveGame.h"
#include "KSLanDiscovery.h"
#include "KSGameInstance.generated.h"

class UKSAssets;
class UNetDriver;

struct FKSMatchOptions
{
	int32 Arena = 0;
	int32 Bots = 2;
	int32 FragLimit = 20;
	int32 Minutes = 8;
	int32 BotSkill = 1;
	// Listen server that friends can join; false is offline training.
	bool bOnline = true;
};

UCLASS()
class UKSGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;
	virtual void Shutdown() override;

	UKSAssets* GetAssets() const { return Assets; }

	const FKSSettings& GetSettings() const { return Settings; }
	FKSSettings& EditSettings() { return Settings; }
	void SaveSettings();
	void ApplyGraphicsSettings();
	// Letters, digits, spaces, '-' and '_', at most 14 characters.
	static FString SanitizeName(const FString& Name);

	void HostGame(const FKSMatchOptions& Options);
	void JoinGame(const FString& Address);
	void CancelJoin();
	void ReturnToMenu();

	bool IsJoining() const { return bJoining; }
	const FString& GetJoinAddress() const { return JoinAddress; }
	void SetMenuMessage(const FString& Message) { MenuMessage = Message; }
	FString TakeMenuMessage();

	void StartLanSearch();
	void StopLanSearch();
	bool IsSearching() const;
	const TArray<FKSFoundGame>& GetFoundGames() const;
	TArray<FString> GetLocalAddressStrings() const;

private:
	bool TickInstance(float DeltaTime);
	void UpdateResponder();
	void LoadSettings();
	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);
	void HandlePostLoadMap(UWorld* World);

	UPROPERTY()
	TObjectPtr<UKSAssets> Assets;

	UPROPERTY()
	FKSSettings Settings;

	TUniquePtr<FKSLanDiscovery> Discovery;
	// Reading the network adapters is slow-ish, and the menu asks every frame.
	mutable TArray<FString> CachedAddresses;
	mutable double AddressCacheTime = -100.0;
	FTSTicker::FDelegateHandle TickHandle;
	FString MenuMessage;
	FString JoinAddress;
	double JoinStartTime = 0.0;
	bool bJoining = false;
};
