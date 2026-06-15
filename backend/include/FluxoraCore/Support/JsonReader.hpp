#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fluxora
{
    class JsonValue final
    {
    public:
        enum class Type
        {
            Null,
            String,
            Number,
            Boolean,
            Object,
            Array
        };

        using Object = std::map<std::wstring, JsonValue>;
        using Array = std::vector<JsonValue>;

        static JsonValue null()
        {
            return JsonValue(Type::Null);
        }

        static JsonValue string(std::wstring value)
        {
            JsonValue json(Type::String);
            json.string_ = std::move(value);
            return json;
        }

        static JsonValue boolean(bool value)
        {
            JsonValue json(Type::Boolean);
            json.boolean_ = value;
            return json;
        }

        static JsonValue number(std::wstring value)
        {
            JsonValue json(Type::Number);
            json.number_ = std::move(value);
            return json;
        }

        static JsonValue object(Object value)
        {
            JsonValue json(Type::Object);
            json.object_ = std::move(value);
            return json;
        }

        static JsonValue array(Array value)
        {
            JsonValue json(Type::Array);
            json.array_ = std::move(value);
            return json;
        }

        [[nodiscard]] Type type() const noexcept
        {
            return type_;
        }

        [[nodiscard]] bool isNull() const noexcept
        {
            return type_ == Type::Null;
        }

        [[nodiscard]] bool isString() const noexcept
        {
            return type_ == Type::String;
        }

        [[nodiscard]] bool isNumber() const noexcept
        {
            return type_ == Type::Number;
        }

        [[nodiscard]] bool isObject() const noexcept
        {
            return type_ == Type::Object;
        }

        [[nodiscard]] bool isArray() const noexcept
        {
            return type_ == Type::Array;
        }

        [[nodiscard]] const std::wstring& asString() const
        {
            if (!isString())
            {
                throw std::runtime_error("JSON value is not a string.");
            }

            return string_;
        }

        [[nodiscard]] const std::wstring& asNumber() const
        {
            if (!isNumber())
            {
                throw std::runtime_error("JSON value is not a number.");
            }

            return number_;
        }

        [[nodiscard]] bool asBoolean() const
        {
            if (type_ != Type::Boolean)
            {
                throw std::runtime_error("JSON value is not a boolean.");
            }

            return boolean_;
        }

        [[nodiscard]] const Object& asObject() const
        {
            if (!isObject())
            {
                throw std::runtime_error("JSON value is not an object.");
            }

            return object_;
        }

        [[nodiscard]] const Array& asArray() const
        {
            if (!isArray())
            {
                throw std::runtime_error("JSON value is not an array.");
            }

            return array_;
        }

        [[nodiscard]] const JsonValue* find(std::wstring_view key) const
        {
            if (!isObject())
            {
                return nullptr;
            }

            const auto match = object_.find(std::wstring(key));
            return match == object_.end() ? nullptr : &match->second;
        }

    private:
        explicit JsonValue(Type type)
            : type_(type)
        {
        }

        Type type_;
        std::wstring string_;
        std::wstring number_;
        bool boolean_{false};
        Object object_;
        Array array_;
    };

    class JsonReader final
    {
    public:
        static JsonValue parse(std::wstring_view text)
        {
            JsonReader reader(text);
            JsonValue value = reader.parseValue();
            reader.skipWhitespace();
            if (!reader.isAtEnd())
            {
                throw std::runtime_error("Unexpected trailing JSON content.");
            }

            return value;
        }

    private:
        explicit JsonReader(std::wstring_view text) noexcept
            : text_(text)
        {
        }

        JsonValue parseValue()
        {
            skipWhitespace();
            if (isAtEnd())
            {
                throw std::runtime_error("Unexpected end of JSON.");
            }

            const wchar_t character = peek();
            if (character == L'{')
            {
                return parseObject();
            }
            if (character == L'[')
            {
                return parseArray();
            }
            if (character == L'"')
            {
                return JsonValue::string(parseString());
            }
            if (character == L'-' || (character >= L'0' && character <= L'9'))
            {
                return JsonValue::number(parseNumber());
            }
            if (matchLiteral(L"true"))
            {
                return JsonValue::boolean(true);
            }
            if (matchLiteral(L"false"))
            {
                return JsonValue::boolean(false);
            }
            if (matchLiteral(L"null"))
            {
                return JsonValue::null();
            }

            throw std::runtime_error("Unsupported JSON value.");
        }

        JsonValue parseObject()
        {
            expect(L'{');
            skipWhitespace();

            JsonValue::Object object;
            if (consume(L'}'))
            {
                return JsonValue::object(std::move(object));
            }

            while (true)
            {
                skipWhitespace();
                std::wstring key = parseString();
                skipWhitespace();
                expect(L':');
                object.emplace(std::move(key), parseValue());
                skipWhitespace();

                if (consume(L'}'))
                {
                    break;
                }

                expect(L',');
            }

            return JsonValue::object(std::move(object));
        }

        JsonValue parseArray()
        {
            expect(L'[');
            skipWhitespace();

            JsonValue::Array array;
            if (consume(L']'))
            {
                return JsonValue::array(std::move(array));
            }

            while (true)
            {
                array.push_back(parseValue());
                skipWhitespace();

                if (consume(L']'))
                {
                    break;
                }

                expect(L',');
            }

            return JsonValue::array(std::move(array));
        }

        std::wstring parseString()
        {
            expect(L'"');

            std::wstring value;
            while (!isAtEnd())
            {
                const wchar_t character = advance();
                if (character == L'"')
                {
                    return value;
                }

                if (character != L'\\')
                {
                    if (character < 0x20)
                    {
                        throw std::runtime_error("Control character in JSON string.");
                    }

                    value.push_back(character);
                    continue;
                }

                if (isAtEnd())
                {
                    throw std::runtime_error("Unfinished JSON escape sequence.");
                }

                const wchar_t escape = advance();
                switch (escape)
                {
                case L'"':
                case L'\\':
                case L'/':
                    value.push_back(escape);
                    break;
                case L'b':
                    value.push_back(L'\b');
                    break;
                case L'f':
                    value.push_back(L'\f');
                    break;
                case L'n':
                    value.push_back(L'\n');
                    break;
                case L'r':
                    value.push_back(L'\r');
                    break;
                case L't':
                    value.push_back(L'\t');
                    break;
                case L'u':
                    value.push_back(parseUnicodeEscape());
                    break;
                default:
                    throw std::runtime_error("Unsupported JSON escape sequence.");
                }
            }

            throw std::runtime_error("Unterminated JSON string.");
        }

        std::wstring parseNumber()
        {
            const std::size_t start = position_;

            consume(L'-');
            if (isAtEnd())
            {
                throw std::runtime_error("Invalid JSON number.");
            }

            if (consume(L'0'))
            {
                // JSON allows exactly one leading zero before a fraction/exponent.
            }
            else
            {
                if (peek() < L'1' || peek() > L'9')
                {
                    throw std::runtime_error("Invalid JSON number.");
                }

                while (!isAtEnd() && peek() >= L'0' && peek() <= L'9')
                {
                    ++position_;
                }
            }

            if (!isAtEnd() && peek() == L'.')
            {
                ++position_;
                if (isAtEnd() || peek() < L'0' || peek() > L'9')
                {
                    throw std::runtime_error("Invalid JSON number.");
                }

                while (!isAtEnd() && peek() >= L'0' && peek() <= L'9')
                {
                    ++position_;
                }
            }

            if (!isAtEnd() && (peek() == L'e' || peek() == L'E'))
            {
                ++position_;
                if (!isAtEnd() && (peek() == L'+' || peek() == L'-'))
                {
                    ++position_;
                }

                if (isAtEnd() || peek() < L'0' || peek() > L'9')
                {
                    throw std::runtime_error("Invalid JSON number.");
                }

                while (!isAtEnd() && peek() >= L'0' && peek() <= L'9')
                {
                    ++position_;
                }
            }

            return std::wstring(text_.substr(start, position_ - start));
        }

        wchar_t parseUnicodeEscape()
        {
            unsigned int codePoint = 0;
            for (int index = 0; index < 4; ++index)
            {
                if (isAtEnd())
                {
                    throw std::runtime_error("Unfinished JSON unicode escape.");
                }

                const wchar_t digit = advance();
                codePoint <<= 4;
                if (digit >= L'0' && digit <= L'9')
                {
                    codePoint += static_cast<unsigned int>(digit - L'0');
                }
                else if (digit >= L'a' && digit <= L'f')
                {
                    codePoint += static_cast<unsigned int>(digit - L'a' + 10);
                }
                else if (digit >= L'A' && digit <= L'F')
                {
                    codePoint += static_cast<unsigned int>(digit - L'A' + 10);
                }
                else
                {
                    throw std::runtime_error("Invalid JSON unicode escape.");
                }
            }

            return static_cast<wchar_t>(codePoint);
        }

        bool matchLiteral(std::wstring_view literal)
        {
            if (text_.substr(position_, literal.size()) != literal)
            {
                return false;
            }

            position_ += literal.size();
            return true;
        }

        void skipWhitespace() noexcept
        {
            while (!isAtEnd())
            {
                const wchar_t character = peek();
                if (character != L' ' && character != L'\n' && character != L'\r' && character != L'\t')
                {
                    return;
                }

                ++position_;
            }
        }

        void expect(wchar_t expected)
        {
            if (!consume(expected))
            {
                throw std::runtime_error("Unexpected JSON token.");
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

        wchar_t advance() noexcept
        {
            return text_[position_++];
        }

        [[nodiscard]] bool isAtEnd() const noexcept
        {
            return position_ >= text_.size();
        }

        std::wstring_view text_;
        std::size_t position_{0};
    };
}
