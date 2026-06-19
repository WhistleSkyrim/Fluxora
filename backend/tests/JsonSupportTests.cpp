#include "FluxoraCore/Support/JsonReader.hpp"
#include "FluxoraCore/Support/JsonWriter.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

namespace fluxora::tests
{
    TEST(JsonSupportTests, WriterOutputRoundTripsThroughReader)
    {
        JsonWriter writer;
        writer.beginObject();
        writer.field(L"name", L"Fluxora \"Core\"");
        writer.field(L"enabled", true);
        writer.field(L"count", 3);
        writer.key(L"paths").beginArray();
        writer.value(L"mods");
        writer.value(L"profiles\\Default");
        writer.endArray();
        writer.key(L"metadata").beginObject();
        writer.key(L"optional").nullValue();
        writer.field(L"line", L"first\nsecond");
        writer.endObject();
        writer.endObject();

        const JsonValue root = JsonReader::parse(writer.str());

        ASSERT_TRUE(root.isObject());
        ASSERT_NE(root.find(L"name"), nullptr);
        EXPECT_EQ(root.find(L"name")->asString(), L"Fluxora \"Core\"");
        ASSERT_NE(root.find(L"enabled"), nullptr);
        EXPECT_TRUE(root.find(L"enabled")->asBoolean());
        ASSERT_NE(root.find(L"count"), nullptr);
        EXPECT_EQ(root.find(L"count")->asNumber(), L"3");

        const JsonValue* paths = root.find(L"paths");
        ASSERT_NE(paths, nullptr);
        ASSERT_TRUE(paths->isArray());
        ASSERT_EQ(paths->asArray().size(), 2U);
        EXPECT_EQ(paths->asArray()[1].asString(), L"profiles\\Default");

        const JsonValue* metadata = root.find(L"metadata");
        ASSERT_NE(metadata, nullptr);
        ASSERT_TRUE(metadata->isObject());
        ASSERT_NE(metadata->find(L"optional"), nullptr);
        EXPECT_TRUE(metadata->find(L"optional")->isNull());
        ASSERT_NE(metadata->find(L"line"), nullptr);
        EXPECT_EQ(metadata->find(L"line")->asString(), L"first\nsecond");
    }

    TEST(JsonSupportTests, ReaderRejectsMalformedJson)
    {
        EXPECT_THROW((void)JsonReader::parse(L"{\"name\":}"), std::runtime_error);
        EXPECT_THROW((void)JsonReader::parse(L"[1,]"), std::runtime_error);
        EXPECT_THROW((void)JsonReader::parse(L"\"unterminated"), std::runtime_error);
    }
}
