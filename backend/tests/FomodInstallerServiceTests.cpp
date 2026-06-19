#include "FluxoraCore/Services/FomodInstallerService.hpp"

#include "TestFilesystem.hpp"

#include <gtest/gtest.h>

#include <filesystem>

namespace fluxora::tests
{
    namespace
    {
        constexpr const char* moduleConfig = R"xml(<?xml version="1.0" encoding="utf-8"?>
<config>
  <moduleName>Example Mod</moduleName>
  <moduleImage path="fomod/images/module.png" />
  <requiredInstallFiles>
    <file source="common/readme.txt" />
  </requiredInstallFiles>
  <installSteps order="Explicit">
    <installStep name="Choose">
      <optionalFileGroups order="Explicit">
        <group name="Variant" type="SelectExactlyOne">
          <plugins order="Explicit">
            <plugin name="Option A">
              <description>Install A</description>
              <image path="fomod/images/option-a.png" />
              <conditionFlags>
                <flag name="variant">A</flag>
              </conditionFlags>
              <typeDescriptor>
                <type name="Recommended" />
              </typeDescriptor>
            </plugin>
            <plugin name="Option B">
              <description>Install B</description>
              <conditionFlags>
                <flag name="variant">B</flag>
              </conditionFlags>
              <typeDescriptor>
                <type name="Optional" />
              </typeDescriptor>
            </plugin>
          </plugins>
        </group>
      </optionalFileGroups>
    </installStep>
  </installSteps>
  <conditionalFileInstalls>
    <patterns>
      <pattern>
        <dependencies operator="And">
          <flagDependency flag="variant" value="A" />
        </dependencies>
        <files>
          <folder source="variant-a" />
        </files>
      </pattern>
      <pattern>
        <dependencies operator="And">
          <flagDependency flag="variant" value="B" />
        </dependencies>
        <files>
          <folder source="variant-b" />
        </files>
      </pattern>
    </patterns>
  </conditionalFileInstalls>
</config>)xml";

        void writePackage(const std::filesystem::path& package)
        {
            writeTextFile(package / "fomod" / "ModuleConfig.xml", moduleConfig);
            writeTextFile(package / "fomod" / "info.xml", R"xml(<fomod><Name>Example Mod</Name><Version MachineVersion="1.2.3">1.2.3</Version><Id>example-mod</Id></fomod>)xml");
            writeTextFile(package / "common" / "readme.txt", "common");
            writeTextFile(package / "variant-a" / "Data" / "plugin.esp", "a");
            writeTextFile(package / "variant-b" / "Data" / "plugin.esp", "b");
        }

        FomodPackageIdentity identity()
        {
            return FomodPackageIdentity{
                L"nexus",
                L"skyrimspecialedition",
                L"123",
                L"456",
                L"nxm://skyrimspecialedition/mods/123/files/456",
                L"Example Mod"
            };
        }
    }

    TEST(FomodInstallerServiceTests, AnalyzeParsesXmlDescriptorAndPreviousSelection)
    {
        TempDirectory temp;
        const std::filesystem::path project = temp.path() / "project";
        const std::filesystem::path package = temp.path() / "package";
        writePackage(package);

        FomodInstallerDescriptor descriptor = FomodInstallerService::analyze(
            project,
            temp.path() / "game",
            temp.path() / "mods",
            package,
            identity());

        ASSERT_TRUE(descriptor.isFomod);
        EXPECT_EQ(L"Example Mod", descriptor.moduleName);
        EXPECT_EQ(L"1.2.3", descriptor.moduleVersion);
        ASSERT_EQ(1u, descriptor.steps.size());
        ASSERT_EQ(1u, descriptor.steps[0].groups.size());
        ASSERT_EQ(2u, descriptor.steps[0].groups[0].options.size());
        EXPECT_EQ(L"choose-variant-option-a", descriptor.steps[0].groups[0].options[0].id);
        EXPECT_EQ(L"fomod/images/module.png", descriptor.moduleImagePath);
        EXPECT_EQ(L"fomod/images/option-a.png", descriptor.steps[0].groups[0].options[0].imagePath);
        EXPECT_FALSE(descriptor.hasPreviousSelection);

        FomodInstallerService::rememberSelection(
            project,
            descriptor,
            {L"choose-variant-option-b"});

        FomodInstallerDescriptor nextDescriptor = FomodInstallerService::analyze(
            project,
            temp.path() / "game",
            temp.path() / "mods",
            package,
            identity());

        ASSERT_TRUE(nextDescriptor.hasPreviousSelection);
        ASSERT_EQ(1u, nextDescriptor.previousSelectedOptionIds.size());
        EXPECT_EQ(L"choose-variant-option-b", nextDescriptor.previousSelectedOptionIds[0]);
    }

    TEST(FomodInstallerServiceTests, InstallCopiesOnlySelectedConditionalFiles)
    {
        TempDirectory temp;
        const std::filesystem::path project = temp.path() / "project";
        const std::filesystem::path package = temp.path() / "package";
        const std::filesystem::path destination = temp.path() / "mods" / "Example Mod";
        writePackage(package);

        std::vector<std::wstring> applied = FomodInstallerService::install(FomodInstallContext{
            project,
            temp.path() / "game",
            temp.path() / "mods",
            package,
            destination,
            identity(),
            {L"choose-variant-option-a"}
        });

        ASSERT_EQ(1u, applied.size());
        EXPECT_EQ(L"choose-variant-option-a", applied[0]);
        EXPECT_TRUE(std::filesystem::exists(destination / "common" / "readme.txt"));
        EXPECT_TRUE(std::filesystem::exists(destination / "Data" / "plugin.esp"));
        EXPECT_EQ("a", readTextFile(destination / "Data" / "plugin.esp"));
    }

    TEST(FomodInstallerServiceTests, AnalyzeReportsFileDependencyState)
    {
        TempDirectory temp;
        const std::filesystem::path project = temp.path() / "project";
        const std::filesystem::path package = temp.path() / "package";
        const std::filesystem::path mods = temp.path() / "mods";
        writeTextFile(package / "fomod" / "ModuleConfig.xml", R"xml(
<config>
  <moduleName>Detected Patch</moduleName>
  <installSteps order="Explicit">
    <installStep name="Patches">
      <optionalFileGroups order="Explicit">
        <group name="Lanterns" type="SelectExactlyOne">
          <plugins order="Explicit">
            <plugin name="Lanterns patch">
              <typeDescriptor>
                <dependencyType>
                  <defaultType name="Optional" />
                  <patterns>
                    <pattern>
                      <dependencies operator="And">
                        <fileDependency file="Data/Lanterns Of Skyrim II.esp" state="Active" />
                      </dependencies>
                      <type name="Recommended" />
                    </pattern>
                  </patterns>
                </dependencyType>
              </typeDescriptor>
            </plugin>
          </plugins>
        </group>
      </optionalFileGroups>
    </installStep>
  </installSteps>
</config>)xml");
        writeTextFile(mods / "Lanterns" / "Data" / "Lanterns Of Skyrim II.esp", "plugin");

        FomodInstallerDescriptor descriptor = FomodInstallerService::analyze(
            project,
            temp.path() / "game",
            mods,
            package,
            identity());

        ASSERT_EQ(1u, descriptor.fileDependencyStates.size());
        EXPECT_EQ(L"Data\\Lanterns Of Skyrim II.esp", descriptor.fileDependencyStates[0].file);
        EXPECT_TRUE(descriptor.fileDependencyStates[0].exists);
    }

    TEST(FomodInstallerServiceTests, FileDependencyUsesSelectedGameDataFolderForNormalizedMods)
    {
        TempDirectory temp;
        const std::filesystem::path project = temp.path() / "project";
        const std::filesystem::path package = temp.path() / "package";
        const std::filesystem::path mods = temp.path() / "mods";
        writeTextFile(package / "fomod" / "ModuleConfig.xml", R"xml(
<config>
  <moduleName>Fictional Patch</moduleName>
  <installSteps order="Explicit">
    <installStep name="Patches">
      <optionalFileGroups order="Explicit">
        <group name="Compatibility" type="SelectAny">
          <plugins order="Explicit">
            <plugin name="Installed plugin patch">
              <typeDescriptor>
                <dependencyType>
                  <defaultType name="Optional" />
                  <patterns>
                    <pattern>
                      <dependencies operator="And">
                        <fileDependency file="Content/Fictional.plugin" state="Active" />
                      </dependencies>
                      <type name="Recommended" />
                    </pattern>
                  </patterns>
                </dependencyType>
              </typeDescriptor>
            </plugin>
          </plugins>
        </group>
      </optionalFileGroups>
    </installStep>
  </installSteps>
</config>)xml");
        writeTextFile(mods / "Installed Fictional Mod" / "Fictional.plugin", "plugin");

        const FomodPackageIdentity fictionalIdentity{
            L"manual",
            L"fictionalgame",
            L"123",
            L"456",
            L"fictional://mods/123/files/456",
            L"Fictional Patch"
        };

        FomodInstallerDescriptor descriptor = FomodInstallerService::analyze(
            project,
            temp.path() / "game",
            mods,
            package,
            fictionalIdentity,
            {L"Content"});

        ASSERT_EQ(1u, descriptor.fileDependencyStates.size());
        EXPECT_EQ(L"Content\\Fictional.plugin", descriptor.fileDependencyStates[0].file);
        EXPECT_TRUE(descriptor.fileDependencyStates[0].exists);
    }

    TEST(FomodInstallerServiceTests, InstallRejectsPathTraversalSources)
    {
        TempDirectory temp;
        const std::filesystem::path project = temp.path() / "project";
        const std::filesystem::path package = temp.path() / "package";
        writeTextFile(package / "fomod" / "ModuleConfig.xml", R"xml(
<config>
  <moduleName>Unsafe</moduleName>
  <requiredInstallFiles>
    <file source="../outside.txt" />
  </requiredInstallFiles>
</config>)xml");

        EXPECT_THROW(
            {
                std::vector<std::wstring> ignored = FomodInstallerService::install(FomodInstallContext{
                project,
                temp.path() / "game",
                temp.path() / "mods",
                package,
                temp.path() / "mods" / "Unsafe",
                identity(),
                {}
                });
                (void)ignored;
            },
            std::invalid_argument);
    }

    TEST(FomodInstallerServiceTests, InstallRejectsPathTraversalDestinations)
    {
        TempDirectory temp;
        const std::filesystem::path project = temp.path() / "project";
        const std::filesystem::path package = temp.path() / "package";
        writeTextFile(package / "fomod" / "ModuleConfig.xml", R"xml(
<config>
  <moduleName>Unsafe Destination</moduleName>
  <requiredInstallFiles>
    <file source="safe.txt" destination="../escaped.txt" />
  </requiredInstallFiles>
</config>)xml");
        writeTextFile(package / "safe.txt", "safe");

        EXPECT_THROW(
            {
                std::vector<std::wstring> ignored = FomodInstallerService::install(FomodInstallContext{
                project,
                temp.path() / "game",
                temp.path() / "mods",
                package,
                temp.path() / "mods" / "Unsafe Destination",
                identity(),
                {}
                });
                (void)ignored;
            },
            std::invalid_argument);
    }
}
