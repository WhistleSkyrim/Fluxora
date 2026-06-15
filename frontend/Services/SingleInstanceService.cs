using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace Fluxora.App.Services;

public sealed class SingleInstanceService : IDisposable
{
    private const string MutexName = @"Local\Fluxora.App.SingleInstance";
    private static readonly string PipeName = BuildPipeName();

    private readonly Mutex mutex = new(false, MutexName);
    private readonly CancellationTokenSource cancellationTokenSource = new();
    private bool hasPrimaryInstanceLock;
    private Task? listenTask;

    public bool TryAcquirePrimaryInstance()
    {
        try
        {
            hasPrimaryInstanceLock = mutex.WaitOne(0);
            return hasPrimaryInstanceLock;
        }
        catch (AbandonedMutexException)
        {
            hasPrimaryInstanceLock = true;
            return true;
        }
    }

    public void StartListening(Action<IReadOnlyList<string>> onActivationReceived)
    {
        if (!hasPrimaryInstanceLock)
        {
            throw new InvalidOperationException("Only the primary instance can listen for activations.");
        }

        listenTask = Task.Run(() => ListenAsync(onActivationReceived, cancellationTokenSource.Token));
    }

    public bool SendActivationToPrimaryInstance(IEnumerable<string> arguments)
    {
        try
        {
            InstanceActivation activation = new()
            {
                Arguments = arguments.Where(argument => !string.IsNullOrWhiteSpace(argument)).ToList()
            };

            string json = JsonSerializer.Serialize(activation);
            using NamedPipeClientStream client = new(
                ".",
                PipeName,
                PipeDirection.Out,
                PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly);
            client.Connect(2000);

            using StreamWriter writer = new(client, new UTF8Encoding(false), leaveOpen: false);
            writer.Write(json);
            writer.Flush();
            return true;
        }
        catch (IOException)
        {
            return false;
        }
        catch (TimeoutException)
        {
            return false;
        }
        catch (UnauthorizedAccessException)
        {
            return false;
        }
    }

    public void Dispose()
    {
        cancellationTokenSource.Cancel();

        try
        {
            listenTask?.Wait(TimeSpan.FromMilliseconds(500));
        }
        catch (AggregateException)
        {
        }

        if (hasPrimaryInstanceLock)
        {
            mutex.ReleaseMutex();
            hasPrimaryInstanceLock = false;
        }

        mutex.Dispose();
        cancellationTokenSource.Dispose();
    }

    private static async Task ListenAsync(
        Action<IReadOnlyList<string>> onActivationReceived,
        CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                using NamedPipeServerStream server = new(
                    PipeName,
                    PipeDirection.In,
                    NamedPipeServerStream.MaxAllowedServerInstances,
                    PipeTransmissionMode.Byte,
                    PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly);

                await server.WaitForConnectionAsync(cancellationToken).ConfigureAwait(false);
                using StreamReader reader = new(server, Encoding.UTF8);
                string json = await reader.ReadToEndAsync(cancellationToken).ConfigureAwait(false);
                IReadOnlyList<string> arguments = DeserializeArguments(json);
                onActivationReceived(NxmProtocolService.ExtractNxmLinks(arguments));
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                return;
            }
            catch (IOException)
            {
            }
            catch (UnauthorizedAccessException)
            {
            }
        }
    }

    private static IReadOnlyList<string> DeserializeArguments(string json)
    {
        if (string.IsNullOrWhiteSpace(json))
        {
            return Array.Empty<string>();
        }

        try
        {
            return JsonSerializer.Deserialize<InstanceActivation>(json)?.Arguments
                ?? new List<string>();
        }
        catch (JsonException)
        {
            return Array.Empty<string>();
        }
    }

    private static string BuildPipeName()
    {
        string userName = $@"{Environment.UserDomainName}\{Environment.UserName}";
        string pipeScope = $"{userName}:{Process.GetCurrentProcess().SessionId}";
        byte[] hash = SHA256.HashData(Encoding.UTF8.GetBytes(pipeScope));
        string suffix = Convert.ToHexString(hash, 0, 8);
        return $"Fluxora.App.SingleInstance.{suffix}";
    }

    private sealed class InstanceActivation
    {
        public List<string> Arguments { get; set; } = new();
    }
}
