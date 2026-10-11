#include "CityPainterRender.h"

#include "CPGroundLayer.h"
#include "ICPRenderLayer.h"

DEFINE_LOG_CATEGORY(LogCPRender);

IMPLEMENT_MODULE(FCityPainterRenderModule, CityPainterRender)

FCPRenderLayerRegistry& FCPRenderLayerRegistry::Get()
{
	static FCPRenderLayerRegistry Instance;
	return Instance;
}

void FCPRenderLayerRegistry::Register(FName Name, FFactory Factory)
{
	Unregister(Name);
	Entries.Emplace(Name, MoveTemp(Factory));
}

void FCPRenderLayerRegistry::Unregister(FName Name)
{
	Entries.RemoveAll([Name](const TPair<FName, FFactory>& E) { return E.Key == Name; });
}

TArray<TUniquePtr<ICPRenderLayer>> FCPRenderLayerRegistry::CreateAll() const
{
	TArray<TUniquePtr<ICPRenderLayer>> Out;
	for (const TPair<FName, FFactory>& E : Entries)
	{
		Out.Add(E.Value());
	}
	return Out;
}

void FCityPainterRenderModule::StartupModule()
{
	// One line per layer. Later: roads, markings, structures, instanced kit, vegetation.
	FCPRenderLayerRegistry::Get().Register(FCPGroundLayer::Name(), [] { return MakeUnique<FCPGroundLayer>(); });
}

void FCityPainterRenderModule::ShutdownModule()
{
	FCPRenderLayerRegistry::Get().Unregister(FCPGroundLayer::Name());
}
