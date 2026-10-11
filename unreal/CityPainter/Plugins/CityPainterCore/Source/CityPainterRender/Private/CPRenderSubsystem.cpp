#include "CPRenderSubsystem.h"

#include "CPSettings.h"
#include "CPWorldSubsystem.h"
#include "CityPainterRender.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

UCPRenderSubsystem::UCPRenderSubsystem() = default;
UCPRenderSubsystem::~UCPRenderSubsystem() = default;

bool UCPRenderSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::Editor;
}

bool UCPRenderSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	if (!World)
	{
		return false;
	}
	// Same rule as UCPWorldSubsystem: only worlds on a City Painter map.
	const FString Package = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
	return Package.StartsWith(GetDefault<UCPSettings>()->MapPathPrefix);
}

void UCPRenderSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UCPWorldSubsystem>();
	Super::Initialize(Collection);
}

void UCPRenderSubsystem::Deinitialize()
{
	LogCounts(TEXT("before teardown"));
	for (TUniquePtr<ICPRenderLayer>& Layer : Layers)
	{
		Layer->Teardown();
	}
	Layers.Reset();
	if (AActor* R = Root.Get())
	{
		R->Destroy();
	}
	bBuilt = false;
	Super::Deinitialize();
}

void UCPRenderSubsystem::Tick(float DeltaTime)
{
	// The world is built on the first tick after the sim exists (editor worlds never call OnWorldBeginPlay).
	if (!bBuilt)
	{
		BuildLayers();
	}
}

TStatId UCPRenderSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCPRenderSubsystem, STATGROUP_Tickables);
}

void UCPRenderSubsystem::BuildLayers()
{
	UWorld* World = GetWorld();
	UCPWorldSubsystem* WorldSub = World ? World->GetSubsystem<UCPWorldSubsystem>() : nullptr;
	if (!WorldSub || !WorldSub->GetSim())
	{
		bBuilt = true;  // nothing to draw; do not retry every frame
		return;
	}
	bBuilt = true;

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;  // never saved into the level
	Params.Name = FName(TEXT("CP_RenderRoot"));
	Params.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
	AActor* RootActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	if (!RootActor)
	{
		UE_LOG(LogCPRender, Error, TEXT("Could not spawn the render root actor"));
		return;
	}
	USceneComponent* Scene = NewObject<USceneComponent>(RootActor, TEXT("Root"));
	RootActor->SetRootComponent(Scene);
	Scene->RegisterComponent();
#if WITH_EDITOR
	RootActor->SetActorLabel(TEXT("CP_RenderRoot (transient)"));
#endif
	Root = RootActor;

	FCPRenderContext Context;
	Context.World = World;
	Context.Root = RootActor;
	Context.Sim = WorldSub->GetSim();
	Layers = FCPRenderLayerRegistry::Get().CreateAll();
	for (TUniquePtr<ICPRenderLayer>& Layer : Layers)
	{
		UE_LOG(LogCPRender, Log, TEXT("Building layer '%s'"), *Layer->LayerName().ToString());
		Layer->Build(Context);
	}
	LogCounts(TEXT("after build"));
}

void UCPRenderSubsystem::LogCounts(const TCHAR* When) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	int32 Actors = 0, Roots = 0, MeshComponents = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		++Actors;
		Roots += It->GetName().StartsWith(TEXT("CP_RenderRoot")) ? 1 : 0;
		TInlineComponentArray<UPrimitiveComponent*> Comps;
		It->GetComponents(Comps);
		for (const UPrimitiveComponent* C : Comps)
		{
			MeshComponents += C->GetClass()->GetName().Contains(TEXT("ProceduralMesh")) ? 1 : 0;
		}
	}
	UE_LOG(LogCPRender, Log, TEXT("World counts (%s, world type %d): %d actors, %d render roots, %d procedural mesh components"), When, static_cast<int32>(World->WorldType), Actors, Roots, MeshComponents);
}
