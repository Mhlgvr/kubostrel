#include "KSArenaData.h"
#include "KSTypes.h"

FBox FKSBlock::GetBounds() const
{
	const FBox Local(-Size * 0.5f, Size * 0.5f);
	return Local.TransformBy(FTransform(Rotation, Center));
}

// ---------------------------------------------------------------------------------------------
// Building helpers
// ---------------------------------------------------------------------------------------------

// Rotates a point around the arena center by Quarter * 90 degrees (used for symmetric layouts).
static FVector KSRotQ(const FVector& V, int32 Quarter)
{
	switch (Quarter & 3)
	{
	case 0: return V;
	case 1: return FVector(-V.Y, V.X, V.Z);
	case 2: return FVector(-V.X, -V.Y, V.Z);
	default: return FVector(V.Y, -V.X, V.Z);
	}
}

static FVector KSSizeQ(const FVector& Size, int32 Quarter)
{
	return (Quarter & 1) ? FVector(Size.Y, Size.X, Size.Z) : Size;
}

static FKSBlock& KSAddBox(FKSArenaDef& D, const FVector& Center, const FVector& Size, uint8 Mat, bool bObstacle = true)
{
	FKSBlock& B = D.Blocks.AddDefaulted_GetRef();
	B.Shape = EKSShape::Cube;
	B.Center = Center;
	B.Size = Size;
	B.Mat = Mat;
	B.bObstacle = bObstacle;
	return B;
}

// Box standing on BaseZ. Only X and Y of XY and SizeXY are used.
static FKSBlock& KSAddBoxOn(FKSArenaDef& D, const FVector& XY, const FVector& SizeXY, float Height, uint8 Mat, float BaseZ = 0.f)
{
	return KSAddBox(D, FVector(XY.X, XY.Y, BaseZ + Height * 0.5f), FVector(SizeXY.X, SizeXY.Y, Height), Mat);
}

// Glowing strip without collision or shadow.
static FKSBlock& KSAddNeon(FKSArenaDef& D, const FVector& Center, const FVector& Size, uint8 Mat)
{
	FKSBlock& B = KSAddBox(D, Center, Size, Mat, false);
	B.bCollision = false;
	B.bCastShadow = false;
	return B;
}

static void KSAddNeonFrameTop(FKSArenaDef& D, const FVector& Center, const FVector& Size, uint8 Mat, float Thick = 8.f)
{
	const float Z = Center.Z + Size.Z * 0.5f;
	const float HX = Size.X * 0.5f;
	const float HY = Size.Y * 0.5f;
	KSAddNeon(D, FVector(Center.X + HX, Center.Y, Z), FVector(Thick, Size.Y + Thick, Thick), Mat);
	KSAddNeon(D, FVector(Center.X - HX, Center.Y, Z), FVector(Thick, Size.Y + Thick, Thick), Mat);
	KSAddNeon(D, FVector(Center.X, Center.Y + HY, Z), FVector(Size.X + Thick, Thick, Thick), Mat);
	KSAddNeon(D, FVector(Center.X, Center.Y - HY, Z), FVector(Size.X + Thick, Thick, Thick), Mat);
}

// Box standing on BaseZ with a glowing cap line on top.
static void KSAddCover(FKSArenaDef& D, const FVector& XY, const FVector& SizeXY, float Height, uint8 Mat, uint8 NeonMat, float BaseZ = 0.f)
{
	KSAddBoxOn(D, XY, SizeXY, Height, Mat, BaseZ);
	KSAddNeonFrameTop(D, FVector(XY.X, XY.Y, BaseZ + Height * 0.5f), FVector(SizeXY.X, SizeXY.Y, Height), NeonMat);
}

static void KSAddCylinder(FKSArenaDef& D, const FVector& Center, float Diameter, float Height, uint8 Mat, bool bCollision = true)
{
	FKSBlock& B = D.Blocks.AddDefaulted_GetRef();
	B.Shape = EKSShape::Cylinder;
	B.Center = Center;
	B.Size = FVector(Diameter, Diameter, Height);
	B.Mat = Mat;
	B.bCollision = bCollision;
	B.bCastShadow = bCollision;
	B.bObstacle = bCollision;
}

static void KSAddPillar(FKSArenaDef& D, const FVector& XY, float Diameter, float Height, uint8 Mat, uint8 NeonMat)
{
	KSAddCylinder(D, FVector(XY.X, XY.Y, Height * 0.5f), Diameter, Height, Mat);
	KSAddCylinder(D, FVector(XY.X, XY.Y, Height - 90.f), Diameter + 14.f, 16.f, NeonMat, false);
	KSAddCylinder(D, FVector(XY.X, XY.Y, 50.f), Diameter + 14.f, 10.f, NeonMat, false);
}

// Walkable ramp whose top surface runs exactly from Bottom to Top.
static void KSAddRamp(FKSArenaDef& D, const FVector& Bottom, const FVector& Top, float Width, uint8 Mat, uint8 NeonMat)
{
	const FVector Dir = Top - Bottom;
	const float Len = Dir.Size();
	const FRotator Rot = Dir.Rotation();
	const float Thick = 30.f;
	const FVector Up = Rot.RotateVector(FVector::UpVector);
	const FVector Side = Rot.RotateVector(FVector::RightVector);

	FKSBlock& B = KSAddBox(D, (Bottom + Top) * 0.5f - Up * (Thick * 0.5f), FVector(Len + 60.f, Width, Thick), Mat, false);
	B.Rotation = Rot;

	for (const float S : { -1.f, 1.f })
	{
		FKSBlock& N = KSAddNeon(D, (Bottom + Top) * 0.5f + Side * (S * (Width * 0.5f - 6.f)) + Up * 4.f, FVector(Len, 8.f, 8.f), NeonMat);
		N.Rotation = Rot;
	}

	const FVector Flat = FVector(Dir.X, Dir.Y, 0.f).GetSafeNormal();
	FKSNavLink& L = D.NavLinks.AddDefaulted_GetRef();
	L.From = Bottom - Flat * 120.f;
	L.To = Top + Flat * 120.f;
	L.bTwoWay = true;
}

static void KSAddDrop(FKSArenaDef& D, const FVector& From, const FVector& To)
{
	FKSNavLink& L = D.NavLinks.AddDefaulted_GetRef();
	L.From = From;
	L.To = To;
	L.bTwoWay = false;
}

static void KSAddJumpPad(FKSArenaDef& D, const FVector& Location, const FVector& Target, float ApexAboveStart)
{
	FKSJumpPad& Pad = D.JumpPads.AddDefaulted_GetRef();
	Pad.Location = Location;
	Pad.Target = Target;
	Pad.Velocity = KSArena::ComputeLaunchVelocity(Location, Target, ApexAboveStart);

	KSAddCylinder(D, Location + FVector(0.f, 0.f, 4.f), 280.f, 8.f, KSMat::Metal, false);
	KSAddCylinder(D, Location + FVector(0.f, 0.f, 9.f), 220.f, 6.f, KSMat::NeonPad, false);
	// Arrow-like marks pointing to where the pad throws you.
	const FVector Flat = FVector(Target.X - Location.X, Target.Y - Location.Y, 0.f).GetSafeNormal();
	const FRotator Yaw = Flat.Rotation();
	for (int32 i = 0; i < 3; ++i)
	{
		FKSBlock& Mark = KSAddNeon(D, Location + Flat * (170.f + i * 60.f) + FVector(0.f, 0.f, 3.f), FVector(18.f, 90.f - i * 18.f, 4.f), KSMat::NeonPad);
		Mark.Rotation = Yaw;
	}
}

static void KSAddSpawn(FKSArenaDef& D, const FVector& FloorPoint)
{
	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(-FloorPoint.Y, -FloorPoint.X));
	D.Spawns.Add(FTransform(FRotator(0.f, Yaw, 0.f), FloorPoint + FVector(0.f, 0.f, KS::CapsuleHalfHeight + 10.f)));
}

static void KSAddPickup(FKSArenaDef& D, const FVector& FloorPoint, uint8 Type)
{
	FKSPickupSpot& P = D.Pickups.AddDefaulted_GetRef();
	P.Location = FloorPoint;
	P.Type = Type;
}

static void KSAddNavGrid(FKSArenaDef& D, const FVector2D& Min, const FVector2D& Max, float Z, float Step)
{
	for (float X = Min.X; X <= Max.X + 1.f; X += Step)
	{
		for (float Y = Min.Y; Y <= Max.Y + 1.f; Y += Step)
		{
			D.NavPoints.Add(FVector(X, Y, Z));
		}
	}
}

// Rectangular room: floor, four walls and neon lines along them.
static void KSAddRoom(FKSArenaDef& D, float HX, float HY, float WallH, uint8 TopNeon, uint8 BottomNeon, float BarStep)
{
	const float WallT = 200.f;
	KSAddBox(D, FVector(0.f, 0.f, -50.f), FVector(2.f * HX + 2.f * WallT, 2.f * HY + 2.f * WallT, 100.f), KSMat::Floor, false).bCastShadow = false;

	// Outer walls don't cast shadows: with a low sunset sun they would darken half the arena.
	KSAddBox(D, FVector(HX + WallT * 0.5f, 0.f, WallH * 0.5f), FVector(WallT, 2.f * HY + 2.f * WallT, WallH), KSMat::Wall).bCastShadow = false;
	KSAddBox(D, FVector(-HX - WallT * 0.5f, 0.f, WallH * 0.5f), FVector(WallT, 2.f * HY + 2.f * WallT, WallH), KSMat::Wall).bCastShadow = false;
	KSAddBox(D, FVector(0.f, HY + WallT * 0.5f, WallH * 0.5f), FVector(2.f * HX, WallT, WallH), KSMat::Wall).bCastShadow = false;
	KSAddBox(D, FVector(0.f, -HY - WallT * 0.5f, WallH * 0.5f), FVector(2.f * HX, WallT, WallH), KSMat::Wall).bCastShadow = false;

	for (const float S : { -1.f, 1.f })
	{
		KSAddNeon(D, FVector(S * (HX - 4.f), 0.f, WallH - 40.f), FVector(8.f, 2.f * HY, 14.f), TopNeon);
		KSAddNeon(D, FVector(S * (HX - 4.f), 0.f, 6.f), FVector(8.f, 2.f * HY, 10.f), BottomNeon);
		KSAddNeon(D, FVector(0.f, S * (HY - 4.f), WallH - 40.f), FVector(2.f * HX, 8.f, 14.f), TopNeon);
		KSAddNeon(D, FVector(0.f, S * (HY - 4.f), 6.f), FVector(2.f * HX, 8.f, 10.f), BottomNeon);

		const int32 BarsX = FMath::FloorToInt((HX - 300.f) / BarStep);
		for (int32 i = -BarsX; i <= BarsX; ++i)
		{
			KSAddNeon(D, FVector(i * BarStep, S * (HY - 4.f), WallH * 0.5f), FVector(12.f, 8.f, WallH - 160.f), TopNeon);
		}
		const int32 BarsY = FMath::FloorToInt((HY - 300.f) / BarStep);
		for (int32 i = -BarsY; i <= BarsY; ++i)
		{
			KSAddNeon(D, FVector(S * (HX - 4.f), i * BarStep, WallH * 0.5f), FVector(8.f, 12.f, WallH - 160.f), TopNeon);
		}
	}
}

// A ring of dark towers with glowing bands outside the arena walls.
static void KSAddSkyline(FKSArenaDef& D, float MinDist, float MaxDist, int32 Count, int32 Seed)
{
	FRandomStream R(Seed);
	const uint8 Neons[3] = { KSMat::NeonA, KSMat::NeonB, KSMat::NeonC };
	for (int32 i = 0; i < Count; ++i)
	{
		const float Angle = (float(i) / Count) * 2.f * UE_PI + R.FRandRange(-0.06f, 0.06f);
		const float Dist = R.FRandRange(MinDist, MaxDist);
		const float W = R.FRandRange(700.f, 1700.f);
		const float L = W * R.FRandRange(0.6f, 1.4f);
		const float H = R.FRandRange(1800.f, 5600.f);
		const FVector Base(FMath::Cos(Angle) * Dist, FMath::Sin(Angle) * Dist, -200.f);
		const FRotator Yaw(0.f, FMath::RadiansToDegrees(Angle), 0.f);

		FKSBlock& B = KSAddBox(D, Base + FVector(0.f, 0.f, H * 0.5f), FVector(W, L, H), KSMat::Skyline, false);
		B.Rotation = Yaw;
		B.bCollision = false;
		B.bCastShadow = false;

		const int32 Bands = R.RandRange(1, 3);
		for (int32 b = 0; b < Bands; ++b)
		{
			const float Z = Base.Z + H * R.FRandRange(0.35f, 0.97f);
			FKSBlock& N = KSAddNeon(D, FVector(Base.X, Base.Y, Z), FVector(W + 14.f, L + 14.f, R.FRandRange(12.f, 40.f)), Neons[R.RandRange(0, 2)]);
			N.Rotation = Yaw;
		}
	}
}

static FKSSurfaceStyle KSStyle(const FLinearColor& Color, float Roughness, float Metallic, float GridScale, float GridStrength,
	const FLinearColor& Glow = FLinearColor::Black, float GridGlow = 0.f)
{
	FKSSurfaceStyle S;
	S.Color = Color;
	S.Roughness = Roughness;
	S.Metallic = Metallic;
	S.GridScale = GridScale;
	S.GridStrength = GridStrength;
	S.GlowColor = Glow;
	S.GridGlow = GridGlow;
	return S;
}

static FKSSurfaceStyle KSNeonStyle(const FLinearColor& Color, float Intensity)
{
	FKSSurfaceStyle S;
	S.Color = Color;
	S.Intensity = Intensity;
	return S;
}

// ---------------------------------------------------------------------------------------------
// Arena 0: "Neon Cube" - square arena, raised center, corner towers, sunset.
// ---------------------------------------------------------------------------------------------
static void KSBuildNeonCube(FKSArenaDef& D)
{
	D.Name = TEXT("Неон-Куб");
	const float H = 2800.f;
	D.HalfSize = FVector2D(H, H);
	KSAddRoom(D, H, H, 1000.f, KSMat::NeonA, KSMat::NeonB, 1100.f);

	// Center platform with two ramps and a pedestal for the rocket launcher.
	const float PH = 280.f;
	const float PS = 700.f;
	KSAddBox(D, FVector(0.f, 0.f, PH * 0.5f), FVector(2.f * PS, 2.f * PS, PH), KSMat::Platform);
	KSAddNeonFrameTop(D, FVector(0.f, 0.f, PH * 0.5f), FVector(2.f * PS, 2.f * PS, PH), KSMat::NeonB);
	KSAddRamp(D, FVector(0.f, PS + 950.f, 0.f), FVector(0.f, PS, PH), 520.f, KSMat::Platform, KSMat::NeonA);
	KSAddRamp(D, FVector(0.f, -PS - 950.f, 0.f), FVector(0.f, -PS, PH), 520.f, KSMat::Platform, KSMat::NeonA);
	KSAddCover(D, FVector(0.f, 0.f, 0.f), FVector(200.f, 200.f, 0.f), 30.f, KSMat::Metal, KSMat::NeonC, PH);
	KSAddPickup(D, FVector(0.f, 0.f, PH + 30.f), KSPickupType::Rocket);
	KSAddNavGrid(D, FVector2D(-560.f, -560.f), FVector2D(560.f, 560.f), PH, 280.f);

	// Jump pads east and west throw players onto the platform.
	KSAddJumpPad(D, FVector(1650.f, 0.f, 0.f), FVector(380.f, 0.f, PH), 560.f);
	KSAddJumpPad(D, FVector(-1650.f, 0.f, 0.f), FVector(-380.f, 0.f, PH), 560.f);

	const float TH = 300.f;
	for (int32 Q = 0; Q < 4; ++Q)
	{
		// Platform corner parapets. They stop short of the middle so there is room to drop down beside them.
		KSAddCover(D, KSRotQ(FVector(PS - 130.f, PS - 30.f, 0.f), Q), KSSizeQ(FVector(250.f, 50.f, 0.f), Q), 110.f, KSMat::CoverB, KSMat::NeonA, PH);
		KSAddCover(D, KSRotQ(FVector(PS - 30.f, PS - 130.f, 0.f), Q), KSSizeQ(FVector(50.f, 250.f, 0.f), Q), 110.f, KSMat::CoverB, KSMat::NeonA, PH);
		// Drop-down spots for bots, between the ramp and the parapet.
		KSAddDrop(D, KSRotQ(FVector(600.f, 350.f, PH), Q), KSRotQ(FVector(980.f, 350.f, 0.f), Q));

		// Corner tower with a ramp along the wall.
		const FVector TowerC = KSRotQ(FVector(2400.f, 2400.f, TH * 0.5f), Q);
		KSAddBox(D, TowerC, FVector(800.f, 800.f, TH), KSMat::Wall);
		KSAddNeonFrameTop(D, TowerC, FVector(800.f, 800.f, TH), KSMat::NeonA);
		KSAddCover(D, KSRotQ(FVector(2025.f, 2150.f, 0.f), Q), KSSizeQ(FVector(50.f, 300.f, 0.f), Q), 100.f, KSMat::CoverB, KSMat::NeonB, TH);
		// The south parapet leaves a gap near the west corner to jump down through.
		KSAddCover(D, KSRotQ(FVector(2575.f, 2025.f, 0.f), Q), KSSizeQ(FVector(450.f, 50.f, 0.f), Q), 100.f, KSMat::CoverB, KSMat::NeonB, TH);
		KSAddRamp(D, KSRotQ(FVector(1000.f, 2550.f, 0.f), Q), KSRotQ(FVector(2000.f, 2550.f, TH), Q), 400.f, KSMat::Wall, KSMat::NeonB);
		D.NavPoints.Add(KSRotQ(FVector(2450.f, 2450.f, TH), Q));
		KSAddSpawn(D, KSRotQ(FVector(2300.f, 2300.f, TH), Q));
		KSAddPickup(D, KSRotQ(FVector(2600.f, 2600.f, TH), Q), (Q % 2 == 0) ? KSPickupType::Rail : KSPickupType::Health);
		KSAddDrop(D, KSRotQ(FVector(2200.f, 2200.f, TH), Q), KSRotQ(FVector(2200.f, 1750.f, 0.f), Q));

		// Crates near the ramps and jump pads.
		for (const float X : { 600.f, -600.f })
		{
			KSAddCover(D, KSRotQ(FVector(X, 1900.f, 0.f), Q), FVector(220.f, 220.f, 0.f), 170.f, KSMat::CoverA, KSMat::NeonC);
		}

		// L-shaped low walls in each quadrant.
		KSAddCover(D, KSRotQ(FVector(1450.f, 1250.f, 0.f), Q), KSSizeQ(FVector(400.f, 60.f, 0.f), Q), 140.f, KSMat::CoverB, KSMat::NeonA);
		KSAddCover(D, KSRotQ(FVector(1250.f, 1450.f, 0.f), Q), KSSizeQ(FVector(60.f, 400.f, 0.f), Q), 140.f, KSMat::CoverB, KSMat::NeonA);

		// Tall pillar.
		KSAddPillar(D, KSRotQ(FVector(1750.f, 1750.f, 0.f), Q), 220.f, 1000.f, KSMat::Metal, KSMat::NeonA);

		// Ground spawns and pickups.
		KSAddSpawn(D, KSRotQ(FVector(600.f, 2500.f, 0.f), Q));
		KSAddSpawn(D, KSRotQ(FVector(-600.f, 2500.f, 0.f), Q));
		KSAddPickup(D, KSRotQ(FVector(0.f, 2250.f, 0.f), Q), (Q % 2 == 0) ? KSPickupType::Shotgun : KSPickupType::Health);
	}

	KSAddSkyline(D, H + 1500.f, H + 6500.f, 30, 1337);

	D.MenuCameraTarget = FVector(0.f, 0.f, 250.f);
	D.MenuCameraRadius = 2300.f;
	D.MenuCameraHeight = 750.f;

	FKSArenaTheme& T = D.Theme;
	T.SunRotation = FRotator(-12.f, 140.f, 0.f);
	T.SunColor = FLinearColor(1.0f, 0.62f, 0.38f);
	T.SunIntensity = 7.f;
	T.SkyLightIntensity = 1.4f;
	T.FogColor = FLinearColor(0.45f, 0.32f, 0.5f);
	T.FogDensity = 0.018f;
	T.FogFalloff = 0.25f;
	T.FogStart = 1500.f;
	T.Bloom = 1.1f;
	T.Mats[KSMat::Floor] = KSStyle(FLinearColor(0.035f, 0.04f, 0.055f), 0.35f, 0.1f, 200.f, 0.6f, FLinearColor(0.0f, 0.55f, 1.0f), 0.8f);
	T.Mats[KSMat::Wall] = KSStyle(FLinearColor(0.62f, 0.6f, 0.62f), 0.8f, 0.f, 400.f, 0.18f);
	T.Mats[KSMat::Platform] = KSStyle(FLinearColor(0.10f, 0.07f, 0.16f), 0.45f, 0.2f, 100.f, 0.5f, FLinearColor(1.0f, 0.1f, 0.6f), 0.6f);
	T.Mats[KSMat::CoverA] = KSStyle(FLinearColor(0.95f, 0.38f, 0.06f), 0.55f, 0.f, 110.f, 0.35f);
	T.Mats[KSMat::CoverB] = KSStyle(FLinearColor(0.05f, 0.55f, 0.6f), 0.5f, 0.f, 100.f, 0.3f);
	T.Mats[KSMat::Metal] = KSStyle(FLinearColor(0.04f, 0.045f, 0.05f), 0.28f, 0.8f, 100.f, 0.2f);
	T.Mats[KSMat::Skyline] = KSStyle(FLinearColor(0.012f, 0.013f, 0.02f), 0.85f, 0.f, 300.f, 0.4f, FLinearColor(0.3f, 0.6f, 1.0f), 0.35f);
	T.Mats[KSMat::NeonA] = KSNeonStyle(FLinearColor(0.0f, 0.75f, 1.0f), 7.f);
	T.Mats[KSMat::NeonB] = KSNeonStyle(FLinearColor(1.0f, 0.08f, 0.55f), 7.f);
	T.Mats[KSMat::NeonC] = KSNeonStyle(FLinearColor(1.0f, 0.42f, 0.04f), 6.f);
	T.Mats[KSMat::NeonPad] = KSNeonStyle(FLinearColor(0.55f, 1.0f, 0.1f), 8.f);
}

// ---------------------------------------------------------------------------------------------
// Arena 1: "Bastions" - long hall with two forts, a central tower and jump pads, blue hour.
// ---------------------------------------------------------------------------------------------
static void KSBuildBastions(FKSArenaDef& D)
{
	D.Name = TEXT("Бастионы");
	const float HX = 3800.f;
	const float HY = 2000.f;
	D.HalfSize = FVector2D(HX, HY);
	KSAddRoom(D, HX, HY, 900.f, KSMat::NeonA, KSMat::NeonB, 1200.f);

	const float FH = 320.f;
	for (const float S : { 1.f, -1.f })
	{
		// Fort platform at the end of the hall.
		const FVector FortC(S * (2600.f + HX) * 0.5f, 0.f, FH * 0.5f);
		const FVector FortSize(HX - 2600.f, 2.f * HY, FH);
		KSAddBox(D, FortC, FortSize, KSMat::Platform);
		KSAddNeonFrameTop(D, FortC, FortSize, KSMat::NeonB);

		// Front parapet with gaps for the ramps and the middle drop.
		const FVector2D Segments[4] = { FVector2D(-950.f, -250.f), FVector2D(250.f, 950.f), FVector2D(1600.f, 2000.f), FVector2D(-2000.f, -1600.f) };
		for (const FVector2D& Seg : Segments)
		{
			KSAddCover(D, FVector(S * 2625.f, (Seg.X + Seg.Y) * 0.5f, 0.f), FVector(50.f, Seg.Y - Seg.X, 0.f), 120.f, KSMat::CoverB, KSMat::NeonA, FH);
		}
		for (const float Y : { 1300.f, -1300.f })
		{
			KSAddRamp(D, FVector(S * 1500.f, Y, 0.f), FVector(S * 2600.f, Y, FH), 450.f, KSMat::Platform, KSMat::NeonA);
		}
		for (const float Y : { 600.f, -600.f })
		{
			KSAddCover(D, FVector(S * 3000.f, Y, 0.f), FVector(200.f, 200.f, 0.f), 160.f, KSMat::CoverA, KSMat::NeonC, FH);
		}
		KSAddNavGrid(D, FVector2D(S > 0.f ? 2750.f : -3650.f, -1850.f), FVector2D(S > 0.f ? 3650.f : -2750.f, 1850.f), FH, 370.f);
		KSAddDrop(D, FVector(S * 2700.f, 0.f, FH), FVector(S * 2300.f, 0.f, 0.f));

		KSAddSpawn(D, FVector(S * 3500.f, 1300.f, FH));
		KSAddSpawn(D, FVector(S * 3500.f, -1300.f, FH));
		KSAddSpawn(D, FVector(S * 3450.f, 450.f, FH));
		KSAddSpawn(D, FVector(S * 2250.f, 1800.f, 0.f));
		KSAddSpawn(D, FVector(S * 2250.f, -1800.f, 0.f));
		KSAddPickup(D, FVector(S * 3550.f, 0.f, FH), KSPickupType::Rail);
		KSAddPickup(D, FVector(S * 2250.f, 0.f, 0.f), KSPickupType::Health);

		KSAddPillar(D, FVector(S * 1500.f, 0.f, 0.f), 240.f, 900.f, KSMat::Metal, KSMat::NeonB);
		KSAddJumpPad(D, FVector(S * 1000.f, 0.f, 0.f), FVector(S * 150.f, 0.f, 450.f), 650.f);

		for (const float SY : { 1.f, -1.f })
		{
			// Low walls along the side lanes.
			KSAddCover(D, FVector(S * 700.f, SY * 1100.f, 0.f), FVector(800.f, 60.f, 0.f), 130.f, KSMat::CoverB, KSMat::NeonA);
			// Crates near the long walls.
			KSAddCover(D, FVector(S * 600.f, SY * 1650.f, 0.f), FVector(220.f, 220.f, 0.f), 170.f, KSMat::CoverA, KSMat::NeonC);
			KSAddSpawn(D, FVector(S * 1250.f, SY * 1800.f, 0.f));
		}
	}

	// Central tower; its roof is reached by the jump pads.
	KSAddBox(D, FVector(0.f, 0.f, 225.f), FVector(600.f, 600.f, 450.f), KSMat::Wall);
	KSAddNeonFrameTop(D, FVector(0.f, 0.f, 225.f), FVector(600.f, 600.f, 450.f), KSMat::NeonB);
	KSAddNeon(D, FVector(0.f, 0.f, 120.f), FVector(612.f, 612.f, 14.f), KSMat::NeonA);
	D.NavPoints.Add(FVector(0.f, 0.f, 450.f));
	KSAddPickup(D, FVector(0.f, 0.f, 450.f), KSPickupType::Rocket);
	for (int32 Q = 0; Q < 4; ++Q)
	{
		KSAddDrop(D, KSRotQ(FVector(230.f, 0.f, 450.f), Q), KSRotQ(FVector(560.f, 0.f, 0.f), Q));
	}
	KSAddPickup(D, FVector(0.f, 1650.f, 0.f), KSPickupType::Shotgun);
	KSAddPickup(D, FVector(0.f, -1650.f, 0.f), KSPickupType::Shotgun);

	KSAddSkyline(D, HX + 1500.f, HX + 6500.f, 30, 4242);

	D.MenuCameraTarget = FVector(0.f, 0.f, 250.f);
	D.MenuCameraRadius = 2600.f;
	D.MenuCameraHeight = 800.f;

	FKSArenaTheme& T = D.Theme;
	T.SunRotation = FRotator(-5.f, -50.f, 0.f);
	T.SunColor = FLinearColor(1.0f, 0.5f, 0.42f);
	T.SunIntensity = 5.f;
	T.SkyLightIntensity = 1.3f;
	T.FogColor = FLinearColor(0.25f, 0.18f, 0.4f);
	T.FogDensity = 0.024f;
	T.FogFalloff = 0.22f;
	T.FogStart = 1200.f;
	T.Bloom = 1.25f;
	T.Mats[KSMat::Floor] = KSStyle(FLinearColor(0.02f, 0.02f, 0.035f), 0.3f, 0.2f, 250.f, 0.6f, FLinearColor(1.0f, 0.15f, 0.6f), 0.9f);
	T.Mats[KSMat::Wall] = KSStyle(FLinearColor(0.2f, 0.2f, 0.27f), 0.7f, 0.f, 300.f, 0.25f, FLinearColor(0.0f, 0.6f, 1.0f), 0.15f);
	T.Mats[KSMat::Platform] = KSStyle(FLinearColor(0.06f, 0.08f, 0.14f), 0.5f, 0.2f, 150.f, 0.45f, FLinearColor(0.0f, 0.7f, 1.0f), 0.5f);
	T.Mats[KSMat::CoverA] = KSStyle(FLinearColor(0.9f, 0.8f, 0.1f), 0.5f, 0.f, 110.f, 0.3f);
	T.Mats[KSMat::CoverB] = KSStyle(FLinearColor(0.45f, 0.1f, 0.75f), 0.5f, 0.f, 100.f, 0.3f);
	T.Mats[KSMat::Metal] = KSStyle(FLinearColor(0.05f, 0.05f, 0.06f), 0.25f, 0.85f, 100.f, 0.2f);
	T.Mats[KSMat::Skyline] = KSStyle(FLinearColor(0.012f, 0.012f, 0.022f), 0.85f, 0.f, 300.f, 0.4f, FLinearColor(1.0f, 0.2f, 0.7f), 0.3f);
	T.Mats[KSMat::NeonA] = KSNeonStyle(FLinearColor(1.0f, 0.1f, 0.6f), 7.f);
	T.Mats[KSMat::NeonB] = KSNeonStyle(FLinearColor(0.0f, 0.8f, 1.0f), 7.f);
	T.Mats[KSMat::NeonC] = KSNeonStyle(FLinearColor(1.0f, 0.85f, 0.1f), 6.f);
	T.Mats[KSMat::NeonPad] = KSNeonStyle(FLinearColor(0.2f, 1.0f, 0.6f), 8.f);
}

// ---------------------------------------------------------------------------------------------

namespace KSArena
{
	static TArray<FKSArenaDef>& KSAllArenas()
	{
		static TArray<FKSArenaDef> Arenas;
		if (Arenas.Num() == 0)
		{
			KSBuildNeonCube(Arenas.AddDefaulted_GetRef());
			KSBuildBastions(Arenas.AddDefaulted_GetRef());
		}
		return Arenas;
	}

	int32 Num()
	{
		return KSAllArenas().Num();
	}

	const FKSArenaDef& Get(int32 Index)
	{
		TArray<FKSArenaDef>& Arenas = KSAllArenas();
		return Arenas[FMath::Clamp(Index, 0, Arenas.Num() - 1)];
	}

	FString GetName(int32 Index)
	{
		return Get(Index).Name;
	}

	FVector ComputeLaunchVelocity(const FVector& From, const FVector& To, float ApexAboveFrom)
	{
		const float G = KS::Gravity;
		const float Apex = FMath::Max(ApexAboveFrom, (To.Z - From.Z) + 80.f);
		const float Vz = FMath::Sqrt(2.f * G * Apex);
		const float TimeUp = Vz / G;
		const float Drop = FMath::Max(10.f, From.Z + Apex - To.Z);
		const float TimeDown = FMath::Sqrt(2.f * Drop / G);
		const FVector Flat(To.X - From.X, To.Y - From.Y, 0.f);
		const FVector Horizontal = Flat / (TimeUp + TimeDown);
		return FVector(Horizontal.X, Horizontal.Y, Vz);
	}
}

// ---------------------------------------------------------------------------------------------
// Navigation graph
// ---------------------------------------------------------------------------------------------

static bool KSSegmentHitsBox2D(const FVector2D& A, const FVector2D& B, const FVector2D& Min, const FVector2D& Max)
{
	// Slab test of segment A->B against an axis aligned rectangle.
	float T0 = 0.f;
	float T1 = 1.f;
	const FVector2D D = B - A;
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		const float P = Axis == 0 ? A.X : A.Y;
		const float Dir = Axis == 0 ? D.X : D.Y;
		const float Lo = Axis == 0 ? Min.X : Min.Y;
		const float Hi = Axis == 0 ? Max.X : Max.Y;
		if (FMath::Abs(Dir) < UE_KINDA_SMALL_NUMBER)
		{
			if (P < Lo || P > Hi)
			{
				return false;
			}
		}
		else
		{
			float Ta = (Lo - P) / Dir;
			float Tb = (Hi - P) / Dir;
			if (Ta > Tb)
			{
				Swap(Ta, Tb);
			}
			T0 = FMath::Max(T0, Ta);
			T1 = FMath::Min(T1, Tb);
			if (T0 > T1)
			{
				return false;
			}
		}
	}
	return true;
}

// True if the block gets in the way of someone walking on level Z.
static bool KSBlocksLevel(const FKSBlock& Block, const FBox& Bounds, float Z)
{
	return Block.bCollision && Block.bObstacle && Bounds.Min.Z < Z + 150.f && Bounds.Max.Z > Z + 35.f;
}

static bool KSIsOnSurface(const FKSArenaDef& Def, const FVector& P)
{
	if (P.Z < 20.f)
	{
		return FMath::Abs(P.X) < Def.HalfSize.X && FMath::Abs(P.Y) < Def.HalfSize.Y;
	}
	for (const FKSBlock& Block : Def.Blocks)
	{
		if (!Block.bCollision || KSMat::IsNeon(Block.Mat) || !Block.Rotation.IsNearlyZero())
		{
			continue;
		}
		const FBox Bounds = Block.GetBounds();
		if (FMath::Abs(Bounds.Max.Z - P.Z) < 25.f
			&& P.X > Bounds.Min.X + 15.f && P.X < Bounds.Max.X - 15.f
			&& P.Y > Bounds.Min.Y + 15.f && P.Y < Bounds.Max.Y - 15.f)
		{
			return true;
		}
	}
	return false;
}

static bool KSPointFree(const FKSArenaDef& Def, const FVector& P, float Margin)
{
	for (const FKSBlock& Block : Def.Blocks)
	{
		const FBox Bounds = Block.GetBounds();
		if (!KSBlocksLevel(Block, Bounds, P.Z))
		{
			continue;
		}
		if (P.X > Bounds.Min.X - Margin && P.X < Bounds.Max.X + Margin && P.Y > Bounds.Min.Y - Margin && P.Y < Bounds.Max.Y + Margin)
		{
			return false;
		}
	}
	return true;
}

static bool KSOverWalkable(const FKSArenaDef& Def, const FVector& P)
{
	// Ground points under ramps or raised blocks are not useful waypoints.
	if (P.Z > 20.f)
	{
		return true;
	}
	for (const FKSBlock& Block : Def.Blocks)
	{
		if (!Block.bCollision || Block.bObstacle)
		{
			continue;
		}
		const FBox Bounds = Block.GetBounds();
		if (Bounds.Max.Z > 20.f && P.X > Bounds.Min.X - 40.f && P.X < Bounds.Max.X + 40.f && P.Y > Bounds.Min.Y - 40.f && P.Y < Bounds.Max.Y + 40.f)
		{
			return false;
		}
	}
	return true;
}

static bool KSSegmentClear(const FKSArenaDef& Def, const FVector& A, const FVector& B)
{
	const FVector2D A2(A.X, A.Y);
	const FVector2D B2(B.X, B.Y);
	const float Margin = 45.f;
	for (const FKSBlock& Block : Def.Blocks)
	{
		const FBox Bounds = Block.GetBounds();
		if (!KSBlocksLevel(Block, Bounds, A.Z))
		{
			continue;
		}
		if (KSSegmentHitsBox2D(A2, B2, FVector2D(Bounds.Min.X - Margin, Bounds.Min.Y - Margin), FVector2D(Bounds.Max.X + Margin, Bounds.Max.Y + Margin)))
		{
			return false;
		}
	}
	// Raised levels: the whole segment must stay on top of something.
	if (A.Z > 20.f)
	{
		const float Len = FVector2D::Distance(A2, B2);
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(Len / 100.f));
		for (int32 i = 1; i < Steps; ++i)
		{
			const FVector P = FMath::Lerp(A, B, float(i) / Steps);
			if (!KSIsOnSurface(Def, P))
			{
				return false;
			}
		}
	}
	else
	{
		// Ground level: don't walk across ramps sideways.
		const float Len = FVector2D::Distance(A2, B2);
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(Len / 100.f));
		for (int32 i = 1; i < Steps; ++i)
		{
			if (!KSOverWalkable(Def, FMath::Lerp(A, B, float(i) / Steps)))
			{
				return false;
			}
		}
	}
	return true;
}

int32 FKSNavGraph::AddNode(const FVector& Location)
{
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		if (FVector::DistSquared(Nodes[i], Location) < FMath::Square(40.f))
		{
			return i;
		}
	}
	Nodes.Add(Location);
	Links.AddDefaulted();
	return Nodes.Num() - 1;
}

void FKSNavGraph::AddLink(int32 From, int32 To)
{
	if (Nodes.IsValidIndex(From) && Nodes.IsValidIndex(To) && From != To)
	{
		Links[From].AddUnique(To);
	}
}

void FKSNavGraph::Build(const FKSArenaDef& Def)
{
	Nodes.Reset();
	Links.Reset();

	// Ground grid, centered so both halves of the arena get the same waypoints.
	const float Step = 500.f;
	const int32 CountX = FMath::FloorToInt((2.f * Def.HalfSize.X - 400.f) / Step);
	const int32 CountY = FMath::FloorToInt((2.f * Def.HalfSize.Y - 400.f) / Step);
	for (int32 IX = 0; IX <= CountX; ++IX)
	{
		for (int32 IY = 0; IY <= CountY; ++IY)
		{
			const FVector P((IX - CountX * 0.5f) * Step, (IY - CountY * 0.5f) * Step, 0.f);
			if (KSPointFree(Def, P, 60.f) && KSOverWalkable(Def, P))
			{
				AddNode(P);
			}
		}
	}
	for (const FVector& P : Def.NavPoints)
	{
		if (KSPointFree(Def, P, 50.f) && KSIsOnSurface(Def, P))
		{
			AddNode(P);
		}
	}
	for (const FKSNavLink& L : Def.NavLinks)
	{
		AddNode(L.From);
		AddNode(L.To);
	}
	for (const FKSJumpPad& Pad : Def.JumpPads)
	{
		AddNode(Pad.Location);
		AddNode(Pad.Target);
	}

	// Automatic links between nearby nodes on the same level.
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		for (int32 j = i + 1; j < Nodes.Num(); ++j)
		{
			const FVector& A = Nodes[i];
			const FVector& B = Nodes[j];
			if (FMath::Abs(A.Z - B.Z) > 30.f)
			{
				continue;
			}
			if (FVector::Dist2D(A, B) > 760.f)
			{
				continue;
			}
			if (KSSegmentClear(Def, A, B))
			{
				AddLink(i, j);
				AddLink(j, i);
			}
		}
	}

	for (const FKSNavLink& L : Def.NavLinks)
	{
		const int32 A = AddNode(L.From);
		const int32 B = AddNode(L.To);
		AddLink(A, B);
		if (L.bTwoWay)
		{
			AddLink(B, A);
		}
	}
	for (const FKSJumpPad& Pad : Def.JumpPads)
	{
		AddLink(AddNode(Pad.Location), AddNode(Pad.Target));
	}
}

int32 FKSNavGraph::FindNearest(const FVector& Location) const
{
	int32 Best = INDEX_NONE;
	float BestScore = TNumericLimits<float>::Max();
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		const FVector D = Nodes[i] - Location;
		// Height differences count triple, so a node on another level is rarely chosen.
		const float Score = FMath::Square(D.X) + FMath::Square(D.Y) + FMath::Square(D.Z * 3.f);
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = i;
		}
	}
	return Best;
}

bool FKSNavGraph::FindPath(int32 From, int32 To, TArray<int32>& OutPath) const
{
	OutPath.Reset();
	if (!Nodes.IsValidIndex(From) || !Nodes.IsValidIndex(To))
	{
		return false;
	}
	const int32 N = Nodes.Num();
	TArray<float> Dist;
	TArray<int32> Prev;
	TArray<bool> Done;
	Dist.Init(TNumericLimits<float>::Max(), N);
	Prev.Init(INDEX_NONE, N);
	Done.Init(false, N);
	Dist[From] = 0.f;

	for (int32 Iter = 0; Iter < N; ++Iter)
	{
		int32 U = INDEX_NONE;
		float Best = TNumericLimits<float>::Max();
		for (int32 i = 0; i < N; ++i)
		{
			if (!Done[i] && Dist[i] < Best)
			{
				Best = Dist[i];
				U = i;
			}
		}
		if (U == INDEX_NONE || U == To)
		{
			break;
		}
		Done[U] = true;
		for (const int32 V : Links[U])
		{
			const float Alt = Dist[U] + FVector::Dist(Nodes[U], Nodes[V]);
			if (Alt < Dist[V])
			{
				Dist[V] = Alt;
				Prev[V] = U;
			}
		}
	}

	if (Dist[To] == TNumericLimits<float>::Max())
	{
		return false;
	}
	for (int32 At = To; At != INDEX_NONE; At = Prev[At])
	{
		OutPath.Insert(At, 0);
	}
	return OutPath.Num() > 0;
}
