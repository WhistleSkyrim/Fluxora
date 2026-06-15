#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fluxora
{
    // Minimal dependency-free JSON writer.
    //
    // The core has no third-party JSON dependency, so template data that crosses
    // the native boundary is serialized with this tiny builder. It only emits the
    // shapes the core produces (objects, arrays, strings, booleans) and keeps the
    // comma/structure bookkeeping internal so call sites stay declarative.
    class JsonWriter final
    {
    public:
        JsonWriter& beginObject()
        {
            prefix();
            out_.push_back(L'{');
            first_.push_back(true);
            return *this;
        }

        JsonWriter& endObject()
        {
            out_.push_back(L'}');
            if (!first_.empty())
            {
                first_.pop_back();
            }
            return *this;
        }

        JsonWriter& beginArray()
        {
            prefix();
            out_.push_back(L'[');
            first_.push_back(true);
            return *this;
        }

        JsonWriter& endArray()
        {
            out_.push_back(L']');
            if (!first_.empty())
            {
                first_.pop_back();
            }
            return *this;
        }

        JsonWriter& key(std::wstring_view name)
        {
            prefix();
            writeString(name);
            out_.push_back(L':');
            pendingValue_ = true;
            return *this;
        }

        JsonWriter& value(std::wstring_view text)
        {
            prefix();
            writeString(text);
            return *this;
        }

        // Explicit pointer overload so string literals (const wchar_t[]) route to
        // the string path; without it they would bind to value(bool) because
        // pointer-to-bool is a better conversion than the wstring_view one.
        JsonWriter& value(const wchar_t* text)
        {
            return value(std::wstring_view(text));
        }

        JsonWriter& value(bool flag)
        {
            prefix();
            out_.append(flag ? L"true" : L"false");
            return *this;
        }

        JsonWriter& value(int number)
        {
            prefix();
            out_.append(std::to_wstring(number));
            return *this;
        }

        JsonWriter& value(std::uintmax_t number)
        {
            prefix();
            out_.append(std::to_wstring(number));
            return *this;
        }

        JsonWriter& numberValue(std::wstring_view number)
        {
            prefix();
            out_.append(number);
            return *this;
        }

        JsonWriter& nullValue()
        {
            prefix();
            out_.append(L"null");
            return *this;
        }

        // Convenience: a full key/value pair on an object.
        JsonWriter& field(std::wstring_view name, std::wstring_view text)
        {
            return key(name).value(text);
        }

        JsonWriter& field(std::wstring_view name, const wchar_t* text)
        {
            return key(name).value(std::wstring_view(text));
        }

        JsonWriter& field(std::wstring_view name, bool flag)
        {
            return key(name).value(flag);
        }

        JsonWriter& field(std::wstring_view name, int number)
        {
            return key(name).value(number);
        }

        JsonWriter& field(std::wstring_view name, std::uintmax_t number)
        {
            return key(name).value(number);
        }

        JsonWriter& stringArray(std::wstring_view name, const std::vector<std::wstring>& items)
        {
            key(name).beginArray();
            for (const auto& item : items)
            {
                value(item);
            }
            endArray();
            return *this;
        }

        [[nodiscard]] const std::wstring& str() const noexcept
        {
            return out_;
        }

    private:
        void prefix()
        {
            if (pendingValue_)
            {
                pendingValue_ = false;
                return;
            }

            if (!first_.empty())
            {
                if (!first_.back())
                {
                    out_.push_back(L',');
                }
                first_.back() = false;
            }
        }

        void writeString(std::wstring_view text)
        {
            out_.push_back(L'"');
            for (wchar_t character : text)
            {
                switch (character)
                {
                case L'"':
                    out_.append(L"\\\"");
                    break;
                case L'\\':
                    out_.append(L"\\\\");
                    break;
                case L'\b':
                    out_.append(L"\\b");
                    break;
                case L'\f':
                    out_.append(L"\\f");
                    break;
                case L'\n':
                    out_.append(L"\\n");
                    break;
                case L'\r':
                    out_.append(L"\\r");
                    break;
                case L'\t':
                    out_.append(L"\\t");
                    break;
                default:
                    if (character < 0x20)
                    {
                        appendUnicodeEscape(character);
                    }
                    else
                    {
                        out_.push_back(character);
                    }
                    break;
                }
            }
            out_.push_back(L'"');
        }

        void appendUnicodeEscape(wchar_t character)
        {
            constexpr wchar_t digits[] = L"0123456789abcdef";
            out_.append(L"\\u00");
            out_.push_back(digits[(character >> 4) & 0xF]);
            out_.push_back(digits[character & 0xF]);
        }

        std::wstring out_;
        std::vector<bool> first_;
        bool pendingValue_{false};
    };
}
