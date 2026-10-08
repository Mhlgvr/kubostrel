#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "Containers/Queue.h"
#include "KSAudio.generated.h"

// All game sounds are synthesized once at startup and mixed by this component on the audio thread.
// It lives on the local player controller; sounds from the world are panned and attenuated by hand.
UCLASS()
class UKSAudioSynth : public USynthComponent
{
	GENERATED_BODY()

public:
	UKSAudioSynth(const FObjectInitializer& ObjectInitializer);

	// Game thread. Pan: -1 left .. 1 right.
	void PlayVoice(uint8 Sound, float Volume, float Pan, float Pitch);

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	struct FKSVoiceRequest
	{
		uint8 Sound = 0;
		float GainL = 0.f;
		float GainR = 0.f;
		float Pitch = 1.f;
		double Time = 0.0;
	};

	struct FKSVoice
	{
		int32 Sound = INDEX_NONE;
		double Position = 0.0;
		float Rate = 1.f;
		float GainL = 0.f;
		float GainR = 0.f;
	};

	void RenderSounds(int32 SampleRate);

	// Written once in Init before audio starts, then only read on the audio thread.
	TArray<TArray<float>> Buffers;
	TQueue<FKSVoiceRequest, EQueueMode::Spsc> Requests;
	static constexpr int32 MaxVoices = 32;
	FKSVoice Voices[MaxVoices];
	bool bReady = false;
};

namespace KSAudio
{
	// Sounds that belong to the local player (UI, own weapon).
	void Play2D(const UObject* WorldContext, uint8 Sound, float Volume = 1.f, float Pitch = 1.f);
	// Sounds that happen somewhere in the world, heard from the local camera.
	void PlayAt(const UObject* WorldContext, uint8 Sound, const FVector& Location, float Volume = 1.f, float Pitch = 1.f);
}
