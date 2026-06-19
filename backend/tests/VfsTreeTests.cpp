#include "FluxoraVfs/VfsTree.hpp"

#include "TestFilesystem.hpp"

#include <gtest/gtest.h>

#include <algorithm>

namespace fluxora::tests
{
    namespace
    {
        bool containsChild(
            const std::vector<vfs::DirChild>& children,
            const std::wstring& name)
        {
            return std::any_of(
                children.begin(),
                children.end(),
                [&name](const vfs::DirChild& child)
                {
                    return vfs::VfsTree::equalsIgnoreCase(child.name, name);
                });
        }

        void expectSamePath(
            const std::wstring& actual,
            const std::filesystem::path& expected)
        {
            EXPECT_EQ(
                normalized(std::filesystem::path(actual)),
                normalized(expected));
        }
    }

    TEST(VfsTreeTests, BuildMergesRealModsOverwriteByPriorityAndKeepsBaseSiblings)
    {
        TempDirectory temp;

        const std::filesystem::path data = temp.path() / L"Game" / L"Data";
        const std::filesystem::path modLow = temp.path() / L"mods" / L"Low Priority";
        const std::filesystem::path modHigh = temp.path() / L"mods" / L"High Priority";
        const std::filesystem::path overwrite = temp.path() / L"overwrite";

        writeTextFile(data / L"textures" / L"base.dds", "base");
        writeTextFile(data / L"textures" / L"shared.dds", "real");
        writeTextFile(modLow / L"textures" / L"shared.dds", "low");
        writeTextFile(modLow / L"textures" / L"low-only.dds", "low-only");
        writeTextFile(modHigh / L"textures" / L"shared.dds", "high");
        writeTextFile(modHigh / L"meshes" / L"high-only.nif", "high-only");
        writeTextFile(overwrite / L"textures" / L"shared.dds", "overwrite");

        vfs::VfsTree tree;
        tree.build(vfs::VfsMountConfig{
            data.wstring(),
            overwrite.wstring(),
            {modLow.wstring(), modHigh.wstring()},
            {}
        });

        const vfs::VfsTree::PathInfo shared = tree.classify(L"textures\\shared.dds");
        ASSERT_EQ(shared.kind, vfs::VfsTree::PathInfo::Kind::File);
        expectSamePath(shared.winner, overwrite / L"textures" / L"shared.dds");

        const vfs::VfsTree::PathInfo highOnly = tree.classify(L"meshes\\high-only.nif");
        ASSERT_EQ(highOnly.kind, vfs::VfsTree::PathInfo::Kind::File);
        expectSamePath(highOnly.winner, modHigh / L"meshes" / L"high-only.nif");

        const std::vector<vfs::DirChild>* textures =
            tree.listing(vfs::VfsTree::toLower(L"textures"));
        ASSERT_NE(textures, nullptr);
        EXPECT_TRUE(containsChild(*textures, L"base.dds"));
        EXPECT_TRUE(containsChild(*textures, L"low-only.dds"));
        EXPECT_TRUE(containsChild(*textures, L"shared.dds"));
    }

    TEST(VfsTreeTests, BuildExcludesRootBuilderTopLevelFolderFromDataMount)
    {
        TempDirectory temp;

        const std::filesystem::path data = temp.path() / L"Game" / L"Data";
        const std::filesystem::path mod = temp.path() / L"mods" / L"Root Builder Mod";
        const std::filesystem::path overwrite = temp.path() / L"overwrite";

        writeTextFile(data / L"Skyrim.esm", "base");
        writeTextFile(mod / L"root" / L"SkyrimSE.exe", "root executable");
        writeTextFile(mod / L"Scripts" / L"mod.pex", "script");

        vfs::VfsTree tree;
        tree.build(vfs::VfsMountConfig{
            data.wstring(),
            overwrite.wstring(),
            {mod.wstring()},
            {L"root"}
        });

        const vfs::VfsTree::PathInfo rootFile = tree.classify(L"root\\SkyrimSE.exe");
        EXPECT_EQ(rootFile.kind, vfs::VfsTree::PathInfo::Kind::Unknown);

        const vfs::VfsTree::PathInfo script = tree.classify(L"Scripts\\mod.pex");
        ASSERT_EQ(script.kind, vfs::VfsTree::PathInfo::Kind::File);
        expectSamePath(script.winner, mod / L"Scripts" / L"mod.pex");

        const std::vector<vfs::DirChild>* root = tree.listing(L"");
        ASSERT_NE(root, nullptr);
        EXPECT_FALSE(containsChild(*root, L"root"));
        EXPECT_TRUE(containsChild(*root, L"Scripts"));
        EXPECT_TRUE(containsChild(*root, L"Skyrim.esm"));
    }

    TEST(VfsTreeTests, BuildSupportsModsWithNestedDataWrapper)
    {
        TempDirectory temp;

        const std::filesystem::path data = temp.path() / L"Game" / L"Data";
        const std::filesystem::path mod = temp.path() / L"mods" / L"Wrapped Mod";
        const std::filesystem::path wrappedData = mod / L"Data";
        const std::filesystem::path overwrite = temp.path() / L"overwrite";

        writeTextFile(data / L"Skyrim.esm", "base");
        writeTextFile(wrappedData / L"Wrapped.esp", "plugin");
        writeTextFile(mod / L"fomod" / L"ModuleConfig.xml", "<config />");

        vfs::VfsTree tree;
        tree.build(vfs::VfsMountConfig{
            data.wstring(),
            overwrite.wstring(),
            {mod.wstring(), wrappedData.wstring()},
            {L"Data"}
        });

        const vfs::VfsTree::PathInfo wrappedPlugin = tree.classify(L"Wrapped.esp");
        ASSERT_EQ(wrappedPlugin.kind, vfs::VfsTree::PathInfo::Kind::File);
        expectSamePath(wrappedPlugin.winner, wrappedData / L"Wrapped.esp");

        const vfs::VfsTree::PathInfo leakedPlugin = tree.classify(L"Data\\Wrapped.esp");
        EXPECT_EQ(leakedPlugin.kind, vfs::VfsTree::PathInfo::Kind::Unknown);

        const std::vector<vfs::DirChild>* root = tree.listing(L"");
        ASSERT_NE(root, nullptr);
        EXPECT_FALSE(containsChild(*root, L"Data"));
        EXPECT_TRUE(containsChild(*root, L"Wrapped.esp"));
    }

    TEST(VfsTreeTests, WildcardMatchSupportsNativeDosStarMasks)
    {
        EXPECT_TRUE(vfs::VfsTree::wildcardMatch(L"enginefixes.dll", L"<.dll"));
        EXPECT_TRUE(vfs::VfsTree::wildcardMatch(L"save1_character_tamriel.ess", L"<.ess"));
        EXPECT_TRUE(vfs::VfsTree::wildcardMatch(L"skse64.log", L"*.log"));
        EXPECT_FALSE(vfs::VfsTree::wildcardMatch(L"save1_character_tamriel.skse", L"<.ess"));
        EXPECT_FALSE(vfs::VfsTree::wildcardMatch(L"enginefixes.toml", L"<.dll"));
    }
}
