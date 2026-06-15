using Fluxora.App.Models;

namespace Fluxora.App.ViewModels;

public sealed class ExecutableMenuItem
{
    private const string MoreId = "__more__";
    private const string PlaceholderId = "__placeholder__";
    private readonly string placeholderText;

    private ExecutableMenuItem(GameExecutableEntry? executable, bool opensManager, string placeholderText = "")
    {
        Executable = executable;
        OpensManager = opensManager;
        this.placeholderText = placeholderText;
    }

    public GameExecutableEntry? Executable { get; }
    public bool OpensManager { get; }

    public bool IsPlaceholder => !OpensManager && Executable is null;

    public string IconPath => Executable?.IconPath ?? string.Empty;

    public string Id => OpensManager
        ? MoreId
        : IsPlaceholder
            ? PlaceholderId
            : Executable?.Id ?? string.Empty;

    public string DisplayName => OpensManager
        ? "Ещё..."
        : IsPlaceholder
            ? placeholderText
        : Executable?.DisplayTitle ?? "Запуск не выбран";

    public string ToolTip => OpensManager
        ? "Открыть управление исполняемыми файлами"
        : IsPlaceholder
            ? "Добавьте исполняемый файл через пункт Ещё..."
        : Executable?.SummaryText ?? string.Empty;

    public static ExecutableMenuItem FromExecutable(GameExecutableEntry executable)
    {
        return new ExecutableMenuItem(executable, false);
    }

    public static ExecutableMenuItem More()
    {
        return new ExecutableMenuItem(null, true);
    }

    public static ExecutableMenuItem Placeholder()
    {
        return new ExecutableMenuItem(null, false, "Нет запусков");
    }
}
