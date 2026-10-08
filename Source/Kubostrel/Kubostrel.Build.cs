using UnrealBuildTool;

public class Kubostrel : ModuleRules
{
	public Kubostrel(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Each file includes what it uses, so the module also builds without unity files.
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"AIModule",
			"NetCore",
			"Sockets",
			"Networking",
			"AudioMixer",
			"ApplicationCore",
			"Slate",
			"SlateCore"
		});
	}
}
