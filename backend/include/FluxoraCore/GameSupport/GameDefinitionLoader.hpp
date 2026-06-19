#pragma once

#include "FluxoraCore/GameSupport/GameDefinition.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace fluxora
{
    struct GameDefinitionOverrideLoadResult
    {
        std::vector<GameDefinition> definitions;
        std::vector<std::string> diagnostics;
        bool overrideApplied{false};
    };

    class GameDefinitionLoader final
    {
    public:
        [[nodiscard]] static std::vector<GameDefinition> loadEmbeddedDefinitions();
        [[nodiscard]] static GameDefinitionOverrideLoadResult loadEmbeddedDefinitionsWithDevOverrides(
            const std::filesystem::path& overrideDirectory);
        [[nodiscard]] static std::vector<GameDefinition> loadDefinitionsFromJsonStrings(
            const std::vector<std::wstring>& jsonDocuments);
        [[nodiscard]] static GameDefinition loadDefinition(std::wstring_view jsonText);
    };
}
