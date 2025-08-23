#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStressGuardsTest,
    "Project.VoxelPhysics.Stress.Guards",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

static bool LoadJsonFromSavedAutomation(const FString& FileName, TSharedPtr<FJsonObject>& OutRoot, FAutomationTestBase& Test)
{
    const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), FileName);
    FString JsonStr;
    if (!FFileHelper::LoadFileToString(JsonStr, *Path))
    {
        Test.AddError(FString::Printf(TEXT("Missing %s"), *Path));
        return false;
    }
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
    if (!FJsonSerializer::Deserialize(Reader, OutRoot) || !OutRoot.IsValid())
    {
        Test.AddError(FString::Printf(TEXT("Failed to parse %s"), *Path));
        return false;
    }
    return true;
}

bool FStressGuardsTest::RunTest(const FString& Parameters)
{
    // Read Saved/Automation/probe.json (written by VoxelStressTestSpec runtime)
    TSharedPtr<FJsonObject> Root;
    if (!LoadJsonFromSavedAutomation(TEXT("probe.json"), Root, *this))
        return false;

    // T6 block
    const TSharedPtr<FJsonObject>* T6 = nullptr;
    if (!Root->TryGetObjectField(TEXT("T6_StressGuards"), T6) || !T6 || !T6->IsValid())
    {
        AddError(TEXT("probe.json missing 'T6_StressGuards' object"));
        return false;
    }

    auto GetInt = [&](const TCHAR* K, int32& Out)->bool{
        if (!(*T6)->HasField(K)) { AddError(FString::Printf(TEXT("Missing int field '%s'"), K)); return false; }
        Out = (int32)(*T6)->GetNumberField(K); return true;
    };
    auto GetDouble = [&](const TCHAR* K, double& Out)->bool{
        if (!(*T6)->HasField(K)) { AddError(FString::Printf(TEXT("Missing number field '%s'"), K)); return false; }
        Out = (*T6)->GetNumberField(K); return true;
    };
    auto GetBool = [&](const TCHAR* K, bool& Out)->bool{
        if (!(*T6)->HasField(K)) { AddError(FString::Printf(TEXT("Missing bool field '%s'"), K)); return false; }
        Out = (*T6)->GetBoolField(K); return true;
    };

    // Inputs (names match your probe.json)
    int32 LiveIslands=0, MaxLiveIslands=0, ProxyRebuilds=0, BudgetExceeded=0, CutsCompleted=0, IslandsCreated=0, IslandsCleaned=0;
    double AvgProxyRebuildMs=0.0, ProxyRebuildBudgetMs=0.0, TotalTimeSeconds=0.0, MaxTimeSeconds=0.0;
    bool bCapsEnforced=false;

    bool ok =
        GetInt(TEXT("liveIslands"), LiveIslands) &
        GetInt(TEXT("maxLiveIslands"), MaxLiveIslands) &
        GetInt(TEXT("proxyRebuilds"), ProxyRebuilds) &
        GetInt(TEXT("budgetExceeded"), BudgetExceeded) &
        GetInt(TEXT("cutsCompleted"), CutsCompleted) &
        GetInt(TEXT("islandsCreated"), IslandsCreated) &
        GetInt(TEXT("islandsCleaned"), IslandsCleaned) &
        GetDouble(TEXT("avgProxyRebuildMs"), AvgProxyRebuildMs) &
        GetDouble(TEXT("proxyRebuildBudgetMs"), ProxyRebuildBudgetMs) &
        GetDouble(TEXT("totalTimeSeconds"), TotalTimeSeconds) &
        GetDouble(TEXT("maxTimeSeconds"), MaxTimeSeconds) &
        GetBool(TEXT("performanceCapsEnforced"), bCapsEnforced);

    if (!ok) return false;

    // Assertions against T6 caps
    bool pass = true;

    if (!bCapsEnforced) { AddError(TEXT("Performance caps not enforced during stress test")); pass = false; }

    if (LiveIslands > MaxLiveIslands) {
        AddError(FString::Printf(TEXT("Live islands exceeded cap: %d > %d"), LiveIslands, MaxLiveIslands));
        pass = false;
    }

    if (AvgProxyRebuildMs > ProxyRebuildBudgetMs) {
        AddError(FString::Printf(TEXT("Average proxy rebuild exceeded budget: %.2f ms > %.2f ms"), AvgProxyRebuildMs, ProxyRebuildBudgetMs));
        pass = false;
    }

    if (BudgetExceeded > 0) {
        AddError(FString::Printf(TEXT("Budget exceeded %d time(s)"), BudgetExceeded));
        pass = false;
    }

    if (TotalTimeSeconds > MaxTimeSeconds) {
        AddError(FString::Printf(TEXT("Stress test ran too long: %.3f s > %.3f s"), TotalTimeSeconds, MaxTimeSeconds));
        pass = false;
    }

    // Useful context in the report
    AddInfo(FString::Printf(TEXT("Cuts=%d, Islands{created=%d, cleaned=%d, live=%d/%d}, Proxy{rebuilds=%d, avg=%.2fms<=%.2fms}, BudgetExceeded=%d, TotalTime=%.3fs<=%.3fs"),
        CutsCompleted, IslandsCreated, IslandsCleaned, LiveIslands, MaxLiveIslands,
        ProxyRebuilds, AvgProxyRebuildMs, ProxyRebuildBudgetMs,
        BudgetExceeded, TotalTimeSeconds, MaxTimeSeconds));

    if (pass) AddInfo(TEXT("T6 Stress.Guards: all limits respected."));

    return pass;
}