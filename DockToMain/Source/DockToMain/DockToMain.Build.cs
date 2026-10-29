using System.IO;
using UnrealBuildTool;

public class DockToMain : ModuleRules
{
	public DockToMain(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"UnrealEd"
		});

		// UEditorStyleSettings lived in the EditorStyle module before 5.1.
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion < 1)
		{
			PrivateDependencyModuleNames.Add("EditorStyle");
		}

		// SDockingTabStack is exported, but its header lives in Slate's Private folder; it is needed to open a live tab in another stack.
		PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Source", "Runtime", "Slate", "Private"));
	}
}
