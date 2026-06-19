#pragma once

#if defined(_WIN32) && defined(FLUXORA_CORE_EXPORTS)
#define FLUXORA_CORE_API __declspec(dllexport)
#elif defined(_WIN32)
#define FLUXORA_CORE_API __declspec(dllimport)
#else
#define FLUXORA_CORE_API
#endif

#if defined(_MSC_VER)
#define FLUXORA_CORE_CALL __cdecl
#else
#define FLUXORA_CORE_CALL
#endif

extern "C"
{
    typedef void (FLUXORA_CORE_CALL *FluxoraCoreProgressCallback)(const wchar_t* progressJson, void* userData);

    enum FluxoraCoreResult
    {
        FluxoraCoreResultOk = 0,
        FluxoraCoreResultInvalidArgument = 1,
        FluxoraCoreResultBufferTooSmall = 2,
        FluxoraCoreResultCoreError = 3
    };

    FLUXORA_CORE_API int fluxora_core_is_available();

    // Sets a thread-local operation id used by native bridge/core/operation log
    // lines. Passing null or an empty string clears the current context.
    FLUXORA_CORE_API int fluxora_set_operation_context(
        const wchar_t* operationId);

    // Returns a JSON array describing the available game templates that can be
    // layered on top of the base template. Deprecated compatibility fields remain
    // in place while additive game-definition fields include uiTemplateId,
    // gameCapabilities, archiveExtensions and requiredFiles:
    //   [ { "id", "displayName", "gameName", "summary", ... }, ... ]
    FLUXORA_CORE_API int fluxora_get_game_templates(
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    // Returns a JSON object with the fully resolved (base + game) template for a
    // given template id. Deprecated compatibility fields remain in place while
    // the frontend migrates; additive game-definition fields include
    // uiTemplateId, gameCapabilities, archiveExtensions, requiredFiles,
    // contentLayoutSummary, executableDisplayMetadata and launchTrackingMetadata.
    FLUXORA_CORE_API int fluxora_resolve_template(
        const wchar_t* templateId,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_preview_project_directory(
        const wchar_t* projectName,
        const wchar_t* installRootDirectory,
        wchar_t* projectDirectoryBuffer,
        int projectDirectoryBufferLength);

    FLUXORA_CORE_API int fluxora_create_project(
        const wchar_t* projectName,
        const wchar_t* templateId,
        const wchar_t* gamePath,
        const wchar_t* installRootDirectory,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    // Returns a JSON array of lightweight build descriptors from a directory of
    // Fluxora build configs. This is intended for the UI catalog and does not
    // fully open or mutate each project instance.
    FLUXORA_CORE_API int fluxora_list_project_configs(
        const wchar_t* buildConfigsDirectory,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    // Opens an existing build from a Fluxora build config and returns a JSON
    // descriptor:
    //   { "id", "name", "gameName", "gamePath", "installRootDirectory",
    //     "projectDirectory", "configPath", "template": { ...resolved... },
    //     "gameCapabilities", "gameHealthSummary", "projectFingerprint",
    //     "contentLayoutSummary", "uiTemplateId" }
    FLUXORA_CORE_API int fluxora_open_project_config(
        const wchar_t* configPath,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_rename_project(
        const wchar_t* configPath,
        const wchar_t* newName,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_delete_project(
        const wchar_t* configPath);

    FLUXORA_CORE_API int fluxora_delete_project_with_progress(
        const wchar_t* configPath,
        FluxoraCoreProgressCallback progressCallback,
        void* progressUserData);

    FLUXORA_CORE_API int fluxora_get_build_path_settings(
        const wchar_t* configPath,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    // settingsJson is:
    //   { "gameDirectory", "modsDirectory", "profilesDirectory",
    //     "downloadsDirectory", "overwriteDirectory" }
    FLUXORA_CORE_API int fluxora_save_build_path_settings(
        const wchar_t* configPath,
        const wchar_t* settingsJson,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    // Writes a manifest-only FluxPack recipe. Source archives are referenced by
    // provider/link/hash metadata; original archives are not embedded.
    FLUXORA_CORE_API int fluxora_export_fluxpack(
        const wchar_t* configPath,
        const wchar_t* outputPath,
        int includeGeneratedAssets,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    // Reads a FluxPack recipe and returns a lightweight summary for the install
    // flow.
    FLUXORA_CORE_API int fluxora_inspect_fluxpack(
        const wchar_t* fluxPackPath,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    // Creates a Fluxora build from a FluxPack recipe, downloads installable
    // source archives, applies embedded configs/profile order and returns:
    //   { "configPath", "projectDirectory", "buildName", source counters, ... }
    FLUXORA_CORE_API int fluxora_install_fluxpack(
        const wchar_t* fluxPackPath,
        const wchar_t* installRootDirectory,
        FluxoraCoreProgressCallback progressCallback,
        void* progressUserData,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_analyze_mod_organizer_instance(
        const wchar_t* sourceDirectory,
        const wchar_t* destinationRootDirectory,
        const wchar_t* existingConfigPath,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_import_mod_organizer_instance(
        const wchar_t* sourceDirectory,
        const wchar_t* destinationRootDirectory,
        const wchar_t* existingConfigPath,
        int replaceExisting,
        FluxoraCoreProgressCallback progressCallback,
        void* progressUserData,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_get_game_executables(
        const wchar_t* configPath,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    // executablesJson is a JSON array:
    //   [ { "id", "displayName", "executablePath", "arguments",
    //       "workingDirectory" }, ... ]
    FLUXORA_CORE_API int fluxora_save_game_executables(
        const wchar_t* configPath,
        const wchar_t* executablesJson,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_launch_game_executable(
        const wchar_t* configPath,
        const wchar_t* executableId,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_get_executable_icon(
        const wchar_t* executablePath,
        wchar_t* iconPathBuffer,
        int iconPathBufferLength);

    // Returns:
    //   { "isConfigured", "isLinked", "displayName", "userId", "message",
    //     "clientId", "redirectUri" }
    FLUXORA_CORE_API int fluxora_get_nexusmods_auth_status(
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    // Starts the official Nexus Mods OAuth2 Authorization Code + PKCE flow for
    // public desktop applications. The core opens the system browser, listens
    // for the registered localhost callback, exchanges the code for tokens, and
    // stores the protected binding in app settings.
    FLUXORA_CORE_API int fluxora_connect_nexusmods(
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_disconnect_nexusmods(
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_get_app_language(
        wchar_t* languageBuffer,
        int languageBufferLength);

    FLUXORA_CORE_API int fluxora_set_app_language(
        const wchar_t* languageCode);

    // Registers Fluxora as the current-user handler for nxm:// Mod Manager
    // download links. The previous command is preserved in the user registry.
    FLUXORA_CORE_API int fluxora_register_nxm_protocol(
        const wchar_t* executablePath,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_get_installed_mods(
        const wchar_t* projectDirectory,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_get_mod_order(
        const wchar_t* projectDirectory,
        const wchar_t* profileName,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_create_mod_separator(
        const wchar_t* projectDirectory,
        const wchar_t* profileName,
        const wchar_t* title,
        int targetIndex,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_delete_mod_separator(
        const wchar_t* projectDirectory,
        const wchar_t* profileName,
        const wchar_t* separatorId,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_move_mod_order_item(
        const wchar_t* projectDirectory,
        const wchar_t* profileName,
        const wchar_t* orderItemId,
        int targetIndex,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_delete_installed_mod(
        const wchar_t* projectDirectory,
        const wchar_t* modPath);

    FLUXORA_CORE_API int fluxora_set_installed_mod_enabled(
        const wchar_t* projectDirectory,
        const wchar_t* modPath,
        int isEnabled);

    FLUXORA_CORE_API int fluxora_set_all_installed_mods_enabled(
        const wchar_t* projectDirectory,
        int isEnabled);

    FLUXORA_CORE_API int fluxora_check_mod_updates(
        const wchar_t* projectDirectory,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_get_mod_file_tree(
        const wchar_t* projectDirectory,
        const wchar_t* modPath,
        const wchar_t* relativeDirectory,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_get_plugins(
        const wchar_t* projectDirectory,
        const wchar_t* templateId,
        const wchar_t* profileName,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_move_plugin(
        const wchar_t* projectDirectory,
        const wchar_t* templateId,
        const wchar_t* profileName,
        const wchar_t* orderItemId,
        int targetIndex,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_create_plugin_separator(
        const wchar_t* projectDirectory,
        const wchar_t* templateId,
        const wchar_t* profileName,
        const wchar_t* title,
        int targetIndex,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_delete_plugin_separator(
        const wchar_t* projectDirectory,
        const wchar_t* templateId,
        const wchar_t* profileName,
        const wchar_t* separatorId,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_set_plugin_enabled(
        const wchar_t* projectDirectory,
        const wchar_t* templateId,
        const wchar_t* profileName,
        const wchar_t* pluginName,
        int isEnabled,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_get_downloads(
        const wchar_t* projectDirectory,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    // nxmLinksJson is a JSON string array: [ "nxm://...", ... ]. Passing an
    // empty project directory stores links in Fluxora's inbound download queue.
    FLUXORA_CORE_API int fluxora_capture_nxm_links(
        const wchar_t* projectDirectory,
        const wchar_t* nxmLinksJson,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_import_inbound_downloads(
        const wchar_t* projectDirectory,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_import_download_file(
        const wchar_t* projectDirectory,
        const wchar_t* sourcePath,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_delete_download(
        const wchar_t* projectDirectory,
        const wchar_t* downloadPath);

    FLUXORA_CORE_API int fluxora_cancel_download(
        const wchar_t* projectDirectory,
        const wchar_t* downloadPath);

    FLUXORA_CORE_API int fluxora_resume_download(
        const wchar_t* projectDirectory,
        const wchar_t* downloadPath,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_install_download(
        const wchar_t* projectDirectory,
        const wchar_t* downloadPath,
        const wchar_t* modName,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    // existingModMode: 0 = fail if a mod with the same folder name exists,
    // 1 = replace the existing mod folder, 2 = merge into it and overwrite
    // files with the same relative path.
    FLUXORA_CORE_API int fluxora_install_download_with_mode(
        const wchar_t* projectDirectory,
        const wchar_t* downloadPath,
        const wchar_t* modName,
        int existingModMode,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    // Returns a JSON placement plan for a regular archive using the selected
    // project's content layout rules. existingModMode has the same values as
    // fluxora_install_download_with_mode.
    FLUXORA_CORE_API int fluxora_analyze_download_content_layout(
        const wchar_t* projectDirectory,
        const wchar_t* downloadPath,
        int existingModMode,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    // Returns a JSON descriptor for an XML FOMOD installer in the archive, or
    // { "isFomod": false } when the download is a regular archive.
    FLUXORA_CORE_API int fluxora_analyze_fomod_download(
        const wchar_t* projectDirectory,
        const wchar_t* downloadPath,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    // selectedOptionIdsJson is a JSON string array of option ids returned by
    // fluxora_analyze_fomod_download.
    FLUXORA_CORE_API int fluxora_install_fomod_download_with_mode(
        const wchar_t* projectDirectory,
        const wchar_t* downloadPath,
        const wchar_t* modName,
        int existingModMode,
        const wchar_t* selectedOptionIdsJson,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    // Returns a JSON placement plan for selected FOMOD options without
    // registering or installing a mod.
    FLUXORA_CORE_API int fluxora_analyze_fomod_download_content_layout(
        const wchar_t* projectDirectory,
        const wchar_t* downloadPath,
        int existingModMode,
        const wchar_t* selectedOptionIdsJson,
        wchar_t* jsonBuffer,
        int jsonBufferLength);

    FLUXORA_CORE_API int fluxora_get_last_error(
        wchar_t* messageBuffer,
        int messageBufferLength);
}
