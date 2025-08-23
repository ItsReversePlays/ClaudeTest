#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditableAfterLandingTest,
    "Project.VoxelPhysics.EditableAfterLanding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FEditableAfterLandingTest::RunTest(const FString& Parameters)
{
    FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/probe.json"));
    FString JsonStr;
    if (!FFileHelper::LoadFileToString(JsonStr, *Path))
        return AddError(TEXT("probe.json missing")), false;

    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonStr), Root) || !Root.IsValid())
        return AddError(TEXT("Invalid JSON in probe.json")), false;

    bool bSettled = Root->GetBoolField(TEXT("island_settled"));
    bool bAwake   = Root->GetBoolField(TEXT("awake"));
    bool bHashOK  = Root->HasField(TEXT("surfaceHash_match")) ? Root->GetBoolField(TEXT("surfaceHash_match")) : false;

    if (!bSettled) AddError(TEXT("Island did not settle"));
    if (bAwake)    AddError(TEXT("Island still awake after landing"));
    if (!bHashOK)  AddError(TEXT("Surface hash mismatch"));

    return bSettled && !bAwake && bHashOK;
}