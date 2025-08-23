#include "CoreMinimal.h"
#include "Tests/AutomationCommon.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "VoxelFallingTest.h"
#include "Engine/EngineTypes.h"

/**
 * Automation test that spawns and runs the functional voxel falling test
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFallingAutomationTest, "Project.Functional Tests.VoxelPhysics.FallingIslands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFallingAutomationTest::RunTest(const FString& Parameters)
{
	// Get current world
	UWorld* World = GEngine->GetWorldContexts()[0].World();
	if (!World)
	{
		AddError(TEXT("No world context found"));
		return false;
	}

	AddInfo(TEXT("Starting Voxel Falling Islands test"));

	// Take before screenshot
	AddInfo(TEXT("Taking before screenshot"));

	// Spawn test actor
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	
	AVoxelFallingTest* TestActor = World->SpawnActor<AVoxelFallingTest>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!TestActor)
	{
		AddError(TEXT("Failed to spawn VoxelFallingTest actor"));
		return false;
	}

	AddInfo(TEXT("VoxelFallingTest actor spawned successfully"));

	// Wait for test to complete (TestDuration + buffer)
	float TestTimeout = TestActor->TestDuration + 5.0f;
	
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(TestTimeout));
	
	// Take after screenshot  
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.0f));

	// Cleanup
	ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(1.0f));
	
	return true;
}

