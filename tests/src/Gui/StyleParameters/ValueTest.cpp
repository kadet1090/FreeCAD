// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <Gui/StyleParameters/Value.h>

using namespace Gui::StyleParameters;

TEST(ValueTest, BooleanIsHeldAndPrinted)
{
    const Value yes {true};
    const Value no {false};

    EXPECT_TRUE(yes.holds<bool>());
    EXPECT_TRUE(yes.get<bool>());
    EXPECT_EQ(yes.toString(), "true");
    EXPECT_EQ(no.toString(), "false");
}

TEST(ValueTest, BooleanIsDistinctFromNumeric)
{
    const std::optional<Value> boolean {Value {true}};
    const std::optional<Value> numeric {Value {Numeric {.value = 1, .unit = ""}}};

    EXPECT_EQ(valueAs<bool>(boolean), std::optional<bool> {true});
    EXPECT_EQ(valueAs<bool>(numeric), std::nullopt);
    EXPECT_EQ(valueAs<int>(boolean), std::nullopt);
}

TEST(ValueTest, StringLiteralStillConstructsString)
{
    // Guards the variant converting-constructor: const char* must not decay to bool.
    const Value value {"#ff0000"};

    EXPECT_TRUE(value.holds<std::string>());
}
