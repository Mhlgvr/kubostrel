#include "KSPlayerState.h"
#include "KSTypes.h"
#include "Net/UnrealNetwork.h"

AKSPlayerState::AKSPlayerState()
{
	// Scores change rarely and every change forces an update, so a low rate is enough.
	SetNetUpdateFrequency(4.f);
}

void AKSPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AKSPlayerState, Kills);
	DOREPLIFETIME(AKSPlayerState, Deaths);
	DOREPLIFETIME(AKSPlayerState, ColorIndex);
	DOREPLIFETIME(AKSPlayerState, Streak);
	DOREPLIFETIME(AKSPlayerState, RespawnTime);
}

FLinearColor AKSPlayerState::GetColor() const
{
	return KS::PlayerColor(ColorIndex);
}

void AKSPlayerState::ResetScore()
{
	Kills = 0;
	Deaths = 0;
	Streak = 0;
	RespawnTime = 0.f;
	ForceNetUpdate();
}
