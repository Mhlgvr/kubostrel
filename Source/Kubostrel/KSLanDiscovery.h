#pragma once

#include "CoreMinimal.h"

class FSocket;

struct FKSFoundGame
{
	// "ip:port", ready for ClientTravel.
	FString Address;
	FString HostName;
	int32 Arena = 0;
	int32 Players = 0;
	int32 MaxPlayers = 6;
	double LastSeen = 0.0;
};

// Finds games on the local network without broadcast (iOS needs a special entitlement for it):
// the searching phone sends a small UDP probe to every address of its /24 subnet, hosts answer.
// The same works over a ZeroTier network, which looks like one more local subnet.
class FKSLanDiscovery
{
public:
	~FKSLanDiscovery();

	void Tick(double Now);

	void StartResponder();
	void StopResponder();
	bool IsResponding() const { return ResponderSocket != nullptr; }
	void SetHostInfo(const FString& HostName, int32 Arena, int32 Players, int32 MaxPlayers, int32 GamePort);

	void StartSearch();
	void StopSearch();
	bool IsSearching() const { return SearchSocket != nullptr; }
	const TArray<FKSFoundGame>& GetGames() const { return Games; }

	// This device's IPv4 addresses (host byte order), loopback and link-local excluded.
	static TArray<uint32> GetLocalAddresses();
	static FString AddressToString(uint32 Ip);
	static bool ParseAddress(const FString& Text, uint32& OutIp);

private:
	FSocket* OpenSocket(int32 Port, const TCHAR* Description);
	void CloseSocket(FSocket*& Socket);
	void SendProbes();
	void ReadProbes();
	void ReadReplies(double Now);

	FSocket* ResponderSocket = nullptr;
	FSocket* SearchSocket = nullptr;
	uint32 Nonce = 0;
	double NextProbeTime = 0.0;
	FString HostReply;
	TArray<FKSFoundGame> Games;
};
