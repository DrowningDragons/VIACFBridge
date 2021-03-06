// Copyright (c) 2019-2021 Drowning Dragons Limited. All Rights Reserved.


#include "VIACFCharacter.h"
#include "GAS/VIAbilitySystemComponent.h"
#include "Pawn/VIPawnVaultComponent.h"
#include "VIMotionWarpingComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"


AVIACFCharacter::AVIACFCharacter(const FObjectInitializer& OI)
	: Super(OI)
{
	AbilitySystem = CreateDefaultSubobject<UVIAbilitySystemComponent>(TEXT("AbilitySystem"));
	AbilitySystem->SetIsReplicated(true);
	AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
	AbilitySystemReplicationMode = (EVIGameplayEffectReplicationMode)(uint8)AbilitySystem->ReplicationMode;

	VaultComponent = CreateDefaultSubobject<UVIPawnVaultComponent>(TEXT("PawnVaulting"));
	MotionWarping = CreateDefaultSubobject<UVIMotionWarpingComponent>(TEXT("MotionWarping"));
}

#if WITH_EDITOR
void AVIACFCharacter::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetPropertyName().IsEqual(GET_MEMBER_NAME_CHECKED(AVIACFCharacter, AbilitySystemReplicationMode)))
	{
		AbilitySystem->SetReplicationMode((EGameplayEffectReplicationMode)(uint8)AbilitySystemReplicationMode);
	}
}
#endif  // WITH_EDITOR

void AVIACFCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Init simulated proxy
	if (AbilitySystem && GetLocalRole() == ROLE_SimulatedProxy)
	{
		// Will never have a valid controller
		AbilitySystem->InitAbilityActorInfo(this, this);
	}
}

void AVIACFCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// Init authority/standalone
	if (AbilitySystem)
	{
		AbilitySystem->InitAbilityActorInfo(this, this);
	}
}

void AVIACFCharacter::OnRep_Controller()
{
	Super::OnRep_Controller();

	// Init local client
	if (AbilitySystem)
	{
		AbilitySystem->InitAbilityActorInfo(this, this);
	}
}

void AVIACFCharacter::CheckJumpInput(float DeltaTime)
{
	const bool bIsVaulting = IsVaulting();

	// Server update simulated proxies with correct vaulting state
	if (GetLocalRole() == ROLE_Authority && GetNetMode() != NM_Standalone)
	{
		bRepIsVaulting = bIsVaulting;
	}

	// Try to vault from local input
	if (IsLocallyControlled() && VaultComponent)
	{
		// Disable jump if vaulting
		if (VaultComponent->bPressedVault)
		{
			bPressedJump = false;
		}

		// Possibly execute vault
		if (GetCharacterMovement())
		{
			VaultComponent->CheckVaultInput(DeltaTime, GetCharacterMovement()->MovementMode);
		}
		else
		{
			VaultComponent->CheckVaultInput(DeltaTime);
		}
	}

	// Pick up changes in vaulting state to change movement mode
	// to something other than flying (required for root motion on Z)
	if (bWasVaulting && !bIsVaulting)
	{
		StopVaultAbility();
	}

	// Call super so we actually jump if we're meant to
	Super::CheckJumpInput(DeltaTime);

	// Cache end of frame
	bWasVaulting = bIsVaulting;
}

void AVIACFCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AVIACFCharacter, bRepIsVaulting, COND_SimulatedOnly);
	DOREPLIFETIME_CONDITION(AVIACFCharacter, RepMotionMatch, COND_SimulatedOnly);
}

void AVIACFCharacter::Jump()
{
	// If missing critical components then jump and exit
	if (!VaultComponent || !GetCharacterMovement())
	{
		Super::Jump();
		return;
	}

	// Either jump or vault, determined by VaultComponent::EVIJumpKeyPriority
	if (VaultComponent->Jump(GetCharacterMovement()->GetGravityZ(), CanJump(), GetCharacterMovement()->IsFalling()))
	{
		// Jump normally
		Super::Jump();
	}
	else
	{
		// Jump key essentially presses the vault input
		VaultComponent->Vault();
	}
}

void AVIACFCharacter::StopJumping()
{
	Super::StopJumping();

	// Release vault input if the jump key pressed vault instead
	if (VaultComponent)
	{
		VaultComponent->StopJumping();
	}
}

void AVIACFCharacter::StartVaultAbility_Implementation()
{
	// Called by GA_Vault
	// Need to be in flying mode to have root motion on Z axis
	if (GetCharacterMovement() && GetLocalRole() > ROLE_SimulatedProxy)
	{
		GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	}
}

void AVIACFCharacter::StopVaultAbility()
{
	// Called by CheckJumpInput()
	// Exiting flying mode
	// This may put is straight into falling if we aren't properly grounded, which is fine
	if (GetCharacterMovement() && GetLocalRole() > ROLE_SimulatedProxy)
	{
		GetCharacterMovement()->SetMovementMode(GetCharacterMovement()->GetGroundMovementMode());
	}

	OnStopVaultAbility();
}

void AVIACFCharacter::OnRep_MotionMatch()
{
	// Simulated proxies update their sync points here, sent from the server during GA_Vault
	MotionWarping->AddOrUpdateSyncPoint(TEXT("VaultSyncPoint"), FVIMotionWarpingSyncPoint(RepMotionMatch.Location, RepMotionMatch.Direction.ToOrientationQuat()));
}

bool AVIACFCharacter::IsVaulting() const
{
	// Simulated proxies use the value provided by server
	if (GetLocalRole() == ROLE_SimulatedProxy)
	{
		return bRepIsVaulting;
	}

	// Local and authority uses gameplay tags for a predicted result
	if (VaultComponent)
	{
		return VaultComponent->IsVaulting();
	}

	return false;
}

FVector AVIACFCharacter::GetVaultDirection_Implementation() const
{
	// Use input vector if available
	if (GetCharacterMovement() && !GetCharacterMovement()->GetCurrentAcceleration().IsNearlyZero())
	{
		return GetCharacterMovement()->GetCurrentAcceleration();
	}

	// Use character facing direction if not providing input
	return GetActorForwardVector();
}

bool AVIACFCharacter::CanVault_Implementation() const
{
	// Vaulting must finish before starting another vault attempt
	if (IsVaulting())
	{
		return false;
	}

	// Invalid components
	if (!VaultComponent || !GetCharacterMovement())
	{
		return false;
	}

	// Animation instance is required to play vault montage
	if (!GetMesh() || !GetMesh()->GetAnimInstance())
	{
		return false;
	}

	// Authority not initialized (this isn't set on clients)
	if (HasAuthority() && !VaultComponent->bVaultAbilityInitialized)
	{
		return false;
	}

	// Exit if character is in a state they cannot vault from
	if (GetCharacterMovement()->IsMovingOnGround() || GetCharacterMovement()->IsFalling() || GetCharacterMovement()->IsSwimming())
	{
		if (GetCharacterMovement()->IsMovingOnGround() && !VaultComponent->bCanVaultFromGround)
		{
			return false;
		}

		if (GetCharacterMovement()->IsFalling() && !VaultComponent->bCanVaultFromFalling)
		{
			return false;
		}

		if (GetCharacterMovement()->IsSwimming() && !VaultComponent->bCanVaultFromSwimming)
		{
			return false;
		}
	}
	else
	{
		return false;
	}

	// Can't vault while crouching
	if (!VaultComponent->bCanVaultFromCrouching && GetCharacterMovement()->IsCrouching())
	{
		return false;
	}

	// Passed all conditions
	return true;
}

void AVIACFCharacter::OnLocalPlayerVault_Implementation(const FVector& Location, const FVector& Direction)
{
	// LocalPlayer just stores the data in the same place for convenience, ease of use, memory reduction, etc
	RepMotionMatch = FVIRepMotionMatch(Location, Direction);
}

void AVIACFCharacter::GetVaultLocationAndDirection_Implementation(FVector& OutLocation, FVector& OutDirection) const
{
	// Because LocalPlayer stores in the same place, no need for any testing as they all use RepMotionMatch to store this

	// This is only currently used for FBIK tracing
	OutLocation = RepMotionMatch.Location;
	OutDirection = RepMotionMatch.Direction;
}

void AVIACFCharacter::ReplicateMotionMatch_Implementation(const FVIRepMotionMatch& MotionMatch)
{
	// GA_Vault has directed server to update it's RepMotionMatch property so that it will
	// be replicated to simulated proxies with 1 decimal point of precision (net quantization)
	RepMotionMatch = MotionMatch;
}

bool AVIACFCharacter::IsWalkable_Implementation(const FHitResult& HitResult) const
{
	// Surface we hit can be walked on or not
	return GetCharacterMovement() && GetCharacterMovement()->IsWalkable(HitResult);
}

bool AVIACFCharacter::CanAutoVaultInCustomMovementMode_Implementation() const
{
	return true;

	// Example usage commented out

	/*

	if (GetCharacterMovement())
	{
		switch (GetCharacterMovement()->CustomMovementMode)
		{
		case 0:
			return false;
		case 1:  // Some example custom mode where auto vault can work
			return true;
		case 2:
			return false;
		default:
			return true;
		}
	}

	*/
}

UAbilitySystemComponent* AVIACFCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystem;
}
