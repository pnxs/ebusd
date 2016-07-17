#include "gtest/gtest.h"
#include "symbol.h"

TEST(TestSymbolString, testEscaped)
{
    SymbolString sstr(true);

    auto result = sstr.parseHex("10feb5050427a915aa", false);
    ASSERT_EQ(result, RESULT_OK);

    ASSERT_EQ(sstr.getDataStr(false, false), "10feb5050427a90015a90177");

    ASSERT_EQ(sstr.getCRC(), 0x77);

    ASSERT_EQ(sstr.getDataStr(true, false), "10feb5050427a915aa77");

}

TEST(TestSymbolString, testUnescaped)
{
    SymbolString sstr(false);

    auto result = sstr.parseHex("10feb5050427a90015a90177", true);
    ASSERT_EQ(result, RESULT_OK);

    ASSERT_EQ(sstr.getDataStr(true, false),"10feb5050427a915aa77");
}