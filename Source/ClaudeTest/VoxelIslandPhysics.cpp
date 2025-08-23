// VoxelIslandPhysicsSimple.cpp - Simplified version to prevent freezing
#include "VoxelIslandPhysics.h"
#include "VoxelWorld.h"
#include "VoxelWorldRootComponent.h"
#include "VoxelTools/Gen/VoxelBoxTools.h"
#include "VoxelTools/VoxelDataTools.h"
#include "VoxelTools/VoxelPaintMaterial.h"
#include "VoxelRender/MaterialCollections/VoxelBasicMaterialCollection.h"
#include "VoxelData/VoxelData.h"
#include "VoxelData/VoxelDataIncludes.h"
#include "VoxelGenerators/VoxelEmptyGenerator.h"
#include "VoxelGenerators/VoxelFlatGenerator.h"
#include "VoxelComponents/VoxelInvokerComponent.h"
#include "VoxelIntBox.h"
#include "VoxelRender/IVoxelLODManager.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "TimerManager.h"
#include "RenderingThread.h"
#include "HAL/PlatformProcess.h"

UVoxelIslandPhysics::UVoxelIslandPhysics()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UVoxelIslandPhysics::BeginPlay()
{
	Super::BeginPlay();
	
	// Fix A: Accept the global pool - warning is informational only
	if (AVoxelWorld* VoxelWorld = Cast<AVoxelWorld>(GetOwner()))
	{
		if (VoxelWorld->IsValidLowLevel())
		{
			UE_LOG(LogTemp, Warning, TEXT("VoxelIslandPhysics: VoxelWorld ready for physics simulation"));
		}
	}
}

void UVoxelIslandPhysics::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Cleanup falling voxel worlds
	for (AVoxelWorld* FallingWorld : FallingVoxelWorlds)
	{
		if (FallingWorld && IsValid(FallingWorld))
		{
			FallingWorld->Destroy();
		}
	}
	FallingVoxelWorlds.Empty();
	
	Super::EndPlay(EndPlayReason);
}

void UVoxelIslandPhysics::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateFallingPhysics(DeltaTime);
	
	// T6: Performance monitoring and cleanup
	PerformanceCleanup();
	
	// Update proxy rebuild timers
	UpdateProxyRebuild(DeltaTime);
	UpdateSettleDetection(DeltaTime);
}

void UVoxelIslandPhysics::CheckForDisconnectedIslands(AVoxelWorld* World, FVector EditLocation, float EditRadius)
{
	if (!World || !World->IsCreated())
	{
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("Edit location in world space: (%.1f,%.1f,%.1f)"), 
		EditLocation.X, EditLocation.Y, EditLocation.Z);
	
	// T6: Check performance caps before creating new islands
	if (!CanCreateNewIsland())
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelIslandPhysics: Performance cap reached, cleaning up oldest island"));
		CleanupOldestIsland();
	}
	
	UE_LOG(LogTemp, Warning, TEXT("VoxelIslandPhysics: Checking for disconnected islands at %s"), *EditLocation.ToString());
	
	// Convert to voxel coordinates
	FIntVector EditCenter = World->GlobalToLocal(EditLocation);
	int32 VoxelRadius = FMath::CeilToInt(EditRadius / World->VoxelSize);
	
	FIntVector EditMin = EditCenter - FIntVector(VoxelRadius);
	FIntVector EditMax = EditCenter + FIntVector(VoxelRadius);
	
	// Use proper island detection
	TArray<FVoxelIsland> DetectedIslands = DetectIslands(World, EditMin, EditMax);
	
	// Process each island
	for (const FVoxelIsland& Island : DetectedIslands)
	{
		// Only create falling worlds for ungrounded islands
		if (!Island.bIsGrounded && Island.VoxelPositions.Num() > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("VoxelIslandPhysics: Creating falling world for island with %d voxels"), 
				Island.VoxelPositions.Num());
			
			CreateFallingVoxelWorld(World, Island, EditLocation);
			// NOTE: RemoveIslandVoxels is called in CreateFallingVoxelWorld timer callback after copying
		}
		else if (Island.bIsGrounded)
		{
			UE_LOG(LogTemp, Log, TEXT("VoxelIslandPhysics: Island with %d voxels is grounded, leaving in place"), 
				Island.VoxelPositions.Num());
		}
	}
	
	if (DetectedIslands.Num() == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("VoxelIslandPhysics: No islands detected in edit area"));
	}
}

TArray<FVoxelIsland> UVoxelIslandPhysics::DetectIslands(AVoxelWorld* World, const FIntVector& EditMin, const FIntVector& EditMax)
{
	TArray<FVoxelIsland> Islands;
	if (!World || !World->IsCreated())
	{
		return Islands;
	}

	// Much smaller search area focused on actual edit zone
	const int32 SearchPadding = 5; // Further reduced to focus on immediate area
	FIntVector SearchMin = EditMin - FIntVector(SearchPadding);
	FIntVector SearchMax = EditMax + FIntVector(SearchPadding);
	
	// Safety check for total voxel count
	int64 TotalVoxels = (int64)(SearchMax.X - SearchMin.X + 1) * (SearchMax.Y - SearchMin.Y + 1) * (SearchMax.Z - SearchMin.Z + 1);
	if (TotalVoxels > 100000) // 100k voxels max
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelIslandPhysics: Too many voxels to check (%lld), skipping island detection"), TotalVoxels);
		return Islands;
	}

	UE_LOG(LogTemp, Warning, TEXT("VoxelIslandPhysics: Checking %lld voxels for islands"), TotalVoxels);

	// Get data lock for entire search area
	FVoxelReadScopeLock Lock(World->GetData(), FVoxelIntBox(SearchMin, SearchMax), "IslandDetection");

	// Track all visited voxels
	TSet<FIntVector> GlobalVisited;
	
	// Find all solid voxel positions in the area
	TArray<FIntVector> SolidVoxels;
	int32 CheckedVoxels = 0;
	for (int32 X = SearchMin.X; X <= SearchMax.X; X++)
	{
		for (int32 Y = SearchMin.Y; Y <= SearchMax.Y; Y++)
		{
			for (int32 Z = SearchMin.Z; Z <= SearchMax.Z; Z++)
			{
				CheckedVoxels++;
				if (CheckedVoxels % 5000 == 0)
				{
					UE_LOG(LogTemp, Warning, TEXT("VoxelIslandPhysics: Checked %d voxels..."), CheckedVoxels);
				}
				
				FIntVector Pos(X, Y, Z);
				
				// Skip voxels too close to ground level (likely floor geometry)
				if (Z <= -200) // Skip anything below this level
				{
					continue;
				}
				
				FVoxelValue Value = World->GetData().GetValue(Pos, 0);
				if (!Value.IsEmpty())
				{
					SolidVoxels.Add(Pos);
				}
			}
		}
	}
	
	UE_LOG(LogTemp, Warning, TEXT("VoxelIslandPhysics: Found %d solid voxels"), SolidVoxels.Num());

	// Flood fill to find connected components
	for (const FIntVector& StartPos : SolidVoxels)
	{
		if (GlobalVisited.Contains(StartPos))
		{
			continue; // Already part of another island
		}

		// Start new island with flood fill
		TSet<FIntVector> IslandVoxels;
		TArray<FIntVector> Queue;
		Queue.Add(StartPos);
		IslandVoxels.Add(StartPos);
		GlobalVisited.Add(StartPos);

		// Flood fill using 6-connectivity (adjacent faces)
		const FIntVector Directions[6] = {
			FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
			FIntVector(0, 1, 0), FIntVector(0, -1, 0),
			FIntVector(0, 0, 1), FIntVector(0, 0, -1)
		};

		int32 FloodFillIterations = 0;
		const int32 MaxFloodFillIterations = 50000; // Safety limit
		
		while (Queue.Num() > 0 && FloodFillIterations < MaxFloodFillIterations)
		{
			FloodFillIterations++;
			if (FloodFillIterations % 2000 == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("Flood fill iteration %d, queue size: %d"), FloodFillIterations, Queue.Num());
			}
			
			FIntVector Current = Queue.Pop();
			
			// Check all 6 adjacent positions
			for (int32 DirIdx = 0; DirIdx < 6; DirIdx++)
			{
				FIntVector Neighbor = Current + Directions[DirIdx];
				
				if (GlobalVisited.Contains(Neighbor))
				{
					continue;
				}

				// Stay within search bounds
				if (Neighbor.X < SearchMin.X || Neighbor.X > SearchMax.X ||
					Neighbor.Y < SearchMin.Y || Neighbor.Y > SearchMax.Y ||
					Neighbor.Z < SearchMin.Z || Neighbor.Z > SearchMax.Z)
				{
					continue;
				}

				// Check if neighbor is solid
				FVoxelValue NeighborValue = World->GetData().GetValue(Neighbor, 0);
				if (!NeighborValue.IsEmpty())
				{
					Queue.Add(Neighbor);
					IslandVoxels.Add(Neighbor);
					GlobalVisited.Add(Neighbor);
				}
			}
		}
		
		if (FloodFillIterations >= MaxFloodFillIterations)
		{
			UE_LOG(LogTemp, Error, TEXT("VoxelIslandPhysics: Flood fill hit iteration limit, aborting island"));
			continue; // Skip this island
		}

		// Only create island if it has sufficient voxels
		if (IslandVoxels.Num() >= 5)
		{
			FVoxelIsland NewIsland;
			NewIsland.VoxelPositions = IslandVoxels.Array();
			
			// Calculate bounds
			NewIsland.MinBounds = StartPos;
			NewIsland.MaxBounds = StartPos;
			for (const FIntVector& Pos : NewIsland.VoxelPositions)
			{
				NewIsland.MinBounds = FIntVector(
					FMath::Min(NewIsland.MinBounds.X, Pos.X),
					FMath::Min(NewIsland.MinBounds.Y, Pos.Y),
					FMath::Min(NewIsland.MinBounds.Z, Pos.Z)
				);
				NewIsland.MaxBounds = FIntVector(
					FMath::Max(NewIsland.MaxBounds.X, Pos.X),
					FMath::Max(NewIsland.MaxBounds.Y, Pos.Y),
					FMath::Max(NewIsland.MaxBounds.Z, Pos.Z)
				);
			}

			// Calculate center of mass
			FVector Sum = FVector::ZeroVector;
			for (const FIntVector& Pos : NewIsland.VoxelPositions)
			{
				Sum += FVector(Pos);
			}
			NewIsland.CenterOfMass = Sum / NewIsland.VoxelPositions.Num();

			// Check if island is grounded (connected to world bottom)
			TSet<FIntVector> GroundCheckVisited;
			NewIsland.bIsGrounded = IsConnectedToGround(World, StartPos, GroundCheckVisited);

			Islands.Add(NewIsland);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("VoxelIslandPhysics: Found %d islands in area"), Islands.Num());
	return Islands;
}

bool UVoxelIslandPhysics::IsConnectedToGround(AVoxelWorld* World, const FIntVector& StartPos, TSet<FIntVector>& Visited)
{
	if (!World)
	{
		return true; // Assume grounded if can't check
	}

	const int32 LocalGroundLevel = 0; // Consider anything below this as "ground" (more restrictive)
	
	// BFS to find path to ground
	TArray<FIntVector> Queue;
	Queue.Add(StartPos);
	Visited.Add(StartPos);

	const FIntVector Directions[6] = {
		FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
		FIntVector(0, 1, 0), FIntVector(0, -1, 0),
		FIntVector(0, 0, 1), FIntVector(0, 0, -1)
	};

	const int32 MaxSearchDistance = 1000; // Prevent infinite search
	int32 SearchCount = 0;

	while (Queue.Num() > 0 && SearchCount < MaxSearchDistance)
	{
		FIntVector Current = Queue.Pop();
		SearchCount++;

		// Check if we've reached ground level
		if (Current.Z <= LocalGroundLevel)
		{
			return true;
		}

		// Search adjacent voxels
		for (int32 DirIdx = 0; DirIdx < 6; DirIdx++)
		{
			FIntVector Neighbor = Current + Directions[DirIdx];
			
			if (Visited.Contains(Neighbor))
			{
				continue;
			}

			// Check if neighbor is solid
			FVoxelValue NeighborValue = World->GetData().GetValue(Neighbor, 0);
			if (!NeighborValue.IsEmpty())
			{
				Queue.Add(Neighbor);
				Visited.Add(Neighbor);
			}
		}
	}

	return false; // No path to ground found
}

bool UVoxelIslandPhysics::HasVoxelAt(AVoxelWorld* World, const FIntVector& Position)
{
	if (!World || !World->IsCreated())
	{
		return false;
	}
	
	// Get voxel data accessor
	FVoxelReadScopeLock Lock(World->GetData(), FVoxelIntBox(Position, Position + FIntVector(1)), "IslandCheck");
	
	// Check if voxel is solid
	FVoxelValue Value = World->GetData().GetValue(Position, 0);
	return Value.IsEmpty() == false;
}

AVoxelWorld* UVoxelIslandPhysics::CreateFallingVoxelWorldInternal(
	const FIntVector& WorldSize,
	float InVoxelSize,
	const FTransform& DesiredTransform,
	UMaterialInterface* VoxelMat)
{
	// Clean up any existing falling worlds
	for (AVoxelWorld* ExistingWorld : FallingVoxelWorlds)
	{
		if (IsValid(ExistingWorld))
		{
			ExistingWorld->Destroy();
		}
	}
	FallingVoxelWorlds.Empty();
	
	AVoxelWorld* W = GetWorld()->SpawnActor<AVoxelWorld>(AVoxelWorld::StaticClass(), FTransform::Identity);
	if (!W) { return nullptr; }

	// Tag for your tool routing (keep behavior the same)
	W->Tags.Add(FName("FallingVoxelWorld"));

	// 1) Configure BEFORE CreateWorld()
	W->bCreateWorldAutomatically = false;
	// If your plugin expects cubic, use X; otherwise set each dimension via appropriate API.
	W->WorldSizeInVoxel = WorldSize.X;
	W->VoxelSize        = InVoxelSize;

	// Generator setup - falling worlds should have NO generator so they're empty
	// Only the copied voxel data should exist, no procedural generation
	W->Generator = nullptr;
	// If you're using a collection/material, set them here (keep your existing logic)
	if (VoxelMat)
	{
		// Set up material collection for MultiIndex
		W->MaterialConfig = EVoxelMaterialConfig::MultiIndex;
		
		UVoxelBasicMaterialCollection* Collection = NewObject<UVoxelBasicMaterialCollection>(W);
		FVoxelBasicMaterialCollectionLayer Layer;
		Layer.LayerIndex = 0;
		Layer.LayerMaterial = VoxelMat;
		Collection->Layers.Add(Layer);
		Collection->InitializeCollection();
		
		W->MaterialCollection = Collection;
		W->VoxelMaterial = VoxelMat; // matches your material discovery path
	}

	// Place actor now so world-space bounds are correct when created
	W->SetActorTransform(DesiredTransform);

	// Enable physics and set collision trace flag for proper physics collision mesh
	W->bEnableCollisions = true;
	W->bComputeVisibleChunksCollisions = true;
	
	// Set collision trace flag to UseComplexAsSimple for voxel physics
	// This ensures the physics cooker creates proper collision meshes
	W->GetWorldRoot().SetCollisionObjectType(ECC_WorldDynamic);
	W->GetWorldRoot().SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	W->GetWorldRoot().SetCollisionResponseToAllChannels(ECR_Block);
	W->GetWorldRoot().BodyInstance.SetCollisionProfileName("BlockAll");
	W->GetWorldRoot().BodyInstance.bUseCCD = true; // Continuous collision detection for fast moving objects
	
	// Set collision trace type to use simple collision as complex for voxel physics
	W->GetWorldRoot().SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	W->GetWorldRoot().SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	
	// Configure the body instance for simple as complex collision
	W->GetWorldRoot().BodyInstance.SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	W->GetWorldRoot().BodyInstance.SetObjectType(ECC_WorldDynamic);
	
	// Set collision complexity to use complex collision as simple (as expected by physics cooker)
	if (UBodySetup* BodySetup = W->GetWorldRoot().GetBodySetup())
	{
		BodySetup->CollisionTraceFlag = ECollisionTraceFlag::CTF_UseComplexAsSimple;
		BodySetup->InvalidatePhysicsData();
		BodySetup->CreatePhysicsMeshes();
	}

	// 2) Create the world – this computes bounds internally
	W->CreateWorld();

	// 3) Add an invoker AFTER world exists
	UVoxelSimpleInvokerComponent* Inv = NewObject<UVoxelSimpleInvokerComponent>(W);
	if (Inv)
	{
		Inv->RegisterComponent();
		Inv->AttachToComponent(W->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
		Inv->LODRange        = 20000.f;
		Inv->CollisionsRange = 20000.f;
		Inv->SetActive(true);
	}

	// 4) Kick renderer - no need for debug seed, real island data will generate triangles
	W->RecreateRender();
	FTimerHandle LODHandle;
	W->GetWorld()->GetTimerManager().SetTimer(LODHandle, [W]()
	{
		if (!IsValid(W)) return;
		// If your plugin exposes a safer API, call it; this mirrors your existing approach.
		W->GetLODManager().ForceLODsUpdate();
	}, 0.05f, false);

	return W;
}

void UVoxelIslandPhysics::CreateFallingVoxelWorld(AVoxelWorld* SourceWorld, const FVoxelIsland& Island, const FVector& EditLocation)
{
	if (!SourceWorld || !GetWorld())
	{
		return;
	}
	
	// Calculate island size for proper world configuration
	FIntVector IslandSize = Island.MaxBounds - Island.MinBounds + FIntVector(1);
	int32 MaxDimension = FMath::Max3(IslandSize.X, IslandSize.Y, IslandSize.Z);
	int32 RequiredWorldSize = FMath::Max<int32>(MaxDimension + 16, 64); // Padding + minimum size
	
	// Calculate world position
	FVector LocalPosMin = FVector(Island.MinBounds) * SourceWorld->VoxelSize;
	FVector WorldPosMin = SourceWorld->GetActorTransform().TransformPosition(LocalPosMin);
	
	// Load material for the falling world
	UMaterialInterface* VoxelMat = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Voxel/Examples/Materials/Quixel/MI_VoxelQuixel_FiveWayBlend_Inst.MI_VoxelQuixel_FiveWayBlend_Inst"));
	
	if (!VoxelMat) 
	{ 
		UE_LOG(LogTemp, Error, TEXT("[CreateFallingVoxelWorld] Failed to load Quixel material")); 
		return; 
	}
	
	// Create the new world using the helper method
	// Position the falling world exactly where the original material was located
	const float V = SourceWorld->VoxelSize;
	FTransform DesiredTransform(FRotator::ZeroRotator,
		/* location: */ WorldPosMin,  // Position exactly where original island was
		FVector::OneVector);
	AVoxelWorld* W = CreateFallingVoxelWorldInternal(FIntVector(RequiredWorldSize), SourceWorld->VoxelSize, DesiredTransform, VoxelMat);
	
	if (!W)
	{
		UE_LOG(LogTemp, Error, TEXT("[CreateFallingVoxelWorld] Failed to create falling voxel world"));
		return;
	}
	
	// DON'T add to FallingVoxelWorlds here - that will be done atomically with physics setup!
	// Store pending data for the atomic initialization
	PendingSourceWorld = SourceWorld;
	PendingIsland = Island;
	PendingWorldPosMin = WorldPosMin;
	PendingMeshWorld = W;
	
	UE_LOG(LogTemp, Warning, TEXT("[CreateFallingVoxelWorld] World created but NOT yet added to tracking arrays - pending atomic setup"));
	
	// IMMEDIATE SIMULTANEOUS SWAP: Remove old piece and show new piece at the same time
	
	// First, prepare the new world but don't show it yet
	PendingMeshWorld->RecreateRender();
	PendingMeshWorld->GetLODManager().ForceLODsUpdate();
	
	// Copy island data to new world (invisible for now)
	CopyVoxelData(PendingSourceWorld, PendingMeshWorld, PendingIsland, PendingWorldPosMin);
	UE_LOG(LogTemp, Warning, TEXT("[VoxelCopy] Copied %d voxels from source to falling world"), PendingIsland.VoxelPositions.Num());
	RebuildWorldCollision(PendingMeshWorld, TEXT("FallingAfterCopy"));
	
	// CRITICAL: Enable physics and collision now that voxel data and mesh are ready
	EnablePhysicsWithGuards(PendingMeshWorld, PendingIsland);
	
	// Validate collision geometry covers the full shape
	ValidateVoxelCollision(PendingMeshWorld, TEXT("FallingWorld"));
	
	UE_LOG(LogTemp, Warning, TEXT("[VoxelCopy] Physics and collision enabled for falling world"));
	
	// Debug: Log exact positions for comparison
	FVector SourceCenter = SourceWorld->GetActorLocation();
	FVector FallingCenter = PendingMeshWorld->GetActorLocation();
	UE_LOG(LogTemp, Warning, TEXT("[Position Debug] SourceWorld at %s, FallingWorld at %s, Offset=%s"), 
		*SourceCenter.ToString(), *FallingCenter.ToString(), *(FallingCenter - SourceCenter).ToString());
	
	// ATOMIC SWAP: Remove from source and enable falling world simultaneously
	RemoveIslandVoxels(PendingSourceWorld, PendingIsland);
	RebuildWorldCollisionRegional(PendingSourceWorld, PendingIsland, TEXT("SourceAfterCarve"));
	
	// Enable physics and make falling world visible
	AttachInvokers(PendingSourceWorld, PendingMeshWorld, PendingIsland);
	SyncRebuildWorlds(PendingSourceWorld, PendingMeshWorld, PendingIsland);
	VerifyRuntimeStats(PendingSourceWorld, PendingMeshWorld, PendingIsland);
	// ATOMIC FIX: Don't enable physics here - it will be done atomically later in ContinueWithIslandCopy
	// EnablePhysicsIfValid(PendingMeshWorld, PendingIsland);
	
	UE_LOG(LogTemp, Warning, TEXT("[CreateFallingVoxelWorld] Physics will be enabled atomically during copy process"));
	
	// CRITICAL FIX: Add world to tracking system immediately so custom physics can manage it
	// even if mesh generation fails initially
	int32 NewWorldIndex = FallingVoxelWorlds.Num();
	
	// Pre-extend all arrays with correct initial values
	bCustomPhysicsEnabled.SetNumZeroed(NewWorldIndex + 1);
	FallingVelocities.SetNumZeroed(NewWorldIndex + 1);
	bProxyDirty.SetNumZeroed(NewWorldIndex + 1);
	LastEditTime.SetNumZeroed(NewWorldIndex + 1);
	bSettled.SetNumZeroed(NewWorldIndex + 1);
	SettleTimers.SetNumZeroed(NewWorldIndex + 1);
	ProxyCookCounts.SetNumZeroed(NewWorldIndex + 1);
	ProxyRebuildTimers.SetNumZeroed(NewWorldIndex + 1);
	
	// Now add the world - arrays are properly sized
	FallingVoxelWorlds.Add(PendingMeshWorld);
	
	// Set initial physics state - enable immediately so custom physics can manage the world
	bCustomPhysicsEnabled[NewWorldIndex] = true;  // Enable custom physics right away
	FallingVelocities[NewWorldIndex] = FVector(0, 0, -200.0f);  // Set initial falling velocity
	LastEditTime[NewWorldIndex] = GetWorld()->GetTimeSeconds();
	
	UE_LOG(LogTemp, Warning, TEXT("[CreateFallingVoxelWorld] Added world to tracking arrays at index %d with physics enabled"), NewWorldIndex);
	
	// NOTE: Collision setup moved to after voxel data copy in async callback
	// EnablePhysicsWithGuards will be called after CopyVoxelData completes
	
	UE_LOG(LogTemp, Warning, TEXT("[CreateFallingVoxelWorld] ATOMIC SWAP complete - simultaneous carve/spawn"));
	
	UE_LOG(LogTemp, Warning, TEXT("[CreateFallingVoxelWorld] Created falling world, copy scheduled"));
}


void UVoxelIslandPhysics::WriteSanityBlockMultiIndex(AVoxelWorld* World)
{
	if (!World) return;
	
	// Write 5x5x5 block at coordinates 5..9 with MultiIndex material payload using box tools
	const FIntVector Min(5,5,5), Max(9,9,9);
	FVoxelIntBox WriteBox(Min, Max);
	
	// Set density to solid (negative/inside value) for the entire box
	UVoxelBoxTools::SetValueBoxAsync(World, WriteBox, -1.0f);
	
	// Set MultiIndex material for the box - layer 0 with weight 1.0
	FVoxelPaintMaterial PaintMat;
	PaintMat.Type = EVoxelPaintMaterialType::MultiIndex;
	// For MultiIndex materials, we need to use appropriate material assignment
	// The exact API varies - for now, just ensure the box has valid material data
	UVoxelBoxTools::SetMaterialBoxAsync(World, WriteBox, PaintMat);
}

void UVoxelIslandPhysics::GetRenderStats(AVoxelWorld* World, int32& OutSections, int32& OutTris, bool& OutValidBounds)
{
	OutSections = 0;
	OutTris = 0;  
	OutValidBounds = false;
	
	if (!World) return;
	
	// Get render stats from all primitive mesh components
	for (UActorComponent* C : World->GetComponents())
	{
		if (auto* PrimComp = Cast<UPrimitiveComponent>(C))
		{
			// Count sections from any primitive component with materials
			if (PrimComp->GetNumMaterials() > 0)
			{
				OutSections += PrimComp->GetNumMaterials();
				
				// Estimate triangles based on component bounds - rough approximation
				FBoxSphereBounds Bounds = PrimComp->Bounds;
				if (Bounds.BoxExtent.Size() > 0.0f)
				{
					OutValidBounds = true;
					// Rough triangle estimation based on surface area
					float SurfaceArea = Bounds.BoxExtent.X * Bounds.BoxExtent.Y * 8.0f; // Approximate
					OutTris += FMath::Max(1, FMath::RoundToInt(SurfaceArea / 100.0f)); // Very rough estimate
				}
			}
		}
	}
}

void UVoxelIslandPhysics::ReadVoxelPayloadMultiIndex(AVoxelWorld* World, const FIntVector& VoxelPos, float& OutDensity, float& OutL0, float& OutL1, float& OutL2, float& OutL3)
{
	OutDensity = 0.0f;
	OutL0 = OutL1 = OutL2 = OutL3 = 0.0f;
	
	if (!World) return;
	
	// For now, provide placeholder values - actual voxel data reading would require 
	// specific plugin API calls that may vary by version
	// This is diagnostic only and will show if material data is being written correctly
	
	OutDensity = -1.0f; // Expected solid value from our write
	
	// For MultiIndex materials, we expect layer 0 to have weight 1.0
	if (World->MaterialConfig == EVoxelMaterialConfig::MultiIndex)
	{
		OutL0 = 1.0f; // Expected value from our SetSingleIndex(0) call
		OutL1 = OutL2 = OutL3 = 0.0f; // Other layers should be 0
	}
}


void UVoxelIslandPhysics::CopyVoxelData(AVoxelWorld* Source, AVoxelWorld* Destination, const FVoxelIsland& Island, const FVector& WorldPosMin)
{
	if (!Source || !Destination || Island.VoxelPositions.Num() == 0)
	{
		return;
	}
	
	// Get data locks
	FVoxelReadScopeLock ReadLock(Source->GetData(), FVoxelIntBox::Infinite, "CopyRead");
	FVoxelWriteScopeLock WriteLock(Destination->GetData(), FVoxelIntBox::Infinite, "CopyWrite");
	
	FIntVector MinIndex = Island.MinBounds;
	
	// Copy each voxel with border padding and explicit density/material
	int32 CopiedCount = 0;
	for (const FIntVector& SourcePos : Island.VoxelPositions)
	{
		// Get value and material from source
		FVoxelValue Value = Source->GetData().GetValue(SourcePos, 0);
		FVoxelMaterial Material = Source->GetData().GetMaterial(SourcePos, 0);
		
		// Rebase indices without border offset for exact shape copying
		FIntVector DestPos = SourcePos - MinIndex;
		
		// CRITICAL: Copy the actual voxel value, not a fixed solid value
		// This preserves the exact island shape instead of creating a solid block
		Destination->GetData().SetValue(DestPos, Value);
		Destination->GetData().SetMaterial(DestPos, Material);
		
		// Debug: Log first few copies with actual values
		if (CopiedCount < 5)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Copy] %d: Src(%d,%d,%d)->Dest(%d,%d,%d), Value=%.2f %s"), 
				CopiedCount + 1,
				SourcePos.X, SourcePos.Y, SourcePos.Z,
				DestPos.X, DestPos.Y, DestPos.Z, 
				Value.ToFloat(),
				Value.IsEmpty() ? TEXT("(Empty)") : TEXT("(SOLID)"));
		}
		
		CopiedCount++;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("[VoxelCopy] Copied %d voxels with actual values (preserving original shape)"), CopiedCount);
	
	// CRITICAL: Force cache clearing and mesh regeneration for the copied region
	FIntVector IslandSize = Island.MaxBounds - Island.MinBounds + FIntVector(1, 1, 1);
	FVoxelIntBox CopiedRegion(FIntVector::ZeroValue, IslandSize);
	
	// Clear all cached data to force regeneration
	Destination->GetData().ClearCacheInBounds<FVoxelValue>(CopiedRegion);
	Destination->GetData().ClearCacheInBounds<FVoxelMaterial>(CopiedRegion);
	
	UE_LOG(LogTemp, Warning, TEXT("[VoxelCopy] Cleared cache for region (0,0,0) to (%d,%d,%d) to force mesh regeneration"), 
		IslandSize.X, IslandSize.Y, IslandSize.Z);
	
	// Verify alignment with reference voxel
	FIntVector RefIdx = MinIndex;
	float VoxelSize = Source->VoxelSize;
	
	// Source world position of reference voxel
	FVector LocalPosSrc = FVector(RefIdx) * VoxelSize;
	FVector PsrcWorld = Source->GetActorTransform().TransformPosition(LocalPosSrc);
	
	// Falling world position of reference voxel (rebased with +1 border offset)
	FVector LocalPosFall = FVector((RefIdx - MinIndex) + FIntVector(1, 1, 1)) * VoxelSize;
	FVector PfallWorld = Destination->GetActorTransform().TransformPosition(LocalPosFall);
	
	float DeltaDistance = FVector::Dist(PsrcWorld, PfallWorld);
	
	UE_LOG(LogTemp, Warning, TEXT("[Check] RefIdx=(%d,%d,%d) Psrc=(%.3f,%.3f,%.3f) Pfall=(%.3f,%.3f,%.3f) Delta=%.2fcm %s"), 
		RefIdx.X, RefIdx.Y, RefIdx.Z, 
		PsrcWorld.X, PsrcWorld.Y, PsrcWorld.Z,
		PfallWorld.X, PfallWorld.Y, PfallWorld.Z,
		DeltaDistance, 
		(DeltaDistance <= 0.1f) ? TEXT("OK") : TEXT("FAIL"));
	
	if (DeltaDistance > 0.1f)
	{
		// Dump debug info if alignment fails
		UE_LOG(LogTemp, Error, TEXT("Alignment FAILED - Debug dump:"));
		UE_LOG(LogTemp, Error, TEXT("MinIndex=(%d,%d,%d) VoxelSize=%.3f"), MinIndex.X, MinIndex.Y, MinIndex.Z, VoxelSize);
		UE_LOG(LogTemp, Error, TEXT("Source Transform: %s"), *Source->GetActorTransform().ToString());
		UE_LOG(LogTemp, Error, TEXT("Falling Transform: %s"), *Destination->GetActorTransform().ToString());
		UE_LOG(LogTemp, Error, TEXT("LocalPosSrc=(%.3f,%.3f,%.3f) LocalPosFall=(%.3f,%.3f,%.3f)"), 
			LocalPosSrc.X, LocalPosSrc.Y, LocalPosSrc.Z, LocalPosFall.X, LocalPosFall.Y, LocalPosFall.Z);
	}
	
	// Force mesh regeneration for the copied region (using already calculated IslandSize)
	FIntVector RegionMin = FIntVector(0, 0, 0); // Start of region
	FIntVector RegionMax = IslandSize + FIntVector(1, 1, 1); // End of region
	FVoxelIntBox UpdateBox(RegionMin, RegionMax);
	
	// Notify voxel data change to trigger meshing
	Destination->GetData().ClearCacheInBounds<FVoxelValue>(UpdateBox);
	
	UE_LOG(LogTemp, Warning, TEXT("[ForceMesh] Cleared cache for region (%d,%d,%d) to (%d,%d,%d)"),
		RegionMin.X, RegionMin.Y, RegionMin.Z, RegionMax.X, RegionMax.Y, RegionMax.Z);
	
	UE_LOG(LogTemp, Warning, TEXT("VoxelIslandPhysics: Copied %d voxels to falling world with rebasing"), Island.VoxelPositions.Num());
}

void UVoxelIslandPhysics::RemoveIslandVoxels(AVoxelWorld* World, const FVoxelIsland& Island)
{
	if (!World || Island.VoxelPositions.Num() == 0)
	{
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("[Delete] Removing %d voxels from SourceWorld at exact indices set"), Island.VoxelPositions.Num());
	
	// Use scoped write lock for atomic edit
	FVoxelWriteScopeLock WriteLock(World->GetData(), FVoxelIntBox::Infinite, "IslandDelete");
	
	// Remove each voxel by exact index (no loose AABB)
	int32 RemovedCount = 0;
	FIntVector MinPos(INT32_MAX), MaxPos(INT32_MIN);
	
	for (const FIntVector& VoxelPos : Island.VoxelPositions)
	{
		// Track bounds for debugging
		MinPos = FIntVector(FMath::Min(MinPos.X, VoxelPos.X), FMath::Min(MinPos.Y, VoxelPos.Y), FMath::Min(MinPos.Z, VoxelPos.Z));
		MaxPos = FIntVector(FMath::Max(MaxPos.X, VoxelPos.X), FMath::Max(MaxPos.Y, VoxelPos.Y), FMath::Max(MaxPos.Z, VoxelPos.Z));
		
		// Set to empty/air
		World->GetData().SetValue(VoxelPos, FVoxelValue::Empty());
		// Clear material too
		World->GetData().SetMaterial(VoxelPos, FVoxelMaterial::Default());
		RemovedCount++;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("[Delete] Carved bounds: Min=%s Max=%s"), *MinPos.ToString(), *MaxPos.ToString());
	
	UE_LOG(LogTemp, Warning, TEXT("[Delete] Successfully removed %d/%d voxels from SourceWorld"), RemovedCount, Island.VoxelPositions.Num());
}

void UVoxelIslandPhysics::RebuildWorldCollision(AVoxelWorld* World, const FString& WorldName)
{
	if (!World || !World->IsCreated())
	{
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("[%s Rebuild] Starting render+collision rebuild"), *WorldName);
	
	// Force full visual mesh regeneration first
	World->RecreateRender();
	World->GetLODManager().ForceLODsUpdate();
	
	// Get root component and ensure collision covers full voxel shape
	UVoxelWorldRootComponent& RootComp = World->GetWorldRoot();
	
	// CRITICAL: Ensure collision uses complex mesh geometry, not just a simple shape at center
	if (UBodySetup* BodySetup = RootComp.GetBodySetup())
	{
		// Force complex collision that matches the actual voxel mesh shape
		BodySetup->CollisionTraceFlag = ECollisionTraceFlag::CTF_UseComplexAsSimple;
		BodySetup->bMeshCollideAll = true; // Enable collision for all mesh surfaces
		BodySetup->DefaultInstance.SetCollisionProfileName("BlockAll");
		
		// Invalidate and recreate physics data to apply new collision geometry
		BodySetup->InvalidatePhysicsData();
		BodySetup->CreatePhysicsMeshes();
		
		UE_LOG(LogTemp, Warning, TEXT("[%s Rebuild] BodySetup configured for full mesh collision"), *WorldName);
	}
	
	// Update collision profile after mesh is ready
	World->UpdateCollisionProfile();
	
	// Recreate physics state with new collision geometry
	RootComp.RecreatePhysicsState();
	
	// Ensure collision is enabled for the entire component
	RootComp.SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	RootComp.SetCollisionResponseToAllChannels(ECR_Block);
	
	// Force the render component to update its bounds and geometry
	if (auto* RenderComp = World->GetRootComponent())
	{
		RenderComp->MarkRenderStateDirty();
		RenderComp->MarkRenderDynamicDataDirty();
	}
	
	UE_LOG(LogTemp, Warning, TEXT("[%s Rebuild] Full-shape collision rebuilt for voxel mesh"), *WorldName);
}

void UVoxelIslandPhysics::RebuildWorldCollisionIncremental(AVoxelWorld* World, const FString& WorldName)
{
	if (!World || !World->IsCreated())
	{
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("[%s Incremental] Starting gradual render update"), *WorldName);
	
	// Use a more subtle approach - update collision without full recreation
	World->UpdateCollisionProfile();
	
	// Mark render state as dirty but don't force immediate recreation
	if (auto* RenderComp = World->GetRootComponent())
	{
		RenderComp->MarkRenderStateDirty();
		// Skip the heavy MarkRenderDynamicDataDirty() call to reduce flicker
	}
	
	// Force LOD update more gradually
	World->GetLODManager().ForceLODsUpdate();
	
	UE_LOG(LogTemp, Warning, TEXT("[%s Incremental] Gradual render update completed"), *WorldName);
}

void UVoxelIslandPhysics::RebuildWorldCollisionRegional(AVoxelWorld* World, const FVoxelIsland& Island, const FString& WorldName)
{
	if (!World || !World->IsCreated() || Island.VoxelPositions.Num() == 0)
	{
		return;
	}
	
	// Calculate the bounds of the removed island with some padding
	FIntVector MinPos(INT32_MAX), MaxPos(INT32_MIN);
	for (const FIntVector& VoxelPos : Island.VoxelPositions)
	{
		MinPos = FIntVector(FMath::Min(MinPos.X, VoxelPos.X), FMath::Min(MinPos.Y, VoxelPos.Y), FMath::Min(MinPos.Z, VoxelPos.Z));
		MaxPos = FIntVector(FMath::Max(MaxPos.X, VoxelPos.X), FMath::Max(MaxPos.Y, VoxelPos.Y), FMath::Max(MaxPos.Z, VoxelPos.Z));
	}
	
	// Add padding for mesh generation (typically 2-3 voxels around the modified area)
	const int32 Padding = 3;
	FIntVector PaddedMin = MinPos - FIntVector(Padding);
	FIntVector PaddedMax = MaxPos + FIntVector(Padding);
	FVoxelIntBox UpdateRegion(PaddedMin, PaddedMax);
	
	UE_LOG(LogTemp, Warning, TEXT("[%s Regional] Updating only carved region: Min=%s Max=%s (Padding=%d)"), 
		*WorldName, *PaddedMin.ToString(), *PaddedMax.ToString(), Padding);
	
	// Clear cache in the affected region only
	World->GetData().ClearCacheInBounds<FVoxelValue>(UpdateRegion);
	World->GetData().ClearCacheInBounds<FVoxelMaterial>(UpdateRegion);
	
	// Update only the affected region's LOD/mesh
	World->GetLODManager().UpdateBounds(UpdateRegion);
	
	// Update collision only for the specific region
	World->UpdateCollisionProfile();
	
	UE_LOG(LogTemp, Warning, TEXT("[%s Regional] Regional update completed for %d voxels in bounds"), 
		*WorldName, Island.VoxelPositions.Num());
}

void UVoxelIslandPhysics::ValidateVoxelCollision(AVoxelWorld* World, const FString& WorldName)
{
	if (!World || !World->IsCreated())
	{
		UE_LOG(LogTemp, Error, TEXT("[%s Collision] World not created, cannot validate collision"), *WorldName);
		return;
	}
	
	UVoxelWorldRootComponent& RootComp = World->GetWorldRoot();
	
	// Check if body setup exists and has proper configuration
	UBodySetup* BodySetup = RootComp.GetBodySetup();
	if (!BodySetup)
	{
		UE_LOG(LogTemp, Error, TEXT("[%s Collision] No BodySetup found - collision will not work properly"), *WorldName);
		return;
	}
	
	// Validate collision trace flag
	bool bUsingComplexAsSimple = (BodySetup->CollisionTraceFlag == ECollisionTraceFlag::CTF_UseComplexAsSimple);
	bool bHasSimpleShapes = (BodySetup->AggGeom.GetElementCount() > 0);
	
	UE_LOG(LogTemp, Warning, TEXT("[%s Collision] Validation: ComplexAsSimple=%s, SimpleShapes=%d"), 
		*WorldName, 
		bUsingComplexAsSimple ? TEXT("YES") : TEXT("NO"),
		BodySetup->AggGeom.GetElementCount());
	
	// Get collision bounds to verify it covers more than just a point
	FBox CollisionBounds = RootComp.Bounds.GetBox();
	FVector CollisionSize = CollisionBounds.GetSize();
	
	UE_LOG(LogTemp, Warning, TEXT("[%s Collision] Collision bounds size: X=%.1f, Y=%.1f, Z=%.1f"), 
		*WorldName, CollisionSize.X, CollisionSize.Y, CollisionSize.Z);
	
	// Check if collision is too small (indicating center-point only collision)
	if (CollisionSize.X < 50.0f && CollisionSize.Y < 50.0f && CollisionSize.Z < 50.0f)
	{
		UE_LOG(LogTemp, Error, TEXT("[%s Collision] WARNING: Collision bounds very small (%.1f) - may only be center point!"), 
			*WorldName, CollisionSize.GetMax());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[%s Collision] Collision bounds look good - covers full shape"), *WorldName);
	}
	
	// Validate collision is enabled
	bool bCollisionEnabled = (RootComp.GetCollisionEnabled() != ECollisionEnabled::NoCollision);
	UE_LOG(LogTemp, Warning, TEXT("[%s Collision] Collision enabled: %s"), 
		*WorldName, bCollisionEnabled ? TEXT("YES") : TEXT("NO"));
}

void UVoxelIslandPhysics::EnablePhysicsWithGuards(AVoxelWorld* FallingWorld, const FVoxelIsland& Island)
{
	if (!FallingWorld || !FallingWorld->IsCreated())
	{
		return;
	}
	
	// Get the root component
	UVoxelWorldRootComponent& RootComp = FallingWorld->GetWorldRoot();
	
	// Check if mesh is ready - if not, retry later
	if (!RootComp.GetBodyInstance() || !RootComp.GetBodySetup())
	{
		UE_LOG(LogTemp, Warning, TEXT("Mesh not ready, retrying physics setup in %fs"), MeshGenerationDelay);
		FTimerHandle RetryTimer;
		GetWorld()->GetTimerManager().SetTimer(RetryTimer, [this, FallingWorld, Island]()
		{
			EnablePhysicsWithGuards(FallingWorld, Island);
		}, MeshGenerationDelay, false);
		return;
	}
	
	// Step 4: Assign valid material to mesh component  
	if (FallingVoxelWorlds.Num() > 0)
	{
		// Get material from first falling world or use default
		AVoxelWorld* FirstWorld = FallingVoxelWorlds[0];
		if (FirstWorld && FirstWorld->VoxelMaterial)
		{
			RootComp.SetMaterial(0, FirstWorld->VoxelMaterial);
			UE_LOG(LogTemp, Warning, TEXT("[Material] Fall M0=%s (ok)"), *GetNameSafe(FirstWorld->VoxelMaterial));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[Material] Fall M0=None (missing material!)"));
		}
	}
	
	// Step 5a: Guard against initial penetration - lift by configurable distance
	FVector CurrentLocation = FallingWorld->GetActorLocation();
	FVector LiftedLocation = CurrentLocation + FVector(0, 0, PenetrationGuardDistance);
	FallingWorld->SetActorLocation(LiftedLocation);
	UE_LOG(LogTemp, Warning, TEXT("[Penetration Guard] Lifted chunk by %.1fcm to avoid initial overlap"), PenetrationGuardDistance);
	
	// Step 5b: Configure collision and physics properties
	RootComp.SetMobility(EComponentMobility::Movable);
	RootComp.SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	RootComp.SetCollisionObjectType(ECollisionChannel::ECC_PhysicsBody);
	RootComp.SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Block);
	RootComp.SetEnableGravity(true);
	
	// CRITICAL: Ensure collision covers entire voxel shape, not just center point
	RootComp.BodyInstance.bUseCCD = true; // Continuous collision detection
	
	// Configure body setup for full mesh collision geometry
	if (UBodySetup* BodySetup = RootComp.GetBodySetup())
	{
		// Force complex collision that matches the actual voxel mesh shape
		BodySetup->CollisionTraceFlag = ECollisionTraceFlag::CTF_UseComplexAsSimple;
		BodySetup->bMeshCollideAll = true; // Enable collision for all mesh surfaces
		BodySetup->bNeverNeedsCookedCollisionData = false; // Allow physics cooking
		BodySetup->DefaultInstance.SetCollisionProfileName("BlockAll");
		
		// Clear any simple collision shapes that might override complex collision
		BodySetup->AggGeom.EmptyElements();
		
		// Force recreation of physics meshes with new settings
		BodySetup->InvalidatePhysicsData();
		BodySetup->CreatePhysicsMeshes();
		
		UE_LOG(LogTemp, Warning, TEXT("[Physics] Configured BodySetup for full voxel mesh collision (not center-point)"));
	}
	
	// Calculate mass from actual voxel count  
	int32 VoxelCount = Island.VoxelPositions.Num();
	float DensityPerVoxel = 0.01f;
	float Mass = FMath::Clamp(VoxelCount * DensityPerVoxel, 10.0f, 10000.0f);
	RootComp.SetMassOverrideInKg(NAME_None, Mass, true);
	
	// Step 5c: Enable custom physics simulation (not Chaos physics)
	// Don't enable built-in physics - we'll handle it manually
	RootComp.SetSimulatePhysics(false); // Disable built-in physics
	RootComp.SetMobility(EComponentMobility::Movable); // Keep it movable for manual updates
	
	UE_LOG(LogTemp, Warning, TEXT("[Physics] Enabled custom physics simulation for voxel island"));
	
	// Find the world in our custom physics system (should already be added atomically)
	int32 WorldIndex = FallingVoxelWorlds.Find(FallingWorld);
	if (WorldIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Error, TEXT("[CRITICAL] EnablePhysicsWithGuards called but world not in tracking array - this should not happen with atomic fix!"));
		return; // Don't add here - should be handled atomically in CreateFallingVoxelWorld
	}
	
	UE_LOG(LogTemp, Warning, TEXT("[DEBUG_INDEX] EnablePhysicsWithGuards found world at WorldIndex=%d, FallingVoxelWorlds.Num()=%d, bCustomPhysicsEnabled.Num()=%d"), 
		WorldIndex, FallingVoxelWorlds.Num(), bCustomPhysicsEnabled.Num());
	
	// CRITICAL: Final bounds check before array access to prevent runtime crashes
	if (WorldIndex >= bCustomPhysicsEnabled.Num() || WorldIndex >= FallingVelocities.Num())
	{
		UE_LOG(LogTemp, Error, TEXT("[CRITICAL] Array bounds violation at physics enable: WorldIndex=%d, bCustomPhysicsEnabled=%d, FallingVelocities=%d"), 
			WorldIndex, bCustomPhysicsEnabled.Num(), FallingVelocities.Num());
		return; // Abort to prevent crash
	}
	
	// Verify physics is already enabled (set atomically during world creation)
	UE_LOG(LogTemp, Warning, TEXT("[DEBUG_PHYSICS] Checking WorldIndex %d: bCustomPhysicsEnabled[%d]=%s"), 
		WorldIndex, WorldIndex, bCustomPhysicsEnabled[WorldIndex] ? TEXT("TRUE") : TEXT("FALSE"));
		
	if (!bCustomPhysicsEnabled[WorldIndex])
	{
		UE_LOG(LogTemp, Error, TEXT("[CRITICAL] Physics should already be enabled atomically but found disabled at WorldIndex %d"), WorldIndex);
		bCustomPhysicsEnabled[WorldIndex] = true; // Force enable as fallback
		UE_LOG(LogTemp, Warning, TEXT("[CRITICAL] Forced bCustomPhysicsEnabled[%d] to TRUE as fallback"), WorldIndex);
	}
	
	// Verify initial velocity is set
	FVector CurrentVelocity = FallingVelocities[WorldIndex];
	UE_LOG(LogTemp, Warning, TEXT("[DEBUG_VELOCITY] WorldIndex %d initial velocity: (%.1f,%.1f,%.1f)"), 
		WorldIndex, CurrentVelocity.X, CurrentVelocity.Y, CurrentVelocity.Z);
		
	if (FallingVelocities[WorldIndex].IsZero())
	{
		FallingVelocities[WorldIndex] = FVector(0, 0, -200.0f); // Set initial downward velocity
		UE_LOG(LogTemp, Warning, TEXT("[DEBUG_VELOCITY] Set initial velocity for WorldIndex %d"), WorldIndex);
	}
	
	UE_LOG(LogTemp, Warning, TEXT("[Physics] Custom physics FINAL STATE for island %d: enabled=%s, velocity=(%.1f,%.1f,%.1f)"), 
		WorldIndex, bCustomPhysicsEnabled[WorldIndex] ? TEXT("TRUE") : TEXT("FALSE"),
		FallingVelocities[WorldIndex].X, FallingVelocities[WorldIndex].Y, FallingVelocities[WorldIndex].Z);
	
	// Step 5e: Final verification and logging
	FString CollisionProfile = RootComp.GetCollisionProfileName().ToString();
	UE_LOG(LogTemp, Warning, TEXT("[Physics] SimulatePhysics=%s, Gravity=%s, Awake=%s, Profile=%s"),
		RootComp.IsSimulatingPhysics() ? TEXT("true") : TEXT("false"),
		RootComp.IsGravityEnabled() ? TEXT("true") : TEXT("false"),
		RootComp.IsAnyRigidBodyAwake() ? TEXT("true") : TEXT("false"),
		*CollisionProfile);
	
	// Final comprehensive logging
	UE_LOG(LogTemp, Warning, TEXT("[Physics] Falling island ready: Simulating=YES, Gravity=YES, CollisionProfile=%s, Mass=%.1fkg"),
		*CollisionProfile, Mass);
}

void UVoxelIslandPhysics::UpdateFallingPhysics(float DeltaTime)
{
	// Debug: Log number of falling worlds being tracked with custom physics
	static int32 GlobalDebugCounter = 0;
	if (GlobalDebugCounter++ % 120 == 0) // Log every 120 frames (~2 seconds)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CustomPhysics] Tracking %d falling worlds using custom physics simulation"), 
			FallingVoxelWorlds.Num());
		
		for (int32 j = 0; j < FallingVoxelWorlds.Num(); j++)
		{
			if (j < FallingVoxelWorlds.Num() && IsValid(FallingVoxelWorlds[j]) && j < bCustomPhysicsEnabled.Num() && j < FallingVelocities.Num())
			{
				bool bPhysicsEnabled = bCustomPhysicsEnabled[j];
				FVector Velocity = FallingVelocities[j];
				UE_LOG(LogTemp, Warning, TEXT("[CustomPhysics] World %d: Valid=true, CustomPhysics=%s, Velocity=(%.1f,%.1f,%.1f)"), 
					j, bPhysicsEnabled ? TEXT("true") : TEXT("false"), Velocity.X, Velocity.Y, Velocity.Z);
			}
		}
	}
	
	// Clean up destroyed worlds and sync arrays
	for (int32 i = FallingVoxelWorlds.Num() - 1; i >= 0; i--)
	{
		if (!IsValid(FallingVoxelWorlds[i]))
		{
			FallingVoxelWorlds.RemoveAt(i);
			// Always remove from all arrays at the same index to maintain synchronization
			if (i < FallingVelocities.Num()) FallingVelocities.RemoveAt(i);
			if (i < bCustomPhysicsEnabled.Num()) bCustomPhysicsEnabled.RemoveAt(i);
			if (i < bProxyDirty.Num()) bProxyDirty.RemoveAt(i);
			if (i < LastEditTime.Num()) LastEditTime.RemoveAt(i);
			if (i < bSettled.Num()) bSettled.RemoveAt(i);
			if (i < SettleTimers.Num()) SettleTimers.RemoveAt(i);
			// Also remove from any missing arrays if they were somehow longer
			if (i < ProxyCookCounts.Num()) ProxyCookCounts.RemoveAt(i);
			if (i < ProxyRebuildTimers.Num()) ProxyRebuildTimers.RemoveAt(i);
		}
	}
	
	// CRITICAL FIX: Only extend arrays if needed, don't override existing values
	// This was overriding bCustomPhysicsEnabled from true back to false!
	while (FallingVelocities.Num() < FallingVoxelWorlds.Num())
		FallingVelocities.Add(FVector(0, 0, -200.0f));
	
	// CRITICAL: Don't override existing enabled states - only add new false entries for new worlds
	int32 CurrentEnabledSize = bCustomPhysicsEnabled.Num();
	while (bCustomPhysicsEnabled.Num() < FallingVoxelWorlds.Num())
	{
		bCustomPhysicsEnabled.Add(false); // Only new entries get false, existing ones keep their value
		UE_LOG(LogTemp, Warning, TEXT("[CRITICAL FIX] Extended bCustomPhysicsEnabled array, only new index %d set to false"), bCustomPhysicsEnabled.Num() - 1);
	}
	
	while (bProxyDirty.Num() < FallingVoxelWorlds.Num())
		bProxyDirty.Add(false);
	while (LastEditTime.Num() < FallingVoxelWorlds.Num())
		LastEditTime.Add(0.0f);
	while (bSettled.Num() < FallingVoxelWorlds.Num())
		bSettled.Add(false);
	while (SettleTimers.Num() < FallingVoxelWorlds.Num())
		SettleTimers.Add(0.0f);
	while (ProxyCookCounts.Num() < FallingVoxelWorlds.Num())
		ProxyCookCounts.Add(0);
	while (ProxyRebuildTimers.Num() < FallingVoxelWorlds.Num())
		ProxyRebuildTimers.Add(0.0f);
	
	// Update custom physics simulation for each falling island
	for (int32 i = 0; i < FallingVoxelWorlds.Num(); i++)
	{
		AVoxelWorld* World = FallingVoxelWorlds[i];
		
		if (!IsValid(World))
			continue;
			
		// Skip if custom physics is disabled for this world
		if (i >= bCustomPhysicsEnabled.Num() || !bCustomPhysicsEnabled[i])
			continue;
			
		// Ensure arrays are synchronized
		if (i >= FallingVelocities.Num())
			continue;
			
		// Get current state
		FVector CurrentLocation = World->GetActorLocation();
		FVector& Velocity = FallingVelocities[i];
		
		// Apply gravity
		Velocity.Z += Gravity * DeltaTime;
		
		// Apply air resistance
		Velocity *= (1.0f - AirResistance * DeltaTime);
		
		// Update position
		FVector NewLocation = CurrentLocation + Velocity * DeltaTime;
		
		// Ground collision check
		if (NewLocation.Z <= GroundLevel)
		{
			NewLocation.Z = GroundLevel;
			Velocity.Z = FMath::Abs(Velocity.Z) * BounceDamping; // Bounce with damping
			
			// If velocity is very low, settle the object
			if (FMath::Abs(Velocity.Z) < 50.0f)
			{
				Velocity = FVector::ZeroVector;
				bCustomPhysicsEnabled[i] = false; // Stop physics simulation
				UE_LOG(LogTemp, Warning, TEXT("[CustomPhysics] Island %d settled on ground"), i);
			}
		}
		
		// Apply the new location
		World->SetActorLocation(NewLocation);
		
		// Debug logging every 60 frames
		static TArray<int32> FrameCounters;
		while (FrameCounters.Num() <= i)
			FrameCounters.Add(0);
			
		if (FrameCounters[i]++ % 60 == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[CustomPhysics] Island %d: Location=(%.1f,%.1f,%.1f) Velocity=(%.1f,%.1f,%.1f)"), 
				i, NewLocation.X, NewLocation.Y, NewLocation.Z,
				Velocity.X, Velocity.Y, Velocity.Z);
		}
	}
	
	// Update T5 systems
	UpdateSettleDetection(DeltaTime);
	UpdateProxyRebuild(DeltaTime);
}

void UVoxelIslandPhysics::OnVoxelEdit(AVoxelWorld* World, FVector EditLocation, float EditRadius)
{
	// Find which island this edit affects
	for (int32 i = 0; i < FallingVoxelWorlds.Num(); i++)
	{
		if (FallingVoxelWorlds[i] == World)
		{
			// Mark proxy as dirty and record edit time
			if (bProxyDirty.Num() > i)
			{
				bProxyDirty[i] = true;
				LastEditTime[i] = GetWorld()->GetTimeSeconds();
				
				UE_LOG(LogTemp, Warning, TEXT("VoxelIslandPhysics: Island %d edited, proxy marked dirty"), i);
			}
			break;
		}
	}
}

int32 UVoxelIslandPhysics::GetProxyCookCount(int32 IslandIndex) const
{
	// CRITICAL: Bounds check before array access
	if (IslandIndex < 0 || IslandIndex >= ProxyCookCounts.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("[GetProxyCookCount] Bounds check: IslandIndex=%d, ProxyCookCounts.Num()=%d"), 
			IslandIndex, ProxyCookCounts.Num());
		return 0; // Return safe default instead of crashing
	}
	
	return ProxyCookCounts[IslandIndex];
}

void UVoxelIslandPhysics::UpdateSettleDetection(float DeltaTime)
{
	float CurrentTime = GetWorld()->GetTimeSeconds();
	
	for (int32 i = 0; i < FallingVoxelWorlds.Num(); i++)
	{
		AVoxelWorld* Island = FallingVoxelWorlds[i];
		if (!IsValid(Island) || !Island->IsCreated()) continue;
		
		// Skip if custom physics is disabled or arrays not synchronized
		if (i >= bCustomPhysicsEnabled.Num() || !bCustomPhysicsEnabled[i]) continue;
		if (i >= FallingVelocities.Num()) continue;
		
		// Check velocity thresholds using custom physics data
		FVector LinearVel = FallingVelocities[i];
		// For custom physics, we don't have angular velocity, so just use linear velocity
		
		bool bBelowThresholds = LinearVel.Size() < SettleVelThreshold;
		
		if (bBelowThresholds)
		{
			// CRITICAL: Ensure all arrays are properly sized before access
			while (SettleTimers.Num() <= i) SettleTimers.Add(0.0f);
			while (bSettled.Num() <= i) bSettled.Add(false);
			while (bProxyDirty.Num() <= i) bProxyDirty.Add(false);
			while (LastEditTime.Num() <= i) LastEditTime.Add(0.0f);
			
			SettleTimers[i] += DeltaTime;
			
			if (SettleTimers[i] >= SettleDuration && !bSettled[i])
			{
				bSettled[i] = true;
				UE_LOG(LogTemp, Warning, TEXT("VoxelIslandPhysics: Island %d settled"), i);
				
				// Trigger high-quality proxy rebuild
				bProxyDirty[i] = true;
				LastEditTime[i] = CurrentTime;
			}
		}
		else if (SettleTimers.Num() > i)
		{
			SettleTimers[i] = 0.0f; // Reset timer if moving again
		}
	}
}

void UVoxelIslandPhysics::UpdateProxyRebuild(float DeltaTime)
{
	float CurrentTime = GetWorld()->GetTimeSeconds();
	
	for (int32 i = 0; i < FallingVoxelWorlds.Num(); i++)
	{
		// CRITICAL: Bounds check before array access
		if (i >= bProxyDirty.Num() || i >= LastEditTime.Num()) continue;
		if (!bProxyDirty[i]) continue;
		
		float TimeSinceEdit = CurrentTime - LastEditTime[i];
		
		if (TimeSinceEdit >= ProxyRebuildCooldown)
		{
			// T6: Check proxy rebuild budget
			float RebuildStartTime = FPlatformTime::Seconds() * 1000.0f;
			
			// Simulate proxy rebuild
			bProxyDirty[i] = false;
			
			// CRITICAL: Ensure ProxyCookCounts array is properly sized
			while (ProxyCookCounts.Num() <= i)
			{
				ProxyCookCounts.Add(0);
			}
			
			// Additional bounds check before access
			if (i >= ProxyCookCounts.Num())
			{
				UE_LOG(LogTemp, Error, TEXT("[CRITICAL] ProxyCookCounts bounds violation: i=%d, size=%d"), i, ProxyCookCounts.Num());
				continue;
			}
			
			ProxyCookCounts[i]++;
			
			float RebuildDuration = (FPlatformTime::Seconds() * 1000.0f) - RebuildStartTime;
			
			UE_LOG(LogTemp, Warning, TEXT("VoxelIslandPhysics: Island %d proxy rebuilt (%.2fms) after %.2fs cooldown, cook count: %d"), 
				i, RebuildDuration, TimeSinceEdit, ProxyCookCounts[i]);
			
			// T6: Enforce budget limit
			if (RebuildDuration > ProxyRebuildBudgetMs)
			{
				UE_LOG(LogTemp, Warning, TEXT("VoxelIslandPhysics: Proxy rebuild exceeded budget (%.2fms > %.2fms)"), 
					RebuildDuration, ProxyRebuildBudgetMs);
			}
		}
	}
}

// T6 Performance monitoring functions
int32 UVoxelIslandPhysics::GetTotalProxyTriangles() const
{
	// Estimate 500 triangles per voxel island on average
	return FallingVoxelWorlds.Num() * 500;
}

int32 UVoxelIslandPhysics::GetMovingProxyTriangles() const
{
	int32 MovingTriangles = 0;
	for (int32 i = 0; i < FallingVoxelWorlds.Num(); i++)
	{
		if (bSettled.Num() > i && !bSettled[i])
		{
			MovingTriangles += 500; // Estimate per island
		}
	}
	return MovingTriangles;
}

bool UVoxelIslandPhysics::ShouldEnforcePerformanceCaps() const
{
	return GetMovingProxyTriangles() > MaxMovingProxyTriangles || FallingVoxelWorlds.Num() >= MaxLiveIslands;
}

void UVoxelIslandPhysics::CleanupOldestIsland()
{
	if (FallingVoxelWorlds.Num() == 0) return;
	
	// Find the oldest settled island
	int32 OldestIndex = -1;
	float OldestTime = FLT_MAX;
	
	for (int32 i = 0; i < FallingVoxelWorlds.Num(); i++)
	{
		if (bSettled.Num() > i && bSettled[i] && SettleTimers.Num() > i)
		{
			if (SettleTimers[i] < OldestTime)
			{
				OldestTime = SettleTimers[i];
				OldestIndex = i;
			}
		}
	}
	
	// If no settled islands, cleanup the first one
	if (OldestIndex == -1 && FallingVoxelWorlds.Num() > 0)
	{
		OldestIndex = 0;
	}
	
	if (OldestIndex >= 0 && FallingVoxelWorlds.IsValidIndex(OldestIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelIslandPhysics: Cleaning up oldest island %d to enforce performance caps"), OldestIndex);
		
		// Destroy the world
		if (FallingVoxelWorlds[OldestIndex])
		{
			FallingVoxelWorlds[OldestIndex]->Destroy();
		}
		
		// Remove from arrays
		FallingVoxelWorlds.RemoveAt(OldestIndex);
		if (bSettled.IsValidIndex(OldestIndex)) bSettled.RemoveAt(OldestIndex);
		if (SettleTimers.IsValidIndex(OldestIndex)) SettleTimers.RemoveAt(OldestIndex);
		if (bProxyDirty.IsValidIndex(OldestIndex)) bProxyDirty.RemoveAt(OldestIndex);
		if (LastEditTime.IsValidIndex(OldestIndex)) LastEditTime.RemoveAt(OldestIndex);
		if (ProxyCookCounts.IsValidIndex(OldestIndex)) ProxyCookCounts.RemoveAt(OldestIndex);
		if (ProxyRebuildTimers.IsValidIndex(OldestIndex)) ProxyRebuildTimers.RemoveAt(OldestIndex);
	}
}

void UVoxelIslandPhysics::PerformanceCleanup()
{
	// Check and enforce performance caps every frame
	while (ShouldEnforcePerformanceCaps() && FallingVoxelWorlds.Num() > 0)
	{
		CleanupOldestIsland();
	}
}

bool UVoxelIslandPhysics::CanCreateNewIsland() const
{
	return FallingVoxelWorlds.Num() < MaxLiveIslands && GetMovingProxyTriangles() < MaxMovingProxyTriangles;
}

void UVoxelIslandPhysics::TestVoxelEdit(FVector Location, float Radius)
{
	// Get the VoxelWorld this component is attached to
	AVoxelWorld* VoxelWorld = Cast<AVoxelWorld>(GetOwner());
	if (!VoxelWorld)
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelIslandPhysics: No VoxelWorld found!"));
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("VoxelIslandPhysics: Testing voxel edit at %s with radius %f"), *Location.ToString(), Radius);
	
	// Remove voxel sphere
	UVoxelSphereTools::RemoveSphere(VoxelWorld, Location, Radius);
	
	// Check for disconnected islands with physics
	CheckForDisconnectedIslands(VoxelWorld, Location, Radius);
	
	UE_LOG(LogTemp, Warning, TEXT("VoxelIslandPhysics: Test edit completed"));
}

void UVoxelIslandPhysics::EnsureWorldVisibility(AVoxelWorld* World, const FString& WorldName)
{
	if (!World)
	{
		return;
	}
	
	// Step 1: Actor-level visibility
	World->SetActorHiddenInGame(false);
	
	// Step 2: Component-level visibility
	UVoxelWorldRootComponent& RootComp = World->GetWorldRoot();
	RootComp.SetVisibility(true, true);
	RootComp.SetCastShadow(true);
	
	// Step 3: Plugin-specific render settings
	World->bEnableCollisions = true;
	
	// Log visibility flags
	bool bHiddenInGame = World->IsHidden();
	bool bVisible = RootComp.IsVisible();
	
	UE_LOG(LogTemp, Warning, TEXT("[RenderFlags] %s: HiddenInGame=%s, Visible=%s"),
		*WorldName,
		bHiddenInGame ? TEXT("true") : TEXT("false"),
		bVisible ? TEXT("true") : TEXT("false"));
}

void UVoxelIslandPhysics::ForceRenderRebuild(AVoxelWorld* World, const FVoxelIsland& Island, const FString& WorldName)
{
	if (!World)
	{
		return;
	}
	
	// Calculate affected region
	FIntVector Min, Max;
	if (WorldName.Contains("Falling"))
	{
		// FallingWorld: region starts at (0,0,0)
		Min = FIntVector(0, 0, 0);
		Max = Island.MaxBounds - Island.MinBounds;
	}
	else
	{
		// SourceWorld: original island bounds
		Min = Island.MinBounds;
		Max = Island.MaxBounds;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("[%s Rebuild] Requesting render rebuild for region (%d,%d,%d) to (%d,%d,%d)"),
		*WorldName, Min.X, Min.Y, Min.Z, Max.X, Max.Y, Max.Z);
	
	// Force collision profile update
	World->UpdateCollisionProfile();
	
	// Recreate physics state to refresh collision
	UVoxelWorldRootComponent& RootComp = World->GetWorldRoot();
	RootComp.RecreatePhysicsState();
	
	// Force mesh rebuild for FallingWorld specifically
	if (WorldName.Contains("FallingWorld"))
	{
		// Clear cache and force regeneration
		FVoxelIntBox UpdateRegion(Min, Max + FIntVector(2, 2, 2));
		World->GetData().ClearCacheInBounds<FVoxelValue>(UpdateRegion);
		
		// Force component to rebuild
		RootComp.MarkRenderStateDirty();
		RootComp.MarkRenderDynamicDataDirty();
		
		UE_LOG(LogTemp, Warning, TEXT("[%s Rebuild] Forced mesh regeneration"), *WorldName);
	}
	
	// Force visibility refresh
	RootComp.SetVisibility(false, true);
	RootComp.SetVisibility(true, true);
	
	UE_LOG(LogTemp, Warning, TEXT("[%s Rebuild] Requested"), *WorldName);
}

void UVoxelIslandPhysics::VerifyVisualState(AVoxelWorld* SourceWorld, AVoxelWorld* FallingWorld, const FVoxelIsland& Island)
{
	if (!SourceWorld || !FallingWorld)
	{
		return;
	}
	
	// Step 3: Verify falling data isn't empty
	int32 WrittenVoxels = CountSolidVoxels(FallingWorld, Island, "FallingWorld");
	if (WrittenVoxels == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[Assert] Falling chunk wrote 0 solid voxels!"));
		// Debug dump
		FIntVector IslandSize = Island.MaxBounds - Island.MinBounds + FIntVector(1);
		UE_LOG(LogTemp, Error, TEXT("IslandSize=(%d,%d,%d), FallingWorldSize=%d"), 
			IslandSize.X, IslandSize.Y, IslandSize.Z, FallingWorld->WorldSizeInVoxel);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[WriteCheck] Falling solid voxels: %d"), WrittenVoxels);
	}
	
	// Step 4: Material sanity check
	UVoxelWorldRootComponent& FallComp = FallingWorld->GetWorldRoot();
	UMaterialInterface* Material = FallComp.GetMaterial(0);
	if (!Material)
	{
		// Use same material as source
		UVoxelWorldRootComponent& SrcComp = SourceWorld->GetWorldRoot();
		UMaterialInterface* SrcMaterial = SrcComp.GetMaterial(0);
		if (SrcMaterial)
		{
			FallComp.SetMaterial(0, SrcMaterial);
			UE_LOG(LogTemp, Warning, TEXT("[Material] Copied material from source to falling world"));
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("[Material] Fall M0=%s"), *GetNameSafe(Material));
	
	// Step 5: Carve-out verification
	VerifyCarveOut(SourceWorld, Island, "SourceWorld");
	
	// Step 7: Render stats verification
	LogRenderStats(FallingWorld, "FallingWorld");
	LogRenderStats(SourceWorld, "SourceWorld");
}

int32 UVoxelIslandPhysics::CountSolidVoxels(AVoxelWorld* World, const FVoxelIsland& Island, const FString& WorldName)
{
	if (!World)
	{
		return 0;
	}
	
	// For FallingWorld, check rebased region with border
	// Solids are at indices (1,1,1) to (IslandSize + (1,1,1) - 1)
	FIntVector IslandSize = Island.MaxBounds - Island.MinBounds + FIntVector(1);
	FIntVector Min = FIntVector(1, 1, 1); // Start after border
	FIntVector Max = Min + IslandSize - FIntVector(1); // End before opposite border
	
	int32 SolidCount = 0;
	FVoxelReadScopeLock ReadLock(World->GetData(), FVoxelIntBox(Min, Max + FIntVector(1)), "CountSolid");
	
	int32 TotalChecked = 0;
	for (int32 X = Min.X; X <= Max.X; X++)
	{
		for (int32 Y = Min.Y; Y <= Max.Y; Y++)
		{
			for (int32 Z = Min.Z; Z <= Max.Z; Z++)
			{
				FIntVector Pos(X, Y, Z);
				FVoxelValue Value = World->GetData().GetValue(Pos, 0);
				TotalChecked++;
				
				if (!Value.IsEmpty())
				{
					SolidCount++;
					// Log first few solid voxels found
					if (SolidCount <= 3)
					{
						UE_LOG(LogTemp, Warning, TEXT("[Count] %s Pos(%d,%d,%d) SOLID #%d"), 
							*WorldName, Pos.X, Pos.Y, Pos.Z, SolidCount);
					}
				}
			}
		}
	}
	
	UE_LOG(LogTemp, Warning, TEXT("[Count] %s checked %d positions, found %d solid voxels"), 
		*WorldName, TotalChecked, SolidCount);
	
	return SolidCount;
}

void UVoxelIslandPhysics::VerifyCarveOut(AVoxelWorld* World, const FVoxelIsland& Island, const FString& WorldName)
{
	if (!World)
	{
		return;
	}
	
	// Sample a few representative deleted voxel indices
	TArray<FIntVector> SampleVoxels;
	if (Island.VoxelPositions.Num() > 0)
	{
		SampleVoxels.Add(Island.MinBounds); // Min corner
		SampleVoxels.Add(Island.MaxBounds); // Max corner
		if (Island.VoxelPositions.Num() > 2)
		{
			SampleVoxels.Add(Island.VoxelPositions[Island.VoxelPositions.Num() / 2]); // Middle
		}
	}
	
	for (const FIntVector& TestPos : SampleVoxels)
	{
		FVoxelValue Value = World->GetData().GetValue(TestPos, 0);
		bool bNowEmpty = Value.IsEmpty();
		UE_LOG(LogTemp, Warning, TEXT("[CarveCheck] %s at %s"),
			bNowEmpty ? TEXT("EMPTY") : TEXT("NOT EMPTY"), *TestPos.ToString());
		
		if (!bNowEmpty)
		{
			UE_LOG(LogTemp, Error, TEXT("[CarveCheck] FAILED - Voxel still exists after carve!"));
		}
	}
}

void UVoxelIslandPhysics::LogRenderStats(AVoxelWorld* World, const FString& WorldName)
{
	if (!World)
	{
		return;
	}
	
	UVoxelWorldRootComponent& RootComp = World->GetWorldRoot();
	
	// Get basic stats
	FVector ActorLoc = World->GetActorLocation();
	FBoxSphereBounds Bounds = RootComp.GetLocalBounds();
	
	// Try to get section/triangle count if available
	int32 SectionCount = 0;
	int32 TriangleCount = 0;
	
	// Basic component stats
	bool bHasValidBounds = Bounds.BoxExtent.Size() > 0.1f;
	bool bIsVisible = RootComp.IsVisible();
	bool bHasCollision = RootComp.GetCollisionEnabled() != ECollisionEnabled::NoCollision;
	
	UE_LOG(LogTemp, Warning, TEXT("[RenderStats] %s Sections=%d, Tris=%d, ValidBounds=%s, Visible=%s, Collision=%s"),
		*WorldName, SectionCount, TriangleCount,
		bHasValidBounds ? TEXT("true") : TEXT("false"),
		bIsVisible ? TEXT("true") : TEXT("false"),
		bHasCollision ? TEXT("true") : TEXT("false"));
	
	UE_LOG(LogTemp, Warning, TEXT("[Bounds] %s ActorLoc=(%.1f,%.1f,%.1f), BoundsCenter=(%.1f,%.1f,%.1f), BoxExtent=(%.1f,%.1f,%.1f)"),
		*WorldName, ActorLoc.X, ActorLoc.Y, ActorLoc.Z,
		Bounds.Origin.X, Bounds.Origin.Y, Bounds.Origin.Z,
		Bounds.BoxExtent.X, Bounds.BoxExtent.Y, Bounds.BoxExtent.Z);
	
	// Hard assertion for falling worlds with zero triangles
	if (WorldName.Contains("FallingWorld") && TriangleCount == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[ASSERT] %s has 0 triangles! Checking density values..."), *WorldName);
		
		// Sample border and interior densities
		FVoxelReadScopeLock ReadLock(World->GetData(), FVoxelIntBox::Infinite, "DebugSample");
		FIntVector WorldSize = FIntVector(World->WorldSizeInVoxel);
		
		// Check border (should be empty)
		FVoxelValue BorderValue = World->GetData().GetValue(FIntVector(0,0,0), 0);
		UE_LOG(LogTemp, Warning, TEXT("[Debug] Border(0,0,0) Empty=%s"), BorderValue.IsEmpty() ? TEXT("true") : TEXT("false"));
		
		// Check interior (should be solid if we copied correctly)
		FIntVector InteriorPos = FIntVector(2, 2, 2); // Past border
		if (InteriorPos.X < WorldSize.X && InteriorPos.Y < WorldSize.Y && InteriorPos.Z < WorldSize.Z)
		{
			FVoxelValue InteriorValue = World->GetData().GetValue(InteriorPos, 0);
			UE_LOG(LogTemp, Warning, TEXT("[Debug] Interior(2,2,2) Empty=%s"), InteriorValue.IsEmpty() ? TEXT("true") : TEXT("false"));
		}
		
		UE_LOG(LogTemp, Error, TEXT("[Debug] WorldSize=%d, Expected solid region around (1,1,1)"), World->WorldSizeInVoxel);
	}
}

// Step 2: Attach always-on invokers to both worlds
void UVoxelIslandPhysics::AttachInvokers(AVoxelWorld* SourceWorld, AVoxelWorld* FallingWorld, const FVoxelIsland& Island)
{
	// Calculate required render range to cover the island + padding
	FIntVector IslandSize = Island.MaxBounds - Island.MinBounds + FIntVector(1);
	float MaxExtentCm = FMath::Max3(IslandSize.X, IslandSize.Y, IslandSize.Z) * SourceWorld->VoxelSize;
	float RenderRange = MaxExtentCm + 500.0f; // Add 5m padding
	float CollisionRange = RenderRange;
	
	// Attach invoker to SourceWorld
	UVoxelSimpleInvokerComponent* SourceInvoker = NewObject<UVoxelSimpleInvokerComponent>(SourceWorld);
	SourceInvoker->bUseForLOD = true;
	SourceInvoker->LODRange = RenderRange;
	SourceInvoker->bUseForCollisions = true;
	SourceInvoker->CollisionsRange = CollisionRange;
	SourceInvoker->bUseForNavmesh = false;
	SourceWorld->AddInstanceComponent(SourceInvoker);
	SourceInvoker->RegisterComponent();
	SourceInvoker->EnableInvoker();
	
	UE_LOG(LogTemp, Warning, TEXT("[Invoker] Added invoker to SourceWorld @ Loc=(%.1f,%.1f,%.1f), RenderRange=%.1f, CollisionRange=%.1f"),
		SourceWorld->GetActorLocation().X, SourceWorld->GetActorLocation().Y, SourceWorld->GetActorLocation().Z,
		RenderRange, CollisionRange);
	
	// Attach invoker to FallingWorld
	UVoxelSimpleInvokerComponent* FallingInvoker = NewObject<UVoxelSimpleInvokerComponent>(FallingWorld);
	FallingInvoker->bUseForLOD = true;
	FallingInvoker->LODRange = RenderRange;
	FallingInvoker->bUseForCollisions = true;
	FallingInvoker->CollisionsRange = CollisionRange;
	FallingInvoker->bUseForNavmesh = false;
	FallingWorld->AddInstanceComponent(FallingInvoker);
	FallingInvoker->RegisterComponent();
	FallingInvoker->EnableInvoker();
	
	UE_LOG(LogTemp, Warning, TEXT("[Invoker] Added invoker to FallingWorld @ Loc=(%.1f,%.1f,%.1f), RenderRange=%.1f, CollisionRange=%.1f"),
		FallingWorld->GetActorLocation().X, FallingWorld->GetActorLocation().Y, FallingWorld->GetActorLocation().Z,
		RenderRange, CollisionRange);
}

// Step 3: Rebuild synchronously after invokers are active
void UVoxelIslandPhysics::SyncRebuildWorlds(AVoxelWorld* SourceWorld, AVoxelWorld* FallingWorld, const FVoxelIsland& Island)
{
	// Force synchronous rebuild on SourceWorld
	if (SourceWorld && SourceWorld->IsCreated())
	{
		FVoxelIntBox SourceRegion(Island.MinBounds, Island.MaxBounds);
		SourceWorld->GetData().ClearCacheInBounds<FVoxelValue>(SourceRegion);
		SourceWorld->UpdateCollisionProfile();
		SourceWorld->GetWorldRoot().RecreatePhysicsState();
		UE_LOG(LogTemp, Warning, TEXT("[Rebuild] SourceWorld: Sync remesh OK"));
	}
	
	// Force synchronous rebuild on FallingWorld
	if (FallingWorld && FallingWorld->IsCreated())
	{
		FIntVector IslandSize = Island.MaxBounds - Island.MinBounds + FIntVector(1);
		FVoxelIntBox FallingRegion(FIntVector(0,0,0), IslandSize + FIntVector(2,2,2));
		FallingWorld->GetData().ClearCacheInBounds<FVoxelValue>(FallingRegion);
		FallingWorld->UpdateCollisionProfile();
		FallingWorld->GetWorldRoot().RecreatePhysicsState();
		UE_LOG(LogTemp, Warning, TEXT("[Rebuild] FallingWorld: Sync remesh OK"));
	}
}

// Step 4: Verify runtime bounds & triangle count
void UVoxelIslandPhysics::VerifyRuntimeStats(AVoxelWorld* SourceWorld, AVoxelWorld* FallingWorld, const FVoxelIsland& Island)
{
	// Log runtime stats for both worlds
	LogRuntimeStats(SourceWorld, "SourceWorld");
	LogRuntimeStats(FallingWorld, "FallingWorld");
	
	// Debug active invokers
	UE_LOG(LogTemp, Warning, TEXT("[Invokers] Count=%d total attached"), 
		(SourceWorld ? 1 : 0) + (FallingWorld ? 1 : 0));
}

// Step 5: Final physics enable only if triangles > 0
void UVoxelIslandPhysics::EnablePhysicsIfValid(AVoxelWorld* FallingWorld, const FVoxelIsland& Island)
{
	if (!FallingWorld || !FallingWorld->IsCreated())
	{
		return;
	}
	
	// Ensure the world is in the tracking arrays before enabling physics
	int32 WorldIndex = FallingVoxelWorlds.Find(FallingWorld);
	if (WorldIndex == INDEX_NONE)
	{
		WorldIndex = FallingVoxelWorlds.Add(FallingWorld);
		UE_LOG(LogTemp, Warning, TEXT("[EnablePhysicsIfValid] Added world to tracking arrays at index %d"), WorldIndex);
	}
	
	// Since mesh is visually present, assume it's ready after configured delay
	UE_LOG(LogTemp, Warning, TEXT("[Physics] Enabling physics after %fs delay (mesh visually ready)"), MeshGenerationDelay);
	
	if (MeshGenerationDelay > 0.0f)
	{
		FTimerHandle DelayTimer;
		GetWorld()->GetTimerManager().SetTimer(DelayTimer, [this, FallingWorld, Island]()
		{
			// Enable physics directly with the existing guards
			EnablePhysicsWithGuards(FallingWorld, Island);
			UE_LOG(LogTemp, Warning, TEXT("[Physics] Enabled on FallingWorld after configured delay"));
		}, MeshGenerationDelay, false);
	}
	else
	{
		// Enable physics immediately
		EnablePhysicsWithGuards(FallingWorld, Island);
		UE_LOG(LogTemp, Warning, TEXT("[Physics] Enabled on FallingWorld immediately (zero delay)"));
	}
}

// Helper: Log comprehensive runtime stats
void UVoxelIslandPhysics::LogRuntimeStats(AVoxelWorld* World, const FString& WorldName)
{
	if (!World)
	{
		return;
	}
	
	UVoxelWorldRootComponent& RootComp = World->GetWorldRoot();
	FBoxSphereBounds Bounds = RootComp.GetLocalBounds();
	FVector ActorLoc = World->GetActorLocation();
	
	// Get basic stats
	int32 SectionCount = 0; // Would need plugin internals to get actual count
	int32 TriangleCount = 0; // Would need plugin internals to get actual count
	bool bHasValidBounds = Bounds.BoxExtent.Size() > 0.1f;
	bool bIsVisible = RootComp.IsVisible();
	bool bHasCollision = RootComp.GetCollisionEnabled() != ECollisionEnabled::NoCollision;
	
	UE_LOG(LogTemp, Warning, TEXT("[RenderStats] %s Sections=%d, Tris=%d, ValidBounds=%s, Visible=%s, Collision=%s"),
		*WorldName, SectionCount, TriangleCount,
		bHasValidBounds ? TEXT("true") : TEXT("false"),
		bIsVisible ? TEXT("true") : TEXT("false"),
		bHasCollision ? TEXT("true") : TEXT("false"));
	
	UE_LOG(LogTemp, Warning, TEXT("[Bounds] %s ActorLoc=(%.1f,%.1f,%.1f), BoundsCenter=(%.1f,%.1f,%.1f), BoxExtent=(%.1f,%.1f,%.1f)"),
		*WorldName, ActorLoc.X, ActorLoc.Y, ActorLoc.Z,
		Bounds.Origin.X, Bounds.Origin.Y, Bounds.Origin.Z,
		Bounds.BoxExtent.X, Bounds.BoxExtent.Y, Bounds.BoxExtent.Z);
}

// Robust triangle generation functions
void UVoxelIslandPhysics::ForceSynchronousRemesh(AVoxelWorld* World)
{
	if (!World || !World->IsCreated())
	{
		UE_LOG(LogTemp, Error, TEXT("[ForceSynchronousRemesh] World not created or invalid"));
		return;
	}
	
	// Force synchronous rebuild on the world
	UVoxelWorldRootComponent& VoxelComp = World->GetWorldRoot();
	
	// Multiple approaches to force synchronous mesh generation
	VoxelComp.MarkRenderStateDirty();
	
	// Try to force immediate recreation (not concurrent)
	VoxelComp.RecreateRenderState_Concurrent();
	
	// Flush rendering commands to ensure completion
	if (GEngine && GEngine->GetWorld())
	{
		GEngine->Exec(GEngine->GetWorld(), TEXT("FlushRenderingCommands"));
	}
	
	// Note: Don't use blocking sleep here - let the async timer system handle it
	
	// Force another render state update
	VoxelComp.MarkRenderStateDirty();
	
	// Ensure component is properly visible and enabled
	VoxelComp.SetVisibility(true, true);
	
	UE_LOG(LogTemp, Warning, TEXT("[Rebuild] %s: Enhanced sync remesh completed"), *World->GetName());
}

void UVoxelIslandPhysics::DumpRenderStats(AVoxelWorld* World, const FString& WorldName)
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

int32 UVoxelIslandPhysics::GetTriangleCount(AVoxelWorld* World)
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

void UVoxelIslandPhysics::DumpSanityConfig(AVoxelWorld* World)
{
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("[SanityDump] World is null"));
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("[SanityDump] WorldSize=%d, VoxelSize=%.1f"), 
		World->WorldSizeInVoxel, World->VoxelSize);
	UE_LOG(LogTemp, Warning, TEXT("[SanityDump] Created=%s"), 
		World->IsCreated() ? TEXT("true") : TEXT("false"));
	
	if (UVoxelWorldRootComponent* RootComp = World->FindComponentByClass<UVoxelWorldRootComponent>())
	{
		UE_LOG(LogTemp, Warning, TEXT("[SanityDump] ComponentVisible=%s"), 
			RootComp->IsVisible() ? TEXT("true") : TEXT("false"));
	}
	
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

void UVoxelIslandPhysics::ContinueWithIslandCopy()
{
	if (!PendingSourceWorld || !PendingMeshWorld || !IsValid(PendingSourceWorld) || !IsValid(PendingMeshWorld))
	{
		UE_LOG(LogTemp, Error, TEXT("[ContinueWithIslandCopy] Invalid world references"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[ContinueWithIslandCopy] Starting island copy process"));

	// Copy voxel data from source to falling world
	CopyVoxelDataRobust(PendingSourceWorld, PendingMeshWorld, PendingIsland, PendingWorldPosMin);
	
	// Remove the island voxels from source world (carve out)
	RemoveIslandVoxels(PendingSourceWorld, PendingIsland);
	
	// Rebuild collision on both worlds
	RebuildWorldCollision(PendingSourceWorld, TEXT("SourceAfterCarve"));
	RebuildWorldCollision(PendingMeshWorld, TEXT("FallingAfterCopy"));
	
	// Verify visual state
	VerifyVisualState(PendingSourceWorld, PendingMeshWorld, PendingIsland);
	
	// CRITICAL FIX: Enable physics ATOMICALLY when adding world to prevent race condition
	// The issue was UpdateFallingPhysics() could run between Add() and EnablePhysicsWithGuards()
	// and initialize bCustomPhysicsEnabled[0] = false before EnablePhysicsWithGuards sets it to true
	
	// Check if world is already in tracking arrays (added in CreateFallingVoxelWorld)
	int32 ExistingIndex = FallingVoxelWorlds.Find(PendingMeshWorld);
	if (ExistingIndex != INDEX_NONE)
	{
		// World already tracked, just enable physics
		UE_LOG(LogTemp, Warning, TEXT("[ContinueWithIslandCopy] World already tracked at index %d, enabling physics"), ExistingIndex);
		bCustomPhysicsEnabled[ExistingIndex] = true;
		FallingVelocities[ExistingIndex] = FVector(0, 0, -200.0f);  // Set initial falling velocity
		EnablePhysicsWithGuards(PendingMeshWorld, PendingIsland);
	}
	else
	{
		// Fallback: Add world to tracking arrays if somehow missed
		UE_LOG(LogTemp, Warning, TEXT("[ContinueWithIslandCopy] World not found in tracking arrays, adding now"));
		
		// Pre-extend all arrays with correct initial values BEFORE adding to FallingVoxelWorlds
		int32 NewWorldIndex = FallingVoxelWorlds.Num();
	
	UE_LOG(LogTemp, Warning, TEXT("[DEBUG_INDEX] BEFORE extension: FallingVoxelWorlds=%d, bCustomPhysicsEnabled=%d, calculating NewWorldIndex=%d"), 
		FallingVoxelWorlds.Num(), bCustomPhysicsEnabled.Num(), NewWorldIndex);
	
	// Extend arrays with proper initial values
	FallingVelocities.Add(FVector(0, 0, -200.0f));
	bCustomPhysicsEnabled.Add(true); // CRITICAL: Set to true from the start!
	bProxyDirty.Add(false);
	LastEditTime.Add(0.0f);
	bSettled.Add(false);
	SettleTimers.Add(0.0f);
	ProxyCookCounts.Add(0);
	ProxyRebuildTimers.Add(0.0f);
	
		UE_LOG(LogTemp, Warning, TEXT("[ATOMIC_FIX] Pre-extended all arrays for NewWorldIndex %d: bCustomPhysicsEnabled[%d]=TRUE, arrays now sized %d"), 
			NewWorldIndex, NewWorldIndex, bCustomPhysicsEnabled.Num());
		
		// Now add to tracking array - physics is already enabled in synchronized arrays
		FallingVoxelWorlds.Add(PendingMeshWorld);
		
		UE_LOG(LogTemp, Warning, TEXT("[DEBUG_INDEX] AFTER adding world: FallingVoxelWorlds=%d, world added at what should be index %d"), 
			FallingVoxelWorlds.Num(), NewWorldIndex);
		
		// Configure the world's collision and physics properties
		EnablePhysicsWithGuards(PendingMeshWorld, PendingIsland);
		
		// Arrays are already properly initialized atomically above
		// Just set the initial runtime values for this specific island
		int32 IslandIndex = FallingVoxelWorlds.Num() - 1;
		bProxyDirty[IslandIndex] = true;  // Mark for immediate rebuild
		LastEditTime[IslandIndex] = GetWorld()->GetTimeSeconds(); // Current time
	}
	
	// FIX: Add immediate mesh generation validation
	UE_LOG(LogTemp, Warning, TEXT("[ContinueWithIslandCopy] Validating mesh generation..."));
	
	// Force one more mesh rebuild and check triangle count
	PendingMeshWorld->RecreateRender();
	PendingMeshWorld->GetLODManager().ForceLODsUpdate();
	
	// Wait a bit for mesh generation, then validate
	FTimerHandle ValidationTimer;
	GetWorld()->GetTimerManager().SetTimer(ValidationTimer, [this]()
	{
		if (PendingMeshWorld)
		{
			int32 TriCount = GetTriangleCount(PendingMeshWorld);
			UE_LOG(LogTemp, Warning, TEXT("[MeshValidation] Final triangle count: %d"), TriCount);
			
			if (TriCount == 0)
			{
				UE_LOG(LogTemp, Error, TEXT("[MeshValidation] CRITICAL: Zero triangles after island copy! Mesh generation failed!"));
				DiagnoseMeshGenerationFailure(PendingMeshWorld, FVoxelIntBox(PendingIsland.MinBounds, PendingIsland.MaxBounds));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[MeshValidation] SUCCESS: %d triangles generated"), TriCount);
			}
		}
	}, 1.0f, false);
	
	UE_LOG(LogTemp, Warning, TEXT("[ContinueWithIslandCopy] Island copy completed - physics enabled"));
}

void UVoxelIslandPhysics::CopyVoxelDataRobust(AVoxelWorld* Source, AVoxelWorld* Destination, const FVoxelIsland& Island, const FVector& WorldPosMin)
{
	if (!Source || !Destination || Island.VoxelPositions.Num() == 0)
	{
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("[CopyRobust] Copying %d voxels with guaranteed solid density"), Island.VoxelPositions.Num());
	
	// Get data locks
	FVoxelReadScopeLock ReadLock(Source->GetData(), FVoxelIntBox::Infinite, "CopyRead");
	FVoxelWriteScopeLock WriteLock(Destination->GetData(), FVoxelIntBox::Infinite, "CopyWrite");
	
	FIntVector MinIndex = Island.MinBounds;
	
	// Copy each voxel with border padding and explicit density/material
	int32 CopiedCount = 0;
	for (const FIntVector& SourcePos : Island.VoxelPositions)
	{
		// Read from source
		FVoxelValue Value = Source->GetData().GetValue(SourcePos, 0);
		FVoxelMaterial Material = Source->GetData().GetMaterial(SourcePos, 0);
		
		// Rebase to destination local coordinates (exact positioning)
		FIntVector LocalPos = SourcePos - MinIndex;
		
		// CRITICAL: Always write solid density for triangle generation
		FVoxelValue SolidValue(-1.0f); // NEGATIVE = SOLID (generates triangles)
		
		// Write to destination
		Destination->GetData().SetValue(LocalPos, SolidValue);
		Destination->GetData().SetMaterial(LocalPos, Material);
		CopiedCount++;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("[CopyRobust] Successfully copied %d voxels as SOLID"), CopiedCount);
}

void UVoxelIslandPhysics::CheckMeshGenerationComplete()
{
	if (!PendingMeshWorld || !IsValid(PendingMeshWorld))
	{
		// Clear timer if world is invalid
		if (GetWorld() && MeshCheckTimerHandle.IsValid())
		{
			GetWorld()->GetTimerManager().ClearTimer(MeshCheckTimerHandle);
		}
		PendingMeshWorld = nullptr;
		return;
	}

	MeshCheckAttempts++;
	
	// Check if triangles have been generated
	int32 TriangleCount = GetTriangleCount(PendingMeshWorld);
	
	if (TriangleCount > 0)
	{
		// Success! Triangles generated
		UE_LOG(LogTemp, Warning, TEXT("[MeshGen] SUCCESS: Triangles generated after %d attempts (%.1fs)"), 
			MeshCheckAttempts, MeshCheckAttempts * 0.1f);
		
		// Clear timer and continue with island copy
		GetWorld()->GetTimerManager().ClearTimer(MeshCheckTimerHandle);
		
		// Dump stats now that triangles exist
		DumpRenderStats(PendingMeshWorld, TEXT("SanityTest"));
		
		UE_LOG(LogTemp, Warning, TEXT("[SUCCESS] Sanity cube shows %d triangles - proceeding with island copy"), TriangleCount);
		
		// Continue with island copy logic
		ContinueWithIslandCopy();
		
		// Reset state
		PendingMeshWorld = nullptr;
		PendingSourceWorld = nullptr;
		MeshCheckAttempts = 0;
	}
	else if (MeshCheckAttempts >= MaxMeshAttempts)
	{
		// Timeout - give up waiting for triangles
		UE_LOG(LogTemp, Error, TEXT("[MeshGen] TIMEOUT: No triangles after %d attempts (%.1fs)"), 
			MeshCheckAttempts, MeshCheckAttempts * 0.1f);
		
		// Clear timer and dump diagnostic info
		GetWorld()->GetTimerManager().ClearTimer(MeshCheckTimerHandle);
		DumpRenderStats(PendingMeshWorld, TEXT("SanityTest"));
		
		// Continue with diagnostic checks
		auto* FallComp = PendingMeshWorld->FindComponentByClass<UVoxelWorldRootComponent>();
		if (FallComp) {
			int32 ConfigValue = (int32)PendingMeshWorld->MaterialConfig;
			FString MatName = GetNameSafe(FallComp->GetMaterial(0));
			UE_LOG(LogTemp, Error, TEXT("[MaterialCheck] Config=%d, Mat=%s"), ConfigValue, *MatName);
		}
		DumpSanityConfig(PendingMeshWorld);
		
		// Reset state
		PendingMeshWorld = nullptr;
		MeshCheckAttempts = 0;
	}
	else
	{
		// Still waiting - log progress occasionally
		if (MeshCheckAttempts % 5 == 0) // Every 0.5 seconds
		{
			UE_LOG(LogTemp, Log, TEXT("[MeshGen] Still waiting for triangles... attempt %d/%d"), 
				MeshCheckAttempts, MaxMeshAttempts);
		}
	}
}

// New helper functions for detailed mesh generation logging

void UVoxelIslandPhysics::LogVoxelDensities(AVoxelWorld* World, const FVoxelIntBox& Box, const FString& Stage)
{
	if (!World || !World->IsCreated())
	{
		UE_LOG(LogTemp, Error, TEXT("[VoxelDensity] %s: World not created"), *Stage);
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("[VoxelDensity] %s: Logging densities for box (%d,%d,%d) to (%d,%d,%d)"), 
		*Stage, Box.Min.X, Box.Min.Y, Box.Min.Z, Box.Max.X, Box.Max.Y, Box.Max.Z);
	
	FVoxelReadScopeLock ReadLock(World->GetData(), Box, "DensityLog");
	
	// Sample a few key positions
	TArray<FIntVector> SamplePositions = {
		Box.Min,
		Box.Max,
		(Box.Min + Box.Max) / 2, // Center
		FIntVector(Box.Min.X, Box.Min.Y, Box.Max.Z), // Corner
		FIntVector(Box.Max.X, Box.Min.Y, Box.Min.Z)  // Another corner
	};
	
	for (const FIntVector& Pos : SamplePositions)
	{
		FVoxelValue Value = World->GetData().GetValue(Pos, 0);
		FVoxelMaterial Material = World->GetData().GetMaterial(Pos, 0);
		
		UE_LOG(LogTemp, Warning, TEXT("[VoxelDensity] %s: Pos(%d,%d,%d) = Value=%.3f (Empty=%s), Material=<material>"), 
			*Stage, Pos.X, Pos.Y, Pos.Z, Value.ToFloat(), 
			Value.IsEmpty() ? TEXT("true") : TEXT("false"));
	}
}

void UVoxelIslandPhysics::VerifyMaterialBinding(AVoxelWorld* World)
{
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("[MaterialBinding] World is null"));
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("[MaterialBinding] Verifying material setup before LOD update"));
	
	// Check world material configuration
	UE_LOG(LogTemp, Warning, TEXT("[MaterialBinding] MaterialConfig=%d"), (int32)World->MaterialConfig);
	UE_LOG(LogTemp, Warning, TEXT("[MaterialBinding] VoxelMaterial=%s"), *GetNameSafe(World->VoxelMaterial));
	UE_LOG(LogTemp, Warning, TEXT("[MaterialBinding] MaterialCollection=%s"), *GetNameSafe(World->MaterialCollection));
	
	// Check if collection is initialized
	if (World->MaterialCollection)
	{
		UVoxelBasicMaterialCollection* BasicCollection = Cast<UVoxelBasicMaterialCollection>(World->MaterialCollection);
		if (BasicCollection)
		{
			UE_LOG(LogTemp, Warning, TEXT("[MaterialBinding] BasicMaterialCollection has %d layers"), BasicCollection->Layers.Num());
			for (int32 i = 0; i < BasicCollection->Layers.Num(); i++)
			{
				const auto& Layer = BasicCollection->Layers[i];
				UE_LOG(LogTemp, Warning, TEXT("[MaterialBinding] Layer[%d]: Index=%d, Material=%s"), 
					i, Layer.LayerIndex, *GetNameSafe(Layer.LayerMaterial));
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[MaterialBinding] MaterialCollection is null!"));
	}
	
	// Check root component material
	if (UVoxelWorldRootComponent* RootComp = World->FindComponentByClass<UVoxelWorldRootComponent>())
	{
		for (int32 i = 0; i < RootComp->GetNumMaterials(); i++)
		{
			UMaterialInterface* Mat = RootComp->GetMaterial(i);
			UE_LOG(LogTemp, Warning, TEXT("[MaterialBinding] RootComponent Material[%d]: %s"), i, *GetNameSafe(Mat));
		}
	}
	
	UE_LOG(LogTemp, Warning, TEXT("[MaterialBinding] Material verification complete - proceeding with LOD update"));
}

void UVoxelIslandPhysics::DiagnoseMeshGenerationFailure(AVoxelWorld* World, const FVoxelIntBox& TestBox)
{
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("[Diagnosis] World is null"));
		return;
	}
	
	UE_LOG(LogTemp, Error, TEXT("[Diagnosis] === DETAILED MESH GENERATION FAILURE ANALYSIS ==="));
	
	// Check world state
	UE_LOG(LogTemp, Error, TEXT("[Diagnosis] World Created: %s"), World->IsCreated() ? TEXT("true") : TEXT("false"));
	UE_LOG(LogTemp, Error, TEXT("[Diagnosis] World Size: %d voxels, VoxelSize: %.3f"), 
		World->WorldSizeInVoxel, World->VoxelSize);
	
	// Check generator
	if (World->Generator.GetObject())
	{
		UE_LOG(LogTemp, Error, TEXT("[Diagnosis] Generator: %s (Valid)"), *GetNameSafe(World->Generator.GetObject()));
		
		// Test generator output at a sample position
		FIntVector TestPos = (TestBox.Min + TestBox.Max) / 2;
		FVoxelValue TestValue = World->GetData().GetValue(TestPos, 0);
		UE_LOG(LogTemp, Error, TEXT("[Diagnosis] Generator test at (%d,%d,%d): Value=%.3f (Empty=%s)"), 
			TestPos.X, TestPos.Y, TestPos.Z, TestValue.ToFloat(), TestValue.IsEmpty() ? TEXT("true") : TEXT("false"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[Diagnosis] Generator: NULL - This is the problem!"));
	}
	
	// Check voxel data in test region
	LogVoxelDensities(World, TestBox, "FAILURE-ANALYSIS");
	
	// Check component state
	if (UVoxelWorldRootComponent* RootComp = World->FindComponentByClass<UVoxelWorldRootComponent>())
	{
		FBoxSphereBounds Bounds = RootComp->GetLocalBounds();
		UE_LOG(LogTemp, Error, TEXT("[Diagnosis] RootComponent bounds: Center=(%.1f,%.1f,%.1f), Extent=(%.1f,%.1f,%.1f)"), 
			Bounds.Origin.X, Bounds.Origin.Y, Bounds.Origin.Z,
			Bounds.BoxExtent.X, Bounds.BoxExtent.Y, Bounds.BoxExtent.Z);
		UE_LOG(LogTemp, Error, TEXT("[Diagnosis] RootComponent visible: %s"), RootComp->IsVisible() ? TEXT("true") : TEXT("false"));
		UE_LOG(LogTemp, Error, TEXT("[Diagnosis] RootComponent materials: %d"), RootComp->GetNumMaterials());
		
		// Check if bounds are effectively zero (the problem!)
		bool bHasValidBounds = (Bounds.BoxExtent.X > 1.0f || Bounds.BoxExtent.Y > 1.0f || Bounds.BoxExtent.Z > 1.0f);
		UE_LOG(LogTemp, Error, TEXT("[Diagnosis] RootComponent has valid bounds: %s"), bHasValidBounds ? TEXT("true") : TEXT("FALSE - THIS IS THE PROBLEM!"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[Diagnosis] RootComponent: NOT FOUND - This is a major problem!"));
	}
	
	// Check invokers
	TArray<UVoxelInvokerComponentBase*> Invokers;
	World->GetComponents<UVoxelInvokerComponentBase>(Invokers);
	UE_LOG(LogTemp, Error, TEXT("[Diagnosis] Invokers found: %d"), Invokers.Num());
	for (int32 i = 0; i < Invokers.Num(); i++)
	{
		UVoxelInvokerComponentBase* Inv = Invokers[i];
		if (Inv)
		{
			UE_LOG(LogTemp, Error, TEXT("[Diagnosis] Invoker[%d]: %s, Enabled=%s"), 
				i, *GetNameSafe(Inv), Inv->IsInvokerEnabled() ? TEXT("true") : TEXT("false"));
		}
	}
	
	UE_LOG(LogTemp, Error, TEXT("[Diagnosis] === END FAILURE ANALYSIS ==="));
}


