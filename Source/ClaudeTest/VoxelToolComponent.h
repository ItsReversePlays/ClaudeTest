// VoxelToolComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "VoxelWorld.h"
#include "VoxelWorldRootComponent.h"
#include "VoxelTools/Gen/VoxelSphereTools.h"
#include "VoxelTools/Gen/VoxelBoxTools.h"
#include "VoxelTools/Impl/VoxelSphereToolsImpl.h"
// Voxel physics includes - re-enabling for proper voxel physics simulation
#include "VoxelTools/VoxelPhysics.h"
#include "VoxelTools/VoxelPhysicsPartSpawner.h"
#include "Engine/StaticMeshActor.h"
#include "VoxelData/VoxelData.h"
#include "VoxelValue.h"
#include "Engine/LatentActionManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "VoxelRender/VoxelProceduralMeshComponent.h"
#include "VoxelRender/IVoxelRenderer.h"
#include "EngineUtils.h"
#include "PhysicsEngine/BodySetup.h"
#include "Engine/StaticMeshActor.h"
#include "VoxelComponents/VoxelNoClippingComponent.h"
#include "VoxelGenerators/VoxelEmptyGenerator.h"
#include "VoxelToolComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class CLAUDETEST_API UVoxelToolComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UVoxelToolComponent();

protected:
	virtual void BeginPlay() override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Tool settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Tools", meta = (ClampMin = "100.0", ClampMax = "500.0"))
	float ToolRadius = 200.0f;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Tools", meta = (ClampMin = "0.1", ClampMax = "3.0", ToolTip = "Tool strength (0.1 = very gentle, 0.3 = gentle, 0.6 = balanced, 1.0 = normal, 2.0 = strong)"))
	float ToolStrength = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Tools", meta = (ClampMin = "500.0", ClampMax = "50000.0", ToolTip = "Maximum distance for line traces to detect surfaces"))
	float MaxTraceDistance = 10000.0f;

	// Preview settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Tools")
	bool bShowToolPreview = true;

	// Tool functions
	UFUNCTION(BlueprintCallable, Category = "Voxel Tools")
	void DigAtLocation(FVector Location);

	UFUNCTION(BlueprintCallable, Category = "Voxel Tools")
	void BuildAtLocation(FVector Location);

	UFUNCTION(BlueprintCallable, Category = "Voxel Tools")
	void DigFromPlayerView();

	UFUNCTION(BlueprintCallable, Category = "Voxel Tools")
	void BuildFromPlayerView();

	UFUNCTION(BlueprintCallable, Category = "Voxel Tools")
	void IncreaseToolSize();

	UFUNCTION(BlueprintCallable, Category = "Voxel Tools")
	void DecreaseToolSize();

	// Multiplayer voxel modification functions
	UFUNCTION(Server, Reliable, Category = "Voxel Tools")
	void ServerBuildAtLocation(FVector Location, float Radius, float Strength);

	UFUNCTION(Server, Reliable, Category = "Voxel Tools")
	void ServerDigAtLocation(FVector Location, float Radius, float Strength);

	UFUNCTION(NetMulticast, Reliable, Category = "Voxel Tools")
	void MulticastBuildAtLocation(FVector Location, float Radius, float Strength);

	UFUNCTION(NetMulticast, Reliable, Category = "Voxel Tools")
	void MulticastDigAtLocation(FVector Location, float Radius, float Strength);

	// Multiplayer terrain placement functions
	UFUNCTION(Server, Reliable, Category = "Voxel Tools")
	void ServerPlacePlayerOnTerrain(FVector BuildLocation, float EffectiveRadius, APawn* TargetPlayer);

	UFUNCTION(NetMulticast, Reliable, Category = "Voxel Tools")
	void MulticastPlacePlayerOnTerrain(FVector BuildLocation, float EffectiveRadius, APawn* TargetPlayer);

	// Debug visualization settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bShowDebugCircle = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	FColor DebugCircleColor = FColor::Green;

	// Build range limit for tools
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Tools", meta = (ClampMin = "100.0", ClampMax = "50000.0", ToolTip = "Maximum distance player can build/dig from their position"))
	float MaxBuildRange = 5000.0f;

	// Cooldown settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Tools", meta = (ClampMin = "0.0", ClampMax = "5.0", ToolTip = "Minimum time in seconds between build/dig actions (0 = no cooldown for smooth building)"))
	float BuildDigCooldown = 0.0f;

	// Smooth player movement settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Tools", meta = (ClampMin = "0.1", ClampMax = "2.0", ToolTip = "Time in seconds to smoothly move player to safe position after building (0.1 = fast, 1.0 = normal, 2.0 = slow)"))
	float SmoothMovementDuration = 0.5f;

	// Anti-clipping safety margin
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Tools", meta = (ClampMin = "20.0", ClampMax = "200.0", ToolTip = "Extra height in units for immediate safety teleport to prevent clipping (20 = minimal, 60 = default, 200 = maximum)"))
	float ImmediateSafetyMargin = 60.0f;

	// Smooth building settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Tools", meta = (ToolTip = "Enable smooth continuous building instead of discrete sphere placement"))
	bool bEnableSmoothBuilding = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Tools", meta = (ClampMin = "10.0", ClampMax = "100.0", ToolTip = "Step size for interpolating between build points (smaller = smoother but more expensive)"))
	float SmoothBuildStepSize = 30.0f;

	// Voxel physics settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Physics", meta = (ToolTip = "Enable automatic physics for floating parts after digging"))
	bool bEnableVoxelPhysics = true;

	// Use fast physics mode during digging to prevent lag while still enabling island creation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Physics", meta = (ToolTip = "Use fast physics mode during digging to prevent lag while still enabling island creation"))
	bool bUseFastPhysicsOnDig = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Physics", meta = (ClampMin = "1", ClampMax = "10", ToolTip = "Minimum number of connected voxels before they fall with physics (1 = all disconnected parts fall, 10 = only small parts fall)"))
	int32 MinPartsForPhysics = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Physics", meta = (ClampMin = "100.0", ClampMax = "2000.0", ToolTip = "Radius around dig location to check for floating parts (larger = more expensive but more accurate)"))
	float PhysicsCheckRadius = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Physics", meta = (ClampMin = "0.5", ClampMax = "1.0", ToolTip = "Minimum disconnection ratio required for structure severance (0.5 = 50% disconnected, 0.75 = 75% disconnected, 1.0 = 100% disconnected - only fully cut structures)"))
	float SeveranceThreshold = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Physics", meta = (ClampMin = "0.1", ClampMax = "0.8", ToolTip = "Minimum gap evidence ratio required for thin cut detection (0.2 = 20% gaps needed, 0.4 = 40% gaps needed)"))
	float GapAnalysisThreshold = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Physics", meta = (ClampMin = "0.1", ClampMax = "0.5", ToolTip = "Minimum consecutive disconnected arc for thin cut detection (0.15 = 15% arc, 0.25 = 25% arc, 0.4 = 40% arc)"))
	float ThinCutThreshold = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Physics", meta = (ClampMin = "11", ClampMax = "31", ToolTip = "Grid size for comprehensive connection scanning (11x11 = 121 tests, 21x21 = 441 tests, 31x31 = 961 tests)"))
	int32 ConnectionScanGridSize = 21;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Physics", meta = (ClampMin = "50.0", ClampMax = "500.0", ToolTip = "Minimum connection depth required for valid structural connection (50 = very sensitive, 200 = balanced, 400 = conservative)"))
	float MinConnectionDepth = 200.0f;

	// Physics spawner class for voxel physics simulation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Physics", meta = (ToolTip = "Type of physics spawner to use for falling voxel parts"))
	TSubclassOf<UVoxelPhysicsPartSpawner_VoxelWorlds> PhysicsPartSpawnerClass;

	// Individual debug circle size offsets for each tool size
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (ClampMin = "-500.0", ClampMax = "500.0", ToolTip = "Debug circle offset for tool size 50"))
	float DebugOffset_50 = -20.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (ClampMin = "-500.0", ClampMax = "500.0", ToolTip = "Debug circle offset for tool size 100"))
	float DebugOffset_100 = -20.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (ClampMin = "-500.0", ClampMax = "500.0", ToolTip = "Debug circle offset for tool size 150"))
	float DebugOffset_150 = -20.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (ClampMin = "-500.0", ClampMax = "500.0", ToolTip = "Debug circle offset for tool size 200"))
	float DebugOffset_200 = -20.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (ClampMin = "-500.0", ClampMax = "500.0", ToolTip = "Debug circle offset for tool size 250"))
	float DebugOffset_250 = -20.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (ClampMin = "-500.0", ClampMax = "500.0", ToolTip = "Debug circle offset for tool size 300"))
	float DebugOffset_300 = -20.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (ClampMin = "-500.0", ClampMax = "500.0", ToolTip = "Debug circle offset for tool size 350"))
	float DebugOffset_350 = -20.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (ClampMin = "-500.0", ClampMax = "500.0", ToolTip = "Debug circle offset for tool size 400"))
	float DebugOffset_400 = -20.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (ClampMin = "-500.0", ClampMax = "500.0", ToolTip = "Debug circle offset for tool size 450"))
	float DebugOffset_450 = -20.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (ClampMin = "-500.0", ClampMax = "500.0", ToolTip = "Debug circle offset for tool size 500"))
	float DebugOffset_500 = -20.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (ClampMin = "-500.0", ClampMax = "500.0", ToolTip = "Debug circle offset for tool size 550"))
	float DebugOffset_550 = -20.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (ClampMin = "-500.0", ClampMax = "500.0", ToolTip = "Debug circle offset for tool size 600"))
	float DebugOffset_600 = -20.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (ClampMin = "-500.0", ClampMax = "500.0", ToolTip = "Debug circle offset for tool size 650"))
	float DebugOffset_650 = -20.0f;

private:
	// Find voxel world in level
	UFUNCTION()
	AVoxelWorld* FindVoxelWorld();
	
	// Find the closest voxel world to a specific location
	UFUNCTION()
	AVoxelWorld* FindVoxelWorldAtLocation(const FVector& Location);

	// Perform line trace from player camera
	UFUNCTION()
	bool TraceToCursor(FVector& OutHitLocation);
	
	// Perform line trace from player camera with surface normal
	bool TraceToCursorWithNormal(FVector& OutHitLocation, FVector& OutSurfaceNormal);

	// Helper function to get player controller
	UFUNCTION()
	APlayerController* GetPlayerController();

	// Update tool preview
	UFUNCTION()
	void UpdateToolPreview();

	// Helper function to place a player on top of terrain
	void PlacePlayerOnTerrain(FVector BuildLocation, float EffectiveRadius, APawn* TargetPlayer);

	// Helper function to get debug circle radius with offset
	float GetDebugCircleRadius() const;

	// Helper function to apply voxel physics after digging
	void ApplyVoxelPhysicsAfterDig(FVector DigLocation, float DigRadius);

	// Delayed physics application to avoid access violations
	void DelayedApplyVoxelPhysics(AVoxelWorld* VoxelWorld, FVoxelIntBox PhysicsBounds);

	
	// Helper function to spawn actual voxel physics simulation
	void SpawnVoxelPhysics(AVoxelWorld* VoxelWorld, FVoxelIntBox PhysicsBounds);

	// Helper function to detect if a structure has been disconnected from the ground
	bool CheckForDisconnectedStructure(AVoxelWorld* VoxelWorld, FVector DigCenter, float SearchRadius);
	
	// NEW: Create a falling static mesh from the actual severed voxel structure
	void CreateFallingVoxelWorld(AVoxelWorld* OriginalWorld, FVector DigCenter, float SearchRadius);
	
	// NEW: Extract mesh geometry and create falling physics object
	void CreateFallingMeshFromVoxels(AVoxelWorld* OriginalWorld, FVector RegionCenter, float RegionRadius);
	
	// NEW: Extract actual mesh data from voxel world
	bool ExtractVoxelMeshData(AVoxelWorld* VoxelWorld, FVoxelIntBox Bounds, TArray<FVector>& OutVertices, TArray<int32>& OutTriangles, TArray<FVector>& OutNormals, TArray<FVector2D>& OutUVs, TArray<FColor>& OutVertexColors);
	
	
	// NEW: Create box mesh geometry for voxel chunks
	void CreateBoxMesh(FVector Center, FVector Extent, TArray<FVector>& OutVertices, TArray<int32>& OutTriangles, TArray<FVector>& OutNormals, TArray<FVector2D>& OutUVs, TArray<FColor>& OutColors);
	
	// NEW: Create chunks by directly sampling voxel data instead of relying on mesh components
	bool CreateChunksFromVoxelData(AVoxelWorld* VoxelWorld, FVoxelIntBox Bounds, AActor* ParentActor, UStaticMesh* CubeMesh, TArray<UStaticMeshComponent*>& OutComponents);
	
	// NEW: Create multiple static mesh components from voxel components with precise filtering
	bool CreateVoxelChunkMeshes(AVoxelWorld* VoxelWorld, FVoxelIntBox Bounds, AActor* ParentActor, FVector SpawnLocation, TArray<UStaticMeshComponent*>& OutComponents);
	
	// NEW: Create Chaos physics object from voxel world mesh
	void CreateChaosPhysicsFromVoxelWorld(AVoxelWorld* SourceVoxelWorld, FVector SpawnLocation);
	
	// Simplified physics creation that avoids temporary VoxelWorld creation
	void CreateSimplifiedChaosPhysics(AVoxelWorld* OriginalWorld, FVector SpawnLocation, float StructureSize);

	// DISABLED: Callback to configure physics voxel worlds (no longer used)
	// UFUNCTION()
	// void OnConfigurePhysicsVoxelWorld(AVoxelWorld* VoxelWorld);

	// Callback when voxel physics is complete
	UFUNCTION()
	void OnVoxelPhysicsComplete();

	// Local helper functions for voxel operations
	void LocalBuildAtLocation(FVector Location, float Radius, float Strength);
	void LocalDigAtLocation(FVector Location, float Radius, float Strength);

	// Cached voxel world reference
	UPROPERTY()
	AVoxelWorld* CachedVoxelWorld = nullptr;

	// Last hit location for preview
	FVector LastHitLocation = FVector::ZeroVector;
	bool bValidHitLocation = false;

	// Smooth building system
	FVector LastBuildLocation = FVector::ZeroVector;
	bool bHasLastBuildLocation = false;
	bool bIsContinuousBuilding = false;
	float LastBuildActionTime = 0.0f;
	
	// Helper functions for smooth building
	void ProcessSmoothBuild(FVector StartLocation, FVector EndLocation, float Radius, float Strength);
	void InterpolateBuildPoints(FVector StartLocation, FVector EndLocation, float Radius, float Strength, float StepSize = 50.0f);

	// Cooldown tracking
	float LastBuildTime = 0.0f;
	float LastDigTime = 0.0f;

	// Smooth movement tracking
	bool bIsSmoothMoving = false;
	FVector SmoothMoveStartLocation = FVector::ZeroVector;
	FVector SmoothMoveTargetLocation = FVector::ZeroVector;
	float SmoothMoveStartTime = 0.0f;
	APawn* SmoothMoveTargetPawn = nullptr;

	// Timer handle for delayed voxel physics
	FTimerHandle VoxelPhysicsTimerHandle;
	
	// New island physics system component
	UPROPERTY()
	class UVoxelIslandPhysics* IslandPhysicsComponent;
};