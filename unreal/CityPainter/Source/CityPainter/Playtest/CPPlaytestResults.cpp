#include "CPPlaytestResults.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"

void FCPPlaytestResults::AddCheck(const FString& Control, const FString& Expected, const FString& Observed, bool bPass, const FString& Image)
{
	Checks.Add({Control, Expected, Observed, bPass, Image});
}

FString FCPPlaytestResults::RequestShot(const FString& Name)
{
	const FString File = Name + TEXT(".png");
	FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputDir, File), /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
	Shots.Add(File);
	return File;
}

int32 FCPPlaytestResults::NumFailed() const
{
	int32 N = 0;
	for (const FCPPlaytestCheck& C : Checks)
	{
		N += C.bPass ? 0 : 1;
	}
	return N;
}

bool FCPPlaytestResults::Write() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("checks_total"), Checks.Num());
	Root->SetNumberField(TEXT("checks_failed"), NumFailed());
	TArray<TSharedPtr<FJsonValue>> Rows;
	for (const FCPPlaytestCheck& C : Checks)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("control"), C.Control);
		O->SetStringField(TEXT("expected"), C.Expected);
		O->SetStringField(TEXT("observed"), C.Observed);
		O->SetBoolField(TEXT("pass"), C.bPass);
		O->SetStringField(TEXT("image"), C.Image);
		Rows.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("checks"), Rows);
	TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
	for (const TPair<FString, double>& P : Metrics)
	{
		M->SetNumberField(P.Key, P.Value);
	}
	Root->SetObjectField(TEXT("metrics"), M);
	TSharedRef<FJsonObject> N = MakeShared<FJsonObject>();
	for (const TPair<FString, FString>& P : Notes)
	{
		N->SetStringField(P.Key, P.Value);
	}
	Root->SetObjectField(TEXT("notes"), N);
	TArray<TSharedPtr<FJsonValue>> S;
	for (const FString& Shot : Shots)
	{
		S.Add(MakeShared<FJsonValueString>(Shot));
	}
	Root->SetArrayField(TEXT("screenshots"), S);

	FString Text;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
	FJsonSerializer::Serialize(Root, Writer);
	return FFileHelper::SaveStringToFile(Text, *FPaths::Combine(OutputDir, TEXT("results.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
