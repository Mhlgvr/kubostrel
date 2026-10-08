#include "KSBasePlayerController.h"
#include "KSAudio.h"
#include "Engine/LocalPlayer.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/App.h"

namespace
{
	constexpr int32 KSMaxTouches = 10;
	constexpr int32 KSMousePointerId = 100;
}

AKSBasePlayerController::AKSBasePlayerController()
{
	bShowMouseCursor = false;
	bEnableClickEvents = false;
	bEnableTouchEvents = false;
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

void AKSBasePlayerController::BeginPlay()
{
	Super::BeginPlay();
#if PLATFORM_IOS || PLATFORM_ANDROID
	bTouchMode = true;
#else
	bTouchMode = FParse::Param(FCommandLine::Get(), TEXT("touch"));
#endif
	if (IsLocalController() && !Synth)
	{
		Synth = NewObject<UKSAudioSynth>(this, TEXT("KSAudio"));
		Synth->RegisterComponent();
		Synth->Start();
	}
}

void AKSBasePlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	UpdatePointers();
	const float RealDelta = (float)FApp::GetDeltaTime();
	if (RealDelta > 0.f)
	{
		SmoothedFPS = FMath::Lerp(SmoothedFPS, 1.f / RealDelta, 0.05f);
	}
}

FVector2D AKSBasePlayerController::GetScreenSize() const
{
	int32 X = 0;
	int32 Y = 0;
	GetViewportSize(X, Y);
	return FVector2D(FMath::Max(X, 1), FMath::Max(Y, 1));
}

float AKSBasePlayerController::GetUIScale() const
{
	return GetScreenSize().Y / 1080.f;
}

void AKSBasePlayerController::UpdatePointers()
{
	TArray<FKSPointer> Previous = MoveTemp(Pointers);
	Pointers.Reset();

	auto Track = [this, &Previous](int32 Id, const FVector2D& Pos, bool bDown)
	{
		const FKSPointer* Old = Previous.FindByPredicate([Id](const FKSPointer& P) { return P.Id == Id; });
		const bool bWasDown = Old && Old->bDown;
		if (bDown)
		{
			FKSPointer& P = Pointers.AddDefaulted_GetRef();
			P.Id = Id;
			P.Pos = Pos;
			P.bDown = true;
			if (bWasDown)
			{
				P.StartPos = Old->StartPos;
				P.Delta = Pos - Old->Pos;
			}
			else
			{
				P.StartPos = Pos;
				P.bPressed = true;
			}
		}
		else if (bWasDown)
		{
			// Kept for one frame so the UI can see the release.
			FKSPointer& P = Pointers.AddDefaulted_GetRef();
			P.Id = Id;
			P.Pos = Pos;
			P.StartPos = Old->StartPos;
			P.bReleased = true;
		}
	};

	for (int32 i = 0; i < KSMaxTouches; ++i)
	{
		float X = 0.f;
		float Y = 0.f;
		bool bPressed = false;
		GetInputTouchState((ETouchIndex::Type)i, X, Y, bPressed);
		if (!bPressed)
		{
			// A finger that just lifted reports where it was released.
			const FKSPointer* Old = Previous.FindByPredicate([i](const FKSPointer& P) { return P.Id == i; });
			if (Old && (X <= 0.f && Y <= 0.f))
			{
				X = Old->Pos.X;
				Y = Old->Pos.Y;
			}
		}
		Track(i, FVector2D(X, Y), bPressed);
	}

#if !(PLATFORM_IOS || PLATFORM_ANDROID)
	// On a computer the mouse acts as a finger whenever the cursor is visible (menus, or -touch testing).
	float MouseX = 0.f;
	float MouseY = 0.f;
	const bool bHasMouse = (bShowMouseCursor || bTouchMode) && GetMousePosition(MouseX, MouseY);
	if (bHasMouse)
	{
		Track(KSMousePointerId, FVector2D(MouseX, MouseY), IsInputKeyDown(EKeys::LeftMouseButton));
	}
	else
	{
		const FKSPointer* Old = Previous.FindByPredicate([](const FKSPointer& P) { return P.Id == KSMousePointerId; });
		if (Old)
		{
			Track(KSMousePointerId, Old->Pos, false);
		}
	}
#endif
}
