#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelAllTests,
    "Project.VoxelPhysics.All",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

static bool LoadProbeJson(TSharedPtr<FJsonObject>& Out, FAutomationTestBase& Test)
{
    const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/probe.json"));
    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *Path))
    {
        Test.AddError(FString::Printf(TEXT("Missing probe.json at %s"), *Path));
        return false;
    }
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, Out) || !Out.IsValid())
    {
        Test.AddError(TEXT("Failed to parse probe.json"));
        return false;
    }
    return true;
}

bool FVoxelAllTests::RunTest(const FString& Parameters)
{
    TSharedPtr<FJsonObject> Root;
    if (!LoadProbeJson(Root, *this)) return false;

    // T5 block
    const TSharedPtr<FJsonObject>* T5 = nullptr;
    if (!Root->TryGetObjectField(TEXT("T5_EditableAfterLanding"), T5) || !T5 || !T5->IsValid())
    {
        AddError(TEXT("probe.json missing 'T5_EditableAfterLanding'"));
        return false;
    }
    const bool bT5Passed = (*T5)->HasField(TEXT("testPassed")) && (*T5)->GetBoolField(TEXT("testPassed"));

    // T6 block
    const TSharedPtr<FJsonObject>* T6 = nullptr;
    if (!Root->TryGetObjectField(TEXT("T6_StressGuards"), T6) || !T6 || !T6->IsValid())
    {
        AddError(TEXT("probe.json missing 'T6_StressGuards'"));
        return false;
    }
    const bool bT6Passed = (*T6)->HasField(TEXT("testPassed")) && (*T6)->GetBoolField(TEXT("testPassed"));

    if (!bT5Passed) AddError(TEXT("T5_EditableAfterLanding.testPassed == false"));
    if (!bT6Passed) AddError(TEXT("T6_StressGuards.testPassed == false"));

    AddInfo(FString::Printf(TEXT("Summary: T5=%s, T6=%s"),
        bT5Passed ? TEXT("PASS") : TEXT("FAIL"),
        bT6Passed ? TEXT("PASS") : TEXT("FAIL")));

    return bT5Passed && bT6Passed;
}