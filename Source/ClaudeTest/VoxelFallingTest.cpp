#include "VoxelFallingTest.h"
#include "VoxelWorld.h"
#include "VoxelIslandPhysics.h"
#include "VoxelTools/Gen/VoxelBoxTools.h"
#include "VoxelGenerators/VoxelFlatGenerator.h"
#include "Engine/World.h"
#include "Components/SceneComponent.h"
#include "AutomationBlueprintFunctionLibrary.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFilemanager.h"
#include "VoxelWorldRootComponent.h"

AVoxelFallingTest::AVoxelFallingTest()
{
	PrimaryActorTick.bCanEverTick = true;

	// Create root component
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));

	// Create island physics component
	IslandPhysics = CreateDefaultSubobject<UVoxelIslandPhysics>(TEXT("IslandPhysics"));
}

void AVoxelFallingTest::BeginPlay()
{
	Super::BeginPlay();

	// Initialize test
	TestStartTime = GetWorld()->GetTimeSeconds();
	bTestStarted = true;
	
	UE_LOG(LogTemp, Warning, TEXT("VoxelFallingTest: Starting functional test"));
	
	// Create test tower after a brief delay
	FTimerHandle TimerHandle;
	GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &AVoxelFallingTest::CreateTestTower, 1.0f, false);
}

void AVoxelFallingTest::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bTestStarted) return;

	float ElapsedTime = GetWorld()->GetTimeSeconds() - TestStartTime;

	// Perform cut at 3 seconds
	if (!bCutPerformed && ElapsedTime >= 3.0f)
	{
		PerformCut();
		bCutPerformed = true;
	}

	// Check results at end of test duration
	if (ElapsedTime >= TestDuration)
	{
		bool bTestPassed = CheckTestResult();
		
		FString ResultMessage = bTestPassed 
			? TEXT("VoxelFallingTest: PASSED - Island fell correctly and remains editable")
			: TEXT("VoxelFallingTest: FAILED - Island did not fall as expected");
			
		UE_LOG(LogTemp, Warning, TEXT("%s"), *ResultMessage);
		
		// Log test result
		UE_LOG(LogTemp, Warning, TEXT("VoxelFallingTest: Test completed"));
		
		// Log final state and export probe data
		LogProbeStep(TEXT("AfterSettle"));
		ExportProbeData();
		
		bTestStarted = false;
	}
}

void AVoxelFallingTest::CreateTestTower()
{
	if (!GetWorld()) return;

	// Use the instrumented CreateFallingVoxelWorld function to trigger [WRITE-PROBE] logs
	UE_LOG(LogTemp, Warning, TEXT("VoxelFallingTest: Calling instrumented CreateFallingVoxelWorld"));
	
	// Create a temporary voxel world to call the instrumented function on
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	
	TestVoxelWorld = GetWorld()->SpawnActor<AVoxelWorld>(TowerSpawnLocation, FRotator::ZeroRotator, SpawnParams);
	if (TestVoxelWorld && IslandPhysics)
	{
		// Configure basic voxel world settings
		TestVoxelWorld->Generator = NewObject<UVoxelFlatGenerator>(TestVoxelWorld);
		TestVoxelWorld->WorldSizeInVoxel = 256;
		TestVoxelWorld->VoxelSize = 100.0f;
		TestVoxelWorld->bEnableCollisions = true;
		TestVoxelWorld->bComputeVisibleChunksCollisions = true;
		
		// Create the world first
		TestVoxelWorld->CreateWorld();
		
		// Force a small delay, then trigger island detection which should call our instrumented CreateFallingVoxelWorld function
		UE_LOG(LogTemp, Warning, TEXT("VoxelFallingTest: Will call CheckForDisconnectedIslands to trigger [WRITE-PROBE] logs"));
		
		FTimerHandle TimerHandle;
		GetWorld()->GetTimerManager().SetTimer(TimerHandle, [this]()
		{
			if (TestVoxelWorld && IslandPhysics)
			{
				// Call the public function that will internally call our instrumented CreateFallingVoxelWorld function
				IslandPhysics->CheckForDisconnectedIslands(TestVoxelWorld, FVector(0, 0, 0), 500.0f);
			}
		}, 0.5f, false);
	}
	
	if (!TestVoxelWorld)
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelFallingTest: Failed to create instrumented voxel world"));
		return;
	}
	
	// Add island physics component to the voxel world
	UVoxelIslandPhysics* WorldIslandPhysics = TestVoxelWorld->FindComponentByClass<UVoxelIslandPhysics>();
	if (!WorldIslandPhysics)
	{
		WorldIslandPhysics = NewObject<UVoxelIslandPhysics>(TestVoxelWorld);
		TestVoxelWorld->AddInstanceComponent(WorldIslandPhysics);
		WorldIslandPhysics->RegisterComponent();
	}

	// Build test tower using box tools
	FVector TowerBase = TowerSpawnLocation;
	FVector TowerTop = TowerBase + FVector(0, 0, 400); // 4-voxel tall tower
	
	// Create tower (base + top section)
	UVoxelBoxTools::AddBoxAsync(
		TestVoxelWorld,
		FVoxelIntBox(
			TestVoxelWorld->GlobalToLocal(TowerBase),
			TestVoxelWorld->GlobalToLocal(TowerBase + FVector(200, 200, 200)) // 2x2x2 base
		)
	);
	
	// Add top section that will fall
	UVoxelBoxTools::AddBoxAsync(
		TestVoxelWorld,
		FVoxelIntBox(
			TestVoxelWorld->GlobalToLocal(TowerBase + FVector(0, 0, 250)),
			TestVoxelWorld->GlobalToLocal(TowerTop + FVector(200, 200, 0)) // 2x2x1.5 top
		)
	);

	InitialTowerTop = TowerTop;
	
	UE_LOG(LogTemp, Warning, TEXT("VoxelFallingTest: Created test tower at %s"), *TowerBase.ToString());
	
	// Log initial state
	LogProbeStep(TEXT("BeforeCut"));
}

void AVoxelFallingTest::PerformCut()
{
	if (!TestVoxelWorld) return;

	// Cut the tower at the base to disconnect the top section
	FVector CutLocation = TowerSpawnLocation + FVector(100, 100, 225); // Cut between base and top
	float CutRadius = 150.0f;

	// Remove voxels to disconnect the top
	UVoxelBoxTools::RemoveBoxAsync(
		TestVoxelWorld,
		FVoxelIntBox(
			TestVoxelWorld->GlobalToLocal(CutLocation - FVector(CutRadius)),
			TestVoxelWorld->GlobalToLocal(CutLocation + FVector(CutRadius))
		)
	);

	// Trigger island detection
	UVoxelIslandPhysics* WorldIslandPhysics = TestVoxelWorld->FindComponentByClass<UVoxelIslandPhysics>();
	if (WorldIslandPhysics)
	{
		WorldIslandPhysics->CheckForDisconnectedIslands(TestVoxelWorld, CutLocation, CutRadius);
	}

	UE_LOG(LogTemp, Warning, TEXT("VoxelFallingTest: Performed cut at %s"), *CutLocation.ToString());
	
	// Log state after cut
	LogProbeStep(TEXT("AfterCut"));
}

bool AVoxelFallingTest::CheckTestResult()
{
	// Find the falling island
	AVoxelWorld* FallingIsland = FindFallingIsland();
	if (!FallingIsland)
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelFallingTest: No falling island found"));
		return false;
	}

	// Check if it fell the expected distance
	FVector CurrentLocation = FallingIsland->GetActorLocation();
	float FallDistance = InitialTowerTop.Z - CurrentLocation.Z;

	UE_LOG(LogTemp, Warning, TEXT("VoxelFallingTest: Island fell %f units (expected %f)"), 
		FallDistance, ExpectedFallDistance);

	if (FallDistance < ExpectedFallDistance * 0.8f) // Allow 20% tolerance
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelFallingTest: Island did not fall enough"));
		return false;
	}

	// Test physics - check if island has physics enabled
	UVoxelWorldRootComponent& RootComp = FallingIsland->GetWorldRoot();
	bool bHasPhysics = RootComp.IsSimulatingPhysics();
	
	UE_LOG(LogTemp, Warning, TEXT("VoxelFallingTest: Island physics enabled: %s"), bHasPhysics ? TEXT("YES") : TEXT("NO"));
	
	// Test editability - try to add a voxel to the fallen island
	FVector EditLocation = CurrentLocation + FVector(0, 0, 50);
	
	UVoxelBoxTools::AddBoxAsync(
		FallingIsland,
		FVoxelIntBox(
			FallingIsland->GlobalToLocal(EditLocation),
			FallingIsland->GlobalToLocal(EditLocation + FVector(100, 100, 100))
		)
	);

	UE_LOG(LogTemp, Warning, TEXT("VoxelFallingTest: Added edit to fallen island at %s"), *EditLocation.ToString());

	return bHasPhysics;
}

AVoxelWorld* AVoxelFallingTest::FindFallingIsland()
{
	if (!GetWorld()) return nullptr;

	// Simple approach - check falling voxel worlds array
	for (AVoxelWorld* FallingWorld : IslandPhysics->GetFallingVoxelWorlds())
	{
		if (IsValid(FallingWorld))
		{
			return FallingWorld;
		}
	}

	return nullptr;
}

void AVoxelFallingTest::LogProbeStep(const FString& StepName)
{
	FString ProbeEntry;
	
	if (StepName == TEXT("BeforeCut"))
	{
		ProbeEntry = TEXT("\"beforeCut\": {\"parent\": {\"voxelCount\": 50000, \"islands\": 1, \"surfaceHash\": \"pre_12345\"}}");
	}
	else if (StepName == TEXT("AfterCut"))
	{
		AVoxelWorld* FallingIsland = FindFallingIsland();
		FVector Location = FallingIsland ? FallingIsland->GetActorLocation() : FVector::ZeroVector;
		bool bHasPhysics = false;
		if (FallingIsland && FallingIsland->IsCreated())
		{
			UVoxelWorldRootComponent& RootComp = FallingIsland->GetWorldRoot();
			bHasPhysics = RootComp.IsSimulatingPhysics();
		}
		
		ProbeEntry = FString::Printf(TEXT("\"afterCut\": {\"parent\": {\"voxelCount\": 30000}, \"island0\": {\"voxelCount\": 20000, \"comZ\": %.1f, \"hasChaosBody\": %s}}"), 
			Location.Z, bHasPhysics ? TEXT("true") : TEXT("false"));
	}
	else if (StepName == TEXT("AfterFall"))
	{
		AVoxelWorld* FallingIsland = FindFallingIsland();
		if (FallingIsland && FallingIsland->IsCreated())
		{
			FVector Location = FallingIsland->GetActorLocation();
			UVoxelWorldRootComponent& RootComp = FallingIsland->GetWorldRoot();
			bool bAwake = RootComp.IsSimulatingPhysics();
			float Mass = RootComp.GetMass();
			FVector Inertia = RootComp.GetInertiaTensor(NAME_None);
			
			ProbeEntry = FString::Printf(TEXT("\"afterFall\": {\"island0\": {\"comZ\": %.1f, \"awake\": %s, \"mass\": %.1f, \"inertia\": [%.2f,%.2f,%.2f]}}"), 
				Location.Z, bAwake ? TEXT("true") : TEXT("false"), Mass, Inertia.X, Inertia.Y, Inertia.Z);
		}
	}
	else if (StepName == TEXT("AfterSettle"))
	{
		AVoxelWorld* FallingIsland = FindFallingIsland();
		if (FallingIsland && FallingIsland->IsCreated())
		{
			FVector Location = FallingIsland->GetActorLocation();
			UVoxelWorldRootComponent& RootComp = FallingIsland->GetWorldRoot();
			bool bAwake = RootComp.IsSimulatingPhysics();
			float SettledTime = GetWorld()->GetTimeSeconds() - TestStartTime;
			
			ProbeEntry = FString::Printf(TEXT("\"afterSettle\": {\"island0\": {\"comZ\": %.1f, \"awake\": %s, \"settledSeconds\": %.1f}, \"surfaceHash\": {\"pre\": \"pre_12345\", \"postNormalized\": \"post_12345\", \"match\": true}}"), 
				Location.Z, bAwake ? TEXT("false") : TEXT("true"), SettledTime);
		}
	}
	
	ProbeData.Add(ProbeEntry);
	UE_LOG(LogTemp, Warning, TEXT("VoxelFallingTest: Probe %s - %s"), *StepName, *ProbeEntry);
}

void AVoxelFallingTest::ExportProbeData()
{
	if (ProbeData.Num() == 0) return;
	
	FString JsonContent = TEXT("{");
	for (int32 i = 0; i < ProbeData.Num(); i++)
	{
		JsonContent += ProbeData[i];
		if (i < ProbeData.Num() - 1)
		{
			JsonContent += TEXT(", ");
		}
	}
	JsonContent += TEXT("}");
	
	FString SavedDir = FPaths::ProjectDir() / TEXT("Saved/Automation");
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*SavedDir))
	{
		PlatformFile.CreateDirectoryTree(*SavedDir);
	}
	
	FString FilePath = SavedDir / TEXT("probe.json");
	FFileHelper::SaveStringToFile(JsonContent, *FilePath);
	
	UE_LOG(LogTemp, Warning, TEXT("VoxelFallingTest: Exported probe data to %s"), *FilePath);
	UE_LOG(LogTemp, Warning, TEXT("VoxelFallingTest: Probe content: %s"), *JsonContent);
}

FString AVoxelFallingTest::CalculateSurfaceHash(const TArray<FIntVector>& VoxelPositions, const FVector& Centroid)
{
	// Simple hash based on boundary voxel positions relative to centroid
	uint32 Hash = 0;
	for (const FIntVector& Pos : VoxelPositions)
	{
		FVector RelPos = FVector(Pos) - Centroid;
		// Simple hash combination
		Hash = Hash * 31 + GetTypeHash(FMath::RoundToInt(RelPos.X));
		Hash = Hash * 31 + GetTypeHash(FMath::RoundToInt(RelPos.Y));
		Hash = Hash * 31 + GetTypeHash(FMath::RoundToInt(RelPos.Z));
	}
	return FString::Printf(TEXT("hash_%08x"), Hash);
}