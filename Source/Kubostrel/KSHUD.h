#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "KSUI.h"
#include "KSHUD.generated.h"

class AKSPlayerController;
class AKSCharacter;
class AKSGameState;
class UKSGameInstance;

// Everything drawn over the match: crosshair, health, weapons, touch buttons, kill feed,
// scoreboard, death and results screens, and the in-match menu.
UCLASS()
class AKSHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawCrosshair(AKSPlayerController* PC, AKSCharacter* Robot, float Now);
	void DrawDamage(AKSPlayerController* PC, AKSCharacter* Robot, float Now);
	void DrawTopBar(AKSPlayerController* PC, AKSGameState* GS);
	void DrawKillFeed(AKSPlayerController* PC, AKSGameState* GS, float Now);
	void DrawStatus(AKSPlayerController* PC, AKSCharacter* Robot, float Now);
	void DrawTouchControls(AKSPlayerController* PC, AKSCharacter* Robot, float Now);
	void DrawMessages(AKSPlayerController* PC, float Now);
	void DrawDeath(AKSPlayerController* PC, AKSCharacter* Robot, AKSGameState* GS);
	void DrawScoreboard(AKSPlayerController* PC, AKSGameState* GS, float Top);
	void DrawMatchEnd(AKSPlayerController* PC, AKSGameState* GS);
	void DrawMenu(AKSPlayerController* PC, UKSGameInstance* GI);
	// A settings row with a label, a value and minus / plus buttons. Returns -1, 0 or 1.
	int32 Stepper(float Y, float Left, float Right, const FString& Label, const FString& Value);
	// A settings row with an on / off button. Returns true when tapped.
	bool Toggle(float Y, float Left, float Right, const FString& Label, const FString& Value, bool bOn);

	FKSUI UI;
};
