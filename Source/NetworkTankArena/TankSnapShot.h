#pragma once

#include "CoreMinimal.h"
#include "TankSnapshot.generated.h"

USTRUCT()
struct FTankSnapshot
{
		GENERATED_BODY()
	
		UPROPERTY()
		FVector Location = FVector::ZeroVector;

		UPROPERTY()
		FRotator Rotation = FRotator::ZeroRotator;
	
		UPROPERTY()
		FRotator TurretRotation = FRotator::ZeroRotator;

		UPROPERTY()
		float Timestamp = 0.f;
};
