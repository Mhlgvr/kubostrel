#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "KSUI.h"
#include "KSMenuHUD.generated.h"

class UKSGameInstance;

// All menu screens, drawn and handled every frame on the canvas.
UCLASS()
class AKSMenuHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	enum class EKSScreen : uint8
	{
		Main,
		Host,
		Training,
		Find,
		Address,
		Name,
		Settings,
		Help
	};

	void DrawMain(UKSGameInstance* GI);
	void DrawHost(UKSGameInstance* GI, bool bOnline);
	void DrawFind(UKSGameInstance* GI);
	void DrawAddress(UKSGameInstance* GI);
	void DrawName(UKSGameInstance* GI);
	void DrawSettings(UKSGameInstance* GI);
	void DrawHelp();
	void DrawConnecting(UKSGameInstance* GI);
	void DrawPopup();

	void Go(EKSScreen NewScreen);
	FBox2D DrawScreenPanel(const FString& Title, float PanelWidth, float PanelHeight);
	bool BackButton(const FBox2D& Panel);
	int32 Stepper(float Y, float Left, float Right, const FString& Label, const FString& Value);
	bool Toggle(float Y, float Left, float Right, const FString& Label, const FString& Value, bool bOn);
	bool Key(const FBox2D& Box, const FString& Label, const FLinearColor& Accent, float TextSize = 40.f);
	void Click();
	FString MyAddressText(UKSGameInstance* GI) const;

	FKSUI UI;
	EKSScreen Screen = EKSScreen::Main;
	bool bStarted = false;
	FString Popup;
	FString AddressInput;
	FString NameInput;
	// 0 = Russian letters, 1 = English letters, 2 = digits and symbols.
	int32 KeyboardLayout = 0;
	bool bUpperCase = true;
	float ScreenStartTime = 0.f;
};
