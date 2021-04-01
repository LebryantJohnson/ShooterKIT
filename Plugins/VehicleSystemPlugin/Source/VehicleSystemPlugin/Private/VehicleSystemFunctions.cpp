// Copyright 2019-2020 Seven47 Software. All Rights Reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited

#include "VehicleSystemFunctions.h"
#include "VehicleSystemPlugin.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Interfaces/IPluginManager.h"
#include <EngineGlobals.h>
#include <Runtime/Engine/Classes/Engine/Engine.h>

UVehicleSystemFunctions::UVehicleSystemFunctions(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{

}

FString UVehicleSystemFunctions::GetPluginVersion()
{
	FString PluginName = "VehicleSystemPlugin";
 
	IPluginManager& PluginManager = IPluginManager::Get();
	TArray<TSharedRef<IPlugin>> Plugins = PluginManager.GetDiscoveredPlugins();
	for(const TSharedRef<IPlugin>& Plugin: Plugins)
	{
		if (Plugin->GetName() == PluginName)
		{
			const FPluginDescriptor& Descriptor = Plugin->GetDescriptor();
			FString Version = Descriptor.VersionName;
			return Version;
		}
	}
	return "Error";
}

// This is necessary because the function provided by the engine doesn't include the bone name
void UVehicleSystemFunctions::SetLinearDamping(UPrimitiveComponent* target, float InDamping, FName BoneName)
{
	if(target)
	{
		FBodyInstance* BI = target->GetBodyInstance(BoneName);
		if (BI)
		{
			BI->LinearDamping = InDamping;
			BI->UpdateDampingProperties();
		}
	}
}

// This is necessary because the function provided by the engine doesn't include the bone name
void UVehicleSystemFunctions::SetAngularDamping(UPrimitiveComponent* target, float InDamping, FName BoneName)
{
	if(target)
	{
		FBodyInstance* BI = target->GetBodyInstance(BoneName);
		if (BI)
		{
			BI->AngularDamping = InDamping;
			BI->UpdateDampingProperties();
		}
	}
}

float UVehicleSystemFunctions::GetMeshDiameter(UPrimitiveComponent* target, FName BoneName)
{
	return GetMeshRadius(target, BoneName) * 2;
}

float UVehicleSystemFunctions::GetMeshRadius(UPrimitiveComponent* target, FName BoneName)
{
	if (target)
	{
		UStaticMeshComponent* targetStatic = Cast<UStaticMeshComponent>(target);
		if (targetStatic)
		{
			FBoxSphereBounds MyBounds = targetStatic->GetStaticMesh()->GetBounds();
			return MyBounds.BoxExtent.Z * targetStatic->GetComponentScale().Z;
		}

		FBodyInstance* BI = target->GetBodyInstance(BoneName);
		if (BI)
		{
			FBoxSphereBounds MyBounds = BI->GetBodyBounds();
			return MyBounds.BoxExtent.Z;
		}

	}
	return 0.0f;
}

FVector UVehicleSystemFunctions::GetBoneBounds(UPrimitiveComponent* target, FName BoneName, FVector &Origin)
{
	if (target)
	{
		FBodyInstance* BI = target->GetBodyInstance(BoneName);
		if (BI)
		{
			FBoxSphereBounds MyBounds = BI->GetBodyBounds();
			Origin = MyBounds.Origin;
			return MyBounds.BoxExtent;
		}

	}
	return FVector();
}

FVector UVehicleSystemFunctions::GetMeshCenterOfMass(UPrimitiveComponent* target, FName BoneName)
{
	FBodyInstance* targetBI = target->GetBodyInstance(BoneName);
	if (targetBI)
	{
		return targetBI->GetMassSpaceLocal().GetLocation();
	}

	return FVector::ZeroVector;
}

void UVehicleSystemFunctions::PrintToScreenWithTag(const FString& InString, FLinearColor TextColor, float Duration, int Tag)
{
	GEngine->AddOnScreenDebugMessage(Tag, Duration, TextColor.ToFColor(true), InString);
}

bool UVehicleSystemFunctions::RunningInEditor_World(UObject* WorldContextObject)
{
	if(!WorldContextObject) return false;
	
	//using a context object to get the world
	UWorld* const World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if(!World) return false;
	
	return (World->WorldType == EWorldType::Editor );
}

bool UVehicleSystemFunctions::RunningInPIE_World(UObject* WorldContextObject)
{
	if(!WorldContextObject) return false;
	
	//using a context object to get the world
	UWorld* const World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if(!World) return false;
	
	return (World->WorldType == EWorldType::PIE );
}

bool UVehicleSystemFunctions::RunningInGame_World(UObject* WorldContextObject)
{
	if(!WorldContextObject) return false;
	
	//using a context object to get the world
	UWorld* const World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if(!World) return false;
	
	return (World->WorldType == EWorldType::Game );
}