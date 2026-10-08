#include "KSMenuHUD.h"
#include "KSArenaData.h"
#include "KSAssets.h"
#include "KSAudio.h"
#include "KSBasePlayerController.h"
#include "KSGameInstance.h"
#include "KSTypes.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"

namespace
{
	FLinearColor KSAlpha(const FLinearColor& Color, float Alpha)
	{
		FLinearColor Result = Color;
		Result.A = Alpha;
		return Result;
	}

	// The engine's own case conversion only knows Latin letters.
	TCHAR KSToLower(TCHAR C)
	{
		if (C >= TEXT('A') && C <= TEXT('Z'))
		{
			return (TCHAR)(C + 32);
		}
		if (C >= 0x0410 && C <= 0x042F)
		{
			return (TCHAR)(C + 0x20);
		}
		if (C == 0x0401)
		{
			return (TCHAR)0x0451;
		}
		return C;
	}

	const TCHAR* KSKeyRows[3][3] =
	{
		{ TEXT("ЙЦУКЕНГШЩЗХ"), TEXT("ФЫВАПРОЛДЖЭ"), TEXT("ЯЧСМИТЬБЮЁЪ") },
		{ TEXT("QWERTYUIOP"), TEXT("ASDFGHJKL"), TEXT("ZXCVBNM") },
		{ TEXT("1234567890"), TEXT("-_"), TEXT("") },
	};

	const TCHAR* KSSkillNames[3] = { TEXT("Лёгкие"), TEXT("Обычные"), TEXT("Сильные") };
	constexpr int32 KSMaxNameLength = 14;
	constexpr int32 KSMaxAddressLength = 21;
}

void AKSMenuHUD::Click()
{
	KSAudio::Play2D(this, KSSound::UIClick);
}

void AKSMenuHUD::Go(EKSScreen NewScreen)
{
	UKSGameInstance* GI = Cast<UKSGameInstance>(GetGameInstance());
	if (GI && Screen == EKSScreen::Find && NewScreen != EKSScreen::Find)
	{
		GI->StopLanSearch();
	}
	if (GI && NewScreen == EKSScreen::Find)
	{
		GI->StartLanSearch();
	}
	Screen = NewScreen;
	ScreenStartTime = (float)GetWorld()->GetRealTimeSeconds();
}

FString AKSMenuHUD::MyAddressText(UKSGameInstance* GI) const
{
	const TArray<FString> Addresses = GI->GetLocalAddressStrings();
	if (Addresses.Num() == 0)
	{
		return TEXT("Нет подключения к сети");
	}
	return FString::Printf(TEXT("Твой адрес в сети: %s"), *FString::Join(Addresses, TEXT(", ")));
}

void AKSMenuHUD::DrawHUD()
{
	Super::DrawHUD();
	AKSBasePlayerController* PC = Cast<AKSBasePlayerController>(PlayerOwner);
	UKSGameInstance* GI = Cast<UKSGameInstance>(GetGameInstance());
	if (!PC || !Canvas || !GI)
	{
		return;
	}
	UI.Begin(Canvas, PC->GetPointers(), UKSAssets::Get(this));
	const FString Message = GI->TakeMenuMessage();
	if (!Message.IsEmpty())
	{
		Popup = Message;
	}

	// While a window is open on top, the screen underneath is drawn but ignores touches.
	const bool bModal = GI->IsJoining() || !Popup.IsEmpty();
	TArray<FKSPointer> SavedPointers;
	if (bModal)
	{
		SavedPointers = MoveTemp(UI.Pointers);
		UI.Pointers.Reset();
	}
	else if (Screen != EKSScreen::Main && PC->WasInputKeyJustPressed(EKeys::Escape))
	{
		Go(EKSScreen::Main);
	}

	switch (Screen)
	{
	case EKSScreen::Host: DrawHost(GI, true); break;
	case EKSScreen::Training: DrawHost(GI, false); break;
	case EKSScreen::Find: DrawFind(GI); break;
	case EKSScreen::Address: DrawAddress(GI); break;
	case EKSScreen::Name: DrawName(GI); break;
	case EKSScreen::Settings: DrawSettings(GI); break;
	case EKSScreen::Help: DrawHelp(); break;
	default: DrawMain(GI); break;
	}

	if (bModal)
	{
		UI.Pointers = MoveTemp(SavedPointers);
		if (GI->IsJoining())
		{
			DrawConnecting(GI);
		}
		else
		{
			DrawPopup();
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Building blocks
// ---------------------------------------------------------------------------------------------

FBox2D AKSMenuHUD::DrawScreenPanel(const FString& Title, float PanelWidth, float PanelHeight)
{
	UI.Rect(FKSUI::BoxAt(0.f, 0.f, UI.Width, UI.Height), FLinearColor(0.f, 0.f, 0.02f, 0.5f));
	const float PanelW = FMath::Min(PanelWidth, UI.Width - 200.f);
	const FBox2D Panel = FKSUI::BoxCentered(FVector2D(UI.Width * 0.5f, UI.Height * 0.5f), PanelW, FMath::Min(PanelHeight, UI.Height - 40.f));
	UI.Panel(Panel, KSColors::Cyan);
	UI.Text(Title, FVector2D(Panel.Min.X + 60.f, Panel.Min.Y + 66.f), 52.f, KSColors::Cyan, EKSAlign::Left, true, true);
	return Panel;
}

bool AKSMenuHUD::BackButton(const FBox2D& Panel)
{
	if (UI.Button(FKSUI::BoxAt(Panel.Min.X + 60.f, Panel.Max.Y - 116.f, 300.f, 86.f), TEXT("Назад"), KSColors::Dim, true, 36.f))
	{
		Click();
		return true;
	}
	return false;
}

int32 AKSMenuHUD::Stepper(float Y, float Left, float Right, const FString& Label, const FString& Value)
{
	UI.Text(Label, FVector2D(Left, Y), 34.f, KSColors::Text, EKSAlign::Left, false, true);
	const float ButtonW = 84.f;
	const float ValueW = 300.f;
	const FBox2D Plus = FKSUI::BoxCentered(FVector2D(Right - ButtonW * 0.5f, Y), ButtonW, 72.f);
	const FBox2D Minus = FKSUI::BoxCentered(FVector2D(Right - ButtonW * 1.5f - ValueW, Y), ButtonW, 72.f);
	UI.Text(Value, FVector2D(Right - ButtonW - ValueW * 0.5f, Y), 34.f, KSColors::Cyan, EKSAlign::Center, true, true);
	int32 Result = 0;
	if (UI.Button(Minus, TEXT("–"), KSColors::Cyan, true, 46.f))
	{
		Result = -1;
	}
	if (UI.Button(Plus, TEXT("+"), KSColors::Cyan, true, 46.f))
	{
		Result = 1;
	}
	if (Result != 0)
	{
		Click();
	}
	return Result;
}

bool AKSMenuHUD::Toggle(float Y, float Left, float Right, const FString& Label, const FString& Value, bool bOn)
{
	UI.Text(Label, FVector2D(Left, Y), 34.f, KSColors::Text, EKSAlign::Left, false, true);
	const FBox2D Box = FKSUI::BoxCentered(FVector2D(Right - 234.f, Y), 468.f, 72.f);
	if (UI.Button(Box, Value, bOn ? KSColors::Green : KSColors::Dim, true, 34.f))
	{
		Click();
		return true;
	}
	return false;
}

bool AKSMenuHUD::Key(const FBox2D& Box, const FString& Label, const FLinearColor& Accent, float TextSize)
{
	if (UI.Button(Box, Label, Accent, true, TextSize))
	{
		Click();
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------------------------

void AKSMenuHUD::DrawMain(UKSGameInstance* GI)
{
	const float X = FMath::Max(UI.Width * 0.07f, 150.f);
	const float BandW = X + 720.f;
	UI.Rect(FBox2D(FVector2D(0.f, 0.f), FVector2D(BandW, UI.Height)), FLinearColor(0.f, 0.f, 0.03f, 0.55f));
	UI.Rect(FBox2D(FVector2D(BandW, 0.f), FVector2D(BandW + 3.f, UI.Height)), KSAlpha(KSColors::Cyan, 0.35f));

	UI.Glow(FVector2D(X + 300.f, 150.f), 420.f, KSAlpha(KSColors::Cyan, 0.16f));
	UI.Text(TEXT("КУБОСТРЕЛ"), FVector2D(X, 150.f), 104.f, KSColors::Cyan, EKSAlign::Left, true, true);
	UI.Text(TEXT("неоновый шутер для 2–6 игроков"), FVector2D(X + 4.f, 224.f), 30.f, KSColors::Dim, EKSAlign::Left, false, true);

	float Y = 290.f;
	const float ButtonW = 640.f;
	const float ButtonH = 96.f;
	const float Gap = 20.f;
	if (UI.Button(FKSUI::BoxAt(X, Y, ButtonW, ButtonH), TEXT("Создать игру"), KSColors::Cyan))
	{
		Click();
		Go(EKSScreen::Host);
	}
	Y += ButtonH + Gap;
	if (UI.Button(FKSUI::BoxAt(X, Y, ButtonW, ButtonH), TEXT("Найти игру"), KSColors::Magenta))
	{
		Click();
		Go(EKSScreen::Find);
	}
	Y += ButtonH + Gap;
	if (UI.Button(FKSUI::BoxAt(X, Y, ButtonW, ButtonH), TEXT("Тренировка с ботами"), KSColors::Green))
	{
		Click();
		Go(EKSScreen::Training);
	}
	Y += ButtonH + Gap;
	if (UI.Button(FKSUI::BoxAt(X, Y, ButtonW, ButtonH), TEXT("Настройки"), KSColors::Orange))
	{
		Click();
		Go(EKSScreen::Settings);
	}
	Y += ButtonH + Gap;
	if (UI.Button(FKSUI::BoxAt(X, Y, ButtonW, ButtonH), TEXT("Как играть"), KSColors::Yellow))
	{
		Click();
		Go(EKSScreen::Help);
	}

	const FString& Name = GI->GetSettings().PlayerName;
	UI.Text(FString::Printf(TEXT("Имя: %s"), *Name), FVector2D(X, 962.f), 32.f, KSColors::Text, EKSAlign::Left, true, true);
	if (UI.Button(FKSUI::BoxAt(X + 430.f, 924.f, 210.f, 76.f), TEXT("Изменить"), KSColors::Cyan, true, 30.f))
	{
		Click();
		NameInput = Name;
		KeyboardLayout = 0;
		bUpperCase = NameInput.IsEmpty();
		Go(EKSScreen::Name);
	}
	UI.Text(MyAddressText(GI), FVector2D(UI.Width - X, 1010.f), 26.f, KSColors::Dim, EKSAlign::Right, false, true);
}

void AKSMenuHUD::DrawHost(UKSGameInstance* GI, bool bOnline)
{
	const FBox2D Panel = DrawScreenPanel(bOnline ? TEXT("Создать игру") : TEXT("Тренировка с ботами"), 1180.f, 900.f);
	const float Left = Panel.Min.X + 60.f;
	const float Right = Panel.Max.X - 60.f;
	const float CenterX = Panel.GetCenter().X;
	FKSSettings& S = GI->EditSettings();
	int32& Arena = bOnline ? S.HostArena : S.TrainingArena;
	int32& Bots = bOnline ? S.HostBots : S.TrainingBots;
	const int32 NumArenas = KSArena::Num();
	Arena = FMath::Clamp(Arena, 0, NumArenas - 1);
	bool bChanged = false;

	float Y = Panel.Min.Y + 170.f;
	const float Step = 96.f;
	if (const int32 D = Stepper(Y, Left, Right, TEXT("Арена"), KSArena::GetName(Arena)))
	{
		Arena = (Arena + D + NumArenas) % NumArenas;
		bChanged = true;
	}
	Y += Step;
	const int32 MinBots = bOnline ? 0 : 1;
	if (const int32 D = Stepper(Y, Left, Right, TEXT("Боты"), Bots == 0 ? FString(TEXT("нет")) : FString::FromInt(Bots)))
	{
		Bots = FMath::Clamp(Bots + D, MinBots, KS::MaxPlayers - 1);
		bChanged = true;
	}
	Bots = FMath::Clamp(Bots, MinBots, KS::MaxPlayers - 1);
	Y += Step;
	S.BotSkill = FMath::Clamp(S.BotSkill, 0, 2);
	if (const int32 D = Stepper(Y, Left, Right, TEXT("Сложность ботов"), KSSkillNames[S.BotSkill]))
	{
		S.BotSkill = FMath::Clamp(S.BotSkill + D, 0, 2);
		bChanged = true;
	}
	Y += Step;
	if (bOnline)
	{
		if (const int32 D = Stepper(Y, Left, Right, TEXT("Фрагов для победы"), FString::FromInt(S.HostFrags)))
		{
			S.HostFrags = FMath::Clamp(S.HostFrags + D * 5, 5, 50);
			bChanged = true;
		}
		Y += Step;
		if (const int32 D = Stepper(Y, Left, Right, TEXT("Время матча"), FString::Printf(TEXT("%d мин"), S.HostMinutes)))
		{
			S.HostMinutes = FMath::Clamp(S.HostMinutes + D, 3, 20);
			bChanged = true;
		}
		Y += Step;
		UI.Text(TEXT("Друзья в той же сети Wi-Fi найдут игру через «Найти игру»."), FVector2D(CenterX, Y), 26.f, KSColors::Dim, EKSAlign::Center, false, true);
		UI.Text(MyAddressText(GI), FVector2D(CenterX, Y + 38.f), 26.f, KSColors::Dim, EKSAlign::Center, false, true);
	}
	else
	{
		UI.Text(TEXT("Матч на этом телефоне, без сети: до 20 фрагов, 10 минут."), FVector2D(CenterX, Y), 26.f, KSColors::Dim, EKSAlign::Center, false, true);
	}
	if (bChanged)
	{
		GI->SaveSettings();
	}

	if (UI.Button(FKSUI::BoxAt(Right - 440.f, Panel.Max.Y - 116.f, 440.f, 86.f), bOnline ? TEXT("Начать игру") : TEXT("Начать"), KSColors::Green))
	{
		Click();
		FKSMatchOptions Options;
		Options.Arena = Arena;
		Options.Bots = Bots;
		Options.BotSkill = S.BotSkill;
		Options.FragLimit = bOnline ? S.HostFrags : 20;
		Options.Minutes = bOnline ? S.HostMinutes : 10;
		Options.bOnline = bOnline;
		GI->SaveSettings();
		GI->HostGame(Options);
		return;
	}
	if (BackButton(Panel))
	{
		Go(EKSScreen::Main);
	}
}

void AKSMenuHUD::DrawFind(UKSGameInstance* GI)
{
	const FBox2D Panel = DrawScreenPanel(TEXT("Найти игру"), 1280.f, 960.f);
	const float Left = Panel.Min.X + 60.f;
	const float Right = Panel.Max.X - 60.f;
	if (!GI->IsSearching())
	{
		GI->StartLanSearch();
	}
	const TArray<FKSFoundGame>& Games = GI->GetFoundGames();
	float Y = Panel.Min.Y + 140.f;
	if (Games.Num() == 0)
	{
		const int32 Dots = (int32)(GetWorld()->GetRealTimeSeconds() * 2.0) % 4;
		UI.Text(FString(TEXT("Ищем игры в твоей сети Wi-Fi")) + FString::ChrN(Dots, TEXT('.')), FVector2D(Left, Y + 40.f), 36.f, KSColors::Text, EKSAlign::Left, false, true);
		UI.Text(TEXT("Попроси друга нажать «Создать игру»."), FVector2D(Left, Y + 104.f), 28.f, KSColors::Dim, EKSAlign::Left, false, true);
		UI.Text(TEXT("Телефоны должны быть подключены к одной сети Wi-Fi."), FVector2D(Left, Y + 144.f), 28.f, KSColors::Dim, EKSAlign::Left, false, true);
	}
	for (int32 i = 0; i < FMath::Min(Games.Num(), 5); ++i)
	{
		const FKSFoundGame& Game = Games[i];
		const FString Label = FString::Printf(TEXT("%s   ·   %s   ·   %d/%d"), *Game.HostName, *KSArena::GetName(Game.Arena), Game.Players, Game.MaxPlayers);
		const bool bFull = Game.Players >= Game.MaxPlayers;
		if (UI.Button(FBox2D(FVector2D(Left, Y), FVector2D(Right, Y + 92.f)), Label, KSColors::Magenta, !bFull, 34.f))
		{
			Click();
			GI->EditSettings().LastAddress = Game.Address;
			GI->SaveSettings();
			GI->JoinGame(Game.Address);
			return;
		}
		Y += 108.f;
	}

	UI.Text(TEXT("Через интернет: установите ZeroTier, войдите в одну сеть"), FVector2D(Left, Panel.Max.Y - 210.f), 26.f, KSColors::Dim, EKSAlign::Left, false, true);
	UI.Text(TEXT("и введите адрес хоста вручную (инструкция в INSTALL.md)."), FVector2D(Left, Panel.Max.Y - 174.f), 26.f, KSColors::Dim, EKSAlign::Left, false, true);
	if (UI.Button(FKSUI::BoxAt(Right - 440.f, Panel.Max.Y - 116.f, 440.f, 86.f), TEXT("Ввести адрес"), KSColors::Cyan, true, 36.f))
	{
		Click();
		AddressInput = GI->GetSettings().LastAddress;
		Go(EKSScreen::Address);
	}
	if (BackButton(Panel))
	{
		Go(EKSScreen::Main);
	}
}

void AKSMenuHUD::DrawAddress(UKSGameInstance* GI)
{
	const FBox2D Panel = DrawScreenPanel(TEXT("Адрес хоста"), 1060.f, 960.f);
	const float Left = Panel.Min.X + 60.f;
	const float Right = Panel.Max.X - 60.f;
	const float Now = (float)GetWorld()->GetRealTimeSeconds();

	const FBox2D Field(FVector2D(Left, Panel.Min.Y + 120.f), FVector2D(Right, Panel.Min.Y + 216.f));
	UI.Rect(Field, FLinearColor(0.f, 0.f, 0.f, 0.5f));
	UI.Frame(Field, KSAlpha(KSColors::Cyan, 0.7f), 2.f);
	if (AddressInput.IsEmpty())
	{
		UI.Text(TEXT("например 192.168.1.23"), FVector2D(Left + 24.f, Field.GetCenter().Y), 40.f, KSAlpha(KSColors::Dim, 0.6f), EKSAlign::Left, false, true);
	}
	else
	{
		const FString Shown = AddressInput + ((FMath::Fmod(Now, 1.f) < 0.5f) ? TEXT("|") : TEXT(" "));
		UI.Text(Shown, FVector2D(Left + 24.f, Field.GetCenter().Y), 44.f, KSColors::Text, EKSAlign::Left, true, true);
	}

	const TCHAR* Keys[12] = { TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4"), TEXT("5"), TEXT("6"), TEXT("7"), TEXT("8"), TEXT("9"), TEXT("."), TEXT("0"), TEXT(":") };
	const float KeyW = 150.f;
	const float KeyH = 100.f;
	const float Gap = 16.f;
	const float GridTop = Panel.Min.Y + 250.f;
	for (int32 i = 0; i < 12; ++i)
	{
		const float KX = Left + (i % 3) * (KeyW + Gap);
		const float KY = GridTop + (i / 3) * (KeyH + Gap);
		if (Key(FKSUI::BoxAt(KX, KY, KeyW, KeyH), Keys[i], KSColors::Cyan, 46.f) && AddressInput.Len() < KSMaxAddressLength)
		{
			AddressInput += Keys[i];
		}
	}
	const float SideX = Left + 3.f * (KeyW + Gap) + 30.f;
	const float SideW = Right - SideX;
	if (Key(FKSUI::BoxAt(SideX, GridTop, SideW, KeyH), TEXT("Стереть"), KSColors::Orange, 36.f) && AddressInput.Len() > 0)
	{
		AddressInput.LeftChopInline(1);
	}
	if (Key(FKSUI::BoxAt(SideX, GridTop + KeyH + Gap, SideW, KeyH), TEXT("Очистить"), KSColors::Orange, 36.f))
	{
		AddressInput.Reset();
	}
	const bool bCanJoin = !AddressInput.IsEmpty();
	if (UI.Button(FKSUI::BoxAt(SideX, GridTop + 2.f * (KeyH + Gap), SideW, KeyH * 2.f + Gap), TEXT("Подключиться"), KSColors::Green, bCanJoin, 40.f))
	{
		Click();
		GI->EditSettings().LastAddress = AddressInput;
		GI->SaveSettings();
		GI->JoinGame(AddressInput);
		return;
	}
	if (BackButton(Panel))
	{
		Go(EKSScreen::Find);
	}
}

void AKSMenuHUD::DrawName(UKSGameInstance* GI)
{
	const FBox2D Panel = DrawScreenPanel(TEXT("Имя игрока"), 1440.f, 980.f);
	const float Left = Panel.Min.X + 50.f;
	const float Right = Panel.Max.X - 50.f;
	const float Now = (float)GetWorld()->GetRealTimeSeconds();

	const FBox2D Field(FVector2D(Left, Panel.Min.Y + 116.f), FVector2D(Right, Panel.Min.Y + 210.f));
	UI.Rect(Field, FLinearColor(0.f, 0.f, 0.f, 0.5f));
	UI.Frame(Field, KSAlpha(KSColors::Cyan, 0.7f), 2.f);
	const FString Shown = NameInput + ((FMath::Fmod(Now, 1.f) < 0.5f) ? TEXT("|") : TEXT(" "));
	UI.Text(Shown, FVector2D(Left + 24.f, Field.GetCenter().Y), 44.f, KSColors::Text, EKSAlign::Left, true, true);
	UI.Text(FString::Printf(TEXT("%d/%d"), NameInput.Len(), KSMaxNameLength), FVector2D(Right - 20.f, Field.GetCenter().Y), 26.f, KSColors::Dim, EKSAlign::Right, false, true);

	const float Gap = 10.f;
	const float KeyW = FMath::Min(104.f, ((Right - Left) - 10.f * Gap) / 11.f);
	const float KeyH = 92.f;
	float Y = Panel.Min.Y + 240.f;
	const int32 Layout = FMath::Clamp(KeyboardLayout, 0, 2);
	for (int32 Row = 0; Row < 3; ++Row)
	{
		const FString Letters = KSKeyRows[Layout][Row];
		const float RowW = Letters.Len() * KeyW + FMath::Max(0, Letters.Len() - 1) * Gap;
		float X = Panel.GetCenter().X - RowW * 0.5f;
		for (int32 i = 0; i < Letters.Len(); ++i)
		{
			const TCHAR Upper = Letters[i];
			const TCHAR Char = (bUpperCase || Layout == 2) ? Upper : KSToLower(Upper);
			const FString Label = FString::Chr(Char);
			if (Key(FKSUI::BoxAt(X, Y, KeyW, KeyH), Label, KSColors::Cyan, 40.f) && NameInput.Len() < KSMaxNameLength)
			{
				NameInput += Label;
				bUpperCase = false;
			}
			X += KeyW + Gap;
		}
		Y += KeyH + Gap;
	}

	// Bottom row: layout switches, case, space, delete and done.
	const float RowY = Y + 6.f;
	const float BottomW = Right - Left;
	float X = Left;
	auto Next = [&X, RowY, KeyH, Gap](float W) { const FBox2D Box = FKSUI::BoxAt(X, RowY, W, KeyH); X += W + Gap; return Box; };
	const float Unit = (BottomW - 5.f * Gap) / 9.f;
	if (Key(Next(Unit), Layout == 1 ? TEXT("РУС") : TEXT("ENG"), KSColors::Magenta, 32.f))
	{
		KeyboardLayout = Layout == 1 ? 0 : 1;
	}
	if (Key(Next(Unit), Layout == 2 ? TEXT("АБВ") : TEXT("123"), KSColors::Magenta, 32.f))
	{
		KeyboardLayout = Layout == 2 ? 0 : 2;
	}
	if (Key(Next(Unit), bUpperCase ? TEXT("АБ") : TEXT("аб"), KSColors::Magenta, 32.f))
	{
		bUpperCase = !bUpperCase;
	}
	if (Key(Next(Unit * 3.f), TEXT("Пробел"), KSColors::Cyan, 32.f) && NameInput.Len() < KSMaxNameLength && !NameInput.IsEmpty())
	{
		NameInput += TEXT(" ");
	}
	if (Key(Next(Unit), TEXT("Стереть"), KSColors::Orange, 30.f) && NameInput.Len() > 0)
	{
		NameInput.LeftChopInline(1);
	}
	const FString Clean = UKSGameInstance::SanitizeName(NameInput);
	if (UI.Button(Next(Right - X), TEXT("Готово"), KSColors::Green, !Clean.IsEmpty(), 34.f))
	{
		Click();
		GI->EditSettings().PlayerName = Clean;
		GI->SaveSettings();
		Go(EKSScreen::Main);
		return;
	}
	if (BackButton(Panel))
	{
		Go(EKSScreen::Main);
	}
}

void AKSMenuHUD::DrawSettings(UKSGameInstance* GI)
{
	const FBox2D Panel = DrawScreenPanel(TEXT("Настройки"), 1180.f, 1000.f);
	const float Left = Panel.Min.X + 60.f;
	const float Right = Panel.Max.X - 60.f;
	FKSSettings& S = GI->EditSettings();
	bool bChanged = false;
	float Y = Panel.Min.Y + 160.f;
	const float Step = 92.f;

	if (const int32 D = Stepper(Y, Left, Right, TEXT("Чувствительность"), FString::Printf(TEXT("%.1f"), S.Sensitivity)))
	{
		S.Sensitivity = FMath::Clamp(FMath::RoundToFloat((S.Sensitivity + D * 0.1f) * 10.f) / 10.f, 0.3f, 3.f);
		bChanged = true;
	}
	Y += Step;
	if (const int32 D = Stepper(Y, Left, Right, TEXT("Угол обзора"), FString::Printf(TEXT("%d°"), FMath::RoundToInt(S.FieldOfView))))
	{
		S.FieldOfView = FMath::Clamp(S.FieldOfView + D * 5.f, 80.f, 115.f);
		bChanged = true;
	}
	Y += Step;
	if (const int32 D = Stepper(Y, Left, Right, TEXT("Громкость"), FString::Printf(TEXT("%d%%"), FMath::RoundToInt(S.Volume * 100.f))))
	{
		S.Volume = FMath::Clamp(FMath::RoundToFloat((S.Volume + D * 0.1f) * 10.f) / 10.f, 0.f, 1.f);
		bChanged = true;
	}
	Y += Step;
	if (Toggle(Y, Left, Right, TEXT("Помощь в прицеливании"), S.bAimAssist ? TEXT("Включена") : TEXT("Выключена"), S.bAimAssist))
	{
		S.bAimAssist = !S.bAimAssist;
		bChanged = true;
	}
	Y += Step;
	if (Toggle(Y, Left, Right, TEXT("Автоогонь"), S.bAutoFire ? TEXT("Включён") : TEXT("Выключен"), S.bAutoFire))
	{
		S.bAutoFire = !S.bAutoFire;
		bChanged = true;
	}
	Y += Step;
	if (Toggle(Y, Left, Right, TEXT("Графика"), S.Quality > 0 ? TEXT("Высокая") : TEXT("Экономная"), S.Quality > 0))
	{
		S.Quality = S.Quality > 0 ? 0 : 1;
		GI->ApplyGraphicsSettings();
		bChanged = true;
	}
	Y += Step;
	if (Toggle(Y, Left, Right, TEXT("Показывать FPS"), S.bShowFPS ? TEXT("Да") : TEXT("Нет"), S.bShowFPS))
	{
		S.bShowFPS = !S.bShowFPS;
		bChanged = true;
	}
	if (bChanged)
	{
		GI->SaveSettings();
	}
	if (BackButton(Panel))
	{
		Go(EKSScreen::Main);
	}
}

void AKSMenuHUD::DrawHelp()
{
	const FBox2D Panel = DrawScreenPanel(TEXT("Как играть"), 1500.f, 980.f);
	const float Left = Panel.Min.X + 60.f;
	static const TCHAR* Lines[] =
	{
		TEXT("Левая часть экрана: коснись и веди палец, чтобы бежать."),
		TEXT("Правая часть экрана: веди пальцем, чтобы поворачиваться."),
		TEXT("Большая кнопка справа стреляет. По ней тоже можно вести палец и целиться."),
		TEXT("Стрелка вверх: прыжок. Светящиеся площадки подбрасывают высоко."),
		TEXT("Внизу оружие: автомат, дробовик, рельса и ракетница. Подбирай их на арене."),
		TEXT("Зелёный крест лечит на 50. Попадание в голову сильнее обычного."),
		TEXT("Автоогонь стреляет сам, когда прицел на противнике. Его можно выключить."),
		TEXT("Подержи палец на счёте сверху, чтобы увидеть таблицу игроков."),
		TEXT("Побеждает тот, кто первым наберёт нужное число фрагов."),
	};
	float Y = Panel.Min.Y + 150.f;
	for (const TCHAR* Line : Lines)
	{
		UI.Circle(FVector2D(Left + 8.f, Y), 7.f, KSColors::Cyan);
		UI.Text(Line, FVector2D(Left + 32.f, Y), 31.f, KSColors::Text, EKSAlign::Left, false, true);
		Y += 70.f;
	}
	if (BackButton(Panel))
	{
		Go(EKSScreen::Main);
	}
}

// ---------------------------------------------------------------------------------------------
// Windows on top
// ---------------------------------------------------------------------------------------------

void AKSMenuHUD::DrawConnecting(UKSGameInstance* GI)
{
	const float W = UI.Width;
	UI.Rect(FKSUI::BoxAt(0.f, 0.f, W, UI.Height), FLinearColor(0.f, 0.f, 0.02f, 0.7f));
	const FBox2D Panel = FKSUI::BoxCentered(FVector2D(W * 0.5f, UI.Height * 0.5f), 1000.f, 440.f);
	UI.Panel(Panel, KSColors::Cyan);
	UI.Text(TEXT("Подключение"), FVector2D(W * 0.5f, Panel.Min.Y + 70.f), 50.f, KSColors::Cyan, EKSAlign::Center, true, true);
	UI.Text(GI->GetJoinAddress(), FVector2D(W * 0.5f, Panel.Min.Y + 136.f), 34.f, KSColors::Text, EKSAlign::Center, false, true);

	const FVector2D Spin(W * 0.5f, Panel.Min.Y + 232.f);
	const float Now = (float)GetWorld()->GetRealTimeSeconds();
	for (int32 i = 0; i < 10; ++i)
	{
		const float Angle = i * (2.f * UE_PI / 10.f);
		const float Phase = FMath::Fmod(Now * 1.5f - i / 10.f + 10.f, 1.f);
		UI.Circle(Spin + FVector2D(FMath::Sin(Angle), -FMath::Cos(Angle)) * 46.f, 8.f, KSAlpha(KSColors::Cyan, 0.2f + 0.8f * (1.f - Phase)));
	}
	if (UI.Button(FKSUI::BoxCentered(FVector2D(W * 0.5f, Panel.Max.Y - 72.f), 360.f, 84.f), TEXT("Отмена"), KSColors::Magenta))
	{
		Click();
		GI->CancelJoin();
	}
}

void AKSMenuHUD::DrawPopup()
{
	const float W = UI.Width;
	UI.Rect(FKSUI::BoxAt(0.f, 0.f, W, UI.Height), FLinearColor(0.f, 0.f, 0.02f, 0.7f));
	const FBox2D Panel = FKSUI::BoxCentered(FVector2D(W * 0.5f, UI.Height * 0.5f), FMath::Min(1300.f, W - 200.f), 380.f);
	UI.Panel(Panel, KSColors::Magenta);
	UI.Text(Popup, FVector2D(W * 0.5f, Panel.Min.Y + 120.f), 36.f, KSColors::Text, EKSAlign::Center, false, true);
	if (UI.Button(FKSUI::BoxCentered(FVector2D(W * 0.5f, Panel.Max.Y - 80.f), 300.f, 84.f), TEXT("Понятно"), KSColors::Cyan))
	{
		Click();
		Popup.Reset();
	}
}
