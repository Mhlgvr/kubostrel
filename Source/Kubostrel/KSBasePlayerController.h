#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "KSTypes.h"
#include "KSBasePlayerController.generated.h"

class UKSAudioSynth;

// Shared by the menu and the match: sound output and a unified list of fingers / mouse pointer.
UCLASS()
class AKSBasePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AKSBasePlayerController();

	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;

	UKSAudioSynth* GetSynth() const { return Synth; }

	// Pointers in screen pixels, updated once per frame.
	const TArray<FKSPointer>& GetPointers() const { return Pointers; }
	bool IsTouchMode() const { return bTouchMode; }
	FVector2D GetScreenSize() const;
	// Pixels per UI unit; the UI is laid out for a screen 1080 units tall.
	float GetUIScale() const;

protected:
	void UpdatePointers();

	UPROPERTY()
	TObjectPtr<UKSAudioSynth> Synth;

	TArray<FKSPointer> Pointers;
	bool bTouchMode = false;
	float SmoothedFPS = 60.f;

public:
	float GetFPS() const { return SmoothedFPS; }
};
