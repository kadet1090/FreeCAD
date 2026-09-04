// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2025 Kacper Donat <kacper@kadet.net>                     *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#include <gtest/gtest.h>

#include <Base/OkLch.h>

#include <Gui/StyleParameters/ColorEffect.h>
#include <Gui/StyleParameters/Parser.h>
#include <Gui/StyleParameters/ParameterManager.h>

using namespace Gui::StyleParameters;

class ColorEffectTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        auto source = std::make_unique<InMemoryParameterSource>(
            std::list<Parameter> {},
            ParameterSource::Metadata {.name = "Test Source"}
        );

        manager.addSource(source.get());
        sources.push_back(std::move(source));
    }

    Value evaluate(const std::string& expression)
    {
        Parser parser(expression);
        auto expr = parser.parse();
        return expr->evaluate({.manager = &manager, .context = {}});
    }

    Gui::StyleParameters::ParameterManager manager;
    std::vector<std::unique_ptr<ParameterSource>> sources;
};

// Parser round-trip: effect(lighten: 0.05) produces a ColorEffect tuple.
TEST_F(ColorEffectTest, ParserRoundTrip)
{
    const Value result = evaluate("effect(lighten: 0.05)");

    ASSERT_TRUE(result.holds<Tuple>());
    const Tuple& tuple = result.get<Tuple>();
    EXPECT_EQ(tuple.kind, TupleKind::ColorEffect);

    const ColorEffect effect(tuple);
    EXPECT_FLOAT_EQ(effect.lighten(), 0.05f);
    EXPECT_FLOAT_EQ(effect.darken(), 0.0f);
}

// Defaults: effect() with no args gives lighten=0.0 and darken=0.0.
TEST_F(ColorEffectTest, Defaults)
{
    const Value result = evaluate("effect()");

    ASSERT_TRUE(result.holds<Tuple>());
    const ColorEffect effect(result.get<Tuple>());
    EXPECT_FLOAT_EQ(effect.lighten(), 0.0f);
    EXPECT_FLOAT_EQ(effect.darken(), 0.0f);
}

// apply() on a dark base: result lightness ≈ base lightness + 0.1.
TEST_F(ColorEffectTest, ApplyLightenDarkBase)
{
    const Base::Color darkColor {0.1f, 0.1f, 0.1f, 1.0f};
    const float baseLightness = Base::toOkLch(darkColor).lightness;

    const ColorEffect effect(evaluate("effect(lighten: 0.1)").get<Tuple>());
    const Base::Color result = effect.apply(darkColor);
    const float resultLightness = Base::toOkLch(result).lightness;

    EXPECT_NEAR(resultLightness, baseLightness + 0.1f, 0.005f);
}

// apply() produces the same absolute lightness delta on a light base — not percent-of-remaining.
// This verifies the key correctness property vs. the old overlay approach.
TEST_F(ColorEffectTest, ApplyProducesAbsoluteDeltaOnLightBase)
{
    const Base::Color lightColor {0.8f, 0.8f, 0.8f, 1.0f};
    const float baseLightness = Base::toOkLch(lightColor).lightness;

    const ColorEffect effect(evaluate("effect(lighten: 0.05)").get<Tuple>());
    const Base::Color result = effect.apply(lightColor);
    const float resultLightness = Base::toOkLch(result).lightness;

    // The shift should be the same absolute 0.05, not proportional to remaining headroom.
    EXPECT_NEAR(resultLightness, baseLightness + 0.05f, 0.005f);
}

// apply() clamps lightness to 1.0 when lighten exceeds remaining headroom.
TEST_F(ColorEffectTest, ApplyClampLighten)
{
    const Base::Color anyColor {0.5f, 0.5f, 0.5f, 1.0f};

    const ColorEffect effect(evaluate("effect(lighten: 2.0)").get<Tuple>());
    const Base::Color result = effect.apply(anyColor);
    const float resultLightness = Base::toOkLch(result).lightness;

    EXPECT_NEAR(resultLightness, 1.0f, 0.005f);
}

// apply() preserves the alpha channel.
TEST_F(ColorEffectTest, ApplyPreservesAlpha)
{
    const Base::Color colorWithAlpha {0.5f, 0.5f, 0.5f, 0.7f};

    const ColorEffect effect(evaluate("effect(lighten: 0.05)").get<Tuple>());
    const Base::Color result = effect.apply(colorWithAlpha);

    EXPECT_NEAR(result.a, 0.7f, 0.005f);
}

// darken() reduces lightness by the specified amount.
TEST_F(ColorEffectTest, ApplyDarken)
{
    const Base::Color midColor {0.5f, 0.5f, 0.5f, 1.0f};
    const float baseLightness = Base::toOkLch(midColor).lightness;

    const ColorEffect effect(evaluate("effect(darken: 0.08)").get<Tuple>());
    const Base::Color result = effect.apply(midColor);
    const float resultLightness = Base::toOkLch(result).lightness;

    EXPECT_NEAR(resultLightness, baseLightness - 0.08f, 0.005f);
}
