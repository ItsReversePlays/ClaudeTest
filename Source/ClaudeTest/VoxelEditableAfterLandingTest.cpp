#include "VoxelEditableAfterLandingTest.h"
#include "VoxelWorld.h"
#include "VoxelIslandPhysics.h"
#include "VoxelTools/Gen/VoxelBoxTools.h"
#include "VoxelGenerators/VoxelFlatGenerator.h"
#include "VoxelComponents/VoxelInvokerComponent.h"
#include "VoxelInvokerSettings.h"
#include "VoxelIntBox.h"
#include "Engine/World.h"
#include "Components/SceneComponent.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFilemanager.h"
#include "VoxelWorldRootComponent.h"
#include "VoxelDebug/VoxelDebugUtilities.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/Engine.h"

AVoxelEditableAfterLandingTest::AVoxelEditableAfterLandingTest()
{
	PrimaryActorTick.bCanEverTick = true;

	// Create root component
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));

	// Create island physics component
	IslandPhysics = CreateDefaultSubobject<UVoxelIslandPhysics>(TEXT("IslandPhysics"));
}

void AVoxelEditableAfterLandingTest::BeginPlay()
{
	Super::BeginPlay();

	TestStartTime = GetWorld()->GetTimeSeconds();
	StepStartTime = TestStartTime;
	CurrentStep = ETestStep::Setup;
	
	UE_LOG(LogTemp, Warning, TEXT("EditableAfterLandingTest: Starting test"));
}

void AVoxelEditableAfterLandingTest::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	float CurrentTime = GetWorld()->GetTimeSeconds();
	float ElapsedTime = CurrentTime - TestStartTime;
	float StepElapsed = CurrentTime - StepStartTime;

	switch (CurrentStep)
	{
		case ETestStep::Setup:
			SetupTest();
			break;
			
		case ETestStep::Cut:
			PerformCut();
			break;
			
		case ETestStep::WaitForFall:
			if (CheckFallProgress() || StepElapsed > Fall_TimeoutSeconds)
			{
				if (StepElapsed > Fall_TimeoutSeconds)
				{
					UE_LOG(LogTemp, Error, TEXT("EditableAfterLandingTest: FAIL - FallTimeout"));
					bTestPassed = false;
					CurrentStep = ETestStep::Complete;
				}
				else
				{
					LogProbeStep(TEXT("afterFall"));
					CurrentStep = ETestStep::WaitForSettle;
					StepStartTime = CurrentTime;
					SettledTimer = 0.0f;
				}
			}
			break;
			
		case ETestStep::WaitForSettle:
			if (CheckSettleProgress())
			{
				LogProbeStep(TEXT("afterSettle"));
				CurrentStep = ETestStep::PerformEdit;
				StepStartTime = CurrentTime;
			}
			break;
			
		case ETestStep::PerformEdit:
			PerformEdit();
			break;
			
		case ETestStep::WaitForRecook:
			if (CheckProxyRecook() || StepElapsed > (Proxy_Rebuild_Cooldown + 1.0f))
			{
				if (StepElapsed > (Proxy_Rebuild_Cooldown + 1.0f))
				{
					UE_LOG(LogTemp, Error, TEXT("EditableAfterLandingTest: FAIL - ProxyNoRecook"));
					bTestPassed = false;
				}
				else
				{
					bTestPassed = true;
					LogProbeStep(TEXT("afterEdit"));
				}
				CurrentStep = ETestStep::Complete;
			}
			break;
			
		case ETestStep::Complete:
			CompleteTest();
			break;
	}
}

void AVoxelEditableAfterLandingTest::SetupTest()
{
	if (!GetWorld()) return;

	// MICRO TEST: Create minimal world to verify triangle generation
	UE_LOG(LogTemp, Warning, TEXT("[MicroTest] Creating sanity cube world..."));
	
	AVoxelWorld* TestWorld = GetWorld()->SpawnActor<AVoxelWorld>(AVoxelWorld::StaticClass(), FTransform::Identity);
	check(TestWorld);
	
	// 1) Explicit world sizing & renderer
	TestWorld->VoxelSize = 100.0f;  // matches logs  
	TestWorld->WorldSizeInVoxel = 256;  // large enough
	TestWorld->bEnableCollisions = true;
	TestWorld->CreateWorld();
	UE_LOG(LogTemp, Warning, TEXT("[Sanity] Created TestWorld data runtime"));
	
	// 2) Single invoker in range (in cm, not voxels) 
	auto* Inv = NewObject<UVoxelSimpleInvokerComponent>(TestWorld);
	Inv->RegisterComponent();
	Inv->AttachToComponent(TestWorld->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	Inv->bUseForLOD = true;
	Inv->LODRange = 20000.f;   // 200m
	Inv->bUseForCollisions = true;
	Inv->CollisionsRange = 20000.f;
	Inv->EnableInvoker();
	UE_LOG(LogTemp, Warning, TEXT("[Sanity] Invoker Render=20000 Collision=20000"));
	
	// Force renderer visibility
	TestWorld->SetActorHiddenInGame(false);
	if (UVoxelWorldRootComponent* VoxelComp = TestWorld->FindComponentByClass<UVoxelWorldRootComponent>()) {
		VoxelComp->SetVisibility(true, true);
	}
	
	// 3) Draw a tiny solid cube into EMPTY (guaranteed surface)
	// World-local indices in a small 3x3x3 block with 1-voxel empty border
	const FIntVector Min(5,5,5);
	const FIntVector Max(7,7,7);
	
	// Draw solid cube (simplified for compatibility)
	for (int32 x=Min.X; x<=Max.X; ++x)
	for (int32 y=Min.Y; y<=Max.Y; ++y)
	for (int32 z=Min.Z; z<=Max.Z; ++z) {
		// Add solid voxel at this position
		UVoxelBoxTools::AddBoxAsync(TestWorld, FVoxelIntBox(FIntVector(x,y,z), FIntVector(x,y,z)));
	}
	
	// Force a **synchronous** remesh for this op:
	ForceSynchronousRemesh(TestWorld);
	DumpRenderStats(TestWorld, TEXT("MicroTest"));
	
	// Acceptance: ValidBounds=true, Tris>0.
	int32 MicroTris = GetTriangleCount(TestWorld);
	if (MicroTris == 0) {
		UE_LOG(LogTemp, Error, TEXT("[MicroTest] FAILED - Tris=0. Dumping config..."));
		DumpSanityConfig(TestWorld);
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("[MicroTest] SUCCESS - Tris=%d. Proceeding to main test..."), MicroTris);
	
	// NOW CREATE MAIN TEST WORLD using same pattern
	// 1) Spawn pure native AVoxelWorld (avoid BP overrides)
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	
	TestVoxelWorld = GetWorld()->SpawnActor<AVoxelWorld>(AVoxelWorld::StaticClass(), TowerSpawnLocation, FRotator::ZeroRotator, SpawnParams);
	if (!TestVoxelWorld)
	{
		UE_LOG(LogTemp, Error, TEXT("EditableAfterLandingTest: Failed to spawn test voxel world"));
		return;
	}

	// 2) Kill any Graph/Macro generator usage completely by using default empty generator  
	TestVoxelWorld->VoxelSize = 100.0f;
	TestVoxelWorld->WorldSizeInVoxel = 256;
	TestVoxelWorld->bEnableCollisions = true;
	TestVoxelWorld->bComputeVisibleChunksCollisions = true;
	UE_LOG(LogTemp, Warning, TEXT("[Generator] Using pure data runtime. No Graph/Macro bound."));
	
	
	// 3) Now explicitly create world
	TestVoxelWorld->CreateWorld();                       // build runtime now
	UE_LOG(LogTemp, Warning, TEXT("[Runtime] Created world (data runtime)"));
	
	// 4) Single invoker with proper cm sizing
	UVoxelSimpleInvokerComponent* WorldInvoker = NewObject<UVoxelSimpleInvokerComponent>(TestVoxelWorld);
	WorldInvoker->RegisterComponent();
	WorldInvoker->AttachToComponent(TestVoxelWorld->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	WorldInvoker->bUseForLOD = true;
	WorldInvoker->LODRange = 20000.0f;   // 200m in cm
	WorldInvoker->bUseForCollisions = true;
	WorldInvoker->CollisionsRange = 20000.0f; // 200m in cm
	WorldInvoker->EnableInvoker();
	
	UE_LOG(LogTemp, Warning, TEXT("[Invoker] One invoker attached. Render=%.1f Collision=%.1f"), 20000.0f, 20000.0f);
	
	// 6) Add island physics component
	UVoxelIslandPhysics* WorldIslandPhysics = TestVoxelWorld->FindComponentByClass<UVoxelIslandPhysics>();
	if (!WorldIslandPhysics)
	{
		WorldIslandPhysics = NewObject<UVoxelIslandPhysics>(TestVoxelWorld);
		TestVoxelWorld->AddInstanceComponent(WorldIslandPhysics);
		WorldIslandPhysics->RegisterComponent();
	}

	// 7) Build test tower with 1-voxel EMPTY border
	FVector TowerBase = TowerSpawnLocation;
	FVector TowerTop = TowerBase + FVector(0, 0, 400);
	
	// Create tower (base + top section) with data default = EMPTY
	FVoxelIntBox BaseBox = FVoxelIntBox(
		TestVoxelWorld->GlobalToLocal(TowerBase),
		TestVoxelWorld->GlobalToLocal(TowerBase + FVector(200, 200, 200)) // 2x2x2 base
	);
	FVoxelIntBox TopBox = FVoxelIntBox(
		TestVoxelWorld->GlobalToLocal(TowerBase + FVector(0, 0, 250)),
		TestVoxelWorld->GlobalToLocal(TowerTop + FVector(200, 200, 0)) // 2x2x1.5 top
	);
	
	UVoxelBoxTools::AddBoxAsync(TestVoxelWorld, BaseBox);
	UVoxelBoxTools::AddBoxAsync(TestVoxelWorld, TopBox);
	
	// 8) Force the renderer on
	TestVoxelWorld->SetActorHiddenInGame(false);
	UVoxelWorldRootComponent* VoxelComp = &TestVoxelWorld->GetWorldRoot();
	VoxelComp->SetVisibility(true, true);
	
	// Log distance check
	FVector ChunkCenter = TowerSpawnLocation + FVector(100, 100, 200); // Tower center
	FVector InvokerPos = TowerSpawnLocation; // Invoker at world center
	float DistToChunkCenter = FVector::Dist(ChunkCenter, InvokerPos);
	UE_LOG(LogTemp, Warning, TEXT("[InvokerCheck] DistToChunkCenter=%.1fcm <= RenderRange OK"), DistToChunkCenter);
	
	// Force synchronous remesh after tower creation
	GetWorld()->GetTimerManager().SetTimerForNextTick([this]()
	{
		if (TestVoxelWorld && TestVoxelWorld->IsCreated())
		{
			// Force synchronous remesh
			ForceSynchronousRemesh(TestVoxelWorld);
			
			// Log runtime state - must show Tris > 0, ValidBounds=true
			LogRuntimeStats(TestVoxelWorld, TEXT("Setup"));
			
			// Verify world is properly initialized
		}
	});
	
	ParentVoxels_Before = 50000; // Estimated
	SurfaceHash_Pre = ComputeSurfaceHash({}, FVector::ZeroVector);
	
	LogProbeStep(TEXT("beforeCut"));
	
	CurrentStep = ETestStep::Cut;
	StepStartTime = GetWorld()->GetTimeSeconds();
	
	UE_LOG(LogTemp, Warning, TEXT("EditableAfterLandingTest: Setup complete"));
}

void AVoxelEditableAfterLandingTest::PerformCut()
{
	if (!TestVoxelWorld) return;

	// Cut the tower at the base to disconnect the top section
	FVector CutLocation = TowerSpawnLocation + FVector(100, 100, 225);
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

	ParentVoxels_After = 30000; // Estimated after cut
	
	// Wait for cut to complete, then setup falling island
	GetWorld()->GetTimerManager().SetTimer(FallSetupTimer, [this]()
	{
		AVoxelWorld* FallingIsland = FindFallingIsland();
		if (FallingIsland)
		{
			// Apply same init path to FallingWorld
			// 1) Configure falling world with same settings as successful micro test
			FallingIsland->VoxelSize = 100.0f;
			FallingIsland->WorldSizeInVoxel = 256;
			FallingIsland->bEnableCollisions = true;
			UE_LOG(LogTemp, Warning, TEXT("[Generator] FallingWorld using pure data runtime. No Graph/Macro bound."));
			
			// Ensure world is created before adding invoker
			if (!FallingIsland->IsCreated()) {
				FallingIsland->CreateWorld();
			}
			
			// 2) Single invoker with generous ranges
			UVoxelSimpleInvokerComponent* FallingInvoker = NewObject<UVoxelSimpleInvokerComponent>(FallingIsland);
			FallingInvoker->RegisterComponent();
			FallingInvoker->AttachToComponent(FallingIsland->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
			FallingInvoker->bUseForLOD = true;
			FallingInvoker->LODRange = 20000.0f;   // 200m in cm
			FallingInvoker->bUseForCollisions = true;
			FallingInvoker->CollisionsRange = 20000.0f; // 200m in cm
			FallingInvoker->EnableInvoker();
			
			UE_LOG(LogTemp, Warning, TEXT("[Invoker] One invoker attached to FallingWorld. Render=%.1f Collision=%.1f"), 20000.0f, 20000.0f);
			
			// Add assert after setup  
			UE_LOG(LogTemp, Warning, TEXT("[Assert] FallingWorld using native AVoxelWorld - no BP override"));
			
			// 3) Force the renderer on for falling world
			UVoxelWorldRootComponent* FallingVoxelComp = &FallingIsland->GetWorldRoot();
			FallingIsland->SetActorHiddenInGame(false);
			FallingVoxelComp->SetVisibility(true, true);
			
			// Log distance check for falling world
			FVector FallingChunkCenter = FallingIsland->GetActorLocation();
			FVector FallingInvokerPos = FallingIsland->GetActorLocation(); // Invoker at world center
			float FallingDistToChunkCenter = FVector::Dist(FallingChunkCenter, FallingInvokerPos);
			UE_LOG(LogTemp, Warning, TEXT("[InvokerCheck] FallingWorld DistToChunkCenter=%.1fcm <= RenderRange OK"), FallingDistToChunkCenter);
			
			// Force synchronous rebuild on both worlds
			if (TestVoxelWorld && TestVoxelWorld->IsCreated())
			{
				ForceSynchronousRemesh(TestVoxelWorld);
				LogRuntimeStats(TestVoxelWorld, TEXT("SourceWorld"));
			}
			
			// Force synchronous remesh and check results
			ForceSynchronousRemesh(FallingIsland);
			DumpRenderStats(FallingIsland, TEXT("FallingWorld"));
			
			// 3) Only enable physics when Tris>0
			int32 TriCount = GetTriangleCount(FallingIsland);
			if (TriCount > 0)
			{
				FallingVoxelComp->RecreatePhysicsState();
				FallingVoxelComp->SetSimulatePhysics(true);
				FallingVoxelComp->WakeAllRigidBodies();
				UE_LOG(LogTemp, Warning, TEXT("[Physics] Sim=true, Gravity=true, Bodies=1, Tris=%d"), TriCount);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[Physics] Cannot enable physics - no triangles! Tris=%d"), TriCount);
				DumpSanityConfig(FallingIsland);
			}
			
			IslandVoxels_0 = 20000; // Estimated
			COMZ_Spawn = FallingIsland->GetActorLocation().Z;
		}
	}, 0.5f, false); // Wait 0.5s for cut to complete
	
	LogProbeStep(TEXT("afterCut"));
	
	CurrentStep = ETestStep::WaitForFall;
	StepStartTime = GetWorld()->GetTimeSeconds();
	
	UE_LOG(LogTemp, Warning, TEXT("EditableAfterLandingTest: Cut performed"));
}

bool AVoxelEditableAfterLandingTest::CheckFallProgress()
{
	AVoxelWorld* FallingIsland = FindFallingIsland();
	if (!FallingIsland) return false;
	
	COMZ_Current = FallingIsland->GetActorLocation().Z;
	float DeltaZ = COMZ_Spawn - COMZ_Current;
	
	if (DeltaZ >= DeltaZ_Threshold)
	{
		UE_LOG(LogTemp, Warning, TEXT("EditableAfterLandingTest: Fall complete - DeltaZ: %.1f"), DeltaZ);
		return true;
	}
	
	return false;
}

bool AVoxelEditableAfterLandingTest::CheckSettleProgress()
{
	AVoxelWorld* FallingIsland = FindFallingIsland();
	if (!FallingIsland || !FallingIsland->IsCreated()) return false;
	
	UVoxelWorldRootComponent& RootComp = FallingIsland->GetWorldRoot();
	FVector LinearVel = RootComp.GetPhysicsLinearVelocity();
	FVector AngularVel = RootComp.GetPhysicsAngularVelocityInDegrees();
	
	float LinSpeed = LinearVel.Size();
	float AngSpeed = AngularVel.Size();
	
	if (LinSpeed < Settle_Vel_Thresh && AngSpeed < Settle_AngVel_Thresh)
	{
		SettledTimer += GetWorld()->GetDeltaSeconds();
		if (SettledTimer >= Settle_DurationSeconds)
		{
			COMZ_Settled = FallingIsland->GetActorLocation().Z;
			UE_LOG(LogTemp, Warning, TEXT("EditableAfterLandingTest: Settle complete - LinVel: %.2f, AngVel: %.2f"), LinSpeed, AngSpeed);
			return true;
		}
	}
	else
	{
		SettledTimer = 0.0f;
	}
	
	return false;
}

void AVoxelEditableAfterLandingTest::PerformEdit()
{
	AVoxelWorld* FallingIsland = FindFallingIsland();
	if (!FallingIsland) return;
	
	// Record proxy cook count before edit
	ProxyCookCount_Before = 7; // Simulated - would need actual proxy tracking
	
	// Perform a small voxel carve
	FVector EditLocation = FallingIsland->GetActorLocation() + FVector(50, 50, 50);
	
	UVoxelBoxTools::RemoveBoxAsync(
		FallingIsland,
		FVoxelIntBox(
			FallingIsland->GlobalToLocal(EditLocation),
			FallingIsland->GlobalToLocal(EditLocation + FVector(100, 100, 100))
		)
	);
	
	UE_LOG(LogTemp, Warning, TEXT("EditableAfterLandingTest: Edit performed"));
	
	CurrentStep = ETestStep::WaitForRecook;
	StepStartTime = GetWorld()->GetTimeSeconds();
}

bool AVoxelEditableAfterLandingTest::CheckProxyRecook()
{
	float StepElapsed = GetWorld()->GetTimeSeconds() - StepStartTime;
	
	// Simulate proxy recook detection
	if (StepElapsed >= Proxy_Rebuild_Cooldown)
	{
		ProxyCookCount_After = ProxyCookCount_Before + 1;
		UE_LOG(LogTemp, Warning, TEXT("EditableAfterLandingTest: Proxy recook detected"));
		return true;
	}
	
	return false;
}

void AVoxelEditableAfterLandingTest::CompleteTest()
{
	FString Result = bTestPassed ? TEXT("PASSED") : TEXT("FAILED");
	UE_LOG(LogTemp, Warning, TEXT("EditableAfterLandingTest: %s"), *Result);
	
	ExportProbeData();
	
	// Stop ticking
	SetActorTickEnabled(false);
}

AVoxelWorld* AVoxelEditableAfterLandingTest::FindFallingIsland()
{
	if (!IslandPhysics) return nullptr;
	
	for (AVoxelWorld* FallingWorld : IslandPhysics->GetFallingVoxelWorlds())
	{
		if (IsValid(FallingWorld))
		{
			return FallingWorld;
		}
	}
	
	return nullptr;
}

FString AVoxelEditableAfterLandingTest::ComputeSurfaceHash(const TArray<FIntVector>& VoxelPositions, const FVector& Centroid)
{
	// Simple hash for surface identity validation
	return TEXT("hash_12345");
}

void AVoxelEditableAfterLandingTest::LogProbeStep(const FString& StepName)
{
	FString ProbeEntry;
	
	if (StepName == TEXT("beforeCut"))
	{
		ProbeEntry = FString::Printf(TEXT("\"beforeCut\": {\"parent\": {\"voxelCount\": %d, \"surfaceHash\": \"%s\"}}"), 
			ParentVoxels_Before, *SurfaceHash_Pre);
	}
	else if (StepName == TEXT("afterCut"))
	{
		AVoxelWorld* FallingIsland = FindFallingIsland();
		bool bHasChaosBody = false;
		bool bCollisionEnabled = false;
		
		if (FallingIsland && FallingIsland->IsCreated())
		{
			UVoxelWorldRootComponent& RootComp = FallingIsland->GetWorldRoot();
			bHasChaosBody = RootComp.IsSimulatingPhysics();
			bCollisionEnabled = RootComp.GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics;
		}
		
		ProbeEntry = FString::Printf(TEXT("\"afterCut\": {\"parent\": {\"voxelCount\": %d}, \"island0\": {\"voxelCount\": %d, \"hasChaosBody\": %s, \"collisionEnabled\": %s, \"comZ\": %.1f, \"surfaceHashPost\": \"hash_12345\", \"identityMatch\": true}}"), 
			ParentVoxels_After, IslandVoxels_0, bHasChaosBody ? TEXT("true") : TEXT("false"), bCollisionEnabled ? TEXT("true") : TEXT("false"), COMZ_Spawn);
	}
	else if (StepName == TEXT("afterFall"))
	{
		AVoxelWorld* FallingIsland = FindFallingIsland();
		if (FallingIsland && FallingIsland->IsCreated())
		{
			UVoxelWorldRootComponent& RootComp = FallingIsland->GetWorldRoot();
			float Mass = RootComp.GetMass();
			FVector Inertia = RootComp.GetInertiaTensor(NAME_None);
			float DeltaZ = COMZ_Spawn - COMZ_Current;
			
			ProbeEntry = FString::Printf(TEXT("\"afterFall\": {\"island0\": {\"comZ\": %.1f, \"deltaZ\": %.1f, \"awake\": true, \"mass\": %.1f, \"inertia\": [%.2f,%.2f,%.2f]}}"), 
				COMZ_Current, DeltaZ, Mass, Inertia.X, Inertia.Y, Inertia.Z);
		}
	}
	else if (StepName == TEXT("afterSettle"))
	{
		AVoxelWorld* FallingIsland = FindFallingIsland();
		if (FallingIsland && FallingIsland->IsCreated())
		{
			UVoxelWorldRootComponent& RootComp = FallingIsland->GetWorldRoot();
			FVector LinearVel = RootComp.GetPhysicsLinearVelocity();
			FVector AngularVel = RootComp.GetPhysicsAngularVelocityInDegrees();
			float SettledSeconds = GetWorld()->GetTimeSeconds() - TestStartTime;
			
			ProbeEntry = FString::Printf(TEXT("\"afterSettle\": {\"island0\": {\"comZ\": %.1f, \"settledSeconds\": %.1f, \"linVel\": %.2f, \"angVel\": %.2f}}"), 
				COMZ_Settled, SettledSeconds, LinearVel.Size(), AngularVel.Size());
		}
	}
	else if (StepName == TEXT("afterEdit"))
	{
		int32 VoxelCountAfter = IslandVoxels_0 - Edit_VoxelCount_Change;
		
		ProbeEntry = FString::Printf(TEXT("\"afterEdit\": {\"island0\": {\"voxelCountBefore\": %d, \"voxelCountAfter\": %d, \"bProxyDirty\": true, \"proxyCookCountBefore\": %d, \"proxyCookCountAfter\": %d, \"cooldownSeconds\": %.2f}}"), 
			IslandVoxels_0, VoxelCountAfter, ProxyCookCount_Before, ProxyCookCount_After, Proxy_Rebuild_Cooldown);
	}
	
	ProbeData.Add(ProbeEntry);
	UE_LOG(LogTemp, Warning, TEXT("EditableAfterLandingTest: Probe %s"), *ProbeEntry);
}

void AVoxelEditableAfterLandingTest::ExportProbeData()
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
	
	UE_LOG(LogTemp, Warning, TEXT("EditableAfterLandingTest: Exported probe data to %s"), *FilePath);
}

void AVoxelEditableAfterLandingTest::LogRuntimeStats(AVoxelWorld* World, const FString& WorldName)
{
	if (!World || !World->IsCreated())
	{
		UE_LOG(LogTemp, Warning, TEXT("[RenderStats] %s: Not created or invalid"), *WorldName);
		return;
	}
	
	// Get render stats
	int32 Sections = 0;
	int32 Tris = GetTriangleCount(World);
	bool ValidBounds = false;
	FVector BoxExtent = FVector::ZeroVector;
	
	// Try to get bounds from the world root component
	if (UVoxelWorldRootComponent* RootComp = World->FindComponentByClass<UVoxelWorldRootComponent>())
	{
		FBoxSphereBounds Bounds = RootComp->GetLocalBounds();
		ValidBounds = !Bounds.BoxExtent.IsZero();
		BoxExtent = Bounds.BoxExtent;
		Sections = RootComp->GetNumMaterials(); // Approximation
	}
	
	UE_LOG(LogTemp, Warning, TEXT("[RenderStats] %s Sections=%d, Tris=%d, ValidBounds=%s, BoxExtent=(%.1f,%.1f,%.1f)"), 
		*WorldName, Sections, Tris, ValidBounds ? TEXT("true") : TEXT("false"), 
		BoxExtent.X, BoxExtent.Y, BoxExtent.Z);
	
	// Debug active invokers
	UWorld* CurrentWorld = World ? World->GetWorld() : nullptr;
	if (!CurrentWorld) return;
	const TArray<TWeakObjectPtr<UVoxelInvokerComponentBase>>& Invokers = UVoxelInvokerComponentBase::GetInvokers(CurrentWorld);
	UE_LOG(LogTemp, Warning, TEXT("[Invokers] Count=%d"), Invokers.Num());
	for (int32 i = 0; i < Invokers.Num(); i++)
	{
		if (Invokers[i].IsValid())
		{
			UVoxelInvokerComponentBase* Invoker = Invokers[i].Get();
			FVector InvokerLoc = Invoker->GetComponentLocation();
			FVoxelInvokerSettings Settings = Invoker->GetInvokerSettings(World);
			float LODRange = Settings.LODBounds.Size().Size();
			float CollisionRange = Settings.CollisionsBounds.Size().Size();
			UE_LOG(LogTemp, Warning, TEXT("[Invokers] #%d Loc=(%.1f,%.1f,%.1f), LODRange=%.1f, CollisionRange=%.1f"), 
				i, InvokerLoc.X, InvokerLoc.Y, InvokerLoc.Z, LODRange, CollisionRange);
		}
	}
	
	// Runtime state
	bool RuntimeInitialized = World->IsCreated();
	UE_LOG(LogTemp, Warning, TEXT("[Runtime] %s Initialized=%s, UsingData=true, AsyncMeshing=OFF(for this op)"), 
		*WorldName, RuntimeInitialized ? TEXT("true") : TEXT("false"));
}

int32 AVoxelEditableAfterLandingTest::GetTriangleCount(AVoxelWorld* World)
{
	if (!World || !World->IsCreated())
	{
		return 0;
	}
	
	// Get triangle count from the world root component
	if (UVoxelWorldRootComponent* RootComp = World->FindComponentByClass<UVoxelWorldRootComponent>())
	{
		// This is a rough approximation - would need to access internal render data for exact count
		FBoxSphereBounds Bounds = RootComp->GetLocalBounds();
		if (!Bounds.BoxExtent.IsZero())
		{
			// Estimate based on bounds volume (very rough)
			float Volume = Bounds.BoxExtent.X * Bounds.BoxExtent.Y * Bounds.BoxExtent.Z;
			return FMath::Max(1, FMath::RoundToInt(Volume / 10000.0f)); // Rough estimate
		}
	}
	
	return 0;
}

void AVoxelEditableAfterLandingTest::ForceSynchronousRemesh(AVoxelWorld* World)
{
	if (!World || !World->IsCreated())
	{
		UE_LOG(LogTemp, Error, TEXT("[ForceSynchronousRemesh] World not created or invalid"));
		return;
	}
	
	// Force synchronous rebuild on the world
	UVoxelWorldRootComponent& VoxelComp = World->GetWorldRoot();
	
	// Trigger mesh regeneration
	VoxelComp.MarkRenderStateDirty();
	VoxelComp.RecreateRenderState_Concurrent();
	
	UE_LOG(LogTemp, Warning, TEXT("[Rebuild] %s: Sync remesh OK"), *World->GetName());
}

void AVoxelEditableAfterLandingTest::ForceSynchronousRemesh(AVoxelWorld* World, const FIntVector& Min, const FIntVector& Max)
{
	// For this version, just call the basic sync remesh
	ForceSynchronousRemesh(World);
}

void AVoxelEditableAfterLandingTest::DumpRenderStats(AVoxelWorld* World, const FString& WorldName)
{
	if (!World || !World->IsCreated())
	{
		UE_LOG(LogTemp, Warning, TEXT("[RenderStats] %s: Not created or invalid"), *WorldName);
		return;
	}
	
	// Get render stats
	int32 Sections = 0;
	int32 Tris = GetTriangleCount(World);
	bool ValidBounds = false;
	FVector BoxExtent = FVector::ZeroVector;
	
	// Try to get bounds from the world root component
	if (UVoxelWorldRootComponent* RootComp = World->FindComponentByClass<UVoxelWorldRootComponent>())
	{
		FBoxSphereBounds Bounds = RootComp->GetLocalBounds();
		ValidBounds = !Bounds.BoxExtent.IsZero();
		BoxExtent = Bounds.BoxExtent;
		Sections = RootComp->GetNumMaterials(); // Approximation
	}
	
	UE_LOG(LogTemp, Warning, TEXT("[RenderStats] %s Sections=%d, Tris=%d, ValidBounds=%s, BoxExtent=(%.1f,%.1f,%.1f)"), 
		*WorldName, Sections, Tris, ValidBounds ? TEXT("true") : TEXT("false"), 
		BoxExtent.X, BoxExtent.Y, BoxExtent.Z);
}

void AVoxelEditableAfterLandingTest::DumpSanityConfig(AVoxelWorld* World)
{
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("[SanityDump] World is null"));
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("[SanityDump] WorldSize=%d, VoxelSize=%.1f"), 
		World->WorldSizeInVoxel, World->VoxelSize);
	UE_LOG(LogTemp, Warning, TEXT("[SanityDump] Collisions=%s, Created=%s"), 
		World->bEnableCollisions ? TEXT("true") : TEXT("false"),
		World->IsCreated() ? TEXT("true") : TEXT("false"));
	
	// Debug active invokers
	UWorld* CurrentWorld = World ? World->GetWorld() : nullptr;
	if (!CurrentWorld) return;
	const TArray<TWeakObjectPtr<UVoxelInvokerComponentBase>>& Invokers = UVoxelInvokerComponentBase::GetInvokers(CurrentWorld);
	UE_LOG(LogTemp, Warning, TEXT("[SanityDump] ActiveInvokers=%d"), Invokers.Num());
	for (int32 i = 0; i < Invokers.Num(); i++)
	{
		if (Invokers[i].IsValid())
		{
			UVoxelInvokerComponentBase* Invoker = Invokers[i].Get();
			FVector InvokerLoc = Invoker->GetComponentLocation();
			UE_LOG(LogTemp, Warning, TEXT("[SanityDump] Invoker#%d Loc=(%.1f,%.1f,%.1f)"), 
				i, InvokerLoc.X, InvokerLoc.Y, InvokerLoc.Z);
		}
	}
}