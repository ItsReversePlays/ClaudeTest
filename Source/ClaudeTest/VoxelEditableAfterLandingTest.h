#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Tests/AutomationCommon.h"
#include "VoxelWorld.h"
#include "VoxelIslandPhysics.h"
#include "Engine/TimerHandle.h"
#include "VoxelEditableAfterLandingTest.generated.h"

/**
 * Functional test for editability after voxel island landing
 * Tests complete T5 workflow: cut -> fall -> settle -> edit -> proxy recook
 */
UCLASS()
class CLAUDETEST_API AVoxelEditableAfterLandingTest : public AActor
{
	GENERATED_BODY()

public:
	AVoxelEditableAfterLandingTest();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

	// Test constants
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Test Config")
	float DeltaZ_Threshold = 100.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Test Config")
	float Fall_TimeoutSeconds = 10.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Test Config") 
	float Settle_Vel_Thresh = 2.5f;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Test Config")
	float Settle_AngVel_Thresh = 1.5f;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Test Config")
	float Settle_DurationSeconds = 2.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Test Config")
	float Proxy_Rebuild_Cooldown = 0.30f;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Test Config")
	int32 Edit_VoxelCount_Change = 25;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Test Config") 
	FVector TowerSpawnLocation = FVector(0, 0, 100);

private:
	UPROPERTY(VisibleAnywhere, Category = "Components")
	class UVoxelIslandPhysics* IslandPhysics;

	UPROPERTY()
	class AVoxelWorld* TestVoxelWorld;

	// Test state
	enum class ETestStep
	{
		Setup,
		Cut,
		WaitForFall,
		WaitForSettle, 
		PerformEdit,
		WaitForRecook,
		Complete
	};
	
	ETestStep CurrentStep = ETestStep::Setup;
	float TestStartTime = 0.0f;
	float StepStartTime = 0.0f;
	bool bTestPassed = false;
	
	// Probe data
	UPROPERTY()
	TArray<FString> ProbeData;
	
	// Test data
	FString SurfaceHash_Pre;
	int32 ParentVoxels_Before = 0;
	int32 ParentVoxels_After = 0;
	int32 IslandVoxels_0 = 0;
	float COMZ_Spawn = 0.0f;
	float COMZ_Current = 0.0f;
	float COMZ_Settled = 0.0f;
	int32 ProxyCookCount_Before = 0;
	int32 ProxyCookCount_After = 0;
	float SettledTimer = 0.0f;
	
	// Timer handle for fall setup
	UPROPERTY()
	FTimerHandle FallSetupTimer;
	
	// Helper functions
	void SetupTest();
	void PerformCut();
	bool CheckFallProgress();
	bool CheckSettleProgress();
	void PerformEdit();
	bool CheckProxyRecook();
	void CompleteTest();
	
	// Utility functions
	AVoxelWorld* FindFallingIsland();
	FString ComputeSurfaceHash(const TArray<FIntVector>& VoxelPositions, const FVector& Centroid);
	void LogProbeStep(const FString& StepName);
	void ExportProbeData();
	
	// Debug utilities
	void LogRuntimeStats(AVoxelWorld* World, const FString& WorldName);
	int32 GetTriangleCount(AVoxelWorld* World);
	void ForceSynchronousRemesh(AVoxelWorld* World);
	void ForceSynchronousRemesh(AVoxelWorld* World, const FIntVector& Min, const FIntVector& Max);
	void DumpRenderStats(AVoxelWorld* World, const FString& WorldName);
	void DumpSanityConfig(AVoxelWorld* World);
};