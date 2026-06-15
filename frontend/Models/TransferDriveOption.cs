using System.IO;
using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace Fluxora.App.Models;

public sealed class TransferDriveOption : INotifyPropertyChanged
{
    private ulong requiredBytes;

    public event PropertyChangedEventHandler? PropertyChanged;

    public required string RootDirectory { get; init; }
    public required string DisplayName { get; init; }
    public ulong AvailableBytes { get; init; }
    public ulong TotalBytes { get; init; }

    public string SpaceText => $"{FormatBytes(AvailableBytes)} свободно из {FormatBytes(TotalBytes)}";

    public ulong RequiredBytes
    {
        get => requiredBytes;
        set
        {
            if (requiredBytes == value)
            {
                return;
            }

            requiredBytes = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(IsSelectable));
        }
    }

    public bool IsSelectable => RequiredBytes == 0 || AvailableBytes >= RequiredBytes;

    public static TransferDriveOption FromDrive(DriveInfo drive)
    {
        string label = string.IsNullOrWhiteSpace(drive.VolumeLabel)
            ? "Локальный диск"
            : drive.VolumeLabel;

        return new TransferDriveOption
        {
            RootDirectory = drive.RootDirectory.FullName,
            DisplayName = $"{label} ({drive.Name.TrimEnd('\\')})",
            AvailableBytes = (ulong)Math.Max(0, drive.AvailableFreeSpace),
            TotalBytes = (ulong)Math.Max(0, drive.TotalSize)
        };
    }

    public static string FormatBytes(ulong bytes)
    {
        string[] units = ["Б", "КБ", "МБ", "ГБ", "ТБ"];
        double value = bytes;
        int unitIndex = 0;
        while (value >= 1024 && unitIndex < units.Length - 1)
        {
            value /= 1024;
            unitIndex++;
        }

        return unitIndex == 0
            ? $"{value:0} {units[unitIndex]}"
            : $"{value:0.##} {units[unitIndex]}";
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}
