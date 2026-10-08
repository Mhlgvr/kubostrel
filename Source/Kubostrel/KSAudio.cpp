#include "KSAudio.h"
#include "Kubostrel.h"
#include "KSTypes.h"
#include "KSBasePlayerController.h"
#include "KSGameInstance.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"

// ---------------------------------------------------------------------------------------------
// Tiny synthesis toolkit
// ---------------------------------------------------------------------------------------------

namespace
{
	struct FKSNoise
	{
		uint32 State = 22222u;
		float Next()
		{
			State = State * 1664525u + 1013904223u;
			return float(State >> 8) * (2.f / 16777216.f) - 1.f;
		}
	};

	struct FKSLowPass
	{
		float Z = 0.f;
		float Run(float X, float CutoffHz, float SampleRate)
		{
			const float Coef = 1.f - FMath::Exp(-2.f * UE_PI * FMath::Max(CutoffHz, 10.f) / SampleRate);
			Z += Coef * (X - Z);
			return Z;
		}
	};

	struct FKSOsc
	{
		float Phase = 0.f;
		float Sine(float FreqHz, float SampleRate)
		{
			Phase += FreqHz / SampleRate;
			Phase -= FMath::FloorToFloat(Phase);
			return FMath::Sin(2.f * UE_PI * Phase);
		}
	};

	float KSDecay(float T, float Tau)
	{
		return T < 0.f ? 0.f : FMath::Exp(-T / Tau);
	}

	float KSAttack(float T, float Time)
	{
		return FMath::Clamp(T / Time, 0.f, 1.f);
	}

	float KSTone(float T, float FreqHz)
	{
		return FMath::Sin(2.f * UE_PI * FreqHz * T);
	}

	// Short metallic click used by reloads and the empty sound.
	float KSClick(float T, float FreqHz, FKSNoise& Noise)
	{
		if (T < 0.f)
		{
			return 0.f;
		}
		return Noise.Next() * KSDecay(T, 0.004f) + 0.35f * KSTone(T, FreqHz) * KSDecay(T, 0.012f);
	}

	struct FKSSoundDef
	{
		float Duration;
		float Gain;
	};

	const FKSSoundDef GKSSoundDefs[KSSound::Count] =
	{
		{ 0.22f, 0.50f }, // RifleShot
		{ 0.55f, 0.80f }, // ShotgunShot
		{ 1.00f, 0.70f }, // RailShot
		{ 0.85f, 0.70f }, // RocketLaunch
		{ 1.70f, 1.00f }, // Explosion
		{ 0.08f, 0.42f }, // HitMarker
		{ 0.50f, 0.55f }, // KillConfirm
		{ 0.32f, 0.55f }, // Headshot
		{ 0.38f, 0.50f }, // Pickup
		{ 0.16f, 0.28f }, // Jump
		{ 0.65f, 0.50f }, // JumpPad
		{ 0.10f, 0.20f }, // Footstep
		{ 0.32f, 0.60f }, // Hurt
		{ 0.62f, 0.45f }, // Reload
		{ 0.06f, 0.32f }, // UIClick
		{ 0.09f, 0.35f }, // Empty
		{ 0.75f, 0.45f }, // Spawn
		{ 1.50f, 0.55f }, // MatchEnd
	};

	void KSRender(int32 Sound, TArray<float>& Out, float SR)
	{
		const int32 Num = FMath::Max(1, FMath::CeilToInt(GKSSoundDefs[Sound].Duration * SR));
		Out.SetNumZeroed(Num);
		FKSNoise Noise;
		Noise.State = 1234567u + uint32(Sound) * 7919u;
		FKSLowPass LP1, LP2, LP3;
		FKSOsc Osc1, Osc2;

		for (int32 i = 0; i < Num; ++i)
		{
			const float T = i / SR;
			const float N = Noise.Next();
			float V = 0.f;
			switch (Sound)
			{
			case KSSound::RifleShot:
			{
				const float Crack = LP1.Run(N, 1500.f + 7000.f * KSDecay(T, 0.02f), SR) - LP2.Run(N, 180.f, SR) * 0.5f;
				const float Thump = Osc1.Sine(55.f + 110.f * KSDecay(T, 0.02f), SR);
				const float Tail = LP3.Run(N, 800.f, SR);
				V = 0.9f * Crack * KSDecay(T, 0.028f) + 0.6f * Thump * KSDecay(T, 0.055f) + 0.25f * Tail * KSDecay(T, 0.09f);
				if (T < 0.002f)
				{
					V += 0.5f * N * (1.f - T / 0.002f);
				}
				break;
			}
			case KSSound::ShotgunShot:
			{
				const float Blast = LP1.Run(N, 900.f + 4200.f * KSDecay(T, 0.03f), SR);
				const float Tail = LP2.Run(N, 480.f, SR);
				const float Thump = Osc1.Sine(45.f + 95.f * KSDecay(T, 0.03f), SR);
				V = 1.1f * Blast * KSDecay(T, 0.075f) + 0.7f * Tail * KSDecay(T, 0.2f) + 0.8f * Thump * KSDecay(T, 0.1f);
				break;
			}
			case KSSound::RailShot:
			{
				const float Zap = Osc1.Sine(170.f + 2300.f * KSDecay(T, 0.12f), SR) * (0.6f + 0.4f * KSTone(T, 55.f));
				const float Hiss = (N - LP1.Run(N, 3000.f, SR)) * KSDecay(T, 0.05f);
				const float Sub = KSTone(T, 68.f) * KSDecay(T, 0.25f);
				V = 0.75f * Zap * KSDecay(T, 0.3f) * KSAttack(T, 0.003f) + 0.5f * Hiss + 0.5f * Sub;
				break;
			}
			case KSSound::RocketLaunch:
			{
				const float Whoosh = LP1.Run(N, 500.f + 1600.f * KSDecay(T, 0.15f), SR) * KSAttack(T, 0.02f) * KSDecay(T, 0.35f);
				const float Thump = Osc1.Sine(60.f + 70.f * KSDecay(T, 0.05f), SR) * KSDecay(T, 0.12f);
				V = 1.3f * Whoosh + 0.7f * Thump;
				break;
			}
			case KSSound::Explosion:
			{
				const float Body = LP1.Run(N, 300.f + 2600.f * KSDecay(T, 0.04f), SR) * KSDecay(T, 0.35f);
				const float Rumble = LP2.Run(N, 110.f, SR) * 3.f * KSDecay(T, 0.6f);
				const float Crack = N * KSDecay(T, 0.012f);
				const float Boom = Osc1.Sine(38.f + 55.f * KSDecay(T, 0.08f), SR) * KSDecay(T, 0.42f);
				V = 1.2f * Body + 0.8f * Rumble + 0.5f * Crack + 0.9f * Boom;
				break;
			}
			case KSSound::HitMarker:
				V = 0.6f * KSTone(T, 1900.f) * KSDecay(T, 0.012f) + 0.3f * KSTone(T, 3800.f) * KSDecay(T, 0.006f);
				break;
			case KSSound::KillConfirm:
			{
				const float T2 = T - 0.09f;
				V = 0.5f * KSTone(T, 880.f) * KSDecay(T, 0.08f) * KSAttack(T, 0.002f);
				if (T2 > 0.f)
				{
					V += 0.6f * (KSTone(T2, 1320.f) + 0.25f * KSTone(T2, 2640.f)) * KSDecay(T2, 0.15f) * KSAttack(T2, 0.002f);
				}
				break;
			}
			case KSSound::Headshot:
				V = 0.45f * KSTone(T, 2600.f) * KSDecay(T, 0.06f) + 0.3f * KSTone(T, 3910.f) * KSDecay(T, 0.04f)
					+ 0.25f * KSTone(T, 1310.f) * KSDecay(T, 0.1f);
				break;
			case KSSound::Pickup:
			{
				const float Freqs[3] = { 660.f, 990.f, 1320.f };
				for (int32 k = 0; k < 3; ++k)
				{
					const float Tk = T - k * 0.07f;
					if (Tk > 0.f)
					{
						V += 0.4f * (KSTone(Tk, Freqs[k]) + 0.3f * KSTone(Tk, Freqs[k] * 3.f)) * KSDecay(Tk, 0.07f) * KSAttack(Tk, 0.003f);
					}
				}
				break;
			}
			case KSSound::Jump:
				V = 0.5f * Osc1.Sine(260.f + 2000.f * T, SR) * KSDecay(T, 0.05f) + 0.3f * LP1.Run(N, 600.f, SR) * KSDecay(T, 0.03f);
				break;
			case KSSound::JumpPad:
				V = 0.55f * Osc1.Sine(180.f + 900.f * (1.f - KSDecay(T, 0.15f)), SR) * KSDecay(T, 0.25f)
					+ 0.45f * LP1.Run(N, 2000.f, SR) * KSDecay(T, 0.2f) * KSAttack(T, 0.01f);
				break;
			case KSSound::Footstep:
				V = 1.4f * LP1.Run(N, 420.f, SR) * KSDecay(T, 0.018f) + 0.4f * KSTone(T, 90.f) * KSDecay(T, 0.02f);
				break;
			case KSSound::Hurt:
				V = 0.6f * Osc1.Sine(140.f + 80.f * KSDecay(T, 0.05f), SR) * KSDecay(T, 0.1f) + 0.45f * LP1.Run(N, 1200.f, SR) * KSDecay(T, 0.04f);
				break;
			case KSSound::Reload:
			{
				V = KSClick(T, 2200.f, Noise) + 0.9f * KSClick(T - 0.42f, 1600.f, Noise);
				if (T > 0.1f && T < 0.34f)
				{
					V += 0.25f * LP1.Run(N, 1400.f, SR) * FMath::Sin(UE_PI * (T - 0.1f) / 0.24f);
				}
				break;
			}
			case KSSound::UIClick:
				V = 0.5f * KSTone(T, 1400.f) * KSDecay(T, 0.008f);
				break;
			case KSSound::Empty:
				V = 0.6f * KSClick(T, 3000.f, Noise);
				break;
			case KSSound::Spawn:
				V = 0.5f * Osc1.Sine(300.f + 1300.f * T, SR) * KSAttack(T, 0.05f) * KSDecay(T, 0.3f) * (0.7f + 0.3f * KSTone(T, 18.f))
					+ 0.25f * Osc2.Sine(600.f + 2600.f * T, SR) * KSAttack(T, 0.05f) * KSDecay(T, 0.2f);
				break;
			case KSSound::MatchEnd:
			{
				const float Chord[4] = { 523.25f, 659.25f, 783.99f, 1046.5f };
				for (int32 k = 0; k < 4; ++k)
				{
					const float Tk = T - k * 0.06f;
					if (Tk > 0.f)
					{
						V += 0.3f * KSTone(Tk, Chord[k]) * KSDecay(Tk, 0.5f) * KSAttack(Tk, 0.01f);
					}
				}
				break;
			}
			default:
				break;
			}
			Out[i] = V;
		}

		// Normalize, short fade-out against clicks, then apply the mix gain.
		float Peak = 0.001f;
		for (const float S : Out)
		{
			Peak = FMath::Max(Peak, FMath::Abs(S));
		}
		const float Scale = 0.85f / Peak * GKSSoundDefs[Sound].Gain;
		const int32 Fade = FMath::Min(Num, FMath::RoundToInt(0.01f * SR));
		for (int32 i = 0; i < Num; ++i)
		{
			const int32 FromEnd = Num - 1 - i;
			const float F = FromEnd < Fade ? float(FromEnd) / Fade : 1.f;
			Out[i] *= Scale * F;
		}
	}
}

// ---------------------------------------------------------------------------------------------

UKSAudioSynth::UKSAudioSynth(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAllowSpatialization = false;
}

bool UKSAudioSynth::Init(int32& SampleRate)
{
	NumChannels = 2;
	RenderSounds(SampleRate);
	return true;
}

void UKSAudioSynth::RenderSounds(int32 SampleRate)
{
	const double Start = FPlatformTime::Seconds();
	const float SR = float(FMath::Max(SampleRate, 8000));
	Buffers.SetNum(KSSound::Count);
	for (int32 i = 0; i < KSSound::Count; ++i)
	{
		KSRender(i, Buffers[i], SR);
	}
	bReady = true;
	UE_LOG(LogKS, Log, TEXT("Synthesized %d sounds at %d Hz in %.1f ms"), (int32)KSSound::Count, SampleRate, (FPlatformTime::Seconds() - Start) * 1000.0);
}

void UKSAudioSynth::PlayVoice(uint8 Sound, float Volume, float Pan, float Pitch)
{
	if (Sound >= KSSound::Count || Volume <= 0.001f)
	{
		return;
	}
	// Equal-power panning.
	const float Angle = (FMath::Clamp(Pan, -1.f, 1.f) + 1.f) * 0.25f * UE_PI;
	FKSVoiceRequest Request;
	Request.Sound = Sound;
	Request.GainL = Volume * FMath::Cos(Angle) * UE_SQRT_2;
	Request.GainR = Volume * FMath::Sin(Angle) * UE_SQRT_2;
	Request.Pitch = FMath::Clamp(Pitch, 0.25f, 4.f);
	Request.Time = FPlatformTime::Seconds();
	Requests.Enqueue(Request);
}

int32 UKSAudioSynth::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	FMemory::Memzero(OutAudio, NumSamples * sizeof(float));
	if (!bReady)
	{
		return NumSamples;
	}

	const double Now = FPlatformTime::Seconds();
	FKSVoiceRequest Request;
	while (Requests.Dequeue(Request))
	{
		// Sounds that waited too long (app was in the background) would all play at once.
		if (Now - Request.Time > 0.3)
		{
			continue;
		}
		int32 Slot = INDEX_NONE;
		double MostPlayed = -1.0;
		for (int32 v = 0; v < MaxVoices; ++v)
		{
			if (Voices[v].Sound == INDEX_NONE)
			{
				Slot = v;
				break;
			}
			const double Played = Voices[v].Position / FMath::Max(1, Buffers[Voices[v].Sound].Num());
			if (Played > MostPlayed)
			{
				MostPlayed = Played;
				Slot = v;
			}
		}
		FKSVoice& Voice = Voices[Slot];
		Voice.Sound = Request.Sound;
		Voice.Position = 0.0;
		Voice.Rate = Request.Pitch;
		Voice.GainL = Request.GainL;
		Voice.GainR = Request.GainR;
	}

	const int32 Frames = NumSamples / 2;
	for (int32 v = 0; v < MaxVoices; ++v)
	{
		FKSVoice& Voice = Voices[v];
		if (Voice.Sound == INDEX_NONE)
		{
			continue;
		}
		const TArray<float>& Buffer = Buffers[Voice.Sound];
		const int32 Len = Buffer.Num();
		for (int32 f = 0; f < Frames; ++f)
		{
			const int32 Index = int32(Voice.Position);
			if (Index >= Len - 1)
			{
				Voice.Sound = INDEX_NONE;
				break;
			}
			const float Alpha = float(Voice.Position - Index);
			const float S = Buffer[Index] + (Buffer[Index + 1] - Buffer[Index]) * Alpha;
			OutAudio[f * 2] += S * Voice.GainL;
			OutAudio[f * 2 + 1] += S * Voice.GainR;
			Voice.Position += Voice.Rate;
		}
	}

	// Soft limiter so many overlapping shots never clip harshly.
	for (int32 i = 0; i < NumSamples; ++i)
	{
		const float X = OutAudio[i];
		OutAudio[i] = X / FMath::Sqrt(1.f + X * X);
	}
	return NumSamples;
}

// ---------------------------------------------------------------------------------------------

namespace KSAudio
{
	static AKSBasePlayerController* KSLocalController(const UObject* WorldContext)
	{
		UWorld* World = (GEngine && WorldContext) ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
		return World ? Cast<AKSBasePlayerController>(World->GetFirstPlayerController()) : nullptr;
	}

	static float KSMasterVolume(const UObject* WorldContext)
	{
		UWorld* World = (GEngine && WorldContext) ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
		const UKSGameInstance* GI = World ? Cast<UKSGameInstance>(World->GetGameInstance()) : nullptr;
		return GI ? GI->GetSettings().Volume : 1.f;
	}

	void Play2D(const UObject* WorldContext, uint8 Sound, float Volume, float Pitch)
	{
		AKSBasePlayerController* PC = KSLocalController(WorldContext);
		if (UKSAudioSynth* Synth = PC ? PC->GetSynth() : nullptr)
		{
			Synth->PlayVoice(Sound, Volume * KSMasterVolume(WorldContext), 0.f, Pitch);
		}
	}

	void PlayAt(const UObject* WorldContext, uint8 Sound, const FVector& Location, float Volume, float Pitch)
	{
		AKSBasePlayerController* PC = KSLocalController(WorldContext);
		UKSAudioSynth* Synth = PC ? PC->GetSynth() : nullptr;
		if (!Synth || !PC->PlayerCameraManager)
		{
			return;
		}
		const FVector Listener = PC->PlayerCameraManager->GetCameraLocation();
		const FRotator View = PC->PlayerCameraManager->GetCameraRotation();
		const FVector Delta = Location - Listener;
		const float Distance = Delta.Size();
		// Full volume up close, smooth falloff, and distant sounds stay faintly audible.
		const float Attenuation = FMath::Max(0.06f, 1.f / (1.f + FMath::Square(FMath::Max(0.f, Distance - 250.f) / 1400.f)));
		const FVector Right = FRotationMatrix(FRotator(0.f, View.Yaw, 0.f)).GetUnitAxis(EAxis::Y);
		const float Pan = Distance > 1.f ? FVector::DotProduct(Delta / Distance, Right) * 0.85f : 0.f;
		Synth->PlayVoice(Sound, Volume * Attenuation * KSMasterVolume(WorldContext), Pan, Pitch);
	}
}
