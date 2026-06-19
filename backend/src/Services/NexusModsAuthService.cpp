#include "FluxoraCore/Services/NexusModsAuthService.hpp"

#include "FluxoraCore/Services/AppSettingsService.hpp"
#include "FluxoraCore/Services/Logger.hpp"
#include "FluxoraCore/Support/JsonReader.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <wincrypt.h>
#include <winhttp.h>
#endif

namespace fluxora
{
    namespace
    {
        constexpr std::wstring_view defaultClientId = L"";
        constexpr std::wstring_view defaultRedirectUri = L"http://127.0.0.1:PORT";
        constexpr std::wstring_view authorizeEndpoint = L"https://users.nexusmods.com/oauth/authorize";
        constexpr std::wstring_view tokenHost = L"users.nexusmods.com";
        constexpr std::wstring_view tokenPath = L"/oauth/token";
        constexpr int callbackTimeoutSeconds = 120;
        constexpr int callbackClientReadTimeoutMilliseconds = 3000;

        struct OAuthConfig
        {
            std::wstring clientId;
            std::wstring redirectUri;
        };

        struct RedirectUriParts
        {
            std::wstring host;
            unsigned short port{0};
            std::wstring path;
        };

        struct CallbackResult
        {
            std::string code;
            std::string state;
            std::string error;
            std::string errorDescription;
        };

        std::wstring callbackPath(const RedirectUriParts& redirect)
        {
            return redirect.path.empty() ? L"/" : redirect.path;
        }

        std::wstring buildRedirectUri(const RedirectUriParts& redirect, unsigned short port)
        {
            std::wstring uri = L"http://" + redirect.host + L":" + std::to_wstring(port);
            uri += redirect.path;
            return uri;
        }

        std::string normalizeRequestTarget(std::string target)
        {
            const bool absoluteHttpUrl = target.starts_with("http://") || target.starts_with("https://");
            if (!absoluteHttpUrl)
            {
                return target;
            }

            const std::size_t schemeEnd = target.find("://");
            const std::size_t pathStart = target.find('/', schemeEnd == std::string::npos ? 0 : schemeEnd + 3);
            return pathStart == std::string::npos ? std::string("/") : target.substr(pathStart);
        }

        struct TokenResponse
        {
            std::wstring accessToken;
            std::wstring refreshToken;
            std::wstring tokenType;
            long long expiresInSeconds{0};
        };

        struct JwtUser
        {
            std::wstring username;
            std::wstring userId;
        };

        std::wstring readEnvironment(std::wstring_view name)
        {
#ifdef _WIN32
            const DWORD requiredLength = GetEnvironmentVariableW(std::wstring(name).c_str(), nullptr, 0);
            if (requiredLength == 0)
            {
                return {};
            }

            std::wstring value(requiredLength, L'\0');
            const DWORD written = GetEnvironmentVariableW(
                std::wstring(name).c_str(),
                value.data(),
                requiredLength);
            value.resize(written);
            return value;
#else
            return {};
#endif
        }

        OAuthConfig loadConfig()
        {
            OAuthConfig config;
            config.clientId = readEnvironment(L"FLUXORA_NEXUS_CLIENT_ID");
            if (config.clientId.empty())
            {
                config.clientId = std::wstring(defaultClientId);
            }

            config.redirectUri = readEnvironment(L"FLUXORA_NEXUS_REDIRECT_URI");
            if (config.redirectUri.empty())
            {
                config.redirectUri = std::wstring(defaultRedirectUri);
            }

            return config;
        }

        std::string toUtf8(const std::wstring& value)
        {
#ifdef _WIN32
            if (value.empty())
            {
                return {};
            }

            const int size = WideCharToMultiByte(
                CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            std::string out(static_cast<std::size_t>(size), '\0');
            WideCharToMultiByte(
                CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
            return out;
#else
            return std::string(value.begin(), value.end());
#endif
        }

        std::wstring fromUtf8(const std::string& value)
        {
#ifdef _WIN32
            if (value.empty())
            {
                return {};
            }

            const int size = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0);
            if (size <= 0)
            {
                throw std::runtime_error("UTF-8 conversion failed.");
            }

            std::wstring out(static_cast<std::size_t>(size), L'\0');
            MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                out.data(),
                size);
            return out;
#else
            return std::wstring(value.begin(), value.end());
#endif
        }

        std::wstring toLower(std::wstring value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
                return static_cast<wchar_t>(::towlower(character));
            });
            return value;
        }

        bool isUnreserved(unsigned char character)
        {
            return (character >= 'A' && character <= 'Z') ||
                (character >= 'a' && character <= 'z') ||
                (character >= '0' && character <= '9') ||
                character == '-' ||
                character == '.' ||
                character == '_' ||
                character == '~';
        }

        std::wstring urlEncode(const std::wstring& value)
        {
            static constexpr wchar_t digits[] = L"0123456789ABCDEF";
            const std::string utf8 = toUtf8(value);
            std::wstring encoded;
            encoded.reserve(utf8.size());

            for (unsigned char character : utf8)
            {
                if (isUnreserved(character))
                {
                    encoded.push_back(static_cast<wchar_t>(character));
                    continue;
                }

                encoded.push_back(L'%');
                encoded.push_back(digits[(character >> 4) & 0xF]);
                encoded.push_back(digits[character & 0xF]);
            }

            return encoded;
        }

        std::string urlDecodeToUtf8(std::string_view value)
        {
            std::string decoded;
            decoded.reserve(value.size());

            for (std::size_t index = 0; index < value.size(); ++index)
            {
                const char character = value[index];
                if (character == '+' )
                {
                    decoded.push_back(' ');
                    continue;
                }

                if (character == '%' && index + 2 < value.size())
                {
                    const auto hex = value.substr(index + 1, 2);
                    const int high = std::isdigit(static_cast<unsigned char>(hex[0]))
                        ? hex[0] - '0'
                        : std::tolower(static_cast<unsigned char>(hex[0])) - 'a' + 10;
                    const int low = std::isdigit(static_cast<unsigned char>(hex[1]))
                        ? hex[1] - '0'
                        : std::tolower(static_cast<unsigned char>(hex[1])) - 'a' + 10;
                    if (high >= 0 && high <= 15 && low >= 0 && low <= 15)
                    {
                        decoded.push_back(static_cast<char>((high << 4) | low));
                        index += 2;
                        continue;
                    }
                }

                decoded.push_back(character);
            }

            return decoded;
        }

        std::map<std::string, std::string> parseQuery(std::string_view query)
        {
            std::map<std::string, std::string> values;
            std::size_t start = 0;
            while (start <= query.size())
            {
                const std::size_t end = query.find('&', start);
                const std::string_view pair = query.substr(
                    start,
                    end == std::string_view::npos ? std::string_view::npos : end - start);
                const std::size_t separator = pair.find('=');
                if (separator != std::string_view::npos)
                {
                    values[urlDecodeToUtf8(pair.substr(0, separator))] =
                        urlDecodeToUtf8(pair.substr(separator + 1));
                }

                if (end == std::string_view::npos)
                {
                    break;
                }

                start = end + 1;
            }

            return values;
        }

        RedirectUriParts parseRedirectUri(const std::wstring& redirectUri)
        {
            constexpr std::wstring_view scheme = L"http://";
            if (!redirectUri.starts_with(scheme))
            {
                throw std::invalid_argument("NexusMods redirect URI must use http://127.0.0.1 for desktop OAuth.");
            }

            const std::wstring_view rest(redirectUri.data() + scheme.size(), redirectUri.size() - scheme.size());
            const std::size_t pathStart = rest.find(L'/');
            const std::wstring hostPort(pathStart == std::wstring_view::npos ? rest : rest.substr(0, pathStart));
            const std::wstring path(pathStart == std::wstring_view::npos ? L"" : std::wstring(rest.substr(pathStart)));

            const std::size_t colon = hostPort.find(L':');
            const std::wstring host = colon == std::wstring::npos ? hostPort : hostPort.substr(0, colon);
            unsigned short port = 80;
            if (colon != std::wstring::npos)
            {
                const std::wstring portText = hostPort.substr(colon + 1);
                if (portText == L"PORT")
                {
                    port = 0;
                }
                else
                {
                    const int parsedPort = std::stoi(portText);
                    if (parsedPort < 0 || parsedPort > 65535)
                    {
                        throw std::invalid_argument("NexusMods redirect URI port is out of range.");
                    }

                    port = static_cast<unsigned short>(parsedPort);
                }
            }

            const std::wstring normalizedHost = toLower(host);
            if (normalizedHost != L"127.0.0.1" && normalizedHost != L"localhost")
            {
                throw std::invalid_argument("NexusMods redirect URI must point to localhost.");
            }

            return RedirectUriParts{
                normalizedHost,
                port,
                path
            };
        }

        std::wstring base64UrlEncode(const std::vector<unsigned char>& bytes)
        {
            static constexpr wchar_t table[] =
                L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

            std::wstring encoded;
            int value = 0;
            int bits = -6;
            for (unsigned char byte : bytes)
            {
                value = (value << 8) + byte;
                bits += 8;
                while (bits >= 0)
                {
                    encoded.push_back(table[(value >> bits) & 0x3F]);
                    bits -= 6;
                }
            }

            if (bits > -6)
            {
                encoded.push_back(table[((value << 8) >> (bits + 8)) & 0x3F]);
            }

            return encoded;
        }

        std::vector<unsigned char> base64UrlDecode(std::string_view value)
        {
            std::vector<int> table(256, -1);
            const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
            for (int index = 0; index < static_cast<int>(alphabet.size()); ++index)
            {
                table[static_cast<unsigned char>(alphabet[static_cast<std::size_t>(index)])] = index;
            }

            std::vector<unsigned char> decoded;
            int accumulator = 0;
            int bits = -8;
            for (unsigned char character : value)
            {
                if (character == '=')
                {
                    break;
                }

                const int digit = table[character];
                if (digit < 0)
                {
                    break;
                }

                accumulator = (accumulator << 6) + digit;
                bits += 6;
                if (bits >= 0)
                {
                    decoded.push_back(static_cast<unsigned char>((accumulator >> bits) & 0xFF));
                    bits -= 8;
                }
            }

            return decoded;
        }

        std::wstring bytesToHex(const unsigned char* bytes, std::size_t count)
        {
            static constexpr wchar_t digits[] = L"0123456789abcdef";
            std::wstring hex;
            hex.reserve(count * 2);
            for (std::size_t index = 0; index < count; ++index)
            {
                const unsigned char byte = bytes[index];
                hex.push_back(digits[(byte >> 4) & 0xF]);
                hex.push_back(digits[byte & 0xF]);
            }

            return hex;
        }

        std::vector<unsigned char> generateRandomBytes(std::size_t count)
        {
#ifdef _WIN32
            std::vector<unsigned char> bytes(count);
            if (BCryptGenRandom(
                    nullptr,
                    bytes.data(),
                    static_cast<ULONG>(bytes.size()),
                    BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
            {
                throw std::runtime_error("Failed to generate secure random bytes.");
            }

            return bytes;
#else
            throw std::runtime_error("NexusMods OAuth is currently implemented for Windows builds.");
#endif
        }

        std::wstring generateHexRandom(std::size_t byteCount)
        {
            const std::vector<unsigned char> bytes = generateRandomBytes(byteCount);
            return bytesToHex(bytes.data(), bytes.size());
        }

        std::vector<unsigned char> sha256(std::string_view value)
        {
#ifdef _WIN32
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            BCRYPT_HASH_HANDLE hash = nullptr;

            if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            {
                throw std::runtime_error("Failed to open SHA-256 provider.");
            }

            DWORD objectLength = 0;
            DWORD resultLength = 0;
            if (BCryptGetProperty(
                    algorithm,
                    BCRYPT_OBJECT_LENGTH,
                    reinterpret_cast<PUCHAR>(&objectLength),
                    sizeof(objectLength),
                    &resultLength,
                    0) < 0)
            {
                BCryptCloseAlgorithmProvider(algorithm, 0);
                throw std::runtime_error("Failed to query SHA-256 object size.");
            }

            DWORD hashLength = 0;
            if (BCryptGetProperty(
                    algorithm,
                    BCRYPT_HASH_LENGTH,
                    reinterpret_cast<PUCHAR>(&hashLength),
                    sizeof(hashLength),
                    &resultLength,
                    0) < 0)
            {
                BCryptCloseAlgorithmProvider(algorithm, 0);
                throw std::runtime_error("Failed to query SHA-256 hash size.");
            }

            std::vector<unsigned char> objectBuffer(objectLength);
            std::vector<unsigned char> digest(hashLength);

            if (BCryptCreateHash(
                    algorithm,
                    &hash,
                    objectBuffer.data(),
                    static_cast<ULONG>(objectBuffer.size()),
                    nullptr,
                    0,
                    0) < 0)
            {
                BCryptCloseAlgorithmProvider(algorithm, 0);
                throw std::runtime_error("Failed to create SHA-256 hash.");
            }

            if (BCryptHashData(
                    hash,
                    reinterpret_cast<PUCHAR>(const_cast<char*>(value.data())),
                    static_cast<ULONG>(value.size()),
                    0) < 0 ||
                BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
            {
                BCryptDestroyHash(hash);
                BCryptCloseAlgorithmProvider(algorithm, 0);
                throw std::runtime_error("Failed to compute SHA-256 hash.");
            }

            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            return digest;
#else
            throw std::runtime_error("NexusMods OAuth is currently implemented for Windows builds.");
#endif
        }

        std::wstring protectSecret(const std::wstring& value)
        {
#ifdef _WIN32
            DATA_BLOB input{};
            input.pbData = reinterpret_cast<BYTE*>(const_cast<wchar_t*>(value.data()));
            input.cbData = static_cast<DWORD>(value.size() * sizeof(wchar_t));

            DATA_BLOB output{};
            if (!CryptProtectData(
                    &input,
                    L"Fluxora NexusMods OAuth token",
                    nullptr,
                    nullptr,
                    nullptr,
                    CRYPTPROTECT_UI_FORBIDDEN,
                    &output))
            {
                throw std::runtime_error("Failed to protect NexusMods OAuth token.");
            }

            const std::wstring protectedValue = bytesToHex(output.pbData, output.cbData);
            LocalFree(output.pbData);
            return protectedValue;
#else
            return value;
#endif
        }

        std::wstring buildAuthorizeUrl(
            const OAuthConfig& config,
            const std::wstring& redirectUri,
            const std::wstring& state,
            const std::wstring& codeChallenge)
        {
            std::wstring url(authorizeEndpoint);
            url += L"?client_id=" + urlEncode(config.clientId);
            url += L"&response_type=code";
            url += L"&scope=" + urlEncode(L"openid profile email");
            url += L"&redirect_uri=" + urlEncode(redirectUri);
            url += L"&state=" + urlEncode(state);
            url += L"&code_challenge_method=S256";
            url += L"&code_challenge=" + urlEncode(codeChallenge);
            return url;
        }

        void openSystemBrowser(const std::wstring& url)
        {
#ifdef _WIN32
            const HINSTANCE result = ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<intptr_t>(result) <= 32)
            {
                throw std::runtime_error("Failed to open the system browser.");
            }
#else
            (void)url;
            throw std::runtime_error("Opening a browser is currently implemented for Windows builds.");
#endif
        }

        std::string readHttpRequest(SOCKET client)
        {
            std::string request;
            char buffer[2048]{};
            while (request.find("\r\n\r\n") == std::string::npos && request.size() < 16384)
            {
                const int received = recv(client, buffer, static_cast<int>(sizeof(buffer)), 0);
                if (received <= 0)
                {
                    break;
                }

                request.append(buffer, buffer + received);
            }

            return request;
        }

        void sendHttpResponse(SOCKET client, std::string_view title, std::string_view body)
        {
            const std::string html = "<!doctype html><html><head><meta charset=\"utf-8\"><title>" +
                std::string(title) +
                "</title></head><body style=\"font-family:Segoe UI,Arial,sans-serif;margin:40px\"><h2>" +
                std::string(title) +
                "</h2><p>" +
                std::string(body) +
                "</p></body></html>";

            const std::string response =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                "Content-Length: " + std::to_string(html.size()) + "\r\n"
                "Connection: close\r\n\r\n" +
                html;

            send(client, response.data(), static_cast<int>(response.size()), 0);
        }

        CallbackResult parseCallbackRequest(const std::string& request, const RedirectUriParts& redirect)
        {
            const std::size_t firstSpace = request.find(' ');
            const std::size_t secondSpace = request.find(' ', firstSpace == std::string::npos ? 0 : firstSpace + 1);
            if (firstSpace == std::string::npos || secondSpace == std::string::npos)
            {
                throw std::runtime_error("Invalid OAuth callback request.");
            }

            const std::string target = normalizeRequestTarget(
                request.substr(firstSpace + 1, secondSpace - firstSpace - 1));
            const std::size_t queryStart = target.find('?');
            const std::string targetPath = target.substr(0, queryStart);
            const std::string expectedPath = toUtf8(callbackPath(redirect));
            if (targetPath != expectedPath)
            {
                throw std::runtime_error("OAuth callback path does not match the configured redirect URI.");
            }

            const std::map<std::string, std::string> values = queryStart == std::string::npos
                ? std::map<std::string, std::string>{}
                : parseQuery(std::string_view(target).substr(queryStart + 1));

            CallbackResult result;
            if (const auto match = values.find("code"); match != values.end())
            {
                result.code = match->second;
            }
            if (const auto match = values.find("state"); match != values.end())
            {
                result.state = match->second;
            }
            if (const auto match = values.find("error"); match != values.end())
            {
                result.error = match->second;
            }
            if (const auto match = values.find("error_description"); match != values.end())
            {
                result.errorDescription = match->second;
            }

            return result;
        }

        class OAuthCallbackListener final
        {
        public:
            explicit OAuthCallbackListener(RedirectUriParts redirect)
                : redirect_(std::move(redirect))
            {
#ifdef _WIN32
                WSADATA wsaData{};
                if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
                {
                    throw std::runtime_error("Failed to initialize the OAuth callback listener.");
                }
                winsockStarted_ = true;

                listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (listener_ == INVALID_SOCKET)
                {
                    cleanup();
                    throw std::runtime_error("Failed to create the OAuth callback listener.");
                }

                sockaddr_in address{};
                address.sin_family = AF_INET;
                address.sin_port = htons(redirect_.port);
                inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

                if (bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
                    listen(listener_, 1) == SOCKET_ERROR)
                {
                    cleanup();
                    throw std::runtime_error("Failed to bind the OAuth callback listener. Check that the redirect port is free.");
                }

                sockaddr_in boundAddress{};
                int boundAddressLength = sizeof(boundAddress);
                if (getsockname(listener_, reinterpret_cast<sockaddr*>(&boundAddress), &boundAddressLength) == SOCKET_ERROR)
                {
                    cleanup();
                    throw std::runtime_error("Failed to resolve the OAuth callback listener port.");
                }

                actualPort_ = ntohs(boundAddress.sin_port);
                redirectUri_ = buildRedirectUri(redirect_, actualPort_);
#else
                (void)redirect_;
                throw std::runtime_error("NexusMods OAuth callback listener is currently implemented for Windows builds.");
#endif
            }

            OAuthCallbackListener(const OAuthCallbackListener&) = delete;
            OAuthCallbackListener& operator=(const OAuthCallbackListener&) = delete;

            ~OAuthCallbackListener()
            {
                cleanup();
            }

            [[nodiscard]] const std::wstring& redirectUri() const noexcept
            {
                return redirectUri_;
            }

            CallbackResult waitForRequest()
            {
#ifdef _WIN32
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(callbackTimeoutSeconds);
                while (true)
                {
                    const auto now = std::chrono::steady_clock::now();
                    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
                    if (remaining.count() <= 0)
                    {
                        throw std::runtime_error("Timed out waiting for NexusMods OAuth callback.");
                    }

                    fd_set readSet;
                    FD_ZERO(&readSet);
                    FD_SET(listener_, &readSet);
                    timeval timeout{};
                    timeout.tv_sec = static_cast<long>(remaining.count() / 1000);
                    timeout.tv_usec = static_cast<long>((remaining.count() % 1000) * 1000);

                    const int selected = select(0, &readSet, nullptr, nullptr, &timeout);
                    if (selected <= 0)
                    {
                        throw std::runtime_error("Timed out waiting for NexusMods OAuth callback.");
                    }

                    client_ = accept(listener_, nullptr, nullptr);
                    if (client_ == INVALID_SOCKET)
                    {
                        throw std::runtime_error("Failed to accept the NexusMods OAuth callback.");
                    }

                    DWORD receiveTimeout = callbackClientReadTimeoutMilliseconds;
                    setsockopt(
                        client_,
                        SOL_SOCKET,
                        SO_RCVTIMEO,
                        reinterpret_cast<const char*>(&receiveTimeout),
                        sizeof(receiveTimeout));

                    try
                    {
                        const std::string request = readHttpRequest(client_);
                        if (request.empty())
                        {
                            closeClient();
                            continue;
                        }

                        CallbackResult result = parseCallbackRequest(request, redirect_);
                        if (result.code.empty() && result.error.empty())
                        {
                            sendHttpResponse(
                                client_,
                                "Fluxora OAuth callback",
                                "Fluxora is waiting for the NexusMods authorization callback.");
                            closeClient();
                            continue;
                        }

                        return result;
                    }
                    catch (...)
                    {
                        respondFailure("Fluxora could not read this local OAuth request. Waiting for the NexusMods callback.");
                        closeClient();
                    }
                }
#else
                throw std::runtime_error("NexusMods OAuth callback listener is currently implemented for Windows builds.");
#endif
            }

            void respondSuccess()
            {
#ifdef _WIN32
                respond("Fluxora authorization received", "You can close this browser tab and return to Fluxora.");
#endif
            }

            void respondFailure(std::string_view body)
            {
#ifdef _WIN32
                respond("Fluxora authorization failed", body);
#else
                (void)body;
#endif
            }

        private:
#ifdef _WIN32
            void respond(std::string_view title, std::string_view body)
            {
                if (client_ == INVALID_SOCKET || responded_)
                {
                    return;
                }

                sendHttpResponse(client_, title, body);
                responded_ = true;
            }

            void closeClient() noexcept
            {
                if (client_ != INVALID_SOCKET)
                {
                    closesocket(client_);
                    client_ = INVALID_SOCKET;
                }

                responded_ = false;
            }
#endif

            void cleanup() noexcept
            {
#ifdef _WIN32
                closeClient();

                if (listener_ != INVALID_SOCKET)
                {
                    closesocket(listener_);
                    listener_ = INVALID_SOCKET;
                }

                if (winsockStarted_)
                {
                    WSACleanup();
                    winsockStarted_ = false;
                }
#endif
            }

            RedirectUriParts redirect_;
            std::wstring redirectUri_;
            unsigned short actualPort_{0};
            bool responded_{false};
#ifdef _WIN32
            SOCKET listener_{INVALID_SOCKET};
            SOCKET client_{INVALID_SOCKET};
            bool winsockStarted_{false};
#endif
        };

        std::string buildTokenRequestBody(
            const OAuthConfig& config,
            const std::wstring& redirectUri,
            const std::string& code,
            const std::wstring& codeVerifier)
        {
            std::wstring body;
            body += L"grant_type=authorization_code";
            body += L"&redirect_uri=" + urlEncode(redirectUri);
            body += L"&client_id=" + urlEncode(config.clientId);
            body += L"&code=" + urlEncode(fromUtf8(code));
            body += L"&code_verifier=" + urlEncode(codeVerifier);
            return toUtf8(body);
        }

        std::wstring readJsonString(const JsonValue& object, std::wstring_view field);

        std::string limitForError(std::string value)
        {
            constexpr std::size_t maxLength = 240;
            value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
            std::replace(value.begin(), value.end(), '\n', ' ');
            if (value.size() > maxLength)
            {
                value.resize(maxLength);
                value += "...";
            }

            return value;
        }

        std::string buildTokenErrorMessage(unsigned long statusCode, const std::string& body)
        {
            std::string message = "NexusMods token endpoint rejected the authorization code";
            message += " (HTTP " + std::to_string(statusCode) + ")";

            try
            {
                const JsonValue root = JsonReader::parse(fromUtf8(body));
                if (root.isObject())
                {
                    const std::wstring error = readJsonString(root, L"error");
                    const std::wstring description = readJsonString(root, L"error_description");
                    if (!error.empty())
                    {
                        message += ": " + toUtf8(error);
                    }
                    if (!description.empty())
                    {
                        message += ". " + toUtf8(description);
                    }

                    return message;
                }
            }
            catch (const std::exception&)
            {
            }

            if (body.find("<!DOCTYPE html>") != std::string::npos || body.find("<html") != std::string::npos)
            {
                message += ": received an HTML response instead of JSON. Check the network connection or NexusMods anti-bot/proxy restrictions.";
                return message;
            }

            if (!body.empty())
            {
                message += ": " + limitForError(body);
            }

            return message;
        }

        std::string postTokenRequest(const std::string& body)
        {
#ifdef _WIN32
            HINTERNET session = WinHttpOpen(
                L"Fluxora/0.1",
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS,
                0);
            if (session == nullptr)
            {
                throw std::runtime_error("Failed to initialize NexusMods token request.");
            }

            WinHttpSetTimeouts(session, 15000, 15000, 15000, 30000);

            HINTERNET connection = WinHttpConnect(
                session,
                std::wstring(tokenHost).c_str(),
                INTERNET_DEFAULT_HTTPS_PORT,
                0);
            if (connection == nullptr)
            {
                WinHttpCloseHandle(session);
                throw std::runtime_error("Failed to connect to NexusMods token endpoint.");
            }

            HINTERNET request = WinHttpOpenRequest(
                connection,
                L"POST",
                std::wstring(tokenPath).c_str(),
                nullptr,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                WINHTTP_FLAG_SECURE);
            if (request == nullptr)
            {
                WinHttpCloseHandle(connection);
                WinHttpCloseHandle(session);
                throw std::runtime_error("Failed to open NexusMods token request.");
            }

            const std::wstring headers =
                L"Content-Type: application/x-www-form-urlencoded; charset=UTF-8\r\n"
                L"Application-Name: Fluxora\r\n"
                L"Application-Version: 0.1.0\r\n";

            const BOOL sent = WinHttpSendRequest(
                request,
                headers.c_str(),
                static_cast<DWORD>(headers.size()),
                const_cast<char*>(body.data()),
                static_cast<DWORD>(body.size()),
                static_cast<DWORD>(body.size()),
                0);
            if (!sent || !WinHttpReceiveResponse(request, nullptr))
            {
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connection);
                WinHttpCloseHandle(session);
                throw std::runtime_error("NexusMods token request failed.");
            }

            DWORD statusCode = 0;
            DWORD statusCodeSize = sizeof(statusCode);
            WinHttpQueryHeaders(
                request,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &statusCode,
                &statusCodeSize,
                WINHTTP_NO_HEADER_INDEX);

            std::string responseBody;
            DWORD available = 0;
            while (WinHttpQueryDataAvailable(request, &available) && available > 0)
            {
                std::vector<char> chunk(available);
                DWORD read = 0;
                if (!WinHttpReadData(request, chunk.data(), available, &read))
                {
                    break;
                }

                responseBody.append(chunk.data(), chunk.data() + read);
            }

            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);

            if (statusCode < 200 || statusCode >= 300)
            {
                throw std::runtime_error(buildTokenErrorMessage(statusCode, responseBody));
            }

            return responseBody;
#else
            (void)body;
            throw std::runtime_error("NexusMods token exchange is currently implemented for Windows builds.");
#endif
        }

        std::wstring readJsonString(const JsonValue& object, std::wstring_view field)
        {
            const JsonValue* value = object.find(field);
            if (value == nullptr || !value->isString())
            {
                return {};
            }

            return value->asString();
        }

        long long readJsonInteger(const JsonValue& object, std::wstring_view field)
        {
            const JsonValue* value = object.find(field);
            if (value == nullptr)
            {
                return 0;
            }

            if (value->isNumber())
            {
                return std::stoll(value->asNumber());
            }

            if (value->isString())
            {
                return std::stoll(value->asString());
            }

            return 0;
        }

        TokenResponse parseTokenResponse(const std::string& body)
        {
            const JsonValue root = JsonReader::parse(fromUtf8(body));
            if (!root.isObject())
            {
                throw std::runtime_error("NexusMods token response was not a JSON object.");
            }

            TokenResponse tokens;
            tokens.accessToken = readJsonString(root, L"access_token");
            tokens.refreshToken = readJsonString(root, L"refresh_token");
            tokens.tokenType = readJsonString(root, L"token_type");
            tokens.expiresInSeconds = readJsonInteger(root, L"expires_in");

            if (tokens.accessToken.empty())
            {
                throw std::runtime_error("NexusMods token response did not include an access token.");
            }

            return tokens;
        }

        JwtUser parseJwtUser(const std::wstring& accessToken)
        {
            JwtUser user;
            const std::string token = toUtf8(accessToken);
            const std::size_t firstDot = token.find('.');
            const std::size_t secondDot = firstDot == std::string::npos ? std::string::npos : token.find('.', firstDot + 1);
            if (firstDot == std::string::npos || secondDot == std::string::npos)
            {
                return user;
            }

            const std::string payloadText = [&]() {
                const std::vector<unsigned char> bytes = base64UrlDecode(
                    std::string_view(token).substr(firstDot + 1, secondDot - firstDot - 1));
                return std::string(bytes.begin(), bytes.end());
            }();

            try
            {
                const JsonValue payload = JsonReader::parse(fromUtf8(payloadText));
                if (!payload.isObject())
                {
                    return user;
                }

                user.userId = readJsonString(payload, L"sub");
                const JsonValue* userObject = payload.find(L"user");
                if (userObject != nullptr && userObject->isObject())
                {
                    user.username = readJsonString(*userObject, L"username");
                    if (user.userId.empty())
                    {
                        const JsonValue* id = userObject->find(L"id");
                        if (id != nullptr)
                        {
                            user.userId = id->isNumber() ? id->asNumber() : readJsonString(*userObject, L"id");
                        }
                    }
                }
            }
            catch (const std::exception&)
            {
                return {};
            }

            return user;
        }

        std::wstring formatUtcExpiry(long long expiresInSeconds)
        {
            using clock = std::chrono::system_clock;
            const clock::time_point expiresAt = clock::now() + std::chrono::seconds(expiresInSeconds);
            const std::time_t time = clock::to_time_t(expiresAt);

            std::tm utc{};
#ifdef _WIN32
            gmtime_s(&utc, &time);
#else
            gmtime_r(&time, &utc);
#endif

            std::wstringstream stream;
            stream << std::put_time(&utc, L"%Y-%m-%dT%H:%M:%SZ");
            return stream.str();
        }

        NexusModsAuthStatus buildStatus(const OAuthConfig& config, const NexusModsStoredAuth& auth)
        {
            NexusModsAuthStatus status;
            status.isConfigured = !config.clientId.empty();
            status.isLinked = auth.linked && !auth.protectedAccessToken.empty();
            status.displayName = status.isLinked ? auth.username : L"";
            status.userId = status.isLinked ? auth.userId : L"";
            status.clientId = config.clientId;
            status.redirectUri = config.redirectUri;

            if (!status.isConfigured)
            {
                status.message = L"Нужен зарегистрированный NexusMods OAuth client_id: задайте FLUXORA_NEXUS_CLIENT_ID.";
            }
            else if (status.isLinked)
            {
                status.message = status.displayName.empty()
                    ? L"NexusMods привязан."
                    : L"NexusMods привязан: " + status.displayName;
            }
            else
            {
                status.message = L"NexusMods не привязан.";
            }

            return status;
        }
    }

    NexusModsAuthService::NexusModsAuthService(Logger& logger, AppSettingsService& settings) noexcept
        : logger_(logger),
          settings_(settings)
    {
    }

    void NexusModsAuthService::initialize()
    {
        if (initialized_)
        {
            return;
        }

        initialized_ = true;
        logger_.write(LogLevel::Info, "NexusMods auth service initialized.");
    }

    void NexusModsAuthService::shutdown()
    {
        if (!initialized_)
        {
            return;
        }

        initialized_ = false;
        logger_.write(LogLevel::Info, "NexusMods auth service shut down.");
    }

    NexusModsAuthStatus NexusModsAuthService::status() const
    {
        return buildStatus(loadConfig(), settings_.loadNexusModsAuth());
    }

    NexusModsAuthStatus NexusModsAuthService::connect()
    {
        const OAuthConfig config = loadConfig();
        if (config.clientId.empty())
        {
            throw std::invalid_argument("NexusMods OAuth client_id is missing. Set FLUXORA_NEXUS_CLIENT_ID.");
        }

        OAuthCallbackListener callbackListener(parseRedirectUri(config.redirectUri));
        const std::wstring redirectUri = callbackListener.redirectUri();
        const std::wstring state = generateHexRandom(16);
        const std::wstring codeVerifier = generateHexRandom(48);
        const std::wstring codeChallenge = base64UrlEncode(sha256(toUtf8(codeVerifier)));
        const std::wstring authorizeUrl = buildAuthorizeUrl(config, redirectUri, state, codeChallenge);

        logger_.write(LogLevel::Info, "Opening NexusMods OAuth authorization URL.");
        openSystemBrowser(authorizeUrl);

        try
        {
            const CallbackResult callback = callbackListener.waitForRequest();
            if (!callback.error.empty())
            {
                throw std::runtime_error(callback.errorDescription.empty()
                    ? "NexusMods authorization was denied."
                    : callback.errorDescription);
            }

            if (callback.code.empty())
            {
                throw std::runtime_error("NexusMods OAuth callback did not include an authorization code.");
            }

            if (callback.state != toUtf8(state))
            {
                throw std::runtime_error("NexusMods OAuth state validation failed.");
            }

            const std::string tokenResponseBody = postTokenRequest(
                buildTokenRequestBody(config, redirectUri, callback.code, codeVerifier));
            const TokenResponse tokens = parseTokenResponse(tokenResponseBody);
            const JwtUser user = parseJwtUser(tokens.accessToken);

            NexusModsStoredAuth stored;
            stored.linked = true;
            stored.username = user.username;
            stored.userId = user.userId;
            stored.tokenType = tokens.tokenType.empty() ? L"Bearer" : tokens.tokenType;
            stored.expiresAtUtc = formatUtcExpiry(tokens.expiresInSeconds);
            stored.protectedAccessToken = protectSecret(tokens.accessToken);
            stored.protectedRefreshToken = tokens.refreshToken.empty()
                ? L""
                : protectSecret(tokens.refreshToken);
            settings_.saveNexusModsAuth(stored);
            callbackListener.respondSuccess();

            OAuthConfig statusConfig = config;
            statusConfig.redirectUri = redirectUri;
            logger_.write(LogLevel::Info, "NexusMods account linked.");
            return buildStatus(statusConfig, stored);
        }
        catch (...)
        {
            callbackListener.respondFailure("Fluxora could not finish the NexusMods OAuth login. Return to Fluxora for details.");
            throw;
        }
    }

    NexusModsAuthStatus NexusModsAuthService::disconnect()
    {
        settings_.clearNexusModsAuth();
        logger_.write(LogLevel::Info, "NexusMods account unlinked.");
        return status();
    }

    bool NexusModsAuthService::isInitialized() const noexcept
    {
        return initialized_;
    }
}
