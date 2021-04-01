// Copyright 2019-2020 Seven47 Software. All Rights Reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited

#include "VehicleSystemBase.h"
#include "TimerManager.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/KismetMathLibrary.h"

AVehicleSystemBase::AVehicleSystemBase()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = true;
	VehicleMesh = CreateDefaultSubobject<UStaticMeshComponent>("VehicleMesh");
	RootComponent = VehicleMesh;

	ReplicateMovement = true;
	NetSendRate = 0.05f;
	NetTimeBehind = 0.15f;
	NetLerpStart = 0.35f;
	NetPositionTolerance = 0.1f;
	NetSmoothing = 10.0f;
}

//Replicated Variables
void AVehicleSystemBase::GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AVehicleSystemBase, RestState);
}

void AVehicleSystemBase::BeginPlay()
{
	Super::BeginPlay();
	SetReplicationTimer(ReplicateMovement);
}

void AVehicleSystemBase::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	if(GetLocalRole() == ROLE_Authority)
	{
		Multicast_ChangedOwner();
	}
	ClearQueue();
}

void AVehicleSystemBase::UnPossessed()
{
	Super::UnPossessed();
	if(GetLocalRole() == ROLE_Authority)
	{
		Multicast_ChangedOwner();
	}
	ClearQueue();
}

void AVehicleSystemBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	SyncTrailerRotation(DeltaTime);
	TickDeltaTime = DeltaTime;
	NetworkRoles CurrentRole = GetNetworkRole();
	if (CurrentRole != NetworkRoles::Owner)
	{
		if (ReplicateMovement && ShouldSyncWithServer)
		{
			SyncPhysics();
		}
	}
}

void AVehicleSystemBase::SyncTrailerRotation_Implementation(float DeltaTime)
{
	//Used in blueprint
}

void AVehicleSystemBase::SetupPlayerInputComponent(UInputComponent *PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
}

float AVehicleSystemBase::GetSteeringFromCurve(float Speed)
{
	const FRichCurve *SCurve = SteeringCurve.GetRichCurveConst();
	return SCurve->Eval(Speed);
}

void AVehicleSystemBase::SetShouldSyncWithServer(bool ShouldSync)
{
	ShouldSyncWithServer = ShouldSync;
	SetReplicationTimer(ShouldSync);
}

void AVehicleSystemBase::SetReplicationTimer(bool Enabled)
{
	if (ReplicateMovement && Enabled)
	{
		GetWorldTimerManager().SetTimer(NetSendTimer, this, &AVehicleSystemBase::NetStateSend, NetSendRate, true);
	}
	else
	{
		GetWorldTimerManager().ClearTimer(NetSendTimer);
		IsResting = false;
		ClearQueue();
	}
}

void AVehicleSystemBase::NetStateSend()
{
	if (GetNetworkRole() == NetworkRoles::Owner)
	{
		FNetState NewState = CreateNetStateForNow();

		//Check if resting
		if (NewState.velocity.Size() > 50) //Not resting
		{
			Server_ReceiveNetState(NewState);
			if(IsResting) //Is resting but should not be
			{
				FNetState BlankRestState;
				Server_ReceiveRestState(BlankRestState);
				if(GetLocalRole() == ROLE_Authority) {OnRep_RestState();} //RepNotify on Server
			}
		}
		else //Is resting
		{
			//Not resting but should be, or distance is too different
			if(!IsResting || FVector::DistXY(RestState.position, NewState.position) > 50)
			{
				Server_ReceiveRestState(NewState);
				if(GetLocalRole() == ROLE_Authority) {OnRep_RestState();} //RepNotify on Server
			}
		}

		if(StateQueue.Num() > 0)
		{
			ClearQueue(); //Clear the queue if we are the owner to avoid syncing to old states
		}
	}
}

FNetState AVehicleSystemBase::CreateNetStateForNow()
{
	FNetState newState;
	FTransform primTransform = VehicleMesh->GetComponentToWorld();
	newState.position = primTransform.GetLocation();
	newState.rotation = primTransform.GetRotation().Rotator();
	newState.velocity = VehicleMesh->GetPhysicsLinearVelocity();
	newState.angularVelocity = VehicleMesh->GetPhysicsAngularVelocityInDegrees();
	newState.timestamp = GetLocalWorldTime();
	return newState;
}

bool AVehicleSystemBase::Server_ReceiveNetState_Validate(FNetState State)
{
	return true;
}
void AVehicleSystemBase::Server_ReceiveNetState_Implementation(FNetState State)
{
	Client_ReceiveNetState(State);
}

bool AVehicleSystemBase::Client_ReceiveNetState_Validate(FNetState State)
{
	return true;
}
void AVehicleSystemBase::Client_ReceiveNetState_Implementation(FNetState State)
{
	if(ShouldSyncWithServer)
	{
		AddStateToQueue(State);
	}
}

bool AVehicleSystemBase::Server_ReceiveRestState_Validate(FNetState State)
{
	return true;
}
void AVehicleSystemBase::Server_ReceiveRestState_Implementation(FNetState State)
{
	RestState = State; //Clients should still receive even when not actively syncing
	if(GetLocalRole() == ROLE_Authority) {OnRep_RestState();} //RepNotify on Server
}

bool AVehicleSystemBase::Multicast_ChangedOwner_Validate()
{
	return true;
}
void AVehicleSystemBase::Multicast_ChangedOwner_Implementation()
{
	ClearQueue();
	OwnerChanged();
}

void AVehicleSystemBase::AddStateToQueue(FNetState StateToAdd)
{
	if (GetNetworkRole() != NetworkRoles::Owner)
	{
		//If we have 10 or more states we are flooded and should drop new states
		if (StateQueue.Num() < 10)
		{
			StateToAdd.timestamp += NetTimeBehind; //Change the timestamp to the future so we can lerp

			if(StateToAdd.timestamp < LastActiveTimestamp)
			{
				return; //This state is late and should be discarded
			}

			if (StateQueue.IsValidIndex(0))
			{
				int8 lastindex = StateQueue.Num() - 1;
				for (int8 i = lastindex; i >= 0; --i)
				{
					if (StateQueue.IsValidIndex(i))
					{
						if (StateQueue[i].timestamp < StateToAdd.timestamp)
						{
							StateQueue.Insert(StateToAdd, i + 1);
							CalculateTimestamps();
							StateToAdd.localtimestamp = StateQueue[i+1].timestamp;
							break;
						}
					}
				}
			}
			else
			{
				StateToAdd.localtimestamp = GetLocalWorldTime() + NetTimeBehind;
				StateQueue.Insert(StateToAdd, 0); //If the queue is empty just add it in the first spot
			}
		}
	}
}

void AVehicleSystemBase::ClearQueue()
{
	StateQueue.Empty();
	CreateNewStartState = true;
	LastActiveTimestamp = 0;
}

void AVehicleSystemBase::CalculateTimestamps()
{
	int8 lastindex = StateQueue.Num() - 1;
	for (int8 i = 0; i <= lastindex; i++)
	{
		//The first state is always our point of reference and should not change
		//Especially since it could be actively syncing
		if(i != 0)
		{
			if (StateQueue.IsValidIndex(i))
			{
				//Calculate the time difference in the owners times and apply it to our local times
				float timeDifference = StateQueue[i].timestamp - StateQueue[i-1].timestamp;
				StateQueue[i].localtimestamp = StateQueue[i-1].localtimestamp + timeDifference;
			}
		}
	}
}

void AVehicleSystemBase::SyncPhysics()
{
	if(IsResting)
	{
		SetVehicleLocation(RestState.position, RestState.rotation);
		if(StateQueue.Num() > 0)
		{
			ClearQueue(); //Queue should be empty while resting
		}
		return;
	}

	if (StateQueue.IsValidIndex(0))
	{
		FNetState NextState = StateQueue[0];
		float ServerTime = GetLocalWorldTime();

		//use physics until we are close enough to this timestamp
		if (ServerTime >= (NextState.localtimestamp - NetLerpStart))
		{
			if (CreateNewStartState)
			{
				LerpStartState = CreateNetStateForNow();
				CreateNewStartState = false;

					//If our start state is nearly equal to our end state, just skip it
					//This keeps the physics from looking weird when moving slowly, and allows physics to settle
				if (FMath::IsNearlyEqual(LerpStartState.position.X, NextState.position.X, NetPositionTolerance) &&
                    FMath::IsNearlyEqual(LerpStartState.position.Y, NextState.position.Y, NetPositionTolerance) &&
                    FMath::IsNearlyEqual(LerpStartState.position.Z, NextState.position.Z, NetPositionTolerance))
				{
					StateQueue.RemoveAt(0);
					CreateNewStartState = true;
					return;
				}
			}
			
			LastActiveTimestamp = NextState.timestamp;

			//Lerp To State
			//Our start state may have been created after the lerp start time, so choose whatever is latest
			float lerpBeginTime = LerpStartState.timestamp;
			float lerpPercent = FMath::Clamp(GetPercentBetweenValues(ServerTime, lerpBeginTime, NextState.localtimestamp), 0.0f, 1.0f);
			FVector NewPosition = UKismetMathLibrary::VLerp(LerpStartState.position, NextState.position, lerpPercent);
			FRotator NewRotation = UKismetMathLibrary::RLerp(LerpStartState.rotation, NextState.rotation, lerpPercent, true);
			SetVehicleLocation(NewPosition, NewRotation);

			if(lerpPercent >= 0.99f || lerpBeginTime > NextState.localtimestamp)
			{
				ApplyExactNetState(NextState);
				StateQueue.RemoveAt(0);
				CreateNewStartState = true;
			}
		}
	}
}

void AVehicleSystemBase::LerpToNetState(FNetState NextState, float CurrentServerTime)
{
	//Our start state may have been created after the lerp start time, so choose whatever is latest
	float lerpBeginTime = FMath::Max(LerpStartState.timestamp, (NextState.timestamp - NetLerpStart));

	float lerpPercent = FMath::Clamp(GetPercentBetweenValues(CurrentServerTime, lerpBeginTime, NextState.timestamp), 0.0f, 1.0f);

	FVector NewPosition = UKismetMathLibrary::VLerp(LerpStartState.position, NextState.position, lerpPercent);
	FRotator NewRotation = UKismetMathLibrary::RLerp(LerpStartState.rotation, NextState.rotation, lerpPercent, true);
	SetVehicleLocation(NewPosition, NewRotation);
}

void AVehicleSystemBase::ApplyExactNetState(FNetState State)
{
	SetVehicleLocation(State.position, State.rotation);
	VehicleMesh->SetPhysicsLinearVelocity(State.velocity);
	VehicleMesh->SetPhysicsAngularVelocityInDegrees(State.angularVelocity);
}

void AVehicleSystemBase::SetVehicleLocation(FVector NewPosition, FRotator NewRotation)
{
	//Move vehicle chassis
	if(FVector::DistXY(VehicleMesh->GetComponentLocation(), NewPosition) < 3000)
	{
		FVector SmoothPos = UKismetMathLibrary::VInterpTo(VehicleMesh->GetComponentLocation(), NewPosition, TickDeltaTime, NetSmoothing);
		FRotator SmoothRot = UKismetMathLibrary::RInterpTo(VehicleMesh->GetComponentRotation(), NewRotation, TickDeltaTime, NetSmoothing);
		SetActorLocationAndRotation(SmoothPos, SmoothRot, false, nullptr, TeleportFlagToEnum(false));
	}
	else
	{
		//Calculate offset of new position
		FTransform VehicleTransform = VehicleMesh->GetComponentToWorld();
		FVector OffsetLocation = NewPosition - VehicleTransform.GetLocation();
		FRotator OffsetRotation = NewRotation - VehicleTransform.Rotator();
		OffsetRotation.Normalize();
		FTransform Offset = FTransform(OffsetRotation, OffsetLocation, FVector::OneVector);
		
		//Teleport Vehicle chassis
		SetActorLocationAndRotation(NewPosition, NewRotation, false, nullptr, TeleportFlagToEnum(true));
		VehicleMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
		VehicleMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);

		//Teleport vehicle wheels
		TeleportWheels();
	}
}