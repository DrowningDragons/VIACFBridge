// Copyright (c) 2019-2021 Drowning Dragons Limited. All Rights Reserved.


#include "VIACFAnimInstance.h"
#include "Pawn/VICharacterBase.h"
#include "VIBlueprintFunctionLibrary.h"
#include "GameFramework/CharacterMovementComponent.h"

void UVIACFAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	Character = (TryGetPawnOwner()) ? Cast<AVICharacterBase>(TryGetPawnOwner()) : nullptr;
}

void UVIACFAnimInstance::NativeUpdateAnimation(float DeltaTime)
{
	Super::NativeUpdateAnimation(DeltaTime);

	if (Character)
	{
		bool bWasVaulting = bIsVaulting;
		bIsVaulting = Character->IsVaulting();
		if (bIsVaulting)
		{
			// Resetting these while vaulting leads to better blending out
			Speed = 0.f;

			// Interp FBIK
			UVIBlueprintFunctionLibrary::InterpolateFBIK(DeltaTime, FBIK);

			// Right Hand
			{
				const FVIBoneFBIKData* BoneData = UVIBlueprintFunctionLibrary::GetBoneForFBIK(RHandName, FBIK);
				bRHand = BoneData->bEnabled;
				RHandLoc = BoneData->Location;
			}
			// Left Hand
			{
				const FVIBoneFBIKData* BoneData = UVIBlueprintFunctionLibrary::GetBoneForFBIK(LHandName, FBIK);
				bLHand = BoneData->bEnabled;
				LHandLoc = BoneData->Location;
			}
			// Both Hands
			{
				bBothHand = (bRHand && bLHand);
				if (bBothHand)
				{
					// Use only control rig with both IK
					// Only ever uses one control rig at a time
					bRHand = false;
					bLHand = false;
				}
			}
		}

		if (bIsVaulting && !bWasVaulting)
		{
			OnStartVault();
		}
		else if (bWasVaulting && !bIsVaulting)
		{
			OnStopVault();
		}
	}
}

void UVIACFAnimInstance::OnStartVault()
{
	K2_OnStartVault();
}

void UVIACFAnimInstance::OnStopVault()
{
	// Stopped vaulting
	bRHand = false;
	bLHand = false;
	bBothHand = false;

	K2_OnStopVault();
}

void UVIACFAnimInstance::SetBoneFBIK_Implementation(const FName& BoneName, const FVector& BoneLocation, bool bEnabled)
{
	UVIBlueprintFunctionLibrary::ToggleBoneFBIK(BoneName, BoneLocation, bEnabled, FBIK);
}
