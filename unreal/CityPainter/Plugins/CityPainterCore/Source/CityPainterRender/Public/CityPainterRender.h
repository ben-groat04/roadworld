#pragma once

#include "Logging/LogMacros.h"
#include "Modules/ModuleManager.h"

CITYPAINTERRENDER_API DECLARE_LOG_CATEGORY_EXTERN(LogCPRender, Log, All);

/** Registers the built-in layers with FCPRenderLayerRegistry at startup. */
class FCityPainterRenderModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
