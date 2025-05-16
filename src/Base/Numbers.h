// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2025 Werner Mayer <wmayer[at]users.sourceforge.net>     *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 *                                                                         *
 **************************************************************************/


#ifndef BASE_NUMBERS_H
#define BASE_NUMBERS_H

#include <limits>
#include <type_traits>

// NOLINTBEGIN
#ifndef FLOAT_MAX
#define FLOAT_MAX 3.402823466E+38F
#endif
// NOLINTEND

// clang-format off
namespace Base
{

namespace numbers
{

template<typename float_type>
  using _Enable_if_floating
    = std::enable_if_t<std::is_floating_point_v<float_type>, float_type>;

/// e
template<typename float_type>
  inline constexpr float_type e_v
    = _Enable_if_floating<float_type>(2.718281828459045235360287471352662498L);

/// log_2 e
template<typename float_type>
  inline constexpr float_type log2e_v
    = _Enable_if_floating<float_type>(1.442695040888963407359924681001892137L);

/// log_10 e
template<typename float_type>
  inline constexpr float_type log10e_v
    = _Enable_if_floating<float_type>(0.434294481903251827651128918916605082L);

/// pi
template<typename float_type>
  inline constexpr float_type pi_v
    = _Enable_if_floating<float_type>(3.141592653589793238462643383279502884L);

/// 1/pi
template<typename float_type>
  inline constexpr float_type inv_pi_v
    = _Enable_if_floating<float_type>(0.318309886183790671537767526745028724L);

/// 1/sqrt(pi)
template<typename float_type>
  inline constexpr float_type inv_sqrtpi_v
    = _Enable_if_floating<float_type>(0.564189583547756286948079451560772586L);

/// log_e 2
template<typename float_type>
  inline constexpr float_type ln2_v
    = _Enable_if_floating<float_type>(0.693147180559945309417232121458176568L);

/// log_e 10
template<typename float_type>
  inline constexpr float_type ln10_v
    = _Enable_if_floating<float_type>(2.302585092994045684017991454684364208L);

/// sqrt(2)
template<typename float_type>
  inline constexpr float_type sqrt2_v
    = _Enable_if_floating<float_type>(1.414213562373095048801688724209698079L);

/// sqrt(3)
template<typename _Tp>
  inline constexpr _Tp sqrt3_v
    = _Enable_if_floating<_Tp>(1.732050807568877293527446341505872367L);

/// 1/sqrt(3)
template<typename float_type>
  inline constexpr float_type inv_sqrt3_v
    = _Enable_if_floating<float_type>(0.577350269189625764509148780501957456L);

/// The Euler-Mascheroni constant
template<typename float_type>
  inline constexpr float_type egamma_v
    = _Enable_if_floating<float_type>(0.577215664901532860606512090082402431L);

/// The golden ratio, (1+sqrt(5))/2
template<typename float_type>
  inline constexpr float_type phi_v
    = _Enable_if_floating<float_type>(1.618033988749894848204586834365638118L);

inline constexpr double e = e_v<double>;
inline constexpr double log2e = log2e_v<double>;
inline constexpr double log10e = log10e_v<double>;
inline constexpr double pi = pi_v<double>;
inline constexpr double inv_pi = inv_pi_v<double>;
inline constexpr double inv_sqrtpi = inv_sqrtpi_v<double>;
inline constexpr double ln2 = ln2_v<double>;
inline constexpr double ln10 = ln10_v<double>;
inline constexpr double sqrt2 = sqrt2_v<double>;
inline constexpr double sqrt3 = sqrt3_v<double>;
inline constexpr double inv_sqrt3 = inv_sqrt3_v<double>;
inline constexpr double egamma = egamma_v<double>;
inline constexpr double phi = phi_v<double>;

}  // namespace numbers

template<class numT>
struct float_traits
{
};

template<>
struct float_traits<float>
{
    using float_type = float;
    [[nodiscard]] static constexpr float_type pi()
    {
        return numbers::pi_v<float_type>;
    }
    [[nodiscard]] static constexpr float_type epsilon()
    {
        return std::numeric_limits<float_type>::epsilon();
    }
    [[nodiscard]] static constexpr float_type maximum()
    {
        return std::numeric_limits<float_type>::max();
    }
};

template<>
struct float_traits<double>
{
    using float_type = double;
    [[nodiscard]] static constexpr float_type pi()
    {
        return numbers::pi_v<float_type>;
    }
    [[nodiscard]] static constexpr float_type epsilon()
    {
        return std::numeric_limits<float_type>::epsilon();
    }
    [[nodiscard]] static constexpr float_type maximum()
    {
        return std::numeric_limits<float_type>::max();
    }
};

}  // namespace Base
// clang-format on

#endif  // BASE_NUMBERS_H
