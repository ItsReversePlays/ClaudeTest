#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Tests/AutomationCommon.h"
#include "VoxelWorld.h"
#include "VoxelIslandPhysics.h"
#include "VoxelFallingTest.generated.h"

/**
 * Functional test for voxel falling physics system
 * Tests that disconnected voxel islands fall under physics while remaining editable
 */
UCLASS()
class CLAUDETEST_API AVoxelFallingTest : public AActor
{
	GENERATED_BODY()

public:
	AVoxelFallingTest();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

	// Test configuration
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Test Config")
	float TestDuration = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Test Config")
	float ExpectedFallDistance = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Test Config") 
	FVector TowerSpawnLocation = FVector(0, 0, 100);

private:
	UPROPERTY(VisibleAnywhere, Category = "Components")
	class UVoxelIslandPhysics* IslandPhysics;

	UPROPERTY()
	class AVoxelWorld* TestVoxelWorld;

	// Test state
	float TestStartTime = 0.0f;
	bool bTestStarted = false;
	bool bCutPerformed = false;
	FVector InitialTowerTop;
	FVector FallingIslandLocation;
	
	// Probe data for deterministic checks
	UPROPERTY()
	TArray<FString> ProbeData;
	
	// Create test voxel tower
	void CreateTestTower();
	
	// Perform the cut that should create falling island
	void PerformCut();
	
	// Check if test conditions are met
	bool CheckTestResult();
	
	// Find falling voxel world (island)
	AVoxelWorld* FindFallingIsland();
	
	// Log probe data for deterministic validation
	void LogProbeStep(const FString& StepName);
	
	// Export probe data to JSON
	void ExportProbeData();
	
	// Calculate surface hash for identity verification
	FString CalculateSurfaceHash(const TArray<FIntVector>& VoxelPositions, const FVector& Centroid);
};