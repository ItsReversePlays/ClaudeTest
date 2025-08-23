// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "ClaudeTestCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputMappingContext;
class UInputAction;
class UVoxelToolComponent;
class UVoxelSimpleInvokerComponent;
class UVoxelNoClippingComponent;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

UCLASS(config=Game)
class AClaudeTestCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Camera boom positioning the camera behind the character */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** Follow camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;

	/** Voxel Tool Component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = VoxelTools, meta = (AllowPrivateAccess = "true"))
	UVoxelToolComponent* VoxelToolComponent;

	/** Voxel Simple Invoker Component - handles LOD and chunk generation around player */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = VoxelTools, meta = (AllowPrivateAccess = "true"))
	UVoxelSimpleInvokerComponent* VoxelInvokerComponent;

	/** Voxel No Clipping Component - prevents player from clipping through voxels */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = VoxelTools, meta = (AllowPrivateAccess = "true"))
	UVoxelNoClippingComponent* VoxelNoClippingComponent;
	
	/** MappingContext */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputMappingContext* DefaultMappingContext;

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* LookAction;

	/** Voxel Dig Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* DigAction;

	/** Voxel Build Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* BuildAction;

	/** Increase Tool Size Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* IncreaseToolSizeAction;

	/** Decrease Tool Size Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* DecreaseToolSizeAction;

public:
	AClaudeTestCharacter();
	
	// Override Tick to add additional terrain collision checks
	virtual void Tick(float DeltaTime) override;

protected:

	/** Called for movement input */
	void Move(const FInputActionValue& Value);

	/** Called for looking input */
	void Look(const FInputActionValue& Value);

	/** Called for voxel dig input */
	void Dig(const FInputActionValue& Value);

	/** Called for voxel build input */
	void Build(const FInputActionValue& Value);

	/** Called for increase tool size input */
	void IncreaseToolSize(const FInputActionValue& Value);

	/** Called for decrease tool size input */
	void DecreaseToolSize(const FInputActionValue& Value);
			

protected:

	virtual void NotifyControllerChanged() override;

	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

public:
	/** Returns CameraBoom subobject **/
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	/** Returns FollowCamera subobject **/
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }
};

