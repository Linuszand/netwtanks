#include "TankPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "Net/UnrealNetwork.h"
#include "TankGameplayMath.h"
#include "TankMovementComponent.h"
#include "TankSnapShot.h"
#include "TankWeaponComponent.h"

ATankPawn::ATankPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	bReplicates = true;
	SetReplicatingMovement(true);
	SetNetUpdateFrequency(20.f);
	SetMinNetUpdateFrequency(20.f);
	AutoPossessAI = EAutoPossessAI::Disabled;
	AutoPossessPlayer = EAutoReceiveInput::Disabled;
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	CollisionRoot = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CollisionRoot"));
	SetRootComponent(CollisionRoot);
	CollisionRoot->InitCapsuleSize(90.f, 100.f);
	CollisionRoot->SetCollisionProfileName(TEXT("Pawn"));
	CollisionRoot->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionRoot->SetSimulatePhysics(false);

	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	VisualRoot->SetupAttachment(CollisionRoot);
	VisualRoot->SetRelativeLocation(FVector(0.0, 0.0, -65.0));
	ChassisVisualPivot = CreateDefaultSubobject<USceneComponent>(TEXT("ChassisVisualPivot"));
	ChassisVisualPivot->SetupAttachment(VisualRoot);
	ChassisMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChassisMesh"));
	ChassisMesh->SetupAttachment(ChassisVisualPivot);
	TurretYawPivot = CreateDefaultSubobject<USceneComponent>(TEXT("TurretYawPivot"));
	TurretYawPivot->SetupAttachment(VisualRoot);
	TurretYawPivot->SetRelativeLocation(FVector(0.0, 0.0, 40.0));
	TurretMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TurretMesh"));
	TurretMesh->SetupAttachment(TurretYawPivot);
	BarrelMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BarrelMesh"));
	BarrelMesh->SetupAttachment(TurretYawPivot);
	BarrelMesh->SetRelativeLocation(FVector(70.0, 0.0, 0.0));
	for (UStaticMeshComponent* Mesh : {ChassisMesh.Get(), TurretMesh.Get(), BarrelMesh.Get()})
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetSimulatePhysics(false);
		Mesh->SetCanEverAffectNavigation(false);
	}

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(CollisionRoot);
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->SetRelativeRotation(FRotator(-60.0, 45.0, 0.0));
	CameraBoom->TargetArmLength = 1500.f;
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->bEnableCameraLag = false;
	CameraBoom->bEnableCameraRotationLag = false;
	CameraBoom->AddTickPrerequisiteActor(this);
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->ProjectionMode = ECameraProjectionMode::Orthographic;
	FollowCamera->OrthoWidth = 2400.f;
	FollowCamera->bUsePawnControlRotation = false;

	TankMovement = CreateDefaultSubobject<UTankMovementComponent>(TEXT("TankMovement"));
	TankMovement->SetUpdatedComponent(CollisionRoot);
	TankWeapon = CreateDefaultSubobject<UTankWeaponComponent>(TEXT("TankWeapon"));
}

UPawnMovementComponent* ATankPawn::GetMovementComponent() const
{
	return TankMovement;
}

void ATankPawn::BeginPlay()
{
	Super::BeginPlay();
	using namespace NetworkTankArena::LocalTank;
	if (HasAuthority())
	{
		CollisionRoot->SetWorldScale3D(FVector::OneVector);
		CollisionRoot->SetWorldRotation(FRotator(0.0, NormalizeYaw(GetActorRotation().Yaw), 0.0));
		CanonicalTurretWorldYaw = ResolveTurretWorldYaw(GetActorRotation().Yaw, GetActorLocation(), FTankControlIntent{});
	}
	CollisionRoot->SetSimulatePhysics(false);
	CollisionRoot->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	for (UStaticMeshComponent* Mesh : {ChassisMesh.Get(), TurretMesh.Get(), BarrelMesh.Get()})
	{
		Mesh->SetSimulatePhysics(false);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	// Import-axis corrections belong on meshes, not the shared yaw/bob pivots.
	VisualRoot->SetRelativeRotation(FRotator::ZeroRotator);
	TurretYawPivot->SetRelativeRotation(FRotator::ZeroRotator);
	BaseVisualTransform = VisualRoot->GetRelativeTransform();
	BaseChassisTransform = ChassisVisualPivot->GetRelativeTransform();
	TankMovement->SetUpdatedComponent(CollisionRoot);
	TankMovement->InitializeGameplayPlane();
	PreviousPresentationYaw = NormalizeYaw(GetActorRotation().Yaw);
	UpdateControllerTickPrerequisite();
	ApplyPresentation(0.f);
	
	TargetLocation = GetActorLocation();
	TargetRotation = GetActorRotation();
	CurrentTurretYaw = CanonicalTurretWorldYaw;
}

void ATankPawn::SubmitControlIntent(const FTankControlIntent& Intent)
{
	if (!HasAuthority())
	{
		return;
	}
	CurrentIntent = NetworkTankArena::LocalTank::SanitizeIntent(Intent);
	ControlIntentAgeSeconds = 0.f;
}

void ATankPawn::ClearControlIntent()
{
	CurrentIntent = FTankControlIntent{};
	ControlIntentAgeSeconds = 0.f;
	TankMovement->Velocity = FVector::ZeroVector;
}

double ATankPawn::GetGameplayPlaneZ() const
{
	return TankMovement->GetGameplayPlaneZ();
}

void ATankPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (HasAuthority())
	{
		if (NetworkTankArena::LocalTank::IsValidDeltaTime(DeltaTime))
		{
			ControlIntentAgeSeconds += DeltaTime;
			if (ControlIntentAgeSeconds >= ControlIntentTimeoutSeconds)
			{
				ClearControlIntent();
			}
		}
		TankMovement->SimulateMovement(CurrentIntent, DeltaTime);
		if (NetworkTankArena::LocalTank::IsValidDeltaTime(DeltaTime))
		{
			CanonicalTurretWorldYaw = NetworkTankArena::LocalTank::ResolveTurretWorldYaw(
				CanonicalTurretWorldYaw, GetActorLocation(), CurrentIntent);
		}
	} else if (GetLocalRole() == ROLE_SimulatedProxy)
	{
		if (SnapshotBuffer.Num() >= 2)
		{
			const float RenderTime = GetWorld()->GetTimeSeconds() - InterpDelay;
			
			int IndexAfter = -1;
			for (int i = 0; i < SnapshotBuffer.Num(); i++)
			{
				if (SnapshotBuffer[i].Timestamp > RenderTime)
				{
					IndexAfter = i;
					break;
				}
			}
			
			if (IndexAfter > 0)
			{
				FTankSnapshot SnapshotBefore = SnapshotBuffer[IndexAfter - 1];
				FTankSnapshot SnapshotAfter = SnapshotBuffer[IndexAfter];
				
				const float TimeDiff = SnapshotAfter.Timestamp - SnapshotBefore.Timestamp;
				const float Alpha = (RenderTime - SnapshotBefore.Timestamp) / TimeDiff;
				
				FVector TargetLoc = FMath::Lerp(SnapshotBefore.Location, SnapshotAfter.Location, Alpha);
				FRotator TargetRot = FQuat::Slerp(SnapshotBefore.Rotation.Quaternion(), SnapshotAfter.Rotation.Quaternion(), Alpha).Rotator();
				
				SetActorLocationAndRotation(TargetLoc, TargetRot);
				
				if (IndexAfter > 0)
				{
					SnapshotBuffer.RemoveAt(0, IndexAfter - 1);
				}
			} else if (IndexAfter == -1)
			{
				FVector SmoothLoc = FMath::VInterpTo(GetActorLocation(), SnapshotBuffer.Last().Location, DeltaTime, InterpSpeed);
				FRotator SmoothRot = FMath::RInterpTo(GetActorRotation(), SnapshotBuffer.Last().Rotation, DeltaTime, InterpSpeed);
				SetActorLocationAndRotation(SmoothLoc, SmoothRot);
			}
		}
	}
	ApplyPresentation(DeltaTime);
}

void ATankPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATankPawn, CanonicalTurretWorldYaw);
}

void ATankPawn::OnRep_CanonicalTurretWorldYaw()
{
	// Initial replication can notify before BeginPlay captures Blueprint visual offsets.
	if (HasActorBegunPlay())
	{
		ApplyPresentation(0.f);
	}
}

void ATankPawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	// APawn normally promotes a remote player's pawn to an autonomous proxy.
	// FRepMovement skips autonomous proxies. With no prediction, ALL client tanks
	// must be simulated proxies, including the owned tank. Ownership still belongs
	// to the controller; its Server RPC carries intent for its possessed pawn.
	SetAutonomousProxy(false);
	ClearControlIntent();
}

void ATankPawn::Restart()
{
	Super::Restart();
	ClearControlIntent();
	UpdateControllerTickPrerequisite();
}

void ATankPawn::ApplyPresentation(float DeltaTime)
{
	using namespace NetworkTankArena::LocalTank;
	if (IsValidDeltaTime(DeltaTime))
	{
		HoverPhase = FMath::Fmod(HoverPhase + 2.0 * UE_PI * NonNegativeFinite(HoverFrequencyHz) * DeltaTime, 2.0 * UE_PI);
		// Derive local banking from observed chassis motion on every machine. No intent
		// or cosmetic transforms need replication; stepped client motion is intentional.
		const float TurnRate = NonNegativeFinite(TankMovement->GetTurnRateDegreesPerSecond());
		const float ObservedTurn = TurnRate > UE_SMALL_NUMBER
			? SanitizeAxis(FMath::FindDeltaAngleDegrees(PreviousPresentationYaw, GetActorRotation().Yaw) / (DeltaTime * TurnRate)) : 0.f;
		const float TargetBank = -ObservedTurn * FMath::Min(NonNegativeFinite(MaxBankDegrees), 15.f);
		CurrentBankDegrees = FMath::FInterpTo(CurrentBankDegrees, TargetBank, DeltaTime, NonNegativeFinite(BankInterpSpeed));
		PreviousPresentationYaw = NormalizeYaw(GetActorRotation().Yaw);
	}
	const double Bob = FMath::Sin(HoverPhase) * NonNegativeFinite(HoverAmplitude);
	VisualRoot->SetRelativeLocation(BaseVisualTransform.GetLocation() + FVector(0.0, 0.0, Bob));
	ChassisVisualPivot->SetRelativeRotation(BaseChassisTransform.Rotator() + FRotator(0.0, 0.0, CurrentBankDegrees));
	
	FRotator CurrentRot(0.f, CurrentTurretYaw, 0.f);
	FRotator TargetRot(0.f, CanonicalTurretWorldYaw, 0.f);
	
	CurrentTurretYaw = FMath::RInterpTo(CurrentRot, TargetRot, DeltaTime, InterpSpeed).Yaw;
	TurretYawPivot->SetRelativeRotation(FRotator(0.0,
		CalculateRelativeTurretYaw(GetActorRotation().Yaw, CurrentTurretYaw), 0.0));
}

void ATankPawn::UpdateControllerTickPrerequisite()
{
	if (AController* Previous = TickController.Get())
	{
		RemoveTickPrerequisiteActor(Previous);
	}
	TickController = GetController();
	if (AController* Current = TickController.Get())
	{
		// Only controller -> pawn -> camera. Movement has no independent tick.
		AddTickPrerequisiteActor(Current);
	}
}

void ATankPawn::NotifyControllerChanged()
{
	ClearControlIntent();
	UpdateControllerTickPrerequisite();
	Super::NotifyControllerChanged();
}

void ATankPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearControlIntent();
	if (AController* Previous = TickController.Get())
	{
		RemoveTickPrerequisiteActor(Previous);
	}
	TickController.Reset();
	Super::EndPlay(EndPlayReason);
}

void ATankPawn::OnRep_ReplicatedMovement()
{
	const FRepMovement& RepMovement = GetReplicatedMovement();

	FTankSnapshot NewSnapshot;
	NewSnapshot.Location = RepMovement.Location;
	NewSnapshot.Rotation = RepMovement.Rotation;
	NewSnapshot.Timestamp = GetWorld()->GetTimeSeconds(); 
	
	SnapshotBuffer.Add(NewSnapshot);
}
