// Docks tabs that would open as separate floating windows into the main editor window.
//
// Slate has no setting for this, so a per-frame ticker looks for newly created top-level windows that
// hold dock tabs and moves each tab into a stack that lives in the main window:
// - nomad tabs (Output Log, Project Settings, tool panels...) go next to the Level Editor tab;
// - panel tabs of an editor (Level Editor "Details 2", a Blueprint editor's "Find Results"...) go next to
//   another tab of the same editor that is already in the main window.
// Asset editors are handled by the engine's own "Asset Editor Open Location = Main Window" preference,
// which this plugin turns on. Windows created by dragging a tab out on purpose are left alone.

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/SDockingTabStack.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "Runtime/Launch/Resources/Version.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "Classes/EditorStyleSettings.h" // moved from EditorStyle to UnrealEd in 5.1
#else
#include "Settings/EditorStyleSettings.h"
#endif
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWindow.h"

DEFINE_LOG_CATEGORY_STATIC(LogDockToMain, Log, All);

namespace DockToMain
{
	static TAutoConsoleVariable<int32> CVarEnabled(
		TEXT("DockToMain.Enabled"), 1,
		TEXT("1 = dock tabs that would open in a new floating window into the main editor window. 0 = engine default."));

	// Seconds after a tab drag ends during which new windows count as deliberately torn off.
	static constexpr double DragGraceSeconds = 1.0;

	// SDockTab::OnTabClosed is protected; a pointer-to-member taken through a derived class reaches it legally.
	struct FDockTabAccess : public SDockTab
	{
		static FOnTabClosedCallback& OnTabClosedOf(SDockTab& Tab)
		{
			static FOnTabClosedCallback SDockTab::* const Member = &FDockTabAccess::OnTabClosed;
			return Tab.*Member;
		}
	};

	static void CollectTabs(const TSharedRef<SWidget>& Widget, TArray<TSharedRef<SDockTab>>& OutTabs, int32 Depth = 0)
	{
		if (Depth > 64)
		{
			return;
		}
		static const FName DockTabType(TEXT("SDockTab"));
		if (Widget->GetType() == DockTabType)
		{
			OutTabs.Add(StaticCastSharedRef<SDockTab>(Widget));
			return;
		}
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
		{
			CollectTabs(Children->GetChildAt(Index), OutTabs, Depth + 1);
		}
	}

	static void CollectWindows(const TSharedRef<SWindow>& Window, TArray<TSharedRef<SWindow>>& OutWindows)
	{
		OutWindows.Add(Window);
		for (const TSharedRef<SWindow>& Child : Window->GetChildWindows())
		{
			CollectWindows(Child, OutWindows);
		}
	}

	// FTabManager::OnTabRelocated is protected; it is what a drag-and-drop dock calls after moving a tab
	// (re-parents the tab's own sub-tab windows, updates DPI and the last docked window).
	struct FTabManagerAccess : public FTabManager
	{
		static void NotifyRelocated(FTabManager& Manager, const TSharedRef<SDockTab>& Tab, const TSharedPtr<SWindow>& NewWindow)
		{
			static void (FTabManager::* const Member)(const TSharedRef<SDockTab>&, const TSharedPtr<SWindow>&) = &FTabManagerAccess::OnTabRelocated;
			(Manager.*Member)(Tab, NewWindow);
		}
	};
}

class FDockToMainModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Asset editors: the engine already supports opening them in the main window.
		UEditorStyleSettings* StyleSettings = GetMutableDefault<UEditorStyleSettings>();
		if (DockToMain::CVarEnabled.GetValueOnGameThread() != 0 && StyleSettings->AssetEditorOpenLocation != EAssetEditorOpenLocation::MainWindow)
		{
			StyleSettings->AssetEditorOpenLocation = EAssetEditorOpenLocation::MainWindow;
			StyleSettings->SaveConfig();
			UE_LOG(LogDockToMain, Log, TEXT("Set Editor Preferences > Asset Editor Open Location to Main Window."));
		}

		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FDockToMainModule::Tick));
	}

	virtual void ShutdownModule() override
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	}

private:
	bool Tick(float DeltaTime)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return true;
		}

		const TSharedPtr<SWindow> RootWindow = FGlobalTabmanager::Get()->GetRootWindow();
		if (!RootWindow.IsValid())
		{
			return true;
		}

		const double Now = FPlatformTime::Seconds();
		if (FSlateApplication::Get().IsDragDropping())
		{
			LastDragTime = Now;
		}
		const bool bTornOff = Now - LastDragTime < DockToMain::DragGraceSeconds;
		const bool bEnabled = DockToMain::CVarEnabled.GetValueOnGameThread() != 0;

		SeenWindows.RemoveAll([](const TWeakPtr<SWindow>& Window) { return !Window.IsValid(); });

		TArray<TSharedRef<SWindow>> Windows;
		for (const TSharedRef<SWindow>& TopLevel : FSlateApplication::Get().GetTopLevelWindows())
		{
			DockToMain::CollectWindows(TopLevel, Windows);
		}
		for (const TSharedRef<SWindow>& Window : Windows)
		{
			if (Window == RootWindow || SeenWindows.ContainsByPredicate([&Window](const TWeakPtr<SWindow>& Seen) { return Seen.Pin() == Window; }))
			{
				continue;
			}
			SeenWindows.Add(Window);
			UE_LOG(LogDockToMain, Verbose, TEXT("New window '%s'."), *Window->GetTitle().ToString());

			if (bEnabled && !bTornOff && !Window->IsModalWindow())
			{
				DockWindowTabs(Window, RootWindow.ToSharedRef());
			}
		}
		return true;
	}

	void DockWindowTabs(const TSharedRef<SWindow>& Window, const TSharedRef<SWindow>& RootWindow)
	{
		TArray<TSharedRef<SDockTab>> Tabs;
		DockToMain::CollectTabs(Window, Tabs);
		UE_LOG(LogDockToMain, Verbose, TEXT("  %d tab(s) in it."), Tabs.Num());

		for (const TSharedRef<SDockTab>& Tab : Tabs)
		{
			// Tabs nested inside another tab's content report no window of their own and travel with their host.
			if (Tab->GetParentWindow() != Window || Tab->GetTabRole() == ETabRole::MajorTab)
			{
				continue;
			}

			const TSharedPtr<SDockTab> Anchor = FindAnchor(Tab, RootWindow);
			const TSharedPtr<SDockingTabStack> TargetStack = Anchor.IsValid() ? Anchor->GetParentDockTabStack() : nullptr;
			if (!TargetStack.IsValid())
			{
				UE_LOG(LogDockToMain, Verbose, TEXT("No main-window home for '%s'; left floating."), *Tab->GetTabLabel().ToString());
				continue;
			}

			const TSharedPtr<FTabManager> OriginManager = Tab->GetTabManagerPtr();

			// Take the tab out of the floating window without firing its "closed" callback (which would tear down its tool).
			SDockTab::FOnTabClosedCallback& ClosedCallback = DockToMain::FDockTabAccess::OnTabClosedOf(*Tab);
			const SDockTab::FOnTabClosedCallback SavedCallback = ClosedCallback;
			ClosedCallback.Unbind();
			Tab->RemoveTabFromParent();
			ClosedCallback = SavedCallback;

			TargetStack->OpenTab(Tab);
			if (OriginManager.IsValid())
			{
				DockToMain::FTabManagerAccess::NotifyRelocated(*OriginManager, Tab, RootWindow);
			}
			if (const TSharedPtr<FTabManager> TabManager = Tab->GetTabManagerPtr())
			{
				TabManager->DrawAttention(Tab);
			}
			// The engine closes the floating window itself once its last tab has left.
			UE_LOG(LogDockToMain, Log, TEXT("Docked '%s' into the main window."), *Tab->GetTabLabel().ToString());
		}
	}

	// A live tab in the main window whose stack the floating tab can join.
	static TSharedPtr<SDockTab> FindAnchor(const TSharedRef<SDockTab>& Tab, const TSharedRef<SWindow>& RootWindow)
	{
		const TSharedPtr<FTabManager> TabManager = Tab->GetTabManagerPtr();
		const TSharedRef<FGlobalTabmanager> Global = FGlobalTabmanager::Get();

		if (!TabManager.IsValid() || TabManager == StaticCastSharedRef<FTabManager>(Global))
		{
			// Nomad / global tab: sit beside the Level Editor tab.
			const TSharedPtr<SDockTab> LevelEditorTab = Global->FindExistingLiveTab(FTabId(TEXT("LevelEditor")));
			return LevelEditorTab.IsValid() && LevelEditorTab->GetParentWindow() == RootWindow ? LevelEditorTab : nullptr;
		}

		// Panel of an editor: join another of that editor's tabs that is already in the main window.
		const TSharedPtr<SDockTab> MajorTab = Global->GetMajorTabForTabManager(TabManager.ToSharedRef());
		if (!MajorTab.IsValid() || MajorTab->GetParentWindow() != RootWindow)
		{
			return nullptr;
		}
		TArray<TSharedRef<SDockTab>> Siblings;
		DockToMain::CollectTabs(MajorTab->GetContent(), Siblings);
		for (const TSharedRef<SDockTab>& Sibling : Siblings)
		{
			if (Sibling != Tab && Sibling->GetTabManagerPtr() == TabManager && Sibling->GetParentWindow() == RootWindow)
			{
				return Sibling;
			}
		}
		return nullptr;
	}

	FTSTicker::FDelegateHandle TickerHandle;
	TArray<TWeakPtr<SWindow>> SeenWindows;
	double LastDragTime = -1000.0;
};

IMPLEMENT_MODULE(FDockToMainModule, DockToMain)
