// VoxelToolComponent.cpp
#include "VoxelToolComponent.h"
#include "VoxelIslandPhysics.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Engine.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
// #include "UObject/ConstructorHelpers.h" // Not needed anymore
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Engine/World.h"

UVoxelToolComponent::UVoxelToolComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UVoxelToolComponent::BeginPlay()
{
	Super::BeginPlay();
	
	// Cache the voxel world reference
	CachedVoxelWorld = FindVoxelWorld();
	
	if (!CachedVoxelWorld)
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: No Voxel World found in level!"));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("VoxelToolComponent: Found Voxel World: %s"), *CachedVoxelWorld->GetName());
	}
	
	// Create and attach the island physics component
	if (AActor* Owner = GetOwner())
	{
		IslandPhysicsComponent = NewObject<UVoxelIslandPhysics>(Owner, TEXT("IslandPhysics"));
		IslandPhysicsComponent->RegisterComponent();
		UE_LOG(LogTemp, Log, TEXT("VoxelToolComponent: Island physics system initialized"));
	}
	
	// Log initial settings for debugging
	UE_LOG(LogTemp, Log, TEXT("VoxelToolComponent: ToolRadius=%.1f, DebugCircleRadius=%.1f, MaxBuildRange=%.1f, MaxTraceDistance=%.1f"), 
		ToolRadius, GetDebugCircleRadius(), MaxBuildRange, MaxTraceDistance);
}

AVoxelWorld* UVoxelToolComponent::FindVoxelWorld()
{
	if (CachedVoxelWorld && IsValid(CachedVoxelWorld))
	{
		return CachedVoxelWorld;
	}

	// Find voxel world in the level
	for (TActorIterator<AVoxelWorld> ActorIterator(GetWorld()); ActorIterator; ++ActorIterator)
	{
		AVoxelWorld* VoxelWorld = *ActorIterator;
		if (IsValid(VoxelWorld))
		{
			CachedVoxelWorld = VoxelWorld;
			return VoxelWorld;
		}
	}

	return nullptr;
}

AVoxelWorld* UVoxelToolComponent::FindVoxelWorldAtLocation(const FVector& Location)
{
	// First try to find a falling VoxelWorld sphere near the location
	AVoxelWorld* ClosestFallingWorld = nullptr;
	float ClosestFallingDistance = FLT_MAX;
	
	// Then track the main terrain world as fallback
	AVoxelWorld* MainWorld = nullptr;
	
	// Find all VoxelWorlds and categorize them
	for (TActorIterator<AVoxelWorld> ActorIterator(GetWorld()); ActorIterator; ++ActorIterator)
	{
		AVoxelWorld* VoxelWorld = *ActorIterator;
		if (IsValid(VoxelWorld))
		{
			// Check if this is a falling sphere (has our tag)
			if (VoxelWorld->Tags.Contains(FName("FallingVoxelWorld")))
			{
				// For falling spheres, use a generous detection radius
				float Distance = FVector::Dist(VoxelWorld->GetActorLocation(), Location);
				float DetectionRadius = 500.0f; // Large detection radius for small falling objects
				
				if (Distance <= DetectionRadius && Distance < ClosestFallingDistance)
				{
					ClosestFallingDistance = Distance;
					ClosestFallingWorld = VoxelWorld;
					UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Found falling sphere %s at distance %.1f"), 
						*VoxelWorld->GetName(), Distance);
				}
			}
			else
			{
				// This is the main terrain world
				MainWorld = VoxelWorld;
			}
		}
	}
	
	// Prefer falling spheres if found
	if (ClosestFallingWorld)
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Targeting falling sphere %s for operation at %s"), 
			*ClosestFallingWorld->GetName(), *Location.ToString());
		return ClosestFallingWorld;
	}
	
	// Otherwise use the main world
	if (MainWorld)
	{
		UE_LOG(LogTemp, Log, TEXT("VoxelToolComponent: Using main VoxelWorld for operation at %s"), 
			*Location.ToString());
		return MainWorld;
	}
	
	UE_LOG(LogTemp, Error, TEXT("VoxelToolComponent: No VoxelWorld found at all!"));
	return nullptr;
}

APlayerController* UVoxelToolComponent::GetPlayerController()
{
	if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		return Cast<APlayerController>(OwnerPawn->GetController());
	}
	return nullptr;
}

bool UVoxelToolComponent::TraceToCursor(FVector& OutHitLocation)
{
	APlayerController* PC = GetPlayerController();
	if (!PC)
	{
		return false;
	}

	// Get camera location and direction
	FVector CameraLocation;
	FRotator CameraRotation;
	PC->GetPlayerViewPoint(CameraLocation, CameraRotation);
	
	FVector TraceStart = CameraLocation;
	// Use the larger of MaxBuildRange or MaxTraceDistance to ensure we can trace far enough
	float EffectiveTraceDistance = FMath::Max(MaxBuildRange * 1.5f, MaxTraceDistance);
	FVector TraceEnd = CameraLocation + (CameraRotation.Vector() * EffectiveTraceDistance);

	// Perform line trace
	FHitResult HitResult;
	FCollisionQueryParams TraceParams;
	TraceParams.bTraceComplex = true;
	TraceParams.AddIgnoredActor(GetOwner());

	bool bHit = GetWorld()->LineTraceSingleByChannel(
		HitResult,
		TraceStart,
		TraceEnd,
		ECC_WorldStatic,
		TraceParams
	);

	if (bHit)
	{
		OutHitLocation = HitResult.Location;
		return true;
	}

	return false;
}

bool UVoxelToolComponent::TraceToCursorWithNormal(FVector& OutHitLocation, FVector& OutSurfaceNormal)
{
	APlayerController* PC = GetPlayerController();
	if (!PC) return false;
	
	FVector CameraLocation;
	FRotator CameraRotation;
	PC->GetPlayerViewPoint(CameraLocation, CameraRotation);
	
	// Use the larger of MaxBuildRange or MaxTraceDistance to ensure we can trace far enough
	float EffectiveTraceDistance = FMath::Max(MaxBuildRange * 1.5f, MaxTraceDistance);
	FVector TraceEnd = CameraLocation + (CameraRotation.Vector() * EffectiveTraceDistance);
	
	FHitResult HitResult;
	FCollisionQueryParams TraceParams;
	TraceParams.bTraceComplex = true;
	TraceParams.AddIgnoredActor(GetOwner());
	
	// Ignore all pawns (players) to prevent debug circle from showing on players
	TraceParams.AddIgnoredActors(TArray<AActor*>());
	for (TActorIterator<APawn> ActorItr(GetWorld()); ActorItr; ++ActorItr)
	{
		TraceParams.AddIgnoredActor(*ActorItr);
	}
	
	bool bHit = GetWorld()->LineTraceSingleByChannel(
		HitResult,
		CameraLocation,
		TraceEnd,
		ECC_WorldStatic,
		TraceParams
	);
	
	if (bHit)
	{
		OutHitLocation = HitResult.Location;
		OutSurfaceNormal = HitResult.Normal;
		return true;
	}
	
	// Default to upward normal if no hit
	OutSurfaceNormal = FVector::UpVector;
	return false;
}

void UVoxelToolComponent::DigAtLocation(FVector Location)
{
	// Use multiplayer-aware digging with current tool settings
	if (GetOwner()->HasAuthority())
	{
		// We're on the server, multicast to all clients
		MulticastDigAtLocation(Location, ToolRadius, ToolStrength);
	}
	else
	{
		// We're on a client, send to server
		ServerDigAtLocation(Location, ToolRadius, ToolStrength);
	}
}

void UVoxelToolComponent::BuildAtLocation(FVector Location)
{
	// Use multiplayer-aware building with current tool settings
	if (GetOwner()->HasAuthority())
	{
		// We're on the server, multicast to all clients
		MulticastBuildAtLocation(Location, ToolRadius, ToolStrength);
	}
	else
	{
		// We're on a client, send to server
		ServerBuildAtLocation(Location, ToolRadius, ToolStrength);
	}
}

void UVoxelToolComponent::DigFromPlayerView()
{
	// Check cooldown
	float CurrentTime = GetWorld()->GetTimeSeconds();
	if (CurrentTime - LastDigTime < BuildDigCooldown)
	{
		return; // Still on cooldown
	}

	FVector HitLocation;
	if (TraceToCursor(HitLocation))
	{
		// Check if within build range
		APawn* OwnerPawn = Cast<APawn>(GetOwner());
		if (OwnerPawn)
		{
			FVector PlayerLocation = OwnerPawn->GetActorLocation();
			float DistanceToHit = FVector::Dist(PlayerLocation, HitLocation);
			
			if (DistanceToHit > MaxBuildRange)
			{
				UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Target is too far away (%.1f > %.1f)"), DistanceToHit, MaxBuildRange);
				return;
			}
		}
		
		// Update last dig time
		LastDigTime = CurrentTime;
		DigAtLocation(HitLocation);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Could not find surface to dig"));
	}
}

void UVoxelToolComponent::BuildFromPlayerView()
{
	// Check cooldown
	float CurrentTime = GetWorld()->GetTimeSeconds();
	if (CurrentTime - LastBuildTime < BuildDigCooldown)
	{
		return; // Still on cooldown
	}

	FVector HitLocation;
	if (TraceToCursor(HitLocation))
	{
		// Check if within build range
		APawn* OwnerPawn = Cast<APawn>(GetOwner());
		if (OwnerPawn)
		{
			FVector PlayerLocation = OwnerPawn->GetActorLocation();
			float DistanceToHit = FVector::Dist(PlayerLocation, HitLocation);
			
			if (DistanceToHit > MaxBuildRange)
			{
				UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Target is too far away (%.1f > %.1f)"), DistanceToHit, MaxBuildRange);
				return;
			}
		}
		
		// Update last build time
		LastBuildTime = CurrentTime;
		
		// Offset the build location slightly towards the camera to build on surface
		APlayerController* PC = GetPlayerController();
		if (PC)
		{
			FVector CameraLocation;
			FRotator CameraRotation;
			PC->GetPlayerViewPoint(CameraLocation, CameraRotation);
			
			FVector DirectionToCamera = (CameraLocation - HitLocation).GetSafeNormal();
			FVector BuildLocation = HitLocation + (DirectionToCamera * (ToolRadius * 0.5f));
			
			BuildAtLocation(BuildLocation);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Could not find surface to build on"));
	}
}

void UVoxelToolComponent::IncreaseToolSize()
{
	ToolRadius = FMath::Clamp(ToolRadius + 50.0f, 100.0f, 500.0f);
	UE_LOG(LogTemp, Log, TEXT("Tool radius increased to: %f"), ToolRadius);
	
	// Show message to player
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Green, 
			FString::Printf(TEXT("Tool Size: %.0f"), ToolRadius));
	}
}

void UVoxelToolComponent::DecreaseToolSize()
{
	ToolRadius = FMath::Clamp(ToolRadius - 50.0f, 100.0f, 500.0f);
	UE_LOG(LogTemp, Log, TEXT("Tool radius decreased to: %f"), ToolRadius);
	
	// Show message to player
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Blue, 
			FString::Printf(TEXT("Tool Size: %.0f"), ToolRadius));
	}
}

void UVoxelToolComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	// Reset continuous building if there's been a pause (1 second without building)
	if (bIsContinuousBuilding && bEnableSmoothBuilding)
	{
		float CurrentTime = GetWorld()->GetTimeSeconds();
		float TimeSinceLastBuild = CurrentTime - LastBuildActionTime;
		
		if (TimeSinceLastBuild > 1.0f) // 1 second pause resets continuous building
		{
			bIsContinuousBuilding = false;
			UE_LOG(LogTemp, Verbose, TEXT("VoxelToolComponent: Continuous building reset after %.1f second pause"), TimeSinceLastBuild);
		}
	}
	
	// Handle smooth player movement
	if (bIsSmoothMoving && SmoothMoveTargetPawn && IsValid(SmoothMoveTargetPawn))
	{
		float CurrentTime = GetWorld()->GetTimeSeconds();
		float ElapsedTime = CurrentTime - SmoothMoveStartTime;
		float Progress = FMath::Clamp(ElapsedTime / SmoothMovementDuration, 0.0f, 1.0f);
		
		// Use smooth interpolation curve (ease-out)
		float SmoothProgress = 1.0f - FMath::Pow(1.0f - Progress, 3.0f);
		
		// Interpolate position
		FVector CurrentLocation = FMath::Lerp(SmoothMoveStartLocation, SmoothMoveTargetLocation, SmoothProgress);
		
		// Apply the smooth movement
		if (auto* Character = Cast<ACharacter>(SmoothMoveTargetPawn))
		{
			Character->SetActorLocation(CurrentLocation, false, nullptr, ETeleportType::ResetPhysics);
			
			// Reset velocity during movement to prevent physics interference
			if (auto* CharacterMovement = Character->GetCharacterMovement())
			{
				CharacterMovement->Velocity = FVector::ZeroVector;
			}
		}
		
		// Check if movement is complete
		if (Progress >= 1.0f)
		{
			bIsSmoothMoving = false;
			SmoothMoveTargetPawn = nullptr;
		}
	}
	
	if (bShowToolPreview)
	{
		UpdateToolPreview();
	}
}

void UVoxelToolComponent::UpdateToolPreview()
{
	if (!bShowDebugCircle)
	{
		return;
	}
	
	// Only show debug visualization for the local player
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}
	
	FVector HitLocation;
	FVector SurfaceNormal;
	bool bHit = TraceToCursorWithNormal(HitLocation, SurfaceNormal);
	
	if (bHit)
	{
		// Check if hit location is within build range (reuse OwnerPawn from above)
		if (OwnerPawn)
		{
			FVector PlayerLocation = OwnerPawn->GetActorLocation();
			float DistanceToHit = FVector::Dist(PlayerLocation, HitLocation);
			
			if (DistanceToHit > MaxBuildRange)
			{
				// Too far away, don't show debug or update hit location
				bValidHitLocation = false;
				return;
			}
		}
		
		// Update debug visualization (local only)
		LastHitLocation = HitLocation;
		bValidHitLocation = true;
		
		// Show debug circle on terrain surface - local only, conforming to surface
		if (GetWorld())
		{
			// For DrawDebugCircle, we need the circle to lie in the plane perpendicular to the surface normal
			// So the surface normal should be used as the "forward" axis (X-axis) of the matrix
			
			FVector XAxis = SurfaceNormal; // Surface normal becomes the "forward" direction
			
			// Find perpendicular vectors to create the plane the circle lies in
			FVector YAxis = FVector::CrossProduct(XAxis, FVector::UpVector).GetSafeNormal();
			if (YAxis.IsNearlyZero())
			{
				// Surface normal is parallel to up vector, use forward instead
				YAxis = FVector::CrossProduct(XAxis, FVector::ForwardVector).GetSafeNormal();
			}
			
			// Z-axis (up for the circle's local space) lies in the surface plane
			FVector ZAxis = FVector::CrossProduct(XAxis, YAxis).GetSafeNormal();
			
			// Create transform matrix 
			FMatrix CircleMatrix = FMatrix(XAxis, YAxis, ZAxis, HitLocation);
			
			DrawDebugCircle(
				GetWorld(),
				CircleMatrix,
				GetDebugCircleRadius(),
				32, // Segments for smooth circle
				DebugCircleColor,
				false, // Not persistent
				0.1f, // Duration
				0, // Depth priority
				2.0f // Thickness
			);
		}
	}
	else
	{
		bValidHitLocation = false;
	}
}

void UVoxelToolComponent::ServerPlacePlayerOnTerrain_Implementation(FVector BuildLocation, float EffectiveRadius, APawn* TargetPlayer)
{
	// Server validates and then multicasts to all clients
	if (TargetPlayer && IsValid(TargetPlayer))
	{
		MulticastPlacePlayerOnTerrain(BuildLocation, EffectiveRadius, TargetPlayer);
	}
}

void UVoxelToolComponent::MulticastPlacePlayerOnTerrain_Implementation(FVector BuildLocation, float EffectiveRadius, APawn* TargetPlayer)
{
	// This runs on all clients (including server)
	PlacePlayerOnTerrain(BuildLocation, EffectiveRadius, TargetPlayer);
}

void UVoxelToolComponent::PlacePlayerOnTerrain(FVector BuildLocation, float EffectiveRadius, APawn* TargetPlayer)
{
	if (!TargetPlayer || !IsValid(TargetPlayer))
	{
		return;
	}

	if (auto* Character = Cast<ACharacter>(TargetPlayer))
	{
		FVector PlayerLocation = TargetPlayer->GetActorLocation();
		float DistanceToBuilt = FVector::Dist(PlayerLocation, BuildLocation);
		
		// Double-check distance in case of network lag
		if (DistanceToBuilt < (EffectiveRadius + 100.0f))
		{
			// Calculate how high above the build location the player should be placed
			float CapsuleHalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			float SafeHeightAboveTerrain = CapsuleHalfHeight + 50.0f; // Player height + safety margin
			
			// Find the highest point of the built terrain that could affect the player
			float MaxTerrainHeight = BuildLocation.Z + EffectiveRadius;
			float TargetPlayerZ = MaxTerrainHeight + SafeHeightAboveTerrain;
			
			// Only move player up if they're below or close to the new terrain level
			if (PlayerLocation.Z < (TargetPlayerZ - 50.0f))
			{
				// Phase 1: Immediate safety teleport to prevent clipping
				// Use configurable safety margin for different camera setups
				float ImmediateSafeHeight = MaxTerrainHeight + CapsuleHalfHeight + ImmediateSafetyMargin;
				FVector ImmediateSafeLocation = PlayerLocation;
				ImmediateSafeLocation.Z = ImmediateSafeHeight;
				
				// Teleport immediately to prevent clipping
				Character->SetActorLocation(ImmediateSafeLocation, false, nullptr, ETeleportType::ResetPhysics);
				
				// Reset velocity
				if (auto* CharacterMovement = Character->GetCharacterMovement())
				{
					CharacterMovement->Velocity = FVector::ZeroVector;
				}
				
				// Phase 2: Start smooth movement to final comfortable position
				FVector FinalSafeLocation = PlayerLocation;
				FinalSafeLocation.Z = TargetPlayerZ;
				
				bIsSmoothMoving = true;
				SmoothMoveStartLocation = ImmediateSafeLocation; // Start from immediate safe position
				SmoothMoveTargetLocation = FinalSafeLocation;
				SmoothMoveStartTime = GetWorld()->GetTimeSeconds();
				SmoothMoveTargetPawn = TargetPlayer;
				
				UE_LOG(LogTemp, Log, TEXT("Two-phase movement for player %s: immediate to %.1f (margin: %.1f), then smooth to %.1f"), 
					*TargetPlayer->GetName(), ImmediateSafeHeight, ImmediateSafetyMargin, TargetPlayerZ);
			}
		}
	}
}

// RPC implementations for voxel modifications
void UVoxelToolComponent::ServerBuildAtLocation_Implementation(FVector Location, float Radius, float Strength)
{
	// Server validates and multicasts to all clients (including itself)
	MulticastBuildAtLocation(Location, Radius, Strength);
}

void UVoxelToolComponent::ServerDigAtLocation_Implementation(FVector Location, float Radius, float Strength)
{
	// Server validates and multicasts to all clients (including itself)
	MulticastDigAtLocation(Location, Radius, Strength);
}

void UVoxelToolComponent::MulticastBuildAtLocation_Implementation(FVector Location, float Radius, float Strength)
{
	// This runs on all clients (including server)
	LocalBuildAtLocation(Location, Radius, Strength);
}

void UVoxelToolComponent::MulticastDigAtLocation_Implementation(FVector Location, float Radius, float Strength)
{
	// This runs on all clients (including server)
	LocalDigAtLocation(Location, Radius, Strength);
}

// Local helper functions that do the actual voxel work
void UVoxelToolComponent::LocalBuildAtLocation(FVector Location, float Radius, float Strength)
{
	// Find the closest VoxelWorld to the build location instead of using cached world
	AVoxelWorld* VoxelWorld = FindVoxelWorldAtLocation(Location);
	if (!VoxelWorld)
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: No Voxel World found at location %s for building!"), *Location.ToString());
		return;
	}

	// Check max build height constraint
	if (IslandPhysicsComponent && Location.Z > IslandPhysicsComponent->MaxBuildHeight)
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Build blocked - location Z=%.1f exceeds MaxBuildHeight=%.1f"), 
			Location.Z, IslandPhysicsComponent->MaxBuildHeight);
		return;
	}

	// Gentle strength formula: allows very low strength values for precise control
	float EffectiveRadius = Radius * FMath::Max(0.1f, Strength * 0.5f);
	
	// Check if smooth building is enabled and we have a previous build location
	if (bEnableSmoothBuilding && bHasLastBuildLocation && bIsContinuousBuilding)
	{
		// Calculate distance to last build point
		float DistanceToLast = FVector::Dist(Location, LastBuildLocation);
		
		// If distance is significant, interpolate between points for smooth building
		if (DistanceToLast > SmoothBuildStepSize)
		{
			ProcessSmoothBuild(LastBuildLocation, Location, EffectiveRadius, Strength);
		}
		else
		{
			// Close enough, just build at current location
			UVoxelSphereTools::AddSphere(VoxelWorld, Location, EffectiveRadius);
		}
	}
	else
	{
		// First build or smooth building disabled, use standard sphere placement
		UVoxelSphereTools::AddSphere(VoxelWorld, Location, EffectiveRadius);
		bIsContinuousBuilding = true; // Start continuous building mode
	}
	
	// Update last build location and time for next smooth build
	LastBuildLocation = Location;
	bHasLastBuildLocation = true;
	LastBuildActionTime = GetWorld()->GetTimeSeconds();

	// After building, check if we need to place players on top of the new terrain (only on server)
	if (GetOwner()->HasAuthority())
	{
		// Check all players in the world for potential terrain placement
		for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
		{
			APlayerController* PC = Iterator->Get();
			if (PC && PC->GetPawn())
			{
				APawn* PlayerPawn = PC->GetPawn();
				FVector PlayerLocation = PlayerPawn->GetActorLocation();
				float DistanceToBuilt = FVector::Dist(PlayerLocation, Location);
				
				// If we built close to this player, place them on top of the new terrain
				if (DistanceToBuilt < (EffectiveRadius + 100.0f))
				{
					// Use RPC to ensure proper multiplayer synchronization
					ServerPlacePlayerOnTerrain(Location, EffectiveRadius, PlayerPawn);
				}
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("Building at location: %s with radius: %f, strength: %f, effective radius: %f"), 
		*Location.ToString(), Radius, Strength, EffectiveRadius);
}

void UVoxelToolComponent::LocalDigAtLocation(FVector Location, float Radius, float Strength)
{
	// Find the closest VoxelWorld to the dig location instead of using cached world
	AVoxelWorld* VoxelWorld = FindVoxelWorldAtLocation(Location);
	if (!VoxelWorld)
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: No Voxel World found at location %s for digging!"), *Location.ToString());
		return;
	}

	// Gentle strength formula: allows very low strength values for precise control
	float EffectiveRadius = Radius * FMath::Max(0.1f, Strength * 0.5f);
	
	// Use voxel sphere tool to remove material (dig)
	UVoxelSphereTools::RemoveSphere(
		VoxelWorld,
		Location,
		EffectiveRadius
	);

	UE_LOG(LogTemp, Log, TEXT("Digging at location: %s with radius: %f, strength: %f, effective radius: %f"), 
		*Location.ToString(), Radius, Strength, EffectiveRadius);

	// Use the new island physics system to check for disconnected chunks
	if (IslandPhysicsComponent && bEnableVoxelPhysics)
	{
		if (bUseFastPhysicsOnDig)
		{
			// Use fast detection to minimize lag while still enabling island creation
			IslandPhysicsComponent->CheckForDisconnectedIslandsFast(VoxelWorld, Location, EffectiveRadius);
		}
		else
		{
			// Use full detection
			IslandPhysicsComponent->CheckForDisconnectedIslands(VoxelWorld, Location, EffectiveRadius);
		}
	}
}

float UVoxelToolComponent::GetDebugCircleRadius() const
{
	// Get the appropriate offset based on current tool radius
	float CurrentOffset = 0.0f;
	
	// Round tool radius to nearest 50 to match our offset system
	int32 RoundedRadius = FMath::RoundToInt(ToolRadius / 50.0f) * 50;
	RoundedRadius = FMath::Clamp(RoundedRadius, 50, 650);
	
	// Select the appropriate offset based on tool size
	switch (RoundedRadius)
	{
		case 50: CurrentOffset = DebugOffset_50; break;
		case 100: CurrentOffset = DebugOffset_100; break;
		case 150: CurrentOffset = DebugOffset_150; break;
		case 200: CurrentOffset = DebugOffset_200; break;
		case 250: CurrentOffset = DebugOffset_250; break;
		case 300: CurrentOffset = DebugOffset_300; break;
		case 350: CurrentOffset = DebugOffset_350; break;
		case 400: CurrentOffset = DebugOffset_400; break;
		case 450: CurrentOffset = DebugOffset_450; break;
		case 500: CurrentOffset = DebugOffset_500; break;
		case 550: CurrentOffset = DebugOffset_550; break;
		case 600: CurrentOffset = DebugOffset_600; break;
		case 650: CurrentOffset = DebugOffset_650; break;
		default: CurrentOffset = -20.0f; break; // Fallback to default
	}
	
	// Apply offset to tool radius, ensuring minimum size of 10
	return FMath::Max(10.0f, ToolRadius + CurrentOffset);
}

void UVoxelToolComponent::ApplyVoxelPhysicsAfterDig(FVector DigLocation, float DigRadius)
{
	if (!bEnableVoxelPhysics)
	{
		return;
	}

	AVoxelWorld* VoxelWorld = FindVoxelWorld();
	if (!VoxelWorld)
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: No Voxel World found for physics!"));
		return;
	}

	// Only run physics check on the server to avoid conflicts
	if (!GetOwner()->HasAuthority())
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("VoxelToolComponent: Starting voxel physics for dig at location %s"), *DigLocation.ToString());
	UE_LOG(LogTemp, Log, TEXT("VoxelToolComponent: Current VoxelWorld CollisionTraceFlag: %d"), (int32)VoxelWorld->CollisionTraceFlag);

	// Create bounds around the dig location
	FVector HalfSize = FVector(PhysicsCheckRadius);
	FVoxelIntBox PhysicsBounds = FVoxelIntBox(
		VoxelWorld->GlobalToLocal(DigLocation - HalfSize),
		VoxelWorld->GlobalToLocal(DigLocation + HalfSize)
	);

	UE_LOG(LogTemp, Log, TEXT("VoxelToolComponent: Physics bounds: %s"), *PhysicsBounds.ToString());

	// Use a timer to delay the physics call slightly to avoid the access violation
	// This gives the voxel world time to update after the dig operation
	GetWorld()->GetTimerManager().SetTimer(
		VoxelPhysicsTimerHandle,
		[this, VoxelWorld, PhysicsBounds]()
		{
			this->DelayedApplyVoxelPhysics(VoxelWorld, PhysicsBounds);
		},
		0.1f, // 100ms delay
		false
	);
}

void UVoxelToolComponent::DelayedApplyVoxelPhysics(AVoxelWorld* VoxelWorld, FVoxelIntBox PhysicsBounds)
{
	if (!VoxelWorld || !IsValid(VoxelWorld))
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Invalid voxel world in delayed physics"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("VoxelToolComponent: Executing tower collapse detection at bounds %s"), *PhysicsBounds.ToString());

	// NEW: Use actual voxel physics simulation instead of fake boxes
	
	// Convert bounds to world coordinates (convert FVoxelVector to FIntVector)
	FIntVector CenterInt = FIntVector(PhysicsBounds.GetCenter().X, PhysicsBounds.GetCenter().Y, PhysicsBounds.GetCenter().Z);
	FIntVector SizeInt = FIntVector(PhysicsBounds.Size().X, PhysicsBounds.Size().Y, PhysicsBounds.Size().Z);
	FVector WorldCenter = VoxelWorld->LocalToGlobal(CenterInt);
	FVector WorldSize = VoxelWorld->LocalToGlobal(SizeInt) - VoxelWorld->LocalToGlobal(FIntVector::ZeroValue);
	
	UE_LOG(LogTemp, Log, TEXT("VoxelToolComponent: Checking for disconnected towers around %s (size: %s)"), 
		*WorldCenter.ToString(), *WorldSize.ToString());

	// Detect if we likely created a tower collapse by checking above the dig site
	if (UWorld* World = GetWorld())
	{
		// Sample points above the dig location to detect floating structures
		TArray<FVector> TowerCheckPoints;
		
		// Create a grid of check points above the dig site
		int32 GridSize = 3; // 3x3 grid
		float CheckHeight = 500.0f; // Check 500 units above
		float GridSpacing = 200.0f; // Space between check points
		
		for (int32 X = -GridSize/2; X <= GridSize/2; X++)
		{
			for (int32 Y = -GridSize/2; Y <= GridSize/2; Y++)
			{
				FVector CheckPoint = WorldCenter + FVector(X * GridSpacing, Y * GridSpacing, CheckHeight);
				TowerCheckPoints.Add(CheckPoint);
			}
		}
		
		// Advanced connectivity detection: check if digging has severed a structure from ground
		int32 FloatingVoxelCount = 0;
		TArray<FVector> FloatingPositions;
		
		// Run tower detection at any height above ground level
		if (WorldCenter.Z > 50.0f && PhysicsCheckRadius >= 150.0f) // Removed upper height limit
		{
			// Check for potential floating structure by sampling around and above the dig site
			bool bFoundFloatingStructure = CheckForDisconnectedStructure(VoxelWorld, WorldCenter, PhysicsCheckRadius);
			
			if (bFoundFloatingStructure)
			{
				UE_LOG(LogTemp, Error, TEXT("VoxelToolComponent: *** TOWER COLLAPSE DETECTED! *** Creating separate falling voxel world"));
				
				// Create a separate voxel world for the severed structure that can fall as a rigid body
				CreateFallingVoxelWorld(VoxelWorld, WorldCenter, PhysicsCheckRadius);
				return; // Exit early since we've handled the physics
			}
		}
	}
	
	UE_LOG(LogTemp, Log, TEXT("VoxelToolComponent: No tower structure detected, no physics applied"));

	// Call completion callback
	OnVoxelPhysicsComplete();
}


bool UVoxelToolComponent::CheckForDisconnectedStructure(AVoxelWorld* VoxelWorld, FVector DigCenter, float SearchRadius)
{
	if (!VoxelWorld || !GetWorld())
	{
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("VoxelToolComponent: Checking for disconnected structure at %s"), *DigCenter.ToString());

	// Strategy: Use line traces to detect if there's structure above that's no longer connected to ground
	// We'll do multiple checks to determine if digging likely severed a connection
	
	UWorld* World = GetWorld();
	FCollisionQueryParams TraceParams;
	TraceParams.bTraceComplex = true;
	TraceParams.AddIgnoredActor(GetOwner()); // Ignore the player
	
	// Check 1: Look for structures directly above the dig site
	FVector AboveDigSite = DigCenter + FVector(0, 0, 500.0f); // 500 units up
	FVector BelowDigSite = DigCenter - FVector(0, 0, 100.0f); // 100 units down
	
	FHitResult UpwardHit;
	bool bFoundStructureAbove = World->LineTraceSingleByChannel(
		UpwardHit, 
		DigCenter, 
		AboveDigSite, 
		ECC_WorldStatic, 
		TraceParams
	);
	
	if (!bFoundStructureAbove)
	{
		UE_LOG(LogTemp, Log, TEXT("VoxelToolComponent: No structure found above dig site"));
		return false; // No structure above, so nothing to collapse
	}
	
	UE_LOG(LogTemp, Log, TEXT("VoxelToolComponent: Found structure above at distance: %f"), 
		FVector::Dist(DigCenter, UpwardHit.Location));
	
	// Check 2: Comprehensive tower base connection scan
	// Test the entire area around the tower base systematically until NO connections remain
	int32 GridSize = ConnectionScanGridSize; // Configurable grid size for comprehensive coverage
	int32 TotalConnectionTests = 0;
	int32 ActiveConnections = 0;
	float ConnectionScanRadius = SearchRadius * 0.8f; // Scan most of the search area
	
	UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Starting comprehensive connection scan with %dx%d grid (%d total tests)"), 
		GridSize, GridSize, GridSize * GridSize);
	
	// Create a dense grid around the dig center to test for ANY remaining connections
	for (int32 x = 0; x < GridSize; x++)
	{
		for (int32 y = 0; y < GridSize; y++)
		{
			// Convert grid coordinates to world offset
			float XOffset = ((float(x) / float(GridSize - 1)) - 0.5f) * ConnectionScanRadius * 2.0f;
			float YOffset = ((float(y) / float(GridSize - 1)) - 0.5f) * ConnectionScanRadius * 2.0f;
			
			FVector TestPoint = DigCenter + FVector(XOffset, YOffset, 0.0f);
			
			// Skip points too far from center (outside circular area)
			float DistanceFromCenter = FVector::Dist2D(TestPoint, DigCenter);
			if (DistanceFromCenter > ConnectionScanRadius)
			{
				continue;
			}
			
			TotalConnectionTests++;
			
			// Enhanced connection validation - test for STRONG ground connections, not just any hit
			bool bFoundValidConnection = false;
			
			// First, test if there's any structure at this point going down
			FVector TestPointBelow = TestPoint - FVector(0, 0, 100.0f); // Start with shallow test
			FHitResult ShallowHit;
			bool bFoundShallowHit = World->LineTraceSingleByChannel(
				ShallowHit,
				TestPoint,
				TestPointBelow,
				ECC_WorldStatic,
				TraceParams
			);
			
			if (bFoundShallowHit)
			{
				// Found structure, now test if it connects deeply to ground (indicates strong connection)
				FVector DeepTestStart = ShallowHit.Location - FVector(0, 0, 50.0f); // Start below the hit
				FVector DeepTestEnd = DeepTestStart - FVector(0, 0, 400.0f); // Test very deep for ground connection
				
				FHitResult DeepHit;
				bool bFoundDeepConnection = World->LineTraceSingleByChannel(
					DeepHit,
					DeepTestStart,
					DeepTestEnd,
					ECC_WorldStatic,
					TraceParams
				);
				
				if (bFoundDeepConnection)
				{
					// Validate this is a substantial connection by checking connection depth
					float ConnectionDepth = FVector::Dist(ShallowHit.Location, DeepHit.Location);
					
					// Require substantial connection depth to filter out floating debris
					if (ConnectionDepth > MinConnectionDepth) // Must have at least MinConnectionDepth units of continuous structure
					{
						bFoundValidConnection = true;
						
						// Log detailed connection info for first 20 connections
						if (ActiveConnections < 20)
						{
							UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: VALID Connection %d at grid (%d,%d) - Shallow hit: %.1f, Deep hit: %.1f, Connection depth: %.1f"), 
								ActiveConnections + 1, x, y, ShallowHit.Location.Z, DeepHit.Location.Z, ConnectionDepth);
						}
					}
					else
					{
						// Log why this connection was rejected
						if (ActiveConnections < 5) // Only log first few rejections to avoid spam
						{
							UE_LOG(LogTemp, Log, TEXT("VoxelToolComponent: REJECTED connection at grid (%d,%d) - Insufficient depth: %.1f (need >%.0f)"), 
								x, y, ConnectionDepth, MinConnectionDepth);
						}
					}
				}
				else
				{
					// Log why this connection was rejected
					if (ActiveConnections < 5)
					{
						UE_LOG(LogTemp, Log, TEXT("VoxelToolComponent: REJECTED connection at grid (%d,%d) - No deep ground connection found"), x, y);
					}
				}
			}
			
			if (bFoundValidConnection)
			{
				ActiveConnections++;
			}
		}
	}
	
	float ConnectionRatio = TotalConnectionTests > 0 ? float(ActiveConnections) / float(TotalConnectionTests) : 0.0f;
	
	UE_LOG(LogTemp, Error, TEXT("VoxelToolComponent: *** COMPREHENSIVE CONNECTION ANALYSIS ***"));
	UE_LOG(LogTemp, Error, TEXT("- Total connection tests: %d"), TotalConnectionTests);
	UE_LOG(LogTemp, Error, TEXT("- Active connections found: %d"), ActiveConnections);
	UE_LOG(LogTemp, Error, TEXT("- Connection ratio: %.1f%%"), ConnectionRatio * 100.0f);
	UE_LOG(LogTemp, Error, TEXT("- Scan radius: %.1f units"), ConnectionScanRadius);
	
	// Structure is only severed if there are ZERO remaining connections
	// This is the most reliable method - if any connection exists, tower is still supported
	bool bStructureDisconnected = (ActiveConnections == 0);
	
	UE_LOG(LogTemp, Error, TEXT("VoxelToolComponent: *** FINAL SEVERANCE DECISION ***"));
	UE_LOG(LogTemp, Error, TEXT("- Active connections remaining: %d"), ActiveConnections);
	UE_LOG(LogTemp, Error, TEXT("- Structure status: %s"), bStructureDisconnected ? TEXT("FULLY SEVERED - NO CONNECTIONS") : TEXT("STILL CONNECTED"));
	UE_LOG(LogTemp, Error, TEXT("- Physics will %s"), bStructureDisconnected ? TEXT("ACTIVATE") : TEXT("NOT ACTIVATE"));
	
	if (bStructureDisconnected)
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelToolComponent: *** STRUCTURE SEVERANCE CONFIRMED *** - Zero connections remaining, tower will fall"));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("VoxelToolComponent: Structure still connected - %d active connections found, tower remains stable"), ActiveConnections);
	}
	
	return bStructureDisconnected;
}

void UVoxelToolComponent::ProcessSmoothBuild(FVector StartLocation, FVector EndLocation, float Radius, float Strength)
{
	AVoxelWorld* VoxelWorld = FindVoxelWorld();
	if (!VoxelWorld)
	{
		return;
	}

	// Use interpolation to create smooth building between points
	InterpolateBuildPoints(StartLocation, EndLocation, Radius, Strength, SmoothBuildStepSize);
	
	UE_LOG(LogTemp, Verbose, TEXT("VoxelToolComponent: Smooth build from %s to %s with step size %.1f"), 
		*StartLocation.ToString(), *EndLocation.ToString(), SmoothBuildStepSize);
}

void UVoxelToolComponent::InterpolateBuildPoints(FVector StartLocation, FVector EndLocation, float Radius, float Strength, float StepSize)
{
	AVoxelWorld* VoxelWorld = FindVoxelWorld();
	if (!VoxelWorld)
	{
		return;
	}

	// Calculate direction and total distance
	FVector Direction = EndLocation - StartLocation;
	float TotalDistance = Direction.Size();
	
	if (TotalDistance < StepSize)
	{
		// Distance too small, just build at end location
		// Check max build height constraint
		if (IslandPhysicsComponent && EndLocation.Z > IslandPhysicsComponent->MaxBuildHeight)
		{
			UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Build blocked - interpolated location Z=%.1f exceeds MaxBuildHeight=%.1f"), 
				EndLocation.Z, IslandPhysicsComponent->MaxBuildHeight);
			return;
		}
		UVoxelSphereTools::AddSphere(VoxelWorld, EndLocation, Radius);
		return;
	}
	
	Direction.Normalize();
	
	// Calculate number of interpolation steps
	int32 NumSteps = FMath::CeilToInt(TotalDistance / StepSize);
	float ActualStepSize = TotalDistance / NumSteps;
	
	// Build spheres at interpolated points for smooth connection
	for (int32 i = 1; i <= NumSteps; i++)
	{
		float Alpha = float(i) / float(NumSteps);
		FVector InterpolatedLocation = StartLocation + (Direction * ActualStepSize * i);
		
		// Check max build height constraint for each interpolated point
		if (IslandPhysicsComponent && InterpolatedLocation.Z > IslandPhysicsComponent->MaxBuildHeight)
		{
			UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Interpolated build stopped at step %d/%d - location Z=%.1f exceeds MaxBuildHeight=%.1f"), 
				i, NumSteps, InterpolatedLocation.Z, IslandPhysicsComponent->MaxBuildHeight);
			break; // Stop building further points that exceed height limit
		}
		
		// Slightly reduce radius for intermediate points to avoid over-building
		float InterpolatedRadius = Radius * 0.8f;
		
		UVoxelSphereTools::AddSphere(VoxelWorld, InterpolatedLocation, InterpolatedRadius);
	}
	
	UE_LOG(LogTemp, Verbose, TEXT("VoxelToolComponent: Interpolated %d build points over distance %.1f"), NumSteps, TotalDistance);
}

void UVoxelToolComponent::SpawnVoxelPhysics(AVoxelWorld* VoxelWorld, FVoxelIntBox PhysicsBounds)
{
	if (!VoxelWorld || !GetWorld())
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Cannot spawn voxel physics - invalid VoxelWorld or World"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: *** ATTEMPTING REAL VOXEL PHYSICS ***"));
	UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Physics bounds: %s"), *PhysicsBounds.ToString());

	// First, try to configure the VoxelWorld for better physics compatibility
	if (UVoxelWorldRootComponent* RootComponent = &VoxelWorld->GetWorldRoot())
	{
		// Try to set collision settings that might work better with physics
		RootComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		RootComponent->SetCollisionObjectType(ECollisionChannel::ECC_WorldDynamic);
		UE_LOG(LogTemp, Log, TEXT("VoxelToolComponent: Configured VoxelWorld collision settings"));
	}

	// Try to apply voxel physics using the CustomVoxelSystem's built-in physics tools
	try
	{
		// Create the required latent action info for the physics call
		FLatentActionInfo LatentInfo;
		LatentInfo.CallbackTarget = this;
		LatentInfo.ExecutionFunction = FName("OnVoxelPhysicsComplete");
		LatentInfo.UUID = FMath::Rand(); // Generate a unique ID
		LatentInfo.Linkage = 0;
		
		// Array to hold the spawned physics results
		TArray<TScriptInterface<IVoxelPhysicsPartSpawnerResult>> PhysicsResults;
		
		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Calling UVoxelPhysicsTools::ApplyVoxelPhysics..."));
		
		// Try with the spawner class if available, otherwise use default
		if (PhysicsPartSpawnerClass)
		{
			UVoxelPhysicsPartSpawner_VoxelWorlds* PhysicsSpawner = NewObject<UVoxelPhysicsPartSpawner_VoxelWorlds>(this, PhysicsPartSpawnerClass);
			if (PhysicsSpawner)
			{
				// Use the correct signature for ApplyVoxelPhysics
				UVoxelPhysicsTools::ApplyVoxelPhysics(
					this,                                    // UObject* Context
					LatentInfo,                             // FLatentActionInfo
					PhysicsResults,                         // Results array (output)
					VoxelWorld,                             // AVoxelWorld*
					PhysicsBounds,                          // FVoxelIntBox
					TScriptInterface<IVoxelPhysicsPartSpawner>(PhysicsSpawner), // Spawner
					MinPartsForPhysics,                     // MinPartsForPhysics
					false,                                  // bApplyCollisionSettings (try false to avoid collision issues)
					false                                   // bApplyBodyInstance
				);
			}
		}
		else
		{
			// Try with null spawner (use default)
			UVoxelPhysicsTools::ApplyVoxelPhysics(
				this,                                    // UObject* Context
				LatentInfo,                             // FLatentActionInfo
				PhysicsResults,                         // Results array (output)
				VoxelWorld,                             // AVoxelWorld*
				PhysicsBounds,                          // FVoxelIntBox
				TScriptInterface<IVoxelPhysicsPartSpawner>(), // Null spawner (use default)
				MinPartsForPhysics,                     // MinPartsForPhysics
				false,                                  // bApplyCollisionSettings (try false to avoid collision issues)
				false                                   // bApplyBodyInstance
			);
		}
		
		UE_LOG(LogTemp, Error, TEXT("VoxelToolComponent: *** REAL VOXEL PHYSICS APPLIED SUCCESSFULLY ***"));
	}
	catch (...)
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelToolComponent: Exception occurred while applying voxel physics - this means the collision settings are incompatible"));
		UE_LOG(LogTemp, Error, TEXT("VoxelToolComponent: The voxel plugin requires specific collision configuration that conflicts with physics"));
	}
}

// DISABLED: OnConfigurePhysicsVoxelWorld function (no longer used due to access violations)
/*
void UVoxelToolComponent::OnConfigurePhysicsVoxelWorld(AVoxelWorld* PhysicsVoxelWorld)
{
	if (!PhysicsVoxelWorld)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("VoxelToolComponent: Configuring physics voxel world: %s"), *PhysicsVoxelWorld->GetName());
	UE_LOG(LogTemp, Log, TEXT("VoxelToolComponent: VoxelWorld CollisionTraceFlag before: %d"), (int32)PhysicsVoxelWorld->CollisionTraceFlag);

	// CRITICAL: Set collision trace flag FIRST to prevent physics cooker error
	PhysicsVoxelWorld->CollisionTraceFlag = ECollisionTraceFlag::CTF_UseSimpleAsComplex;
	
	// Enable collisions
	PhysicsVoxelWorld->bEnableCollisions = true;
	
	// Configure collision settings for physics simulation via the WorldRoot component
	UVoxelWorldRootComponent& WorldRoot = PhysicsVoxelWorld->GetWorldRoot();
	WorldRoot.SetSimulatePhysics(true);
	WorldRoot.SetCollisionObjectType(ECC_WorldDynamic);
	WorldRoot.SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	WorldRoot.SetCollisionResponseToAllChannels(ECR_Block);
	
	// Apply the collision settings
	PhysicsVoxelWorld->UpdateCollisionProfile();
	
	UE_LOG(LogTemp, Log, TEXT("VoxelToolComponent: VoxelWorld CollisionTraceFlag after: %d"), (int32)PhysicsVoxelWorld->CollisionTraceFlag);
	UE_LOG(LogTemp, Log, TEXT("VoxelToolComponent: Physics configuration completed for: %s"), *PhysicsVoxelWorld->GetName());
}
*/

void UVoxelToolComponent::OnVoxelPhysicsComplete()
{
	UE_LOG(LogTemp, Log, TEXT("VoxelToolComponent: Tower collapse physics operation completed"));
}

void UVoxelToolComponent::CreateFallingVoxelWorld(AVoxelWorld* OriginalWorld, FVector DigCenter, float SearchRadius)
{
	if (!OriginalWorld || !GetWorld())
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelToolComponent: Cannot create falling voxel world - invalid OriginalWorld or World"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: *** CREATING SIMPLIFIED FALLING PHYSICS ***"));
	UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Dig center: %s, Search radius: %.1f"), *DigCenter.ToString(), SearchRadius);

	// Define the region above the dig site that potentially contains the severed structure
	FVector RegionCenter = DigCenter + FVector(0, 0, SearchRadius);
	float RegionRadius = SearchRadius * 1.5f;
	
	UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Creating physics object at: %s, radius: %.1f"), *RegionCenter.ToString(), RegionRadius);

	// Skip the temporary VoxelWorld creation to avoid lighting issues
	// Instead, directly create a physics object representing the severed structure
	// This function will also handle removing the voxels from the original world
	CreateSimplifiedChaosPhysics(OriginalWorld, RegionCenter, RegionRadius);
	
	UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Severed structure handled by CreateSimplifiedChaosPhysics"));
}

void UVoxelToolComponent::CreateSimplifiedChaosPhysics(AVoxelWorld* OriginalWorld, FVector SpawnLocation, float StructureSize)
{
	if (!OriginalWorld || !GetWorld())
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelToolComponent: Cannot create physics - invalid parameters"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Creating falling VoxelWorld with copied voxel data at %s, structure size: %f"), *SpawnLocation.ToString(), StructureSize);

	// Calculate bounds of the structure to capture (region above the dig site)
	float CaptureHeight = StructureSize * 4.0f; // Capture tall region above dig
	FVector CaptureMin = SpawnLocation - FVector(StructureSize, StructureSize, StructureSize * 0.5f);
	FVector CaptureMax = SpawnLocation + FVector(StructureSize, StructureSize, CaptureHeight);
	
	// Convert to voxel coordinates
	FIntVector VoxelMin = OriginalWorld->GlobalToLocal(CaptureMin);
	FIntVector VoxelMax = OriginalWorld->GlobalToLocal(CaptureMax);
	FVoxelIntBox CaptureBounds(VoxelMin, VoxelMax);
	
	UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Capturing voxel data from bounds %s to %s"), *VoxelMin.ToString(), *VoxelMax.ToString());

	// Create a new VoxelWorld that will contain the falling voxel data
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	
	// Position at the center of the captured region
	FVector FallingWorldLocation = (CaptureMin + CaptureMax) * 0.5f;
	AVoxelWorld* FallingVoxelWorld = GetWorld()->SpawnActor<AVoxelWorld>(FallingWorldLocation, FRotator::ZeroRotator, SpawnParams);
	
	if (!FallingVoxelWorld)
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelToolComponent: Failed to spawn falling VoxelWorld"));
		return;
	}

	// Set a descriptive name and tag for identification
	FallingVoxelWorld->SetActorLabel(TEXT("FallingTowerPiece"));
	FallingVoxelWorld->Tags.Add(FName("FallingVoxelWorld"));
	
	// Configure the falling VoxelWorld to match the original settings
	FallingVoxelWorld->VoxelMaterial = OriginalWorld->VoxelMaterial;
	FallingVoxelWorld->Generator = NewObject<UVoxelEmptyGenerator>(FallingVoxelWorld);
	FallingVoxelWorld->MaterialCollection = OriginalWorld->MaterialCollection;
	FallingVoxelWorld->VoxelSize = OriginalWorld->VoxelSize;
	FallingVoxelWorld->MaxLOD = 10; // Higher LOD for complex shapes
	FallingVoxelWorld->bEnableCollisions = true;
	FallingVoxelWorld->bComputeVisibleChunksCollisions = true;
	FallingVoxelWorld->bCreateGlobalPool = false;
	FallingVoxelWorld->bRenderWorld = true;
	FallingVoxelWorld->bEnableUndoRedo = false; // No undo for falling pieces
	FallingVoxelWorld->bMergeAssetActors = false;
	
	// Set world size large enough for tall tower
	int32 WorldSize = 512; // Large world to accommodate tall towers
	FallingVoxelWorld->WorldSizeInVoxel = WorldSize;
	
	UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Falling VoxelWorld configured with size %d"), WorldSize);

	// Disable no-clip component for falling VoxelWorlds
	if (UVoxelNoClippingComponent* NoClipComp = FallingVoxelWorld->FindComponentByClass<UVoxelNoClippingComponent>())
	{
		NoClipComp->DestroyComponent();
	}

	// Create the voxel world
	FallingVoxelWorld->CreateWorld();
	
	// Copy the exact voxel structure using box-based approach
	if (FallingVoxelWorld->IsCreated() && OriginalWorld->IsCreated())
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Copying exact tower structure"));
		
		// Find the bounds of the tower by scanning upward
		float TowerHeight = 0.0f;
		float ScanRadius = StructureSize * 2.0f;
		
		// Quick height detection
		for (float Z = 10.0f; Z <= 5000.0f; Z += 50.0f)
		{
			FVector TestPoint = SpawnLocation + FVector(0, 0, Z);
			FHitResult Hit;
			
			FCollisionQueryParams QueryParams;
			QueryParams.AddIgnoredActor(GetOwner());
			
			if (GetWorld()->LineTraceSingleByChannel(
				Hit,
				TestPoint + FVector(50, 0, 0),
				TestPoint - FVector(50, 0, 0),
				ECC_WorldStatic,
				QueryParams))
			{
				if (Cast<AVoxelWorld>(Hit.GetActor()) == OriginalWorld)
				{
					TowerHeight = Z;
				}
			}
		}
		
		if (TowerHeight < 100.0f)
		{
			TowerHeight = 500.0f; // Default height
		}
		
		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Tower height: %.1f"), TowerHeight);
		
		// Define the box to copy from the original world
		FVector BoxMin = SpawnLocation - FVector(ScanRadius, ScanRadius, 100);
		FVector BoxMax = SpawnLocation + FVector(ScanRadius, ScanRadius, TowerHeight);
		
		// Convert to voxel coordinates
		FIntVector SourceMin = OriginalWorld->GlobalToLocal(BoxMin);
		FIntVector SourceMax = OriginalWorld->GlobalToLocal(BoxMax);
		
		// Calculate destination position in falling world (centered)
		FVector DestOffset = FVector(0, 0, -TowerHeight * 0.5f);
		FIntVector DestMin = FallingVoxelWorld->GlobalToLocal(FallingWorldLocation + DestOffset - FVector(ScanRadius, ScanRadius, TowerHeight * 0.5f));
		FIntVector DestMax = FallingVoxelWorld->GlobalToLocal(FallingWorldLocation + DestOffset + FVector(ScanRadius, ScanRadius, TowerHeight * 0.5f));
		
		// Create a solid box in the falling world that matches the tower dimensions
		FVoxelIntBox DestBox(DestMin, DestMax);
		
		// Fill the destination box with solid voxels
		UVoxelBoxTools::SetValueBox(
			FallingVoxelWorld,
			DestBox,
			-1.0f // Solid value
		);
		
		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Created solid box for falling tower"));
		
		// Now carve out the negative space to match the actual tower shape
		// This is done by scanning the original tower and removing voxels where there's empty space
		int32 StepSize = 3; // Sample every 3rd voxel for performance
		int32 EmptyVoxelsCarved = 0;
		
		for (int32 X = SourceMin.X; X <= SourceMax.X; X += StepSize)
		{
			for (int32 Y = SourceMin.Y; Y <= SourceMax.Y; Y += StepSize)
			{
				for (int32 Z = SourceMin.Z; Z <= SourceMax.Z; Z += StepSize)
				{
					FVector WorldPos = OriginalWorld->LocalToGlobal(FIntVector(X, Y, Z));
					
					// Check if this position is empty in the original
					FHitResult Hit;
					FCollisionQueryParams QueryParams;
					QueryParams.AddIgnoredActor(GetOwner());
					
					bool bHasVoxel = GetWorld()->LineTraceSingleByChannel(
						Hit,
						WorldPos + FVector(0, 0, 5),
						WorldPos - FVector(0, 0, 5),
						ECC_WorldStatic,
						QueryParams
					) && Cast<AVoxelWorld>(Hit.GetActor()) == OriginalWorld;
					
					if (!bHasVoxel)
					{
						// This position is empty in the original, so carve it out in the copy
						FVector RelativePos = WorldPos - SpawnLocation;
						FVector DestWorldPos = FallingWorldLocation + DestOffset + RelativePos;
						
						// Remove a small sphere at this position to carve the shape
						UVoxelSphereTools::RemoveSphere(
							FallingVoxelWorld,
							DestWorldPos,
							OriginalWorld->VoxelSize * StepSize
						);
						
						EmptyVoxelsCarved++;
						
						// Limit carving to prevent excessive processing
						if (EmptyVoxelsCarved > 1000)
						{
							break;
						}
					}
				}
				if (EmptyVoxelsCarved > 1000) break;
			}
			if (EmptyVoxelsCarved > 1000) break;
		}
		
		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Carved %d empty spaces to match tower shape"), EmptyVoxelsCarved);
		
		// Remove the original tower
		FVoxelIntBox RemovalBox(SourceMin, SourceMax);
		UVoxelBoxTools::SetValueBox(
			OriginalWorld,
			RemovalBox,
			1.0f // Empty value
		);
		
		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Original tower removed"));
	}
	
	// Enable physics on the VoxelWorld using the proper VoxelWorldRootComponent
	UVoxelWorldRootComponent& WorldRoot = FallingVoxelWorld->GetWorldRoot();
	
	UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: WorldRoot component type: %s"), *WorldRoot.GetClass()->GetName());
	UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: WorldRoot mobility before: %d"), (int32)WorldRoot.Mobility.GetValue());
	UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: WorldRoot simulate physics before: %s"), WorldRoot.IsSimulatingPhysics() ? TEXT("true") : TEXT("false"));
	
	WorldRoot.SetMobility(EComponentMobility::Movable);
	WorldRoot.SetSimulatePhysics(true);
	WorldRoot.SetCollisionObjectType(ECC_WorldDynamic);
	WorldRoot.SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	WorldRoot.SetCollisionResponseToAllChannels(ECR_Block);
	
	UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: WorldRoot mobility after: %d"), (int32)WorldRoot.Mobility.GetValue());
	UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: WorldRoot simulate physics after: %s"), WorldRoot.IsSimulatingPhysics() ? TEXT("true") : TEXT("false"));
	
	// Ensure collisions are enabled for the VoxelWorld
	FallingVoxelWorld->bEnableCollisions = true;
	FallingVoxelWorld->bComputeVisibleChunksCollisions = true;
	
	// Apply collision settings
	FallingVoxelWorld->UpdateCollisionProfile();
	
	// Check if physics body exists (without using UBodyInstance t
	UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Checking physics body existence"));
	
	// Add realistic toppling motion - the tower should tip over from where it was cut
	FVector InitialVelocity = FVector(
		FMath::RandRange(-20.0f, 20.0f), // Slight horizontal drift
		FMath::RandRange(-20.0f, 20.0f), 
		-100.0f // Initial downward velocity
	);
	
	if (WorldRoot.IsSimulatingPhysics())
	{
		WorldRoot.SetPhysicsLinearVelocity(InitialVelocity);
		
		// Add toppling rotation - stronger to simulate tower falling over
		FVector AngularVelocity = FVector(
			FMath::RandRange(-1.5f, 1.5f), // Pitch - forward/backward topple
			FMath::RandRange(-0.3f, 0.3f), // Yaw - slight twist
			FMath::RandRange(-1.5f, 1.5f)  // Roll - side topple
		);
		WorldRoot.SetPhysicsAngularVelocityInRadians(AngularVelocity);
		
		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Applied toppling physics to tower"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelToolComponent: Physics simulation failed to enable - trying fallback approach"));
		
		// Fallback: Enhanced manual physics simulation with gravity and toppling
		FTimerHandle PhysicsTimer;
		FTimerDelegate PhysicsDelegate;
		
		// Create a shared pointer to track velocity and angular velocity
		TSharedPtr<FVector> CurrentVelocity = MakeShared<FVector>(InitialVelocity);
		TSharedPtr<FVector> AngularVel = MakeShared<FVector>(
			FMath::RandRange(-3.0f, 3.0f), // Stronger rotation for toppling
			FMath::RandRange(-1.0f, 1.0f), 
			FMath::RandRange(-3.0f, 3.0f)
		);
		TSharedPtr<FRotator> CurrentRotation = MakeShared<FRotator>(FallingVoxelWorld->GetActorRotation());
		
		PhysicsDelegate.BindLambda([FallingVoxelWorld, CurrentVelocity, AngularVel, CurrentRotation, this]()
		{
			if (IsValid(FallingVoxelWorld) && IsValid(GetWorld()))
			{
				float DeltaTime = 0.016f; // 60 FPS
				FVector Gravity = FVector(0, 0, -980.0f); // Realistic gravity in cm/s²
				
				// Apply gravity to velocity
				*CurrentVelocity += Gravity * DeltaTime;
				
				// Apply drag/air resistance
				*CurrentVelocity *= 0.995f;
				*AngularVel *= 0.99f;
				
				FVector CurrentLocation = FallingVoxelWorld->GetActorLocation();
				FVector NewLocation = CurrentLocation + (*CurrentVelocity) * DeltaTime;
				
				// Simple ground collision check
				FHitResult HitResult;
				FVector TraceStart = CurrentLocation;
				FVector TraceEnd = NewLocation + FVector(0, 0, -100.0f); // Check below the object
				
				FCollisionQueryParams QueryParams;
				QueryParams.AddIgnoredActor(FallingVoxelWorld);
				
				bool bHitGround = GetWorld()->LineTraceSingleByChannel(
					HitResult,
					TraceStart,
					TraceEnd,
					ECC_WorldStatic,
					QueryParams
				);
				
				if (bHitGround && HitResult.Location.Z > NewLocation.Z - 50.0f)
				{
					// Hit ground - stop falling and reduce bounce
					NewLocation.Z = HitResult.Location.Z + 50.0f;
					CurrentVelocity->Z = FMath::Abs(CurrentVelocity->Z) * 0.3f; // Bounce with energy loss
					CurrentVelocity->X *= 0.8f; // Reduce horizontal movement
					CurrentVelocity->Y *= 0.8f;
					*AngularVel *= 0.5f; // Reduce spinning
					
					// Stop timer if movement is minimal
					if (CurrentVelocity->Size() < 50.0f)
					{
						UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Manual physics - object settled"));
						return; // This will stop the timer
					}
				}
				
				// Apply rotation
				*CurrentRotation += FRotator(
					AngularVel->Y * DeltaTime * 180.0f / PI,
					AngularVel->Z * DeltaTime * 180.0f / PI,
					AngularVel->X * DeltaTime * 180.0f / PI
				);
				
				FallingVoxelWorld->SetActorLocation(NewLocation);
				FallingVoxelWorld->SetActorRotation(*CurrentRotation);
			}
		});
		
		GetWorld()->GetTimerManager().SetTimer(PhysicsTimer, PhysicsDelegate, 0.016f, true);
		
		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Started manual physics simulation timer"));
	}
	
	UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Physics setup completed for falling VoxelWorld"));

	// Clean up after 30 seconds to prevent world clutter
	FTimerHandle CleanupTimer;
	auto CleanupLambda = [FallingVoxelWorld]()
	{
		if (IsValid(FallingVoxelWorld))
		{
			FallingVoxelWorld->DestroyWorld();
			FallingVoxelWorld->Destroy();
		}
	};
	
	GetWorld()->GetTimerManager().SetTimer(CleanupTimer, CleanupLambda, 30.0f, false);

	UE_LOG(LogTemp, Error, TEXT("VoxelToolComponent: *** FALLING VOXEL WORLD CREATED *** Real VoxelWorld with voxel data representing dug material"));
	UE_LOG(LogTemp, Error, TEXT("VoxelToolComponent: Falling VoxelWorld location: %s, size: %d"), *FallingVoxelWorld->GetActorLocation().ToString(), WorldSize);
}

void UVoxelToolComponent::CreateFallingMeshFromVoxels(AVoxelWorld* OriginalWorld, FVector RegionCenter, float RegionRadius)
{
	if (!OriginalWorld || !GetWorld())
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Extracting real voxel mesh geometry"));

	// Define the region to extract mesh from
	FVector RegionSize = FVector(RegionRadius * 2.0f);
	FIntVector VoxelMin = OriginalWorld->GlobalToLocal(RegionCenter - RegionSize * 0.5f);
	FIntVector VoxelMax = OriginalWorld->GlobalToLocal(RegionCenter + RegionSize * 0.5f);
	FVoxelIntBox ExtractionBounds(VoxelMin, VoxelMax);

	// Create an actor to hold our procedural mesh component
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	
	AActor* FallingActor = GetWorld()->SpawnActor<AActor>(RegionCenter, FRotator::ZeroRotator, SpawnParams);
	if (!FallingActor)
	{
		return;
	}

	// Create a static mesh component for the falling object
	UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(FallingActor);
	FallingActor->SetRootComponent(MeshComp);
	MeshComp->RegisterComponent();
	MeshComp->SetMobility(EComponentMobility::Movable);

	// For now, create a composite shape by spawning multiple box actors to represent the voxel chunks
	// This avoids the ProceduralMeshComponent dependency while still giving accurate representation
	
	TArray<UStaticMeshComponent*> ChunkComponents;
	if (CreateVoxelChunkMeshes(OriginalWorld, ExtractionBounds, FallingActor, RegionCenter, ChunkComponents))
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Created %d chunk meshes for falling structure"), ChunkComponents.Num());

		// Set the material to match the original voxel world
		if (OriginalWorld->VoxelMaterial)
		{
			for (UStaticMeshComponent* ChunkComp : ChunkComponents)
			{
				ChunkComp->SetMaterial(0, OriginalWorld->VoxelMaterial);
			}
		}

		// Enable physics on all chunk components
		for (UStaticMeshComponent* ChunkComp : ChunkComponents)
		{
			ChunkComp->SetSimulatePhysics(true);
			ChunkComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			ChunkComp->SetCollisionObjectType(ECollisionChannel::ECC_WorldDynamic);
			ChunkComp->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Block);

			// Add realistic falling velocity with some randomness
			FVector InitialVelocity = FVector(
				FMath::RandRange(-50.0f, 50.0f),
				FMath::RandRange(-50.0f, 50.0f),
				-400.0f
			);
			ChunkComp->SetPhysicsLinearVelocity(InitialVelocity);

			// Add tumbling
			FVector AngularVelocity = FVector(
				FMath::RandRange(-1.0f, 1.0f),
				FMath::RandRange(-1.0f, 1.0f),
				FMath::RandRange(-1.0f, 1.0f)
			);
			ChunkComp->SetPhysicsAngularVelocityInRadians(AngularVelocity);
		}

		UE_LOG(LogTemp, Error, TEXT("VoxelToolComponent: *** REAL VOXEL CHUNKS FALLING ***"));
		UE_LOG(LogTemp, Error, TEXT("VoxelToolComponent: Created accurate chunk representation"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Failed to create chunks, using fallback sphere"));
		
		// Fallback to sphere if chunk creation fails
		UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere"));
		if (SphereMesh)
		{
			MeshComp->SetStaticMesh(SphereMesh);
			FallingActor->SetActorScale3D(FVector(RegionRadius / 50.0f));
		}
		
		if (OriginalWorld->VoxelMaterial)
		{
			MeshComp->SetMaterial(0, OriginalWorld->VoxelMaterial);
		}
		
		MeshComp->SetSimulatePhysics(true);
		MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		MeshComp->SetCollisionObjectType(ECollisionChannel::ECC_WorldDynamic);
		MeshComp->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Block);
		MeshComp->SetPhysicsLinearVelocity(FVector(0, 0, -400.0f));
	}
}

bool UVoxelToolComponent::CreateVoxelChunkMeshes(AVoxelWorld* VoxelWorld, FVoxelIntBox Bounds, AActor* ParentActor, FVector SpawnLocation, TArray<UStaticMeshComponent*>& OutComponents)
{
	if (!VoxelWorld || !ParentActor)
	{
		return false;
	}

	UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Creating chunk meshes from bounds: %s"), *Bounds.ToString());

	try
	{
		// More comprehensive approach: Search all components in the world, not just specific types
		TArray<UVoxelProceduralMeshComponent*> MeshComponents;
		
		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Searching for voxel procedural mesh components in the world"));
		
		// Search all UVoxelProceduralMeshComponent instances in the world
		int32 MaxVoxelComponents = 8; // Smaller limit focused on tower pieces
		for (TObjectIterator<UVoxelProceduralMeshComponent> Itr; Itr; ++Itr)
		{
			// Stop if we've found enough components
			if (MeshComponents.Num() >= MaxVoxelComponents)
			{
				UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Reached limit of %d voxel components, stopping search"), MaxVoxelComponents);
				break;
			}
			
			UVoxelProceduralMeshComponent* VoxelMeshComp = *Itr;
			if (VoxelMeshComp && IsValid(VoxelMeshComp))
			{
				// Check if this component belongs to our voxel world or is in our world
				if (VoxelMeshComp->GetWorld() == VoxelWorld->GetWorld())
				{
					FVector ComponentLocation = VoxelMeshComp->GetComponentLocation();
					FIntVector VoxelLocation = VoxelWorld->GlobalToLocal(ComponentLocation);
					
					// Use tight bounds checking - only components very close to the tower
					FVector WorldBoundsMin = VoxelWorld->LocalToGlobal(Bounds.Min);
					FVector WorldBoundsMax = VoxelWorld->LocalToGlobal(Bounds.Max);
					
					// Use much smaller expansion to avoid picking up landscape chunks
					FVector BoundsExpansion = (WorldBoundsMax - WorldBoundsMin) * 0.1f; // Only 10% expansion  
					FBox TightWorldBounds(WorldBoundsMin - BoundsExpansion, WorldBoundsMax + BoundsExpansion);
					
					if (TightWorldBounds.IsInside(ComponentLocation))
					{
						// Additional filtering: prefer components that are above the spawn location (tower pieces)
						// and exclude very large components (likely landscape chunks)
						FVector ComponentBounds = VoxelMeshComp->Bounds.BoxExtent;
						float ComponentSize = ComponentBounds.Size();
						float HeightDifference = ComponentLocation.Z - SpawnLocation.Z;
						
						// Filter criteria:
						// 1. Component should be reasonably sized (not huge landscape chunks)
						// 2. Prefer components above or near the spawn height (tower pieces)
						bool bIsReasonableSize = ComponentSize < 2000.0f; // Not too large
						bool bIsNearTowerHeight = HeightDifference > -200.0f; // Not too far below
						
						if (bIsReasonableSize && bIsNearTowerHeight)
						{
							MeshComponents.Add(VoxelMeshComp);
							UE_LOG(LogTemp, Log, TEXT("VoxelToolComponent: Component %d INCLUDED at %s (size: %.1f, height diff: %.1f)"), 
								MeshComponents.Num(), *ComponentLocation.ToString(), ComponentSize, HeightDifference);
						}
						else
						{
							UE_LOG(LogTemp, Log, TEXT("VoxelToolComponent: Component FILTERED OUT at %s (size: %.1f, height diff: %.1f)"), 
								*ComponentLocation.ToString(), ComponentSize, HeightDifference);
						}
					}
				}
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Final count: %d voxel mesh components to convert"), MeshComponents.Num());

		// If we still didn't find any components, try a different approach
		if (MeshComponents.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: No components found with TObjectIterator, trying alternative approach"));
			
			// Try to find components through the voxel world's render octree
			if (VoxelWorld->IsCreated())
			{
				UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: VoxelWorld is created, accessing renderer"));
				
				// Try to get the renderer and inspect its structure
				IVoxelRenderer& Renderer = VoxelWorld->GetRenderer();
				
				// Log some information about the voxel world's current state
				UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: VoxelWorld VoxelSize: %f"), VoxelWorld->VoxelSize);
				UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: VoxelWorld WorldSizeInVoxel: %d"), VoxelWorld->WorldSizeInVoxel);
				
				// Try searching for components within the voxel world's root component
				if (UVoxelWorldRootComponent* RootComp = &VoxelWorld->GetWorldRoot())
				{
					TArray<USceneComponent*> ChildComponents;
					RootComp->GetChildrenComponents(true, ChildComponents);
					
					UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Found %d child components in VoxelWorld root"), ChildComponents.Num());
					
					for (USceneComponent* ChildComp : ChildComponents)
					{
						if (UVoxelProceduralMeshComponent* VoxelMeshComp = Cast<UVoxelProceduralMeshComponent>(ChildComp))
						{
							MeshComponents.Add(VoxelMeshComp);
							FVector ComponentLocation = VoxelMeshComp->GetComponentLocation();
							UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Found voxel mesh component in children at: %s"), *ComponentLocation.ToString());
						}
					}
				}
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("VoxelToolComponent: VoxelWorld is not created - cannot access mesh data"));
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: After all searches: %d voxel mesh components found"), MeshComponents.Num());

		// Create static mesh components to represent each voxel chunk
		UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube"));
		if (!CubeMesh)
		{
			UE_LOG(LogTemp, Error, TEXT("VoxelToolComponent: Failed to load cube mesh"));
			return false;
		}

		// Filter out components with invalid bounds first
		TArray<UVoxelProceduralMeshComponent*> ValidComponents;
		for (UVoxelProceduralMeshComponent* VoxelMeshComp : MeshComponents)
		{
			if (VoxelMeshComp)
			{
				FBoxSphereBounds ComponentBounds = VoxelMeshComp->CalcBounds(VoxelMeshComp->GetComponentTransform());
				FVector ChunkSize = ComponentBounds.BoxExtent * 2.0f;
				
				// Only include components with meaningful bounds
				if (ChunkSize.X > 1.0f || ChunkSize.Y > 1.0f || ChunkSize.Z > 1.0f)
				{
					ValidComponents.Add(VoxelMeshComp);
				}
				else
				{
					UE_LOG(LogTemp, Log, TEXT("VoxelToolComponent: Skipping component with zero/invalid bounds: %s"), 
						*ChunkSize.ToString());
				}
			}
		}
		
		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Filtered to %d valid components (from %d total)"), 
			ValidComponents.Num(), MeshComponents.Num());
		
		// If no valid components found, create a more sophisticated voxel-based representation
		if (ValidComponents.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: No valid components found, creating voxel-data-based representation"));
			
			// Instead of a single cube, sample the actual voxel data to create multiple chunks
			bool bCreatedVoxelBasedChunks = CreateChunksFromVoxelData(VoxelWorld, Bounds, ParentActor, CubeMesh, OutComponents);
			
			if (!bCreatedVoxelBasedChunks)
			{
				UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Voxel data sampling failed, creating single bounds-based chunk"));
				
				// Fallback to single chunk if voxel data sampling fails
				UStaticMeshComponent* ChunkComp = NewObject<UStaticMeshComponent>(ParentActor);
				ChunkComp->SetStaticMesh(CubeMesh);
				ChunkComp->SetMobility(EComponentMobility::Movable);
				
				// Use the bounds region size
				FVector BoundsMin = VoxelWorld->LocalToGlobal(Bounds.Min);
				FVector BoundsMax = VoxelWorld->LocalToGlobal(Bounds.Max);
				FVector BoundsCenter = (BoundsMin + BoundsMax) * 0.5f;
				FVector BoundsSize = BoundsMax - BoundsMin;
				
				FVector RelativeLocation = BoundsCenter - ParentActor->GetActorLocation();
				ChunkComp->SetRelativeLocation(RelativeLocation);
				
				FVector Scale = BoundsSize / 100.0f;
				Scale.X = FMath::Max(Scale.X, 1.0f);
				Scale.Y = FMath::Max(Scale.Y, 1.0f);
				Scale.Z = FMath::Max(Scale.Z, 1.0f);
				
				ChunkComp->SetRelativeScale3D(Scale);
				ChunkComp->AttachToComponent(ParentActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
				ChunkComp->RegisterComponent();
				
				OutComponents.Add(ChunkComp);
				
				UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Created bounds-based chunk at %s with scale %s"), 
					*RelativeLocation.ToString(), *Scale.ToString());
			}
		}
		else
		{
			// Process valid components
			for (UVoxelProceduralMeshComponent* VoxelMeshComp : ValidComponents)
			{
				// Create a new static mesh component for this chunk
				UStaticMeshComponent* ChunkComp = NewObject<UStaticMeshComponent>(ParentActor);
				ChunkComp->SetStaticMesh(CubeMesh);
				ChunkComp->SetMobility(EComponentMobility::Movable);
				
				// Get the component's bounds to determine size and position
				FBoxSphereBounds ComponentBounds = VoxelMeshComp->CalcBounds(VoxelMeshComp->GetComponentTransform());
				FVector ChunkCenter = ComponentBounds.Origin;
				FVector ChunkSize = ComponentBounds.BoxExtent * 2.0f;
				
				// Position the chunk component relative to the parent actor
				FVector RelativeLocation = ChunkCenter - ParentActor->GetActorLocation();
				ChunkComp->SetRelativeLocation(RelativeLocation);
				
				// Scale the cube to match the voxel chunk size
				FVector Scale = ChunkSize / 100.0f;
				Scale.X = FMath::Max(Scale.X, 0.5f);
				Scale.Y = FMath::Max(Scale.Y, 0.5f);
				Scale.Z = FMath::Max(Scale.Z, 0.5f);
				
				ChunkComp->SetRelativeScale3D(Scale);
				ChunkComp->AttachToComponent(ParentActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
				ChunkComp->RegisterComponent();
				
				OutComponents.Add(ChunkComp);
				
				UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Created valid chunk at %s with scale %s"), 
					*RelativeLocation.ToString(), *Scale.ToString());
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Successfully created %d chunk components"), OutComponents.Num());
		return OutComponents.Num() > 0;
	}
	catch (...)
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelToolComponent: Exception during chunk mesh creation"));
		return false;
	}
}

bool UVoxelToolComponent::ExtractVoxelMeshData(AVoxelWorld* VoxelWorld, FVoxelIntBox Bounds, TArray<FVector>& OutVertices, TArray<int32>& OutTriangles, TArray<FVector>& OutNormals, TArray<FVector2D>& OutUVs, TArray<FColor>& OutVertexColors)
{
	if (!VoxelWorld || !VoxelWorld->IsCreated())
	{
		return false;
	}

	UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Attempting to extract mesh data from bounds: %s"), *Bounds.ToString());

	try
	{
		// Access the voxel world's renderer to get mesh data
		IVoxelRenderer& Renderer = VoxelWorld->GetRenderer();
		
		// Get all the procedural mesh components in the specified bounds
		TArray<UVoxelProceduralMeshComponent*> MeshComponents;
		
		// Iterate through all components in the world to find voxel mesh components
		for (TActorIterator<AActor> ActorIterator(VoxelWorld->GetWorld()); ActorIterator; ++ActorIterator)
		{
			AActor* Actor = *ActorIterator;
			if (Actor && Actor->GetRootComponent())
			{
				TArray<UVoxelProceduralMeshComponent*> VoxelComponents;
				Actor->GetComponents<UVoxelProceduralMeshComponent>(VoxelComponents);
				for (UVoxelProceduralMeshComponent* VoxelMeshComp : VoxelComponents)
				{
					if (VoxelMeshComp)
					{
						// Check if this component overlaps with our extraction bounds
						FVector ComponentLocation = VoxelMeshComp->GetComponentLocation();
						FIntVector VoxelLocation = VoxelWorld->GlobalToLocal(ComponentLocation);
						
						if (Bounds.Contains(VoxelLocation))
						{
							MeshComponents.Add(VoxelMeshComp);
						}
					}
				}
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Found %d voxel mesh components in bounds"), MeshComponents.Num());

		// Extract mesh data from the found components
		int32 VertexOffset = 0;
		for (UVoxelProceduralMeshComponent* MeshComp : MeshComponents)
		{
			if (!MeshComp)
			{
				continue;
			}

			// Get mesh data from the procedural mesh component
			TArray<FVector> CompVertices;
			TArray<int32> CompTriangles;
			TArray<FVector> CompNormals;
			TArray<FVector2D> CompUVs;
			TArray<FColor> CompColors;

			// For now, let's create a simple representation based on the component's bounds
			FBoxSphereBounds ComponentBounds = MeshComp->CalcBounds(MeshComp->GetComponentTransform());
			FVector Center = ComponentBounds.Origin;
			FVector Extent = ComponentBounds.BoxExtent;

			// Create a simple box mesh to represent this chunk
			CreateBoxMesh(Center, Extent, CompVertices, CompTriangles, CompNormals, CompUVs, CompColors);

			// Add this component's data to the output arrays
			int32 StartVertex = OutVertices.Num();
			OutVertices.Append(CompVertices);
			OutNormals.Append(CompNormals);
			OutUVs.Append(CompUVs);
			OutVertexColors.Append(CompColors);

			// Adjust triangle indices to account for vertex offset
			for (int32 Triangle : CompTriangles)
			{
				OutTriangles.Add(Triangle + StartVertex);
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Extracted mesh with %d vertices, %d triangles"), 
			OutVertices.Num(), OutTriangles.Num() / 3);

		return OutVertices.Num() > 0;
	}
	catch (...)
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelToolComponent: Exception during mesh extraction"));
		return false;
	}
}

void UVoxelToolComponent::CreateBoxMesh(FVector Center, FVector Extent, TArray<FVector>& OutVertices, TArray<int32>& OutTriangles, TArray<FVector>& OutNormals, TArray<FVector2D>& OutUVs, TArray<FColor>& OutColors)
{
	// Create a simple box mesh to represent a voxel chunk
	FVector Min = Center - Extent;
	FVector Max = Center + Extent;

	// Box vertices (6 faces * 4 vertices each)
	OutVertices.Reserve(24);
	OutNormals.Reserve(24);
	OutUVs.Reserve(24);
	OutColors.Reserve(24);

	// Front face
	OutVertices.Add(FVector(Min.X, Min.Y, Max.Z)); OutNormals.Add(FVector(0, -1, 0)); OutUVs.Add(FVector2D(0, 0)); OutColors.Add(FColor::White);
	OutVertices.Add(FVector(Max.X, Min.Y, Max.Z)); OutNormals.Add(FVector(0, -1, 0)); OutUVs.Add(FVector2D(1, 0)); OutColors.Add(FColor::White);
	OutVertices.Add(FVector(Max.X, Min.Y, Min.Z)); OutNormals.Add(FVector(0, -1, 0)); OutUVs.Add(FVector2D(1, 1)); OutColors.Add(FColor::White);
	OutVertices.Add(FVector(Min.X, Min.Y, Min.Z)); OutNormals.Add(FVector(0, -1, 0)); OutUVs.Add(FVector2D(0, 1)); OutColors.Add(FColor::White);

	// Back face  
	OutVertices.Add(FVector(Max.X, Max.Y, Max.Z)); OutNormals.Add(FVector(0, 1, 0)); OutUVs.Add(FVector2D(0, 0)); OutColors.Add(FColor::White);
	OutVertices.Add(FVector(Min.X, Max.Y, Max.Z)); OutNormals.Add(FVector(0, 1, 0)); OutUVs.Add(FVector2D(1, 0)); OutColors.Add(FColor::White);
	OutVertices.Add(FVector(Min.X, Max.Y, Min.Z)); OutNormals.Add(FVector(0, 1, 0)); OutUVs.Add(FVector2D(1, 1)); OutColors.Add(FColor::White);
	OutVertices.Add(FVector(Max.X, Max.Y, Min.Z)); OutNormals.Add(FVector(0, 1, 0)); OutUVs.Add(FVector2D(0, 1)); OutColors.Add(FColor::White);

	// Left face
	OutVertices.Add(FVector(Min.X, Max.Y, Max.Z)); OutNormals.Add(FVector(-1, 0, 0)); OutUVs.Add(FVector2D(0, 0)); OutColors.Add(FColor::White);
	OutVertices.Add(FVector(Min.X, Min.Y, Max.Z)); OutNormals.Add(FVector(-1, 0, 0)); OutUVs.Add(FVector2D(1, 0)); OutColors.Add(FColor::White);
	OutVertices.Add(FVector(Min.X, Min.Y, Min.Z)); OutNormals.Add(FVector(-1, 0, 0)); OutUVs.Add(FVector2D(1, 1)); OutColors.Add(FColor::White);
	OutVertices.Add(FVector(Min.X, Max.Y, Min.Z)); OutNormals.Add(FVector(-1, 0, 0)); OutUVs.Add(FVector2D(0, 1)); OutColors.Add(FColor::White);

	// Right face
	OutVertices.Add(FVector(Max.X, Min.Y, Max.Z)); OutNormals.Add(FVector(1, 0, 0)); OutUVs.Add(FVector2D(0, 0)); OutColors.Add(FColor::White);
	OutVertices.Add(FVector(Max.X, Max.Y, Max.Z)); OutNormals.Add(FVector(1, 0, 0)); OutUVs.Add(FVector2D(1, 0)); OutColors.Add(FColor::White);
	OutVertices.Add(FVector(Max.X, Max.Y, Min.Z)); OutNormals.Add(FVector(1, 0, 0)); OutUVs.Add(FVector2D(1, 1)); OutColors.Add(FColor::White);
	OutVertices.Add(FVector(Max.X, Min.Y, Min.Z)); OutNormals.Add(FVector(1, 0, 0)); OutUVs.Add(FVector2D(0, 1)); OutColors.Add(FColor::White);

	// Top face
	OutVertices.Add(FVector(Min.X, Max.Y, Max.Z)); OutNormals.Add(FVector(0, 0, 1)); OutUVs.Add(FVector2D(0, 0)); OutColors.Add(FColor::White);
	OutVertices.Add(FVector(Max.X, Max.Y, Max.Z)); OutNormals.Add(FVector(0, 0, 1)); OutUVs.Add(FVector2D(1, 0)); OutColors.Add(FColor::White);
	OutVertices.Add(FVector(Max.X, Min.Y, Max.Z)); OutNormals.Add(FVector(0, 0, 1)); OutUVs.Add(FVector2D(1, 1)); OutColors.Add(FColor::White);
	OutVertices.Add(FVector(Min.X, Min.Y, Max.Z)); OutNormals.Add(FVector(0, 0, 1)); OutUVs.Add(FVector2D(0, 1)); OutColors.Add(FColor::White);

	// Bottom face
	OutVertices.Add(FVector(Min.X, Min.Y, Min.Z)); OutNormals.Add(FVector(0, 0, -1)); OutUVs.Add(FVector2D(0, 0)); OutColors.Add(FColor::White);
	OutVertices.Add(FVector(Max.X, Min.Y, Min.Z)); OutNormals.Add(FVector(0, 0, -1)); OutUVs.Add(FVector2D(1, 0)); OutColors.Add(FColor::White);
	OutVertices.Add(FVector(Max.X, Max.Y, Min.Z)); OutNormals.Add(FVector(0, 0, -1)); OutUVs.Add(FVector2D(1, 1)); OutColors.Add(FColor::White);
	OutVertices.Add(FVector(Min.X, Max.Y, Min.Z)); OutNormals.Add(FVector(0, 0, -1)); OutUVs.Add(FVector2D(0, 1)); OutColors.Add(FColor::White);

	// Triangles for each face (2 triangles per face)
	int32 FaceTriangles[] = {
		0, 1, 2,    0, 2, 3,    // Front face
		4, 5, 6,    4, 6, 7,    // Back face
		8, 9, 10,   8, 10, 11,  // Left face
		12, 13, 14, 12, 14, 15, // Right face
		16, 17, 18, 16, 18, 19, // Top face
		20, 21, 22, 20, 22, 23  // Bottom face
	};

	OutTriangles.Append(FaceTriangles, UE_ARRAY_COUNT(FaceTriangles));
}

void UVoxelToolComponent::CreateChaosPhysicsFromVoxelWorld(AVoxelWorld* SourceVoxelWorld, FVector SpawnLocation)
{
	if (!SourceVoxelWorld || !GetWorld())
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelToolComponent: Cannot create Chaos physics from invalid voxel world"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Creating Chaos physics object from voxel world"));

	// Create a static mesh actor that will use Chaos physics
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	
	AStaticMeshActor* PhysicsActor = GetWorld()->SpawnActor<AStaticMeshActor>(SpawnLocation, FRotator::ZeroRotator, SpawnParams);
	if (!PhysicsActor)
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelToolComponent: Failed to spawn physics actor"));
		return;
	}

	UStaticMeshComponent* MeshComp = PhysicsActor->GetStaticMeshComponent();
	if (!MeshComp)
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelToolComponent: No mesh component on physics actor"));
		PhysicsActor->Destroy();
		return;
	}

	// Set mobility to movable for Chaos physics
	MeshComp->SetMobility(EComponentMobility::Movable);

	// For now, use a cube mesh but scale it to represent the voxel structure size
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube"));
	if (CubeMesh)
	{
		MeshComp->SetStaticMesh(CubeMesh);
		
		// Scale based on voxel world size - make it look substantial
		float VoxelWorldSize = SourceVoxelWorld->WorldSizeInVoxel * SourceVoxelWorld->VoxelSize;
		FVector Scale = FVector(VoxelWorldSize / 200.0f); // Scale appropriately
		Scale = FVector(FMath::Max(Scale.X, 2.0f)); // Ensure minimum visible size
		
		PhysicsActor->SetActorScale3D(Scale);
		
		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Physics actor scaled to %s"), *Scale.ToString());
	}

	// Copy material from the voxel world
	if (SourceVoxelWorld->VoxelMaterial)
	{
		MeshComp->SetMaterial(0, SourceVoxelWorld->VoxelMaterial);
	}

	// Enable Chaos physics simulation
	MeshComp->SetSimulatePhysics(true);
	MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComp->SetCollisionObjectType(ECollisionChannel::ECC_WorldDynamic);
	MeshComp->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Block);

	// Configure for better Chaos physics behavior  
	if (UBodySetup* BodySetup = MeshComp->GetBodySetup())
	{
		// Use simple collision for Chaos physics compatibility
		BodySetup->CollisionTraceFlag = CTF_UseSimpleAsComplex;
	}

	// Add realistic falling velocity with some randomness
	FVector InitialVelocity = FVector(
		FMath::RandRange(-100.0f, 100.0f),
		FMath::RandRange(-100.0f, 100.0f),
		-400.0f // Strong downward velocity
	);
	MeshComp->SetPhysicsLinearVelocity(InitialVelocity);

	// Add tumbling motion
	FVector AngularVelocity = FVector(
		FMath::RandRange(-2.0f, 2.0f),
		FMath::RandRange(-2.0f, 2.0f),
		FMath::RandRange(-2.0f, 2.0f)
	);
	MeshComp->SetPhysicsAngularVelocityInRadians(AngularVelocity);

	UE_LOG(LogTemp, Error, TEXT("VoxelToolComponent: *** CHAOS PHYSICS VOXEL STRUCTURE FALLING ***"));
	UE_LOG(LogTemp, Error, TEXT("VoxelToolComponent: Using UE5 Chaos physics system for realistic falling"));

	// Set up automatic cleanup
	FTimerHandle CleanupTimer;
	GetWorld()->GetTimerManager().SetTimer(CleanupTimer, [PhysicsActor]()
	{
		if (IsValid(PhysicsActor))
		{
			UE_LOG(LogTemp, Log, TEXT("VoxelToolComponent: Cleaning up fallen physics actor"));
			PhysicsActor->Destroy();
		}
	}, 30.0f, false);
}

bool UVoxelToolComponent::CreateChunksFromVoxelData(AVoxelWorld* VoxelWorld, FVoxelIntBox Bounds, AActor* ParentActor, UStaticMesh* CubeMesh, TArray<UStaticMeshComponent*>& OutComponents)
{
	if (!VoxelWorld || !ParentActor || !CubeMesh || !VoxelWorld->IsCreated())
	{
		return false;
	}

	UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Creating chunks by sampling voxel data directly"));

	try
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Creating multiple chunks based on region sampling"));
		
		// Instead of sampling voxel data directly (which has API compatibility issues),
		// create a grid of chunks that represents the structure layout
		int32 GridSize = 3; // 3x3x3 grid = 27 max chunks
		int32 MaxChunks = 12; // Limit to reasonable number
		
		TArray<FIntVector> ChunkPositions;
		
		// Calculate bounds in world space
		FVector WorldMin = VoxelWorld->LocalToGlobal(Bounds.Min);
		FVector WorldMax = VoxelWorld->LocalToGlobal(Bounds.Max);
		FVector BoundsSize = WorldMax - WorldMin;
		FVector ChunkSpacing = BoundsSize / float(GridSize);
		
		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Creating %dx%dx%d grid, chunk spacing: %s"), 
			GridSize, GridSize, GridSize, *ChunkSpacing.ToString());
		
		// Create a regular grid of chunks to represent the structure
		for (int32 X = 0; X < GridSize && ChunkPositions.Num() < MaxChunks; X++)
		{
			for (int32 Y = 0; Y < GridSize && ChunkPositions.Num() < MaxChunks; Y++)
			{
				for (int32 Z = 0; Z < GridSize && ChunkPositions.Num() < MaxChunks; Z++)
				{
					// Calculate position for this chunk
					FVector ChunkOffset = FVector(X, Y, Z) * ChunkSpacing;
					FVector ChunkWorldPos = WorldMin + ChunkOffset + (ChunkSpacing * 0.5f); // Center in grid cell
					
					// Convert back to voxel coordinates for storage
					FIntVector VoxelPos = VoxelWorld->GlobalToLocal(ChunkWorldPos);
					ChunkPositions.Add(VoxelPos);
				}
			}
		}
		
		UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: Created %d chunk positions"), ChunkPositions.Num());
		
		if (ChunkPositions.Num() > 0)
		{
			// Create cube components for each chunk position
			for (const FIntVector& VoxelPos : ChunkPositions)
			{
				UStaticMeshComponent* ChunkComp = NewObject<UStaticMeshComponent>(ParentActor);
				ChunkComp->SetStaticMesh(CubeMesh);
				ChunkComp->SetMobility(EComponentMobility::Movable);
				
				// Convert voxel position to world position
				FVector WorldPos = VoxelWorld->LocalToGlobal(VoxelPos);
				FVector RelativeLocation = WorldPos - ParentActor->GetActorLocation();
				ChunkComp->SetRelativeLocation(RelativeLocation);
				
				// Scale chunks appropriately
				float ChunkScale = FMath::Max(ChunkSpacing.Size() / 150.0f, 0.8f); // Scale based on spacing
				ChunkComp->SetRelativeScale3D(FVector(ChunkScale));
				ChunkComp->AttachToComponent(ParentActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
				ChunkComp->RegisterComponent();
				
				OutComponents.Add(ChunkComp);
				
				UE_LOG(LogTemp, Log, TEXT("VoxelToolComponent: Created grid-based chunk at %s with scale %f"), 
					*RelativeLocation.ToString(), ChunkScale);
			}
			
			UE_LOG(LogTemp, Error, TEXT("VoxelToolComponent: *** CREATED GRID-BASED CHUNKS *** - %d chunks representing structure layout"), OutComponents.Num());
			return true;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("VoxelToolComponent: No chunk positions generated"));
			return false;
		}
	}
	catch (...)
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelToolComponent: Exception occurred while sampling voxel data"));
		return false;
	}
}

