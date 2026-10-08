#include "KSLanDiscovery.h"
#include "Kubostrel.h"
#include "KSTypes.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Misc/OutputDeviceRedirector.h"

static const TCHAR* KSProbeTag = TEXT("KSP");
static const TCHAR* KSReplyTag = TEXT("KSR");
static constexpr double KSProbeInterval = 2.5;
static constexpr double KSGameTimeout = 7.0;

static ISocketSubsystem* KSSockets()
{
	return ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
}

static void KSSendText(FSocket* Socket, const FString& Text, const FInternetAddr& To)
{
	FTCHARToUTF8 Utf8(*Text);
	int32 Sent = 0;
	Socket->SendTo(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length(), Sent, To);
}

// Reads one datagram as text. Returns false when nothing is waiting.
static bool KSReceiveText(FSocket* Socket, FString& OutText, TSharedRef<FInternetAddr>& OutFrom)
{
	uint32 Pending = 0;
	if (!Socket->HasPendingData(Pending))
	{
		return false;
	}
	uint8 Buffer[1024];
	int32 Read = 0;
	if (!Socket->RecvFrom(Buffer, sizeof(Buffer), Read, *OutFrom) || Read <= 0)
	{
		return false;
	}
	FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Buffer), Read);
	OutText = FString(Text.Length(), Text.Get());
	return true;
}

FKSLanDiscovery::~FKSLanDiscovery()
{
	StopResponder();
	StopSearch();
}

FSocket* FKSLanDiscovery::OpenSocket(int32 Port, const TCHAR* Description)
{
	ISocketSubsystem* Subsystem = KSSockets();
	if (!Subsystem)
	{
		return nullptr;
	}
	FSocket* Socket = Subsystem->CreateSocket(NAME_DGram, Description, FNetworkProtocolTypes::IPv4);
	if (!Socket)
	{
		UE_LOG(LogKS, Warning, TEXT("Discovery: could not create socket"));
		return nullptr;
	}
	Socket->SetNonBlocking(true);
	Socket->SetReuseAddr(true);
	Socket->SetBroadcast(true);
	TSharedRef<FInternetAddr> Addr = Subsystem->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
	Addr->SetAnyAddress();
	Addr->SetPort(Port);
	if (!Socket->Bind(*Addr))
	{
		UE_LOG(LogKS, Warning, TEXT("Discovery: could not bind port %d"), Port);
		Socket->Close();
		Subsystem->DestroySocket(Socket);
		return nullptr;
	}
	return Socket;
}

void FKSLanDiscovery::CloseSocket(FSocket*& Socket)
{
	if (Socket)
	{
		Socket->Close();
		if (ISocketSubsystem* Subsystem = KSSockets())
		{
			Subsystem->DestroySocket(Socket);
		}
		Socket = nullptr;
	}
}

void FKSLanDiscovery::StartResponder()
{
	if (!ResponderSocket)
	{
		ResponderSocket = OpenSocket(KS::DiscoveryPort, TEXT("KS discovery responder"));
	}
}

void FKSLanDiscovery::StopResponder()
{
	CloseSocket(ResponderSocket);
}

void FKSLanDiscovery::SetHostInfo(const FString& HostName, int32 Arena, int32 Players, int32 MaxPlayers, int32 GamePort)
{
	// Everything after the nonce; the nonce is filled in per probe.
	HostReply = FString::Printf(TEXT("%d|%d|%d|%d|%s"), GamePort, Players, MaxPlayers, Arena, *HostName.Replace(TEXT("|"), TEXT(" ")));
}

void FKSLanDiscovery::StartSearch()
{
	if (!SearchSocket)
	{
		// Port 0: the system picks a free one; hosts reply to whatever port the probe came from.
		SearchSocket = OpenSocket(0, TEXT("KS discovery search"));
		Nonce = FMath::Rand() ^ (uint32(FPlatformTime::Cycles()) << 7);
		NextProbeTime = 0.0;
		Games.Reset();
	}
}

void FKSLanDiscovery::StopSearch()
{
	CloseSocket(SearchSocket);
}

void FKSLanDiscovery::Tick(double Now)
{
	if (ResponderSocket)
	{
		ReadProbes();
	}
	if (SearchSocket)
	{
		if (Now >= NextProbeTime)
		{
			SendProbes();
			NextProbeTime = Now + KSProbeInterval;
		}
		ReadReplies(Now);
		Games.RemoveAll([Now](const FKSFoundGame& Game) { return Now - Game.LastSeen > KSGameTimeout; });
	}
}

void FKSLanDiscovery::SendProbes()
{
	ISocketSubsystem* Subsystem = KSSockets();
	if (!Subsystem || !SearchSocket)
	{
		return;
	}
	const FString Probe = FString::Printf(TEXT("%s|%s|%u"), KSProbeTag, KS::GameVersion, Nonce);
	TSharedRef<FInternetAddr> To = Subsystem->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
	To->SetPort(KS::DiscoveryPort);

	for (const uint32 Local : GetLocalAddresses())
	{
		const uint32 Subnet = Local & 0xFFFFFF00u;
		for (uint32 Host = 1; Host <= 254; ++Host)
		{
			const uint32 Target = Subnet | Host;
			if (Target != Local)
			{
				To->SetIp(Target);
				KSSendText(SearchSocket, Probe, *To);
			}
		}
	}
	// Works on desktops and on some phones; harmless where it is blocked.
	To->SetIp(0xFFFFFFFFu);
	KSSendText(SearchSocket, Probe, *To);
}

void FKSLanDiscovery::ReadProbes()
{
	ISocketSubsystem* Subsystem = KSSockets();
	if (!Subsystem)
	{
		return;
	}
	TSharedRef<FInternetAddr> From = Subsystem->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
	FString Text;
	for (int32 Guard = 0; Guard < 64 && KSReceiveText(ResponderSocket, Text, From); ++Guard)
	{
		TArray<FString> Parts;
		Text.ParseIntoArray(Parts, TEXT("|"), false);
		if (Parts.Num() < 3 || Parts[0] != KSProbeTag || Parts[1] != KS::GameVersion || HostReply.IsEmpty())
		{
			continue;
		}
		const uint32 ProbeNonce = (uint32)FCString::Strtoui64(*Parts[2], nullptr, 10);
		KSSendText(ResponderSocket, FString::Printf(TEXT("%s|%s|%u|%s"), KSReplyTag, KS::GameVersion, ProbeNonce, *HostReply), *From);
	}
}

void FKSLanDiscovery::ReadReplies(double Now)
{
	ISocketSubsystem* Subsystem = KSSockets();
	if (!Subsystem)
	{
		return;
	}
	TSharedRef<FInternetAddr> From = Subsystem->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
	FString Text;
	for (int32 Guard = 0; Guard < 64 && KSReceiveText(SearchSocket, Text, From); ++Guard)
	{
		TArray<FString> Parts;
		Text.ParseIntoArray(Parts, TEXT("|"), false);
		if (Parts.Num() < 8 || Parts[0] != KSReplyTag || Parts[1] != KS::GameVersion)
		{
			continue;
		}
		if ((uint32)FCString::Strtoui64(*Parts[2], nullptr, 10) != Nonce)
		{
			continue;
		}
		uint32 Ip = 0;
		if (!ParseAddress(From->ToString(false), Ip))
		{
			continue;
		}
		const FString Address = FString::Printf(TEXT("%s:%d"), *AddressToString(Ip), FCString::Atoi(*Parts[3]));
		FKSFoundGame* Game = Games.FindByPredicate([&Address](const FKSFoundGame& G) { return G.Address == Address; });
		if (!Game)
		{
			Game = &Games.AddDefaulted_GetRef();
			Game->Address = Address;
		}
		Game->Players = FCString::Atoi(*Parts[4]);
		Game->MaxPlayers = FCString::Atoi(*Parts[5]);
		Game->Arena = FCString::Atoi(*Parts[6]);
		Game->HostName = Parts[7];
		Game->LastSeen = Now;
	}
}

bool FKSLanDiscovery::ParseAddress(const FString& InText, uint32& OutIp)
{
	FString Text = InText.TrimStartAndEnd();
	// IPv4 addresses mapped into IPv6 look like "::ffff:192.168.1.5".
	int32 Colon = INDEX_NONE;
	if (Text.FindLastChar(TEXT(':'), Colon) && Text.Contains(TEXT(".")))
	{
		Text = Text.Mid(Colon + 1);
	}
	TArray<FString> Parts;
	Text.ParseIntoArray(Parts, TEXT("."), false);
	if (Parts.Num() != 4)
	{
		return false;
	}
	uint32 Ip = 0;
	for (const FString& Part : Parts)
	{
		if (Part.IsEmpty() || Part.Len() > 3 || !Part.IsNumeric())
		{
			return false;
		}
		const int32 Value = FCString::Atoi(*Part);
		if (Value < 0 || Value > 255)
		{
			return false;
		}
		Ip = (Ip << 8) | uint32(Value);
	}
	OutIp = Ip;
	return true;
}

FString FKSLanDiscovery::AddressToString(uint32 Ip)
{
	return FString::Printf(TEXT("%u.%u.%u.%u"), (Ip >> 24) & 255u, (Ip >> 16) & 255u, (Ip >> 8) & 255u, Ip & 255u);
}

TArray<uint32> FKSLanDiscovery::GetLocalAddresses()
{
	TArray<uint32> Result;
	ISocketSubsystem* Subsystem = KSSockets();
	if (!Subsystem)
	{
		return Result;
	}

	auto AddText = [&Result](const FString& Text)
	{
		uint32 Ip = 0;
		if (!ParseAddress(Text, Ip))
		{
			return;
		}
		const uint32 First = Ip >> 24;
		const bool bLoopback = First == 127;
		const bool bLinkLocal = (Ip >> 16) == ((169u << 8) | 254u);
		if (Ip != 0 && !bLoopback && !bLinkLocal && First < 224)
		{
			Result.AddUnique(Ip);
		}
	};

	TArray<TSharedPtr<FInternetAddr>> Adapters;
	if (Subsystem->GetLocalAdapterAddresses(Adapters))
	{
		for (const TSharedPtr<FInternetAddr>& Adapter : Adapters)
		{
			if (Adapter.IsValid())
			{
				AddText(Adapter->ToString(false));
			}
		}
	}
	bool bCanBindAll = false;
	TSharedRef<FInternetAddr> HostAddr = Subsystem->GetLocalHostAddr(*GLog, bCanBindAll);
	AddText(HostAddr->ToString(false));
	return Result;
}
