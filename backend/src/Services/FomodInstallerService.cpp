#include "FluxoraCore/Services/FomodInstallerService.hpp"

#include "FluxoraCore/Services/PathSafetyService.hpp"
#include "FluxoraCore/Storage/AtomicFileStore.hpp"
#include "FluxoraCore/Support/JsonReader.hpp"
#include "FluxoraCore/Support/JsonWriter.hpp"

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fluxora
{
    namespace
    {
        constexpr std::wstring_view memoryDirectoryName = L".flow";
        constexpr std::wstring_view memoryFileName = L"fomod-memory.json";

        struct XmlNode
        {
            std::wstring name;
            std::map<std::wstring, std::wstring> attributes;
            std::wstring text;
            std::vector<XmlNode> children;
        };

        struct FomodMemoryEntry
        {
            std::wstring key;
            std::wstring moduleName;
            std::wstring moduleVersion;
            std::vector<std::wstring> selectedOptionIds;
        };

        struct FomodEnvironment
        {
            std::filesystem::path projectDirectory;
            std::filesystem::path gameDirectory;
            std::filesystem::path modsDirectory;
            std::vector<std::wstring> gameDataFolders;
        };

        struct SelectedOptionSet
        {
            std::set<std::wstring> requestedIds;
            std::set<std::wstring> appliedIds;
            std::map<std::wstring, std::wstring> flags;
        };

        struct PlannedFile
        {
            FomodFileEntry file;
            std::size_t sequence{0};
        };

        [[nodiscard]] std::wstring trim(std::wstring value)
        {
            const auto first = value.find_first_not_of(L" \t\r\n");
            if (first == std::wstring::npos)
            {
                return {};
            }

            const auto last = value.find_last_not_of(L" \t\r\n");
            return value.substr(first, last - first + 1);
        }

        [[nodiscard]] std::wstring toLower(std::wstring value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
            {
                return static_cast<wchar_t>(std::towlower(character));
            });
            return value;
        }

        [[nodiscard]] bool equalsIgnoreCase(std::wstring_view left, std::wstring_view right)
        {
            return toLower(std::wstring(left)) == toLower(std::wstring(right));
        }

        [[nodiscard]] std::string toUtf8(const std::wstring& value)
        {
#ifdef _WIN32
            if (value.empty())
            {
                return {};
            }

            const int size = WideCharToMultiByte(
                CP_UTF8,
                0,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0,
                nullptr,
                nullptr);
            if (size <= 0)
            {
                throw std::runtime_error("Failed to encode text as UTF-8.");
            }

            std::string out(static_cast<std::size_t>(size), '\0');
            WideCharToMultiByte(
                CP_UTF8,
                0,
                value.data(),
                static_cast<int>(value.size()),
                out.data(),
                size,
                nullptr,
                nullptr);
            return out;
#else
            return std::string(value.begin(), value.end());
#endif
        }

        [[nodiscard]] std::wstring fromUtf8Bytes(const std::string& value)
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
                throw std::invalid_argument("Text is not valid UTF-8.");
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

        [[nodiscard]] std::string readBinaryFile(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::in | std::ios::binary);
            if (!file)
            {
                throw std::runtime_error("Failed to open FOMOD file.");
            }

            return std::string(
                std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>());
        }

        void writeTextFile(const std::filesystem::path& path, const std::string& content)
        {
            AtomicFileStore().writeTextFile(
                path,
                content,
                AtomicFileWriteOptions{
                    L"generated FOMOD metadata",
                    ProjectStateValidation::JsonObject
                });
        }

        [[nodiscard]] std::wstring decodeUtf16(const std::string& bytes, bool bigEndian, std::size_t offset)
        {
            std::wstring text;
            for (std::size_t index = offset; index + 1 < bytes.size(); index += 2)
            {
                const auto first = static_cast<unsigned char>(bytes[index]);
                const auto second = static_cast<unsigned char>(bytes[index + 1]);
                const std::uint16_t codeUnit = bigEndian
                    ? static_cast<std::uint16_t>((first << 8) | second)
                    : static_cast<std::uint16_t>((second << 8) | first);
                text.push_back(static_cast<wchar_t>(codeUnit));
            }

            return text;
        }

        [[nodiscard]] std::wstring readXmlTextFile(const std::filesystem::path& path)
        {
            const std::string bytes = readBinaryFile(path);
            if (bytes.size() >= 2)
            {
                const auto b0 = static_cast<unsigned char>(bytes[0]);
                const auto b1 = static_cast<unsigned char>(bytes[1]);
                if (b0 == 0xFF && b1 == 0xFE)
                {
                    return decodeUtf16(bytes, false, 2);
                }
                if (b0 == 0xFE && b1 == 0xFF)
                {
                    return decodeUtf16(bytes, true, 2);
                }
            }

            if (bytes.size() >= 3 &&
                static_cast<unsigned char>(bytes[0]) == 0xEF &&
                static_cast<unsigned char>(bytes[1]) == 0xBB &&
                static_cast<unsigned char>(bytes[2]) == 0xBF)
            {
                return fromUtf8Bytes(bytes.substr(3));
            }

            try
            {
                return fromUtf8Bytes(bytes);
            }
            catch (const std::exception&)
            {
                std::wstring fallback;
                fallback.reserve(bytes.size());
                for (char byte : bytes)
                {
                    fallback.push_back(static_cast<unsigned char>(byte));
                }
                return fallback;
            }
        }

        [[nodiscard]] std::wstring decodeXmlEntities(std::wstring_view value)
        {
            std::wstring decoded;
            decoded.reserve(value.size());
            for (std::size_t index = 0; index < value.size(); ++index)
            {
                if (value[index] != L'&')
                {
                    decoded.push_back(value[index]);
                    continue;
                }

                const std::size_t semicolon = value.find(L';', index + 1);
                if (semicolon == std::wstring_view::npos)
                {
                    decoded.push_back(value[index]);
                    continue;
                }

                const std::wstring entity(value.substr(index + 1, semicolon - index - 1));
                if (entity == L"amp")
                {
                    decoded.push_back(L'&');
                }
                else if (entity == L"lt")
                {
                    decoded.push_back(L'<');
                }
                else if (entity == L"gt")
                {
                    decoded.push_back(L'>');
                }
                else if (entity == L"quot")
                {
                    decoded.push_back(L'"');
                }
                else if (entity == L"apos")
                {
                    decoded.push_back(L'\'');
                }
                else if (entity.starts_with(L"#x") || entity.starts_with(L"#X"))
                {
                    decoded.push_back(static_cast<wchar_t>(std::stoul(std::wstring(entity.substr(2)), nullptr, 16)));
                }
                else if (entity.starts_with(L"#"))
                {
                    decoded.push_back(static_cast<wchar_t>(std::stoul(std::wstring(entity.substr(1)), nullptr, 10)));
                }
                else
                {
                    decoded.push_back(L'&');
                    decoded.append(entity);
                    decoded.push_back(L';');
                }

                index = semicolon;
            }

            return decoded;
        }

        [[nodiscard]] std::wstring localName(std::wstring name)
        {
            if (const std::size_t colon = name.find(L':'); colon != std::wstring::npos)
            {
                name.erase(0, colon + 1);
            }

            return toLower(std::move(name));
        }

        class XmlParser final
        {
        public:
            explicit XmlParser(std::wstring_view text) noexcept
                : text_(text)
            {
            }

            [[nodiscard]] XmlNode parse()
            {
                skipWhitespace();
                while (startsWith(L"<?") || startsWith(L"<!--") || startsWith(L"<!DOCTYPE"))
                {
                    skipSpecial();
                    skipWhitespace();
                }

                XmlNode root = parseElement();
                skipWhitespace();
                return root;
            }

        private:
            [[nodiscard]] XmlNode parseElement()
            {
                expect(L'<');
                if (startsWith(L"!--") || startsWith(L"?") || startsWith(L"![CDATA["))
                {
                    throw std::runtime_error("Unexpected XML node.");
                }

                XmlNode node;
                node.name = readName();
                skipWhitespace();
                while (!isAtEnd() && peek() != L'>' && !startsWith(L"/>"))
                {
                    const std::wstring name = readName();
                    skipWhitespace();
                    expect(L'=');
                    skipWhitespace();
                    const std::wstring value = readQuotedValue();
                    node.attributes.emplace(name, value);
                    skipWhitespace();
                }

                if (consume(L'/'))
                {
                    expect(L'>');
                    return node;
                }

                expect(L'>');
                while (!isAtEnd())
                {
                    if (startsWith(L"</"))
                    {
                        position_ += 2;
                        const std::wstring closeName = readName();
                        if (!equalsIgnoreCase(localName(closeName), localName(node.name)))
                        {
                            throw std::runtime_error("Mismatched XML closing tag.");
                        }
                        skipWhitespace();
                        expect(L'>');
                        return node;
                    }

                    if (startsWith(L"<!--") || startsWith(L"<?") || startsWith(L"<!DOCTYPE"))
                    {
                        skipSpecial();
                        continue;
                    }

                    if (startsWith(L"<![CDATA["))
                    {
                        position_ += 9;
                        const std::size_t end = text_.find(L"]]>", position_);
                        if (end == std::wstring_view::npos)
                        {
                            throw std::runtime_error("Unterminated XML CDATA.");
                        }
                        node.text.append(text_.substr(position_, end - position_));
                        position_ = end + 3;
                        continue;
                    }

                    if (peek() == L'<')
                    {
                        node.children.push_back(parseElement());
                        continue;
                    }

                    const std::size_t nextTag = text_.find(L'<', position_);
                    const std::size_t end = nextTag == std::wstring_view::npos ? text_.size() : nextTag;
                    node.text.append(decodeXmlEntities(text_.substr(position_, end - position_)));
                    position_ = end;
                }

                throw std::runtime_error("Unterminated XML element.");
            }

            [[nodiscard]] std::wstring readName()
            {
                const std::size_t start = position_;
                while (!isAtEnd())
                {
                    const wchar_t character = peek();
                    if (std::iswalnum(character) ||
                        character == L'_' ||
                        character == L'-' ||
                        character == L'.' ||
                        character == L':')
                    {
                        ++position_;
                        continue;
                    }
                    break;
                }

                if (position_ == start)
                {
                    throw std::runtime_error("Expected XML name.");
                }

                return std::wstring(text_.substr(start, position_ - start));
            }

            [[nodiscard]] std::wstring readQuotedValue()
            {
                const wchar_t quote = peek();
                if (quote != L'\'' && quote != L'"')
                {
                    throw std::runtime_error("Expected XML attribute quote.");
                }
                ++position_;
                const std::size_t start = position_;
                while (!isAtEnd() && peek() != quote)
                {
                    ++position_;
                }
                if (isAtEnd())
                {
                    throw std::runtime_error("Unterminated XML attribute.");
                }
                const std::wstring value = decodeXmlEntities(text_.substr(start, position_ - start));
                ++position_;
                return value;
            }

            void skipSpecial()
            {
                if (startsWith(L"<!--"))
                {
                    const std::size_t end = text_.find(L"-->", position_ + 4);
                    if (end == std::wstring_view::npos)
                    {
                        throw std::runtime_error("Unterminated XML comment.");
                    }
                    position_ = end + 3;
                    return;
                }

                if (startsWith(L"<?"))
                {
                    const std::size_t end = text_.find(L"?>", position_ + 2);
                    if (end == std::wstring_view::npos)
                    {
                        throw std::runtime_error("Unterminated XML processing instruction.");
                    }
                    position_ = end + 2;
                    return;
                }

                if (startsWith(L"<!DOCTYPE"))
                {
                    const std::size_t end = text_.find(L'>', position_ + 9);
                    if (end == std::wstring_view::npos)
                    {
                        throw std::runtime_error("Unterminated XML doctype.");
                    }
                    position_ = end + 1;
                    return;
                }
            }

            void skipWhitespace() noexcept
            {
                while (!isAtEnd() && std::iswspace(peek()))
                {
                    ++position_;
                }
            }

            [[nodiscard]] bool startsWith(std::wstring_view value) const noexcept
            {
                return text_.substr(position_, value.size()) == value;
            }

            void expect(wchar_t expected)
            {
                if (!consume(expected))
                {
                    throw std::runtime_error("Unexpected XML token.");
                }
            }

            bool consume(wchar_t expected) noexcept
            {
                if (isAtEnd() || peek() != expected)
                {
                    return false;
                }

                ++position_;
                return true;
            }

            [[nodiscard]] wchar_t peek() const noexcept
            {
                return text_[position_];
            }

            [[nodiscard]] bool isAtEnd() const noexcept
            {
                return position_ >= text_.size();
            }

            std::wstring_view text_;
            std::size_t position_{0};
        };

        [[nodiscard]] const XmlNode* firstChild(const XmlNode& node, std::wstring_view name)
        {
            for (const XmlNode& child : node.children)
            {
                if (localName(child.name) == toLower(std::wstring(name)))
                {
                    return &child;
                }
            }

            return nullptr;
        }

        [[nodiscard]] std::vector<const XmlNode*> childrenNamed(const XmlNode& node, std::wstring_view name)
        {
            std::vector<const XmlNode*> children;
            for (const XmlNode& child : node.children)
            {
                if (localName(child.name) == toLower(std::wstring(name)))
                {
                    children.push_back(&child);
                }
            }

            return children;
        }

        [[nodiscard]] std::wstring attribute(const XmlNode& node, std::wstring_view name, std::wstring_view fallback = L"")
        {
            const std::wstring wanted = toLower(std::wstring(name));
            for (const auto& [key, value] : node.attributes)
            {
                if (localName(key) == wanted)
                {
                    return value;
                }
            }

            return std::wstring(fallback);
        }

        [[nodiscard]] std::wstring childText(const XmlNode& node, std::wstring_view name)
        {
            const XmlNode* child = firstChild(node, name);
            return child == nullptr ? std::wstring() : trim(child->text);
        }

        [[nodiscard]] int parseInt(std::wstring_view value, int fallback = 0)
        {
            try
            {
                return value.empty() ? fallback : std::stoi(std::wstring(value));
            }
            catch (const std::exception&)
            {
                return fallback;
            }
        }

        [[nodiscard]] bool parseBool(std::wstring_view value)
        {
            const std::wstring lower = toLower(trim(std::wstring(value)));
            return lower == L"true" || lower == L"1" || lower == L"yes";
        }

        [[nodiscard]] std::filesystem::path memoryPath(const std::filesystem::path& projectDirectory)
        {
            return projectDirectory /
                std::filesystem::path(std::wstring(memoryDirectoryName)) /
                std::filesystem::path(std::wstring(memoryFileName));
        }

        [[nodiscard]] std::wstring readStringOrDefault(
            const JsonValue& object,
            std::wstring_view key,
            std::wstring_view fallback = L"")
        {
            const JsonValue* value = object.find(key);
            return value != nullptr && value->isString()
                ? value->asString()
                : std::wstring(fallback);
        }

        [[nodiscard]] std::vector<std::wstring> readStringArrayOrEmpty(
            const JsonValue& object,
            std::wstring_view key)
        {
            const JsonValue* value = object.find(key);
            if (value == nullptr || !value->isArray())
            {
                return {};
            }

            std::vector<std::wstring> items;
            for (const JsonValue& item : value->asArray())
            {
                if (item.isString())
                {
                    items.push_back(item.asString());
                }
            }
            return items;
        }

        [[nodiscard]] std::vector<FomodMemoryEntry> loadMemory(const std::filesystem::path& projectDirectory)
        {
            const std::filesystem::path path = memoryPath(projectDirectory);
            if (!std::filesystem::exists(path))
            {
                return {};
            }

            try
            {
                static_cast<void>(AtomicFileStore().recoverFile(
                    path,
                    AtomicFileWriteOptions{
                        L"generated FOMOD metadata",
                        ProjectStateValidation::JsonObject
                    }));
                const JsonValue root = JsonReader::parse(fromUtf8Bytes(readBinaryFile(path)));
                const JsonValue* entriesValue = root.find(L"entries");
                if (entriesValue == nullptr || !entriesValue->isArray())
                {
                    return {};
                }

                std::vector<FomodMemoryEntry> entries;
                for (const JsonValue& value : entriesValue->asArray())
                {
                    if (!value.isObject())
                    {
                        continue;
                    }

                    FomodMemoryEntry entry;
                    entry.key = readStringOrDefault(value, L"key");
                    entry.moduleName = readStringOrDefault(value, L"moduleName");
                    entry.moduleVersion = readStringOrDefault(value, L"moduleVersion");
                    entry.selectedOptionIds = readStringArrayOrEmpty(value, L"selectedOptionIds");
                    if (!entry.key.empty())
                    {
                        entries.push_back(std::move(entry));
                    }
                }
                return entries;
            }
            catch (const std::exception&)
            {
                return {};
            }
        }

        void saveMemory(
            const std::filesystem::path& projectDirectory,
            std::vector<FomodMemoryEntry> entries)
        {
            JsonWriter writer;
            writer.beginObject();
            writer.field(L"schemaVersion", 1);
            writer.key(L"entries").beginArray();
            for (const FomodMemoryEntry& entry : entries)
            {
                writer.beginObject();
                writer.field(L"key", entry.key);
                writer.field(L"moduleName", entry.moduleName);
                writer.field(L"moduleVersion", entry.moduleVersion);
                writer.stringArray(L"selectedOptionIds", entry.selectedOptionIds);
                writer.endObject();
            }
            writer.endArray();
            writer.endObject();

            writeTextFile(memoryPath(projectDirectory), toUtf8(writer.str()));
        }

        void saveRememberedSelection(
            const std::filesystem::path& projectDirectory,
            const FomodInstallerDescriptor& descriptor,
            const std::vector<std::wstring>& selectedOptionIds)
        {
            if (descriptor.memoryKey.empty())
            {
                return;
            }

            std::vector<FomodMemoryEntry> entries = loadMemory(projectDirectory);
            const auto match = std::find_if(entries.begin(), entries.end(), [&](const FomodMemoryEntry& entry)
            {
                return entry.key == descriptor.memoryKey;
            });

            FomodMemoryEntry next{
                descriptor.memoryKey,
                descriptor.moduleName,
                descriptor.moduleVersion,
                selectedOptionIds
            };

            if (match == entries.end())
            {
                entries.push_back(std::move(next));
            }
            else
            {
                *match = std::move(next);
            }

            saveMemory(projectDirectory, std::move(entries));
        }

        [[nodiscard]] std::wstring normalizeKeyPart(std::wstring value)
        {
            value = toLower(trim(std::move(value)));
            std::wstring normalized;
            bool pendingDash = false;
            for (wchar_t character : value)
            {
                if (std::iswalnum(character))
                {
                    if (pendingDash && !normalized.empty())
                    {
                        normalized.push_back(L'-');
                    }
                    pendingDash = false;
                    normalized.push_back(character);
                    continue;
                }
                pendingDash = true;
            }

            return normalized;
        }

        [[nodiscard]] std::wstring makeMemoryKey(
            const FomodPackageIdentity& identity,
            std::wstring_view moduleId,
            std::wstring_view moduleName)
        {
            if (!trim(identity.gameDomain).empty() && !trim(identity.remoteModId).empty())
            {
                return L"nexus:" + normalizeKeyPart(identity.gameDomain) + L":" + normalizeKeyPart(identity.remoteModId);
            }

            if (!trim(std::wstring(moduleId)).empty())
            {
                return L"fomod-id:" + normalizeKeyPart(std::wstring(moduleId));
            }

            if (!trim(std::wstring(moduleName)).empty())
            {
                return L"module:" + normalizeKeyPart(std::wstring(moduleName));
            }

            return L"archive:" + normalizeKeyPart(identity.fallbackName);
        }

        [[nodiscard]] std::filesystem::path candidateFile(
            const std::filesystem::path& directory,
            std::initializer_list<std::wstring_view> names)
        {
            for (std::wstring_view name : names)
            {
                std::filesystem::path candidate = directory / std::filesystem::path(std::wstring(name));
                if (std::filesystem::exists(candidate) && std::filesystem::is_regular_file(candidate))
                {
                    return candidate;
                }
            }

            return {};
        }

        [[nodiscard]] std::filesystem::path fomodDirectoryForRoot(const std::filesystem::path& root)
        {
            for (std::wstring_view folderName : {L"fomod", L"FOMOD", L"Fomod"})
            {
                const std::filesystem::path directory = root / std::filesystem::path(folderName);
                if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory))
                {
                    continue;
                }

                if (!candidateFile(directory, {L"ModuleConfig.xml", L"moduleconfig.xml", L"ModuleConfig.XML"}).empty())
                {
                    return directory;
                }
            }

            return {};
        }

        [[nodiscard]] std::filesystem::path packageRootWithFomod(const std::filesystem::path& packageDirectory)
        {
            if (!fomodDirectoryForRoot(packageDirectory).empty())
            {
                return packageDirectory;
            }

            for (const auto& entry : std::filesystem::directory_iterator(packageDirectory))
            {
                if (!entry.is_directory())
                {
                    continue;
                }

                if (!fomodDirectoryForRoot(entry.path()).empty())
                {
                    return entry.path();
                }
            }

            return {};
        }

        [[nodiscard]] std::filesystem::path moduleConfigPath(const std::filesystem::path& packageRoot)
        {
            const std::filesystem::path fomodDirectory = fomodDirectoryForRoot(packageRoot);
            if (fomodDirectory.empty())
            {
                return {};
            }

            return candidateFile(fomodDirectory, {L"ModuleConfig.xml", L"moduleconfig.xml", L"ModuleConfig.XML"});
        }

        [[nodiscard]] std::filesystem::path infoPath(const std::filesystem::path& packageRoot)
        {
            const std::filesystem::path fomodDirectory = fomodDirectoryForRoot(packageRoot);
            if (fomodDirectory.empty())
            {
                return {};
            }

            return candidateFile(fomodDirectory, {L"info.xml", L"Info.xml", L"INFO.XML"});
        }

        [[nodiscard]] XmlNode parseXmlFile(const std::filesystem::path& path)
        {
            const std::wstring text = readXmlTextFile(path);
            XmlParser parser(text);
            return parser.parse();
        }

        [[nodiscard]] FomodFileEntry parseFileEntry(const XmlNode& node)
        {
            const std::wstring nodeName = localName(node.name);
            FomodFileEntry entry;
            entry.isFolder = nodeName == L"folder";
            entry.source = trim(attribute(node, L"source"));
            entry.destination = trim(attribute(node, L"destination"));
            entry.alwaysInstall = parseBool(attribute(node, L"alwaysInstall"));
            entry.installIfUsable = parseBool(attribute(node, L"installIfUsable"));
            entry.priority = parseInt(attribute(node, L"priority"));
            return entry;
        }

        [[nodiscard]] std::vector<FomodFileEntry> parseFileList(const XmlNode* files)
        {
            std::vector<FomodFileEntry> entries;
            if (files == nullptr)
            {
                return entries;
            }

            for (const XmlNode& child : files->children)
            {
                const std::wstring name = localName(child.name);
                if (name == L"file" || name == L"folder")
                {
                    entries.push_back(parseFileEntry(child));
                }
            }

            return entries;
        }

        [[nodiscard]] FomodDependencyNode parseDependencyNode(const XmlNode& node)
        {
            const std::wstring name = localName(node.name);
            if (name == L"filedependency")
            {
                FomodDependencyNode dependency;
                dependency.kind = L"file";
                dependency.file = trim(attribute(node, L"file"));
                dependency.state = trim(attribute(node, L"state", L"Active"));
                return dependency;
            }

            if (name == L"flagdependency")
            {
                FomodDependencyNode dependency;
                dependency.kind = L"flag";
                dependency.flag = trim(attribute(node, L"flag"));
                dependency.value = trim(attribute(node, L"value"));
                return dependency;
            }

            if (name == L"gamedependency" || name == L"fommdependency")
            {
                FomodDependencyNode dependency;
                dependency.kind = name == L"gamedependency" ? L"game" : L"fomm";
                dependency.version = trim(attribute(node, L"version"));
                return dependency;
            }

            FomodDependencyNode composite;
            composite.kind = L"composite";
            composite.op = trim(attribute(node, L"operator", L"And"));
            if (composite.op.empty())
            {
                composite.op = L"And";
            }
            for (const XmlNode& child : node.children)
            {
                const std::wstring childName = localName(child.name);
                if (childName == L"filedependency" ||
                    childName == L"flagdependency" ||
                    childName == L"gamedependency" ||
                    childName == L"fommdependency" ||
                    childName == L"dependencies")
                {
                    composite.children.push_back(parseDependencyNode(child));
                }
            }

            return composite;
        }

        [[nodiscard]] std::vector<FomodConditionFlag> parseFlags(const XmlNode* conditionFlags)
        {
            std::vector<FomodConditionFlag> flags;
            if (conditionFlags == nullptr)
            {
                return flags;
            }

            for (const XmlNode* flag : childrenNamed(*conditionFlags, L"flag"))
            {
                FomodConditionFlag parsed{
                    trim(attribute(*flag, L"name")),
                    trim(flag->text)
                };
                if (!parsed.name.empty())
                {
                    flags.push_back(std::move(parsed));
                }
            }
            return flags;
        }

        [[nodiscard]] std::vector<FomodTypePattern> parseTypePatterns(const XmlNode* dependencyType)
        {
            std::vector<FomodTypePattern> patterns;
            if (dependencyType == nullptr)
            {
                return patterns;
            }

            const XmlNode* patternsNode = firstChild(*dependencyType, L"patterns");
            if (patternsNode == nullptr)
            {
                return patterns;
            }

            for (const XmlNode* patternNode : childrenNamed(*patternsNode, L"pattern"))
            {
                const XmlNode* dependencies = firstChild(*patternNode, L"dependencies");
                const XmlNode* type = firstChild(*patternNode, L"type");
                if (dependencies == nullptr || type == nullptr)
                {
                    continue;
                }

                patterns.push_back(FomodTypePattern{
                    parseDependencyNode(*dependencies),
                    trim(attribute(*type, L"name", L"Optional"))
                });
            }

            return patterns;
        }

        [[nodiscard]] FomodOption parseOption(
            const XmlNode& plugin,
            std::wstring_view stepName,
            std::wstring_view groupName,
            std::map<std::wstring, int>& idCounts)
        {
            FomodOption option;
            option.name = trim(attribute(plugin, L"name", L"Option"));
            option.description = childText(plugin, L"description");
            if (const XmlNode* image = firstChild(plugin, L"image"); image != nullptr)
            {
                option.imagePath = trim(attribute(*image, L"path"));
            }
            option.files = parseFileList(firstChild(plugin, L"files"));
            option.flags = parseFlags(firstChild(plugin, L"conditionFlags"));

            if (const XmlNode* typeDescriptor = firstChild(plugin, L"typeDescriptor"); typeDescriptor != nullptr)
            {
                if (const XmlNode* type = firstChild(*typeDescriptor, L"type"); type != nullptr)
                {
                    option.type = trim(attribute(*type, L"name", L"Optional"));
                    option.defaultType = option.type;
                }
                else if (const XmlNode* dependencyType = firstChild(*typeDescriptor, L"dependencyType"); dependencyType != nullptr)
                {
                    if (const XmlNode* defaultType = firstChild(*dependencyType, L"defaultType"); defaultType != nullptr)
                    {
                        option.defaultType = trim(attribute(*defaultType, L"name", L"Optional"));
                    }
                    option.type = option.defaultType;
                    option.typePatterns = parseTypePatterns(dependencyType);
                }
            }

            std::wstring base = normalizeKeyPart(std::wstring(stepName) + L"-" + std::wstring(groupName) + L"-" + option.name);
            if (base.empty())
            {
                base = L"option";
            }
            const int count = ++idCounts[base];
            option.id = count == 1 ? base : base + L"-" + std::to_wstring(count);
            return option;
        }

        template <typename T>
        void applyOrder(std::vector<T>& items, std::wstring_view order)
        {
            if (equalsIgnoreCase(order, L"Explicit"))
            {
                return;
            }

            std::stable_sort(items.begin(), items.end(), [&](const T& left, const T& right)
            {
                const int direction = equalsIgnoreCase(order, L"Descending") ? -1 : 1;
                return direction > 0
                    ? toLower(left.name) < toLower(right.name)
                    : toLower(left.name) > toLower(right.name);
            });
        }

        [[nodiscard]] FomodGroup parseGroup(
            const XmlNode& groupNode,
            std::wstring_view stepName,
            std::map<std::wstring, int>& idCounts)
        {
            FomodGroup group;
            group.name = trim(attribute(groupNode, L"name", L"Options"));
            group.type = trim(attribute(groupNode, L"type", L"SelectAny"));
            group.id = normalizeKeyPart(std::wstring(stepName) + L"-" + group.name);
            if (group.id.empty())
            {
                group.id = L"group";
            }

            const XmlNode* plugins = firstChild(groupNode, L"plugins");
            if (plugins != nullptr)
            {
                for (const XmlNode* plugin : childrenNamed(*plugins, L"plugin"))
                {
                    group.options.push_back(parseOption(*plugin, stepName, group.name, idCounts));
                }
                applyOrder(group.options, attribute(*plugins, L"order", L"Ascending"));
            }

            return group;
        }

        [[nodiscard]] std::vector<FomodStep> parseSteps(const XmlNode& config)
        {
            std::vector<FomodStep> steps;
            const XmlNode* installSteps = firstChild(config, L"installSteps");
            if (installSteps == nullptr)
            {
                return steps;
            }

            std::map<std::wstring, int> idCounts;
            for (const XmlNode* stepNode : childrenNamed(*installSteps, L"installStep"))
            {
                FomodStep step;
                step.name = trim(attribute(*stepNode, L"name", L"Install"));
                step.id = normalizeKeyPart(step.name);
                if (step.id.empty())
                {
                    step.id = L"step";
                }
                if (const XmlNode* visible = firstChild(*stepNode, L"visible"); visible != nullptr)
                {
                    step.visible = parseDependencyNode(*visible);
                }

                if (const XmlNode* groups = firstChild(*stepNode, L"optionalFileGroups"); groups != nullptr)
                {
                    for (const XmlNode* group : childrenNamed(*groups, L"group"))
                    {
                        step.groups.push_back(parseGroup(*group, step.name, idCounts));
                    }
                    applyOrder(step.groups, attribute(*groups, L"order", L"Ascending"));
                }

                steps.push_back(std::move(step));
            }
            applyOrder(steps, attribute(*installSteps, L"order", L"Ascending"));
            return steps;
        }

        [[nodiscard]] std::vector<FomodConditionalFilePattern> parseConditionalPatterns(const XmlNode& config)
        {
            std::vector<FomodConditionalFilePattern> patterns;
            const XmlNode* conditionalInstalls = firstChild(config, L"conditionalFileInstalls");
            if (conditionalInstalls == nullptr)
            {
                return patterns;
            }

            const XmlNode* patternsNode = firstChild(*conditionalInstalls, L"patterns");
            if (patternsNode == nullptr)
            {
                return patterns;
            }

            for (const XmlNode* pattern : childrenNamed(*patternsNode, L"pattern"))
            {
                const XmlNode* dependencies = firstChild(*pattern, L"dependencies");
                const XmlNode* files = firstChild(*pattern, L"files");
                if (dependencies == nullptr || files == nullptr)
                {
                    continue;
                }

                patterns.push_back(FomodConditionalFilePattern{
                    parseDependencyNode(*dependencies),
                    parseFileList(files)
                });
            }

            return patterns;
        }

        [[nodiscard]] std::filesystem::path safeRelativePath(std::wstring_view value, std::wstring_view fieldName)
        {
            std::wstring text = trim(std::wstring(value));
            std::replace(text.begin(), text.end(), L'/', std::filesystem::path::preferred_separator);
            const std::filesystem::path path(text);
            if (path.empty())
            {
                return {};
            }
            if (path == L".")
            {
                return {};
            }

            const PathSafetyResult validation = PathSafetyService().validateRelativePath(path);
            if (!validation.safe())
            {
                throw std::invalid_argument(
                    std::string("FOMOD ") + toUtf8(std::wstring(fieldName)) + " path is unsafe.");
            }

            return validation.normalizedRelativePath == L"." ? std::filesystem::path() : validation.normalizedRelativePath;
        }

        [[nodiscard]] bool pathStartsWithSegment(
            const std::filesystem::path& path,
            const std::filesystem::path& segment)
        {
            auto pathIt = path.begin();
            auto segmentIt = segment.begin();
            for (; segmentIt != segment.end(); ++segmentIt, ++pathIt)
            {
                if (pathIt == path.end() || !equalsIgnoreCase(pathIt->wstring(), segmentIt->wstring()))
                {
                    return false;
                }
            }

            return true;
        }

        [[nodiscard]] std::filesystem::path stripPrefix(
            const std::filesystem::path& path,
            const std::filesystem::path& prefix)
        {
            std::filesystem::path stripped;
            auto pathIt = path.begin();
            auto prefixIt = prefix.begin();
            for (; pathIt != path.end() && prefixIt != prefix.end(); ++pathIt, ++prefixIt)
            {
            }
            for (; pathIt != path.end(); ++pathIt)
            {
                stripped /= *pathIt;
            }

            return stripped.lexically_normal();
        }

        [[nodiscard]] std::vector<std::filesystem::path> normalizedDataFolderPaths(
            const std::vector<std::wstring>& gameDataFolders)
        {
            std::vector<std::filesystem::path> folders;
            for (const std::wstring& folder : gameDataFolders)
            {
                if (trim(folder).empty())
                {
                    continue;
                }

                std::filesystem::path parsed = safeRelativePath(folder, L"gameDataFolder");
                if (!parsed.empty())
                {
                    folders.push_back(std::move(parsed));
                }
            }

            return folders;
        }

        [[nodiscard]] std::vector<std::filesystem::path> gameDependencyCandidates(
            const std::filesystem::path& relativePath,
            const std::vector<std::filesystem::path>& dataFolders)
        {
            std::vector<std::filesystem::path> candidates{relativePath};
            for (const std::filesystem::path& dataFolder : dataFolders)
            {
                if (!pathStartsWithSegment(relativePath, dataFolder))
                {
                    candidates.push_back((dataFolder / relativePath).lexically_normal());
                }
            }

            return candidates;
        }

        [[nodiscard]] std::vector<std::filesystem::path> modDependencyCandidates(
            const std::filesystem::path& relativePath,
            const std::vector<std::filesystem::path>& dataFolders)
        {
            std::vector<std::filesystem::path> candidates{relativePath};
            for (const std::filesystem::path& dataFolder : dataFolders)
            {
                if (pathStartsWithSegment(relativePath, dataFolder))
                {
                    const std::filesystem::path stripped = stripPrefix(relativePath, dataFolder);
                    if (!stripped.empty() && stripped != L".")
                    {
                        candidates.push_back(stripped);
                    }
                }
            }

            return candidates;
        }

        [[nodiscard]] bool fileExistsInMods(
            const std::filesystem::path& modsDirectory,
            const std::vector<std::filesystem::path>& relativePaths)
        {
            if (modsDirectory.empty() || !std::filesystem::exists(modsDirectory))
            {
                return false;
            }

            for (const auto& entry : std::filesystem::directory_iterator(modsDirectory))
            {
                if (!entry.is_directory())
                {
                    continue;
                }

                for (const std::filesystem::path& relativePath : relativePaths)
                {
                    if (std::filesystem::exists(entry.path() / relativePath))
                    {
                        return true;
                    }
                }
            }

            return false;
        }

        [[nodiscard]] bool fileExistsInGame(
            const std::filesystem::path& gameDirectory,
            const std::vector<std::filesystem::path>& relativePaths)
        {
            if (gameDirectory.empty())
            {
                return false;
            }

            for (const std::filesystem::path& relativePath : relativePaths)
            {
                if (std::filesystem::exists(gameDirectory / relativePath))
                {
                    return true;
                }
            }

            return false;
        }

        [[nodiscard]] bool fileDependencyExists(const FomodDependencyNode& dependency, const FomodEnvironment& environment)
        {
            const std::filesystem::path relativePath = safeRelativePath(dependency.file, L"fileDependency");
            const std::vector<std::filesystem::path> dataFolders =
                normalizedDataFolderPaths(environment.gameDataFolders);
            return
                fileExistsInGame(environment.gameDirectory, gameDependencyCandidates(relativePath, dataFolders)) ||
                fileExistsInMods(environment.modsDirectory, modDependencyCandidates(relativePath, dataFolders));
        }

        [[nodiscard]] bool fileDependencySatisfied(const FomodDependencyNode& dependency, const FomodEnvironment& environment)
        {
            const bool exists = fileDependencyExists(dependency, environment);
            const std::wstring state = toLower(trim(dependency.state));
            if (state == L"missing")
            {
                return !exists;
            }

            return exists;
        }

        [[nodiscard]] bool evaluateDependency(
            const FomodDependencyNode& dependency,
            const std::map<std::wstring, std::wstring>& flags,
            const FomodEnvironment& environment)
        {
            if (dependency.kind == L"flag")
            {
                const auto match = flags.find(dependency.flag);
                return match != flags.end() && match->second == dependency.value;
            }

            if (dependency.kind == L"file")
            {
                return fileDependencySatisfied(dependency, environment);
            }

            if (dependency.kind == L"game" || dependency.kind == L"fomm")
            {
                return true;
            }

            const bool useOr = equalsIgnoreCase(dependency.op, L"Or");
            if (dependency.children.empty())
            {
                return true;
            }

            for (const FomodDependencyNode& child : dependency.children)
            {
                const bool satisfied = evaluateDependency(child, flags, environment);
                if (useOr && satisfied)
                {
                    return true;
                }
                if (!useOr && !satisfied)
                {
                    return false;
                }
            }

            return !useOr;
        }

        [[nodiscard]] std::wstring effectiveType(
            const FomodOption& option,
            const std::map<std::wstring, std::wstring>& flags,
            const FomodEnvironment& environment)
        {
            for (const FomodTypePattern& pattern : option.typePatterns)
            {
                if (evaluateDependency(pattern.dependencies, flags, environment))
                {
                    return pattern.type.empty() ? L"Optional" : pattern.type;
                }
            }

            return option.type.empty() ? L"Optional" : option.type;
        }

        void collectFileDependencyStates(
            const FomodDependencyNode& dependency,
            const FomodEnvironment& environment,
            std::map<std::wstring, FomodFileDependencyState>& states)
        {
            if (dependency.kind == L"file" && !trim(dependency.file).empty())
            {
                const std::filesystem::path relativePath = safeRelativePath(dependency.file, L"fileDependency");
                const std::wstring key = toLower(relativePath.wstring());
                states.try_emplace(key, FomodFileDependencyState{
                    relativePath.wstring(),
                    fileDependencyExists(dependency, environment)
                });
            }

            for (const FomodDependencyNode& child : dependency.children)
            {
                collectFileDependencyStates(child, environment, states);
            }
        }

        [[nodiscard]] std::vector<FomodFileDependencyState> collectFileDependencyStates(
            const FomodInstallerDescriptor& descriptor,
            const FomodEnvironment& environment)
        {
            std::map<std::wstring, FomodFileDependencyState> states;

            for (const FomodStep& step : descriptor.steps)
            {
                if (step.visible.has_value())
                {
                    collectFileDependencyStates(step.visible.value(), environment, states);
                }

                for (const FomodGroup& group : step.groups)
                {
                    for (const FomodOption& option : group.options)
                    {
                        for (const FomodTypePattern& pattern : option.typePatterns)
                        {
                            collectFileDependencyStates(pattern.dependencies, environment, states);
                        }
                    }
                }
            }

            for (const FomodConditionalFilePattern& pattern : descriptor.conditionalFilePatterns)
            {
                collectFileDependencyStates(pattern.dependencies, environment, states);
            }

            std::vector<FomodFileDependencyState> result;
            result.reserve(states.size());
            for (const auto& [_, state] : states)
            {
                result.push_back(state);
            }

            return result;
        }

        [[nodiscard]] bool isUsableType(std::wstring_view type)
        {
            return !equalsIgnoreCase(type, L"NotUsable");
        }

        [[nodiscard]] bool isRequiredType(std::wstring_view type)
        {
            return equalsIgnoreCase(type, L"Required");
        }

        [[nodiscard]] bool isRecommendedType(std::wstring_view type)
        {
            return equalsIgnoreCase(type, L"Recommended");
        }

        void addPlannedFiles(
            std::vector<PlannedFile>& plan,
            const std::vector<FomodFileEntry>& files,
            std::size_t& sequence)
        {
            for (const FomodFileEntry& file : files)
            {
                if (trim(file.source).empty())
                {
                    continue;
                }
                plan.push_back(PlannedFile{file, sequence++});
            }
        }

        void applyOptionFlags(
            const FomodOption& option,
            std::map<std::wstring, std::wstring>& flags)
        {
            for (const FomodConditionFlag& flag : option.flags)
            {
                if (!flag.name.empty())
                {
                    flags[flag.name] = flag.value;
                }
            }
        }

        [[nodiscard]] std::vector<const FomodOption*> selectedOptionsForGroup(
            const FomodGroup& group,
            const SelectedOptionSet& selected,
            const FomodEnvironment& environment,
            const std::map<std::wstring, std::wstring>& flags)
        {
            std::vector<const FomodOption*> options;
            for (const FomodOption& option : group.options)
            {
                const std::wstring type = effectiveType(option, flags, environment);
                if (!isUsableType(type))
                {
                    continue;
                }

                if (equalsIgnoreCase(group.type, L"SelectAll") ||
                    isRequiredType(type) ||
                    selected.requestedIds.contains(option.id))
                {
                    options.push_back(&option);
                }
            }

            if (options.empty() &&
                (equalsIgnoreCase(group.type, L"SelectExactlyOne") ||
                 equalsIgnoreCase(group.type, L"SelectAtLeastOne")))
            {
                for (const FomodOption& option : group.options)
                {
                    const std::wstring type = effectiveType(option, flags, environment);
                    if (isUsableType(type) && isRecommendedType(type))
                    {
                        options.push_back(&option);
                        break;
                    }
                }
            }

            return options;
        }

        void validateGroupSelection(
            const FomodGroup& group,
            const std::vector<const FomodOption*>& selected)
        {
            if (equalsIgnoreCase(group.type, L"SelectExactlyOne") && selected.size() != 1)
            {
                throw std::invalid_argument("FOMOD group requires exactly one option: " + toUtf8(group.name));
            }
            if (equalsIgnoreCase(group.type, L"SelectAtLeastOne") && selected.empty())
            {
                throw std::invalid_argument("FOMOD group requires at least one option: " + toUtf8(group.name));
            }
            if (equalsIgnoreCase(group.type, L"SelectAtMostOne") && selected.size() > 1)
            {
                throw std::invalid_argument("FOMOD group allows at most one option: " + toUtf8(group.name));
            }
        }

        [[nodiscard]] std::vector<PlannedFile> buildPlan(
            const FomodInstallerDescriptor& descriptor,
            const FomodEnvironment& environment,
            SelectedOptionSet& selected)
        {
            std::vector<PlannedFile> plan;
            std::size_t sequence = 0;
            addPlannedFiles(plan, descriptor.requiredFiles, sequence);

            for (const FomodStep& step : descriptor.steps)
            {
                if (step.visible.has_value() &&
                    !evaluateDependency(step.visible.value(), selected.flags, environment))
                {
                    continue;
                }

                for (const FomodGroup& group : step.groups)
                {
                    std::vector<const FomodOption*> selectedOptions =
                        selectedOptionsForGroup(group, selected, environment, selected.flags);
                    validateGroupSelection(group, selectedOptions);

                    std::set<std::wstring> selectedInGroup;
                    for (const FomodOption* option : selectedOptions)
                    {
                        selectedInGroup.insert(option->id);
                    }

                    for (const FomodOption& option : group.options)
                    {
                        const std::wstring type = effectiveType(option, selected.flags, environment);
                        const bool optionSelected = selectedInGroup.contains(option.id);
                        if (optionSelected)
                        {
                            selected.appliedIds.insert(option.id);
                            applyOptionFlags(option, selected.flags);
                        }

                        std::vector<FomodFileEntry> filesToAdd;
                        for (const FomodFileEntry& file : option.files)
                        {
                            if (optionSelected ||
                                file.alwaysInstall ||
                                (file.installIfUsable && isUsableType(type)))
                            {
                                filesToAdd.push_back(file);
                            }
                        }
                        addPlannedFiles(plan, filesToAdd, sequence);
                    }
                }
            }

            for (const FomodConditionalFilePattern& pattern : descriptor.conditionalFilePatterns)
            {
                if (evaluateDependency(pattern.dependencies, selected.flags, environment))
                {
                    addPlannedFiles(plan, pattern.files, sequence);
                }
            }

            std::stable_sort(plan.begin(), plan.end(), [](const PlannedFile& left, const PlannedFile& right)
            {
                if (left.file.priority != right.file.priority)
                {
                    return left.file.priority < right.file.priority;
                }

                return left.sequence < right.sequence;
            });

            return plan;
        }

        void copyFileEntry(
            const std::filesystem::path& packageRoot,
            const std::filesystem::path& destinationRoot,
            const FomodFileEntry& entry)
        {
            const PathSafetyService safety;
            const std::filesystem::path sourceRelative = safeRelativePath(entry.source, L"source");
            const std::filesystem::path sourcePath = packageRoot / sourceRelative;
            safety.validateContainedPath(packageRoot, sourcePath)
                .throwIfUnsafe("FOMOD source path is unsafe");
            if (entry.isFolder)
            {
                if (!std::filesystem::exists(sourcePath) || !std::filesystem::is_directory(sourcePath))
                {
                    throw std::runtime_error("FOMOD source folder is missing: " + toUtf8(entry.source));
                }

                const std::filesystem::path destinationRelative = safeRelativePath(entry.destination, L"destination");
                const std::filesystem::path destinationDirectory = destinationRelative.empty()
                    ? destinationRoot
                    : destinationRoot / destinationRelative;
                safety.validateWritePath(destinationRoot, destinationDirectory)
                    .throwIfUnsafe("FOMOD destination folder is unsafe");
                std::filesystem::create_directories(destinationDirectory);

                for (const auto& child : std::filesystem::recursive_directory_iterator(sourcePath))
                {
                    safety.validateContainedPath(sourcePath, child.path())
                        .throwIfUnsafe("FOMOD child source path is unsafe");
                    const std::filesystem::path relative = std::filesystem::relative(child.path(), sourcePath);
                    const std::filesystem::path target = destinationDirectory / relative;
                    if (child.is_directory())
                    {
                        safety.validateWritePath(destinationRoot, target)
                            .throwIfUnsafe("FOMOD child destination folder is unsafe");
                        std::filesystem::create_directories(target);
                        continue;
                    }
                    if (!child.is_regular_file())
                    {
                        continue;
                    }

                    std::filesystem::create_directories(target.parent_path());
                    std::error_code sizeError;
                    const std::uintmax_t bytes = child.file_size(sizeError);
                    safety.validateWritePath(
                        destinationRoot,
                        target,
                        PathSafetyWriteOptions{sizeError ? 0 : bytes, false})
                        .throwIfUnsafe("FOMOD child destination file is unsafe");
                    std::filesystem::copy_file(child.path(), target, std::filesystem::copy_options::overwrite_existing);
                }
                return;
            }

            if (!std::filesystem::exists(sourcePath) || !std::filesystem::is_regular_file(sourcePath))
            {
                throw std::runtime_error("FOMOD source file is missing: " + toUtf8(entry.source));
            }

            std::filesystem::path destinationRelative = safeRelativePath(entry.destination, L"destination");
            if (destinationRelative.empty())
            {
                destinationRelative = sourceRelative;
            }
            const std::filesystem::path target = destinationRoot / destinationRelative;
            std::error_code sizeError;
            const std::uintmax_t bytes = std::filesystem::file_size(sourcePath, sizeError);
            safety.validateWritePath(
                destinationRoot,
                target,
                PathSafetyWriteOptions{sizeError ? 0 : bytes, false})
                .throwIfUnsafe("FOMOD destination file is unsafe");
            std::filesystem::create_directories(target.parent_path());
            std::filesystem::copy_file(sourcePath, target, std::filesystem::copy_options::overwrite_existing);
        }

        [[nodiscard]] FomodInstallerDescriptor parseDescriptor(
            const std::filesystem::path& projectDirectory,
            const std::filesystem::path& packageRoot,
            const FomodPackageIdentity& identity)
        {
            const std::filesystem::path configPath = moduleConfigPath(packageRoot);
            if (configPath.empty())
            {
                return {};
            }

            XmlNode config = parseXmlFile(configPath);
            if (localName(config.name) != L"config")
            {
                throw std::runtime_error("FOMOD ModuleConfig.xml root element must be config.");
            }

            FomodInstallerDescriptor descriptor;
            descriptor.isFomod = true;
            descriptor.moduleName = childText(config, L"moduleName");
            if (descriptor.moduleName.empty())
            {
                descriptor.moduleName = identity.fallbackName.empty() ? L"FOMOD installer" : identity.fallbackName;
            }
            if (const XmlNode* moduleImage = firstChild(config, L"moduleImage"); moduleImage != nullptr)
            {
                descriptor.moduleImagePath = trim(attribute(*moduleImage, L"path"));
            }
            descriptor.requiredFiles = parseFileList(firstChild(config, L"requiredInstallFiles"));
            descriptor.steps = parseSteps(config);
            descriptor.conditionalFilePatterns = parseConditionalPatterns(config);

            const std::filesystem::path metadataPath = infoPath(packageRoot);
            if (!metadataPath.empty())
            {
                try
                {
                    XmlNode info = parseXmlFile(metadataPath);
                    descriptor.moduleId = childText(info, L"Id");
                    if (const XmlNode* version = firstChild(info, L"Version"); version != nullptr)
                    {
                        descriptor.moduleVersion = trim(attribute(*version, L"MachineVersion"));
                        if (descriptor.moduleVersion.empty())
                        {
                            descriptor.moduleVersion = trim(version->text);
                        }
                    }
                    if (std::wstring infoName = childText(info, L"Name"); !infoName.empty())
                    {
                        descriptor.moduleName = infoName;
                    }
                }
                catch (const std::exception&)
                {
                }
            }

            descriptor.memoryKey = makeMemoryKey(identity, descriptor.moduleId, descriptor.moduleName);
            for (const FomodMemoryEntry& entry : loadMemory(projectDirectory))
            {
                if (entry.key == descriptor.memoryKey)
                {
                    descriptor.hasPreviousSelection = !entry.selectedOptionIds.empty();
                    descriptor.previousSelectedOptionIds = entry.selectedOptionIds;
                    break;
                }
            }

            return descriptor;
        }
    }

    bool FomodInstallerService::hasXmlInstaller(const std::filesystem::path& packageDirectory)
    {
        return !packageRootWithFomod(packageDirectory).empty();
    }

    FomodInstallerDescriptor FomodInstallerService::analyze(
        const std::filesystem::path& projectDirectory,
        const std::filesystem::path& gameDirectory,
        const std::filesystem::path& modsDirectory,
        const std::filesystem::path& packageDirectory,
        const FomodPackageIdentity& identity,
        const std::vector<std::wstring>& gameDataFolders)
    {
        const std::filesystem::path packageRoot = packageRootWithFomod(packageDirectory);
        if (packageRoot.empty())
        {
            return {};
        }

        FomodInstallerDescriptor descriptor = parseDescriptor(projectDirectory, packageRoot, identity);
        descriptor.fileDependencyStates = collectFileDependencyStates(
            descriptor,
            FomodEnvironment{
                projectDirectory,
                gameDirectory,
                modsDirectory,
                gameDataFolders
            });
        return descriptor;
    }

    std::vector<std::wstring> FomodInstallerService::install(const FomodInstallContext& context)
    {
        const std::filesystem::path packageRoot = packageRootWithFomod(context.packageDirectory);
        if (packageRoot.empty())
        {
            throw std::invalid_argument("Download does not contain an XML FOMOD installer.");
        }

        FomodInstallerDescriptor descriptor = parseDescriptor(
            context.projectDirectory,
            packageRoot,
            context.identity);
        FomodEnvironment environment{
            context.projectDirectory,
            context.gameDirectory,
            context.modsDirectory,
            context.gameDataFolders
        };
        SelectedOptionSet selected;
        selected.requestedIds.insert(context.selectedOptionIds.begin(), context.selectedOptionIds.end());

        const std::vector<PlannedFile> plan = buildPlan(descriptor, environment, selected);
        PathSafetyService().validateDirectoryWriteRoot(context.destinationDirectory)
            .throwIfUnsafe("FOMOD destination root is unsafe");
        std::filesystem::create_directories(context.destinationDirectory);
        for (const PlannedFile& item : plan)
        {
            copyFileEntry(packageRoot, context.destinationDirectory, item.file);
        }

        return std::vector<std::wstring>(selected.appliedIds.begin(), selected.appliedIds.end());
    }

    void FomodInstallerService::rememberSelection(
        const std::filesystem::path& projectDirectory,
        const FomodInstallerDescriptor& descriptor,
        const std::vector<std::wstring>& selectedOptionIds)
    {
        saveRememberedSelection(projectDirectory, descriptor, selectedOptionIds);
    }
}
