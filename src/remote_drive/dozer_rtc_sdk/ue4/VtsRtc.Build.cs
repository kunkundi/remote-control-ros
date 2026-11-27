using System.IO;
using UnrealBuildTool;

public class VtsRtc : ModuleRules
{
    public VtsRtc(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;
        if (Target.Platform != UnrealTargetPlatform.Win64)
            throw new BuildException("VtsRtc requires a Win64 target.");

        PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "include"));
        PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "lib", "Win64", "vtsrtc.lib"));
        PublicDelayLoadDLLs.Add("vtsrtc.dll");
        RuntimeDependencies.Add("$(ProjectDir)/Binaries/Win64/vtsrtc.dll",
            Path.Combine(ModuleDirectory, "bin", "Win64", "vtsrtc.dll"), StagedFileType.NonUFS);
    }
}
