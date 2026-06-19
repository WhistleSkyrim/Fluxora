#pragma once

#include "FluxoraCore/GameSupport/GameSupportRegistry.hpp"
#include "FluxoraCore/Services/IService.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fluxora
{
    class Logger;

    // A single functional module a build can expose. Capabilities describe both
    // interface (which panels the UI should render) and functionality (which
    // features the build supports). The base template contributes the universal
    // modules; a game template adds its own and may refine the base ones.
    struct TemplateCapability
    {
        std::wstring id;
        std::wstring displayName;
        std::wstring description;
    };

    // Optional script-extender metadata supplied by the selected game definition.
    struct ScriptExtender
    {
        std::wstring name;
        std::wstring loaderExecutable;
        std::wstring website;
    };

    // Temporary compatibility projection serialized by the bridge for the
    // current frontend. New core code should prefer typed GameDefinition and
    // GameSupport rules; these fields stay until the C# model migrates.
    struct BuildTemplate
    {
        std::wstring id;
        std::wstring displayName;

        // Deprecated bridge compatibility fields kept for the existing UI
        // template shape.
        std::wstring gameName;
        std::wstring summary;
        std::wstring baseTemplateId;
        std::wstring defaultProfileName;
        std::wstring dataDirectory;
        std::wstring nexusDomain;
        std::vector<std::wstring> folders;
        std::vector<std::wstring> profileFiles;
        std::vector<std::wstring> basePlugins;
        std::vector<std::wstring> pluginExtensions;
        std::vector<std::wstring> executables;
        std::vector<TemplateCapability> capabilities;
        std::optional<ScriptExtender> scriptExtender;
        bool isBase{false};
    };

    // Owns the base template and the registry of game templates, and produces the
    // resolved template by overlaying a game template on top of the base.
    class TemplateService final : public IService
    {
    public:
        explicit TemplateService(Logger& logger) noexcept;

        void initialize() override;
        void shutdown() override;

        [[nodiscard]] const BuildTemplate& baseTemplate() const noexcept;
        [[nodiscard]] const std::vector<BuildTemplate>& gameTemplates() const noexcept;
        [[nodiscard]] const BuildTemplate* findGameTemplate(std::wstring_view id) const noexcept;

        // Overlay the requested game template on top of the base template.
        // Throws std::invalid_argument when the id is unknown.
        [[nodiscard]] BuildTemplate resolve(std::wstring_view gameId) const;

        [[nodiscard]] bool isInitialized() const noexcept;

    private:
        void registerBuiltInTemplates();

        Logger& logger_;
        GameSupportRegistry gameSupport_;
        BuildTemplate base_;
        std::vector<BuildTemplate> games_;
        bool initialized_{false};
    };
}
