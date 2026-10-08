#include "KSAssets.h"
#include "Kubostrel.h"
#include "KSGameInstance.h"
#include "KSTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

static UMaterialInterface* KSLoadMaterial(const TCHAR* Path)
{
	return LoadObject<UMaterialInterface>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
}

static UStaticMesh* KSLoadMesh(const TCHAR* Path)
{
	return LoadObject<UStaticMesh>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
}

void UKSAssets::Initialize()
{
	CubeMesh = KSLoadMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	CylinderMesh = KSLoadMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	SphereMesh = KSLoadMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	SurfaceMaterial = KSLoadMaterial(TEXT("/Game/KS/Materials/M_KS_Surface.M_KS_Surface"));
	NeonMaterial = KSLoadMaterial(TEXT("/Game/KS/Materials/M_KS_Neon.M_KS_Neon"));
	GlowMaterial = KSLoadMaterial(TEXT("/Game/KS/Materials/M_KS_Glow.M_KS_Glow"));
	CharacterMaterial = KSLoadMaterial(TEXT("/Game/KS/Materials/M_KS_Character.M_KS_Character"));
	FallbackMaterial = KSLoadMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	bGenerated = SurfaceMaterial && NeonMaterial && GlowMaterial && CharacterMaterial;

	if (!bGenerated)
	{
		UE_LOG(LogKS, Warning, TEXT("Generated materials not found, using BasicShapeMaterial. Open the project in the editor once to create them."));
	}

	// UI shapes are white with an alpha mask so the HUD can tint them.
	// Big touch buttons are drawn at 200+ pixels, so their shapes get more texels.
	CircleTexture = MakeTexture(256, [](float U, float V)
	{
		const float R = FVector2D::Distance(FVector2D(U, V), FVector2D(0.5f, 0.5f));
		return (0.5f - R) / 0.006f;
	});
	RingTexture = MakeTexture(256, [](float U, float V)
	{
		const float R = FVector2D::Distance(FVector2D(U, V), FVector2D(0.5f, 0.5f));
		return FMath::Min((0.5f - R) / 0.006f, (R - 0.43f) / 0.006f);
	});
	SoftTexture = MakeTexture(64, [](float U, float V)
	{
		const float R = FVector2D::Distance(FVector2D(U, V), FVector2D(0.5f, 0.5f));
		return FMath::Square(FMath::Clamp(1.f - R / 0.5f, 0.f, 1.f));
	});
	VignetteTexture = MakeTexture(128, [](float U, float V)
	{
		const float R = FVector2D::Distance(FVector2D(U, V), FVector2D(0.5f, 0.5f));
		return FMath::SmoothStep(0.28f, 0.72f, R);
	});
	ArrowTexture = MakeTexture(64, [](float U, float V)
	{
		// Wedge pointing up, used to show where damage came from.
		if (V < 0.1f || V > 0.6f)
		{
			return 0.f;
		}
		const float HalfWidth = (V - 0.1f) * 0.9f;
		return FMath::Min((HalfWidth - FMath::Abs(U - 0.5f)) / 0.02f, (0.6f - V) / 0.02f);
	});
}

UKSAssets* UKSAssets::Get(const UObject* WorldContext)
{
	UWorld* World = (GEngine && WorldContext) ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	UKSGameInstance* GI = World ? Cast<UKSGameInstance>(World->GetGameInstance()) : nullptr;
	return GI ? GI->GetAssets() : nullptr;
}

UStaticMesh* UKSAssets::GetMesh(EKSShape Shape) const
{
	switch (Shape)
	{
	case EKSShape::Cylinder: return CylinderMesh;
	case EKSShape::Sphere: return SphereMesh;
	default: return CubeMesh;
	}
}

UMaterialInstanceDynamic* UKSAssets::MakeSurface(UObject* Outer, const FKSSurfaceStyle& Style)
{
	UMaterialInterface* Base = SurfaceMaterial ? SurfaceMaterial.Get() : FallbackMaterial.Get();
	UMaterialInstanceDynamic* M = Base ? UMaterialInstanceDynamic::Create(Base, Outer) : nullptr;
	if (M)
	{
		M->SetVectorParameterValue(TEXT("Color"), Style.Color);
		M->SetScalarParameterValue(TEXT("Roughness"), Style.Roughness);
		M->SetScalarParameterValue(TEXT("Metallic"), Style.Metallic);
		M->SetScalarParameterValue(TEXT("GridScale"), Style.GridScale);
		M->SetScalarParameterValue(TEXT("GridStrength"), Style.GridStrength);
		M->SetVectorParameterValue(TEXT("GlowColor"), Style.GlowColor);
		M->SetScalarParameterValue(TEXT("GridGlow"), Style.GridGlow);
	}
	return M;
}

UMaterialInstanceDynamic* UKSAssets::MakeNeon(UObject* Outer, const FLinearColor& Color, float Intensity)
{
	UMaterialInterface* Base = NeonMaterial ? NeonMaterial.Get() : FallbackMaterial.Get();
	UMaterialInstanceDynamic* M = Base ? UMaterialInstanceDynamic::Create(Base, Outer) : nullptr;
	if (M)
	{
		M->SetVectorParameterValue(TEXT("Color"), Color);
		M->SetScalarParameterValue(TEXT("Intensity"), Intensity);
	}
	return M;
}

UMaterialInstanceDynamic* UKSAssets::MakeGlow(UObject* Outer, const FLinearColor& Color, float Intensity, float Opacity)
{
	UMaterialInterface* Base = GlowMaterial ? GlowMaterial.Get() : (NeonMaterial ? NeonMaterial.Get() : FallbackMaterial.Get());
	UMaterialInstanceDynamic* M = Base ? UMaterialInstanceDynamic::Create(Base, Outer) : nullptr;
	if (M)
	{
		M->SetVectorParameterValue(TEXT("Color"), Color);
		M->SetScalarParameterValue(TEXT("Intensity"), Intensity);
		M->SetScalarParameterValue(TEXT("Opacity"), Opacity);
	}
	return M;
}

UMaterialInstanceDynamic* UKSAssets::MakeCharacter(UObject* Outer, const FLinearColor& Color, float Rim)
{
	UMaterialInterface* Base = CharacterMaterial ? CharacterMaterial.Get() : FallbackMaterial.Get();
	UMaterialInstanceDynamic* M = Base ? UMaterialInstanceDynamic::Create(Base, Outer) : nullptr;
	if (M)
	{
		M->SetVectorParameterValue(TEXT("Color"), Color);
		M->SetScalarParameterValue(TEXT("Rim"), Rim);
		M->SetScalarParameterValue(TEXT("Flash"), 0.f);
	}
	return M;
}

UMaterialInstanceDynamic* UKSAssets::SharedNeon(const FLinearColor& Color, float Intensity)
{
	const uint32 Key = KS::ColorKey(Color, Intensity) ^ 0x1u;
	if (TObjectPtr<UMaterialInstanceDynamic>* Found = SharedCache.Find(Key))
	{
		return *Found;
	}
	UMaterialInstanceDynamic* M = MakeNeon(this, Color, Intensity);
	SharedCache.Add(Key, M);
	return M;
}

UMaterialInstanceDynamic* UKSAssets::SharedSurface(const FLinearColor& Color, float Roughness, float Metallic)
{
	const uint32 Key = (KS::ColorKey(Color, Roughness * 10.f + Metallic) ^ 0x2u) + 0x9E3779B9u;
	if (TObjectPtr<UMaterialInstanceDynamic>* Found = SharedCache.Find(Key))
	{
		return *Found;
	}
	FKSSurfaceStyle Style;
	Style.Color = Color;
	Style.Roughness = Roughness;
	Style.Metallic = Metallic;
	Style.GridStrength = 0.f;
	UMaterialInstanceDynamic* M = MakeSurface(this, Style);
	SharedCache.Add(Key, M);
	return M;
}

UTexture2D* UKSAssets::MakeTexture(int32 Size, TFunctionRef<float(float, float)> AlphaFn)
{
	UTexture2D* Tex = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8);
	if (!Tex)
	{
		return nullptr;
	}
	Tex->Filter = TF_Bilinear;
	Tex->AddressX = TA_Clamp;
	Tex->AddressY = TA_Clamp;

	FTexturePlatformData* PlatformData = Tex->GetPlatformData();
	if (PlatformData && PlatformData->Mips.Num() > 0)
	{
		FTexture2DMipMap& Mip = PlatformData->Mips[0];
		FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		if (Pixels)
		{
			for (int32 Y = 0; Y < Size; ++Y)
			{
				for (int32 X = 0; X < Size; ++X)
				{
					const float A = FMath::Clamp(AlphaFn((X + 0.5f) / Size, (Y + 0.5f) / Size), 0.f, 1.f);
					Pixels[Y * Size + X] = FColor(255, 255, 255, (uint8)FMath::RoundToInt(A * 255.f));
				}
			}
		}
		Mip.BulkData.Unlock();
	}
	Tex->UpdateResource();
	return Tex;
}
