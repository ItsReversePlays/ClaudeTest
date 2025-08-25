// VoxelIslandPhysics.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VoxelWorld.h"
#include "VoxelData/VoxelData.h"
#include "VoxelTools/Gen/VoxelBoxTools.h"
#include "VoxelTools/Gen/VoxelSphereTools.h"
#include "VoxelIslandPhysics.generated.h"

USTRUCT()
struct FVoxelIsland
{
	GENERATED_BODY()

	TArray<FIntVector> VoxelPositions;
	FIntVector MinBounds;
	FIntVector MaxBounds;
	FVector CenterOfMass;
	bool bIsGrounded;
};

/**
 * Component that handles detection and physics simulation of disconnected voxel islands
 * This preserves the exact voxel data while enabling physics on disconnected chunks
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class CLAUDETEST_API UVoxelIslandPhysics : public UActorComponent
{
	GENERATED_BODY()

public:
	UVoxelIslandPhysics();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Main function to check for disconnected islands after voxel edit
	UFUNCTION(BlueprintCallable, Category = "Voxel Physics")
	void CheckForDisconnectedIslands(AVoxelWorld* World, FVector EditLocation, float EditRadius);

	// Fast version with reduced search parameters for digging operations
	UFUNCTION(BlueprintCallable, Category = "Voxel Physics")
	void CheckForDisconnectedIslandsFast(AVoxelWorld* World, FVector EditLocation, float EditRadius);

	// Get falling voxel worlds for testing
	UFUNCTION(BlueprintCallable, Category = "Voxel Physics")
	const TArray<AVoxelWorld*>& GetFallingVoxelWorlds() const { return FallingVoxelWorlds; }

	// Configurable delay for mesh generation (in seconds)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Physics", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float MeshGenerationDelay = 0.0f;

	// Maximum attempts to check for mesh generation (total wait time = MaxMeshAttempts * 0.1s)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Physics", meta = (ClampMin = "10", ClampMax = "200"))
	int32 MaxMeshAttempts = 100; // Default 10 seconds

	// Distance to lift voxel worlds to prevent initial penetration (in cm)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Physics", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float PenetrationGuardDistance = 500.0f; // Default 5 meters

	// Timer callback for checking mesh generation completion
	UFUNCTION()
	void CheckMeshGenerationComplete();

	// Continue with island copy after successful mesh generation
	void ContinueWithIslandCopy();

private:
	// Island detection using flood fill algorithm
	TArray<FVoxelIsland> DetectIslands(AVoxelWorld* World, const FIntVector& EditMin, const FIntVector& EditMax);
	
	// Check if a voxel position is connected to ground
	bool IsConnectedToGround(AVoxelWorld* World, const FIntVector& StartPos, TSet<FIntVector>& Visited);
	
	// Create a new falling voxel world for disconnected island
	void CreateFallingVoxelWorld(AVoxelWorld* SourceWorld, const FVoxelIsland& Island, const FVector& EditLocation);
	
	// Helper method that implements the proper world creation flow
	AVoxelWorld* CreateFallingVoxelWorldInternal(const FIntVector& WorldSize, float InVoxelSize, const FTransform& DesiredTransform, UMaterialInterface* VoxelMat);
	
	// Copy exact voxel data from source to destination with rebasing
	void CopyVoxelData(AVoxelWorld* Source, AVoxelWorld* Destination, const FVoxelIsland& Island, const FVector& WorldPosMin);
	
	// Rebuild collision on a world after voxel changes
	void RebuildWorldCollision(AVoxelWorld* World, const FString& WorldName);
	void RebuildWorldCollisionIncremental(AVoxelWorld* World, const FString& WorldName);
	void RebuildWorldCollisionRegional(AVoxelWorld* World, const FVoxelIsland& Island, const FString& WorldName);
	
	// Enable physics on a falling voxel world with penetration guards
	void EnablePhysicsWithGuards(AVoxelWorld* FallingWorld, const FVoxelIsland& Island);
	
	// Validate that collision geometry covers the full voxel shape
	void ValidateVoxelCollision(AVoxelWorld* World, const FString& WorldName);
	
	// Check if voxel exists at position
	bool HasVoxelAt(AVoxelWorld* World, const FIntVector& Position);
	
	// Remove voxels from source world
	void RemoveIslandVoxels(AVoxelWorld* World, const FVoxelIsland& Island);

	// MultiIndex sanity write helper functions
	void WriteSanityBlockMultiIndex(AVoxelWorld* World);
	void GetRenderStats(AVoxelWorld* World, int32& OutSections, int32& OutTris, bool& OutValidBounds);
	void ReadVoxelPayloadMultiIndex(AVoxelWorld* World, const FIntVector& VoxelPos, float& OutDensity, float& OutL0, float& OutL1, float& OutL2, float& OutL3);

	// Track active falling voxel worlds
	UPROPERTY()
	TArray<AVoxelWorld*> FallingVoxelWorlds;
	
	// Physics update for falling worlds
	void UpdateFallingPhysics(float DeltaTime);
	
	// Custom physics simulation variables
	UPROPERTY()
	TArray<FVector> FallingVelocities;
	
	UPROPERTY()
	TArray<bool> bCustomPhysicsEnabled;
	
	// Physics constants
	float Gravity = -980.0f; // cm/s^2 (realistic gravity)
	float AirResistance = 0.02f; // drag coefficient
	float GroundLevel = 0.0f; // Ground check threshold
	float BounceDamping = 0.3f; // Energy loss on bounce

	// T5/T6 scaffolds
	UPROPERTY()
	TArray<bool> bProxyDirty;
	
	UPROPERTY()
	TArray<float> LastEditTime;
	
	UPROPERTY()
	TArray<bool> bSettled;
	
	UPROPERTY()
	TArray<float> SettleTimers;
	
	// Cooldown for proxy rebuild after edits
	float ProxyRebuildCooldown = 0.3f;

	// Mesh generation check timer system
	FTimerHandle MeshCheckTimerHandle;
	AVoxelWorld* PendingMeshWorld = nullptr;
	int32 MeshCheckAttempts = 0;
	
	// Store parameters for async mesh generation callback
	AVoxelWorld* PendingSourceWorld = nullptr;
	FVoxelIsland PendingIsland;
	FVector PendingWorldPosMin;
	
	// T5 functions
	UFUNCTION(BlueprintCallable, Category = "Voxel Physics")
	void OnVoxelEdit(AVoxelWorld* World, FVector EditLocation, float EditRadius);
	
	// Simple test function for voxel editing with physics
	UFUNCTION(BlueprintCallable, Category = "Voxel Physics")
	void TestVoxelEdit(FVector Location = FVector(-1620, -1620, -580), float Radius = 200.0f);
	
	UFUNCTION(BlueprintCallable, Category = "Voxel Physics")
	int32 GetProxyCookCount(int32 IslandIndex) const;
	
	void UpdateSettleDetection(float DeltaTime);
	void UpdateProxyRebuild(float DeltaTime);
	
	// Settle detection params
	float SettleVelThreshold = 2.5f;
	float SettleAngVelThreshold = 1.5f;
	float SettleDuration = 2.0f;
	
	// T6 Performance guards
	UPROPERTY(EditAnywhere, Category = "Performance Guards")
	int32 MaxLiveIslands = 32;
	
	UPROPERTY(EditAnywhere, Category = "Performance Guards")
	int32 MaxMovingProxyTriangles = 15000;
	
	UPROPERTY(EditAnywhere, Category = "Performance Guards")
	float ProxyRebuildBudgetMs = 3.0f;

public:
	// Flood Fill Detection Parameters - Editable at Runtime
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Detection", meta = (ClampMin = "1", ClampMax = "100"))
	int32 SearchPadding = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Detection", meta = (ClampMin = "10000", ClampMax = "500000"))
	int32 MaxFloodFillIterations = 50000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Detection", meta = (ClampMin = "10000", ClampMax = "500000000"))
	int32 MaxTotalVoxels = 25000000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Detection", meta = (ClampMin = "1000", ClampMax = "1000000"))
	int32 MaxQuickScanVoxels = 100000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Detection", meta = (ClampMin = "500.0", ClampMax = "50000.0"))
	float TowerHeightLimit = 5000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Detection", meta = (ClampMin = "500.0", ClampMax = "50000.0"))
	float HorizontalStructureLimit = 8000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Detection", meta = (ClampMin = "1000", ClampMax = "100000"))
	int32 MaxIslandVoxels = 10000;

	// Maximum build height in world units (prevents building above this Z coordinate)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Build Constraints", meta = (ClampMin = "1000.0", ClampMax = "20000.0"))
	float MaxBuildHeight = 3500.0f;

private:
	
	UPROPERTY()
	TArray<int32> ProxyCookCounts;
	
	UPROPERTY()
	TArray<float> ProxyRebuildTimers;
	
	// Performance monitoring
	int32 GetTotalProxyTriangles() const;
	int32 GetMovingProxyTriangles() const;
	bool ShouldEnforcePerformanceCaps() const;
	void CleanupOldestIsland();
	
	// T6 island lifecycle management
	void PerformanceCleanup();
	bool CanCreateNewIsland() const;
	
	// Visibility and render diagnostics
	void EnsureWorldVisibility(AVoxelWorld* World, const FString& WorldName);
	void ForceRenderRebuild(AVoxelWorld* World, const FVoxelIsland& Island, const FString& WorldName);
	void VerifyVisualState(AVoxelWorld* SourceWorld, AVoxelWorld* FallingWorld, const FVoxelIsland& Island);
	int32 CountSolidVoxels(AVoxelWorld* World, const FVoxelIsland& Island, const FString& WorldName);
	void VerifyCarveOut(AVoxelWorld* World, const FVoxelIsland& Island, const FString& WorldName);
	void LogRenderStats(AVoxelWorld* World, const FString& WorldName);
	
	// Comprehensive invoker-based fix functions
	void AttachInvokers(AVoxelWorld* SourceWorld, AVoxelWorld* FallingWorld, const FVoxelIsland& Island);
	void SyncRebuildWorlds(AVoxelWorld* SourceWorld, AVoxelWorld* FallingWorld, const FVoxelIsland& Island);
	void VerifyRuntimeStats(AVoxelWorld* SourceWorld, AVoxelWorld* FallingWorld, const FVoxelIsland& Island);
	void EnablePhysicsIfValid(AVoxelWorld* FallingWorld, const FVoxelIsland& Island);
	void LogRuntimeStats(AVoxelWorld* World, const FString& WorldName);
	
	// Robust triangle generation functions
	void ForceSynchronousRemesh(AVoxelWorld* World);
	void DumpRenderStats(AVoxelWorld* World, const FString& WorldName);
	int32 GetTriangleCount(AVoxelWorld* World);
	void DumpSanityConfig(AVoxelWorld* World);
	void CopyVoxelDataRobust(AVoxelWorld* Source, AVoxelWorld* Destination, const FVoxelIsland& Island, const FVector& WorldPosMin);
	
	// Enhanced mesh generation diagnostics
	void LogVoxelDensities(AVoxelWorld* World, const FVoxelIntBox& Box, const FString& Stage);
	void VerifyMaterialBinding(AVoxelWorld* World);
	void DiagnoseMeshGenerationFailure(AVoxelWorld* World, const FVoxelIntBox& TestBox);
	
	// Copy island detection settings to a new falling world
	void CopyIslandDetectionSettings(AVoxelWorld* FallingWorld);
};