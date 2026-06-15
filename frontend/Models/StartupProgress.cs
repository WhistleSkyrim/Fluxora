namespace Fluxora.App.Models;

public sealed record StartupProgress(
    string Title,
    string Detail,
    double Percent);
