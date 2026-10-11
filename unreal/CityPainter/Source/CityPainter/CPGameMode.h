#pragma once

// ACPGameMode: names the pawn and controller classes. Nothing else.

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "CPGameMode.generated.h"

UCLASS()
class ACPGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACPGameMode();
};
