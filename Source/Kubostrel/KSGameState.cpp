#include "KSGameState.h"
#include "KSArenaBuilder.h"
#include "KSAudio.h"
#include "KSPlayerController.h"
#include "KSPlayerState.h"
#include "KSTypes.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

namespace
{
	constexpr int32 KSMaxFeed = 5;
	const FLinearColor KSNoticeColor(0.55f, 0.6f, 0.7f, 1.f);
}

AKSGameState::AKSGameState()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AKSGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AKSGameState, ArenaIndex);
	DOREPLIFETIME(AKSGameState, MatchPhase);
	DOREPLIFETIME(AKSGameState, PhaseEndTime);
	DOREPLIFETIME(AKSGameState, FragLimit);
	DOREPLIFETIME(AKSGameState, Winner);
}

void AKSGameState::SetArena(int32 Index)
{
	ArenaIndex = Index;
	OnRep_ArenaIndex();
	ForceNetUpdate();
}

void AKSGameState::OnRep_ArenaIndex()
{
	if (ArenaIndex != INDEX_NONE)
	{
		AKSArenaBuilder::SpawnArena(GetWorld(), ArenaIndex);
	}
}

void AKSGameState::SetPhase(EKSMatchPhase Phase, float EndTime, APlayerState* InWinner)
{
	const bool bChanged = Phase != MatchPhase;
	MatchPhase = Phase;
	PhaseEndTime = EndTime;
	Winner = InWinner;
	ForceNetUpdate();
	if (bChanged)
	{
		OnRep_MatchPhase();
	}
}

void AKSGameState::OnRep_MatchPhase()
{
	PhaseChangeTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (MatchPhase == EKSMatchPhase::Ended)
	{
		KSAudio::Play2D(this, KSSound::MatchEnd);
	}
	else
	{
		// A new match: old kills no longer matter.
		Feed.Reset();
	}
}

float AKSGameState::GetTimeLeft() const
{
	return FMath::Max(0.f, PhaseEndTime - GetServerTime());
}

TArray<AKSPlayerState*> AKSGameState::GetRanking() const
{
	TArray<AKSPlayerState*> Result;
	for (APlayerState* PS : PlayerArray)
	{
		if (AKSPlayerState* KSPS = Cast<AKSPlayerState>(PS))
		{
			Result.Add(KSPS);
		}
	}
	Result.Sort([](const AKSPlayerState& A, const AKSPlayerState& B)
	{
		if (A.Kills != B.Kills)
		{
			return A.Kills > B.Kills;
		}
		if (A.Deaths != B.Deaths)
		{
			return A.Deaths < B.Deaths;
		}
		return A.GetPlayerName() < B.GetPlayerName();
	});
	return Result;
}

void AKSGameState::AddFeed(const FKSFeedEntry& Entry)
{
	FKSFeedEntry& Added = Feed.Add_GetRef(Entry);
	Added.Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (Feed.Num() > KSMaxFeed)
	{
		Feed.RemoveAt(0, Feed.Num() - KSMaxFeed);
	}
}

void AKSGameState::MulticastKill_Implementation(APlayerState* Killer, APlayerState* Victim, uint8 Weapon, bool bHeadshot, uint8 Streak)
{
	const AKSPlayerState* KillerPS = Cast<AKSPlayerState>(Killer);
	const AKSPlayerState* VictimPS = Cast<AKSPlayerState>(Victim);

	FKSFeedEntry Entry;
	Entry.bHeadshot = bHeadshot;
	if (VictimPS)
	{
		Entry.Right = VictimPS->GetPlayerName();
		Entry.RightColor = VictimPS->GetColor();
	}
	if (KillerPS && KillerPS != VictimPS)
	{
		Entry.Left = KillerPS->GetPlayerName();
		Entry.LeftColor = KillerPS->GetColor();
	}
	if (Weapon < KSWeapon::Count)
	{
		const FKSWeaponInfo& Info = KS::Weapon(Weapon);
		Entry.Middle = (KillerPS && KillerPS == VictimPS) ? FString(TEXT("Свой взрыв")) : FString(Info.Name);
		Entry.MiddleColor = Info.Color;
	}
	else
	{
		Entry.Middle = TEXT("Падение");
		Entry.MiddleColor = KSNoticeColor;
	}
	AddFeed(Entry);

	if (AKSPlayerController* PC = Cast<AKSPlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		PC->OnKillFeed(Killer, Victim, Weapon, bHeadshot, Streak);
	}
}

void AKSGameState::MulticastNotice_Implementation(const FString& Name, int32 ColorIndex, bool bJoined)
{
	FKSFeedEntry Entry;
	Entry.Left = Name;
	Entry.LeftColor = KS::PlayerColor(ColorIndex);
	Entry.Middle = bJoined ? TEXT("заходит в игру") : TEXT("выходит из игры");
	Entry.MiddleColor = KSNoticeColor;
	AddFeed(Entry);
	KSAudio::Play2D(this, KSSound::UIClick, 0.6f, bJoined ? 1.2f : 0.8f);
}
