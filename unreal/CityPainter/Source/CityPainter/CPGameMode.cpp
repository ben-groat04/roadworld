#include "CPGameMode.h"

#include "CPCameraPawn.h"
#include "CPPlayerController.h"

ACPGameMode::ACPGameMode()
{
	DefaultPawnClass = ACPCameraPawn::StaticClass();
	PlayerControllerClass = ACPPlayerController::StaticClass();
	HUDClass = nullptr;
}
