using UnrealBuildTool;
using System.Collections.Generic;

public class KubostrelEditorTarget : TargetRules
{
	public KubostrelEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Kubostrel");
	}
}
