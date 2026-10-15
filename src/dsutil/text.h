/*
 * (C) 2003-2006 Gabest
 * (C) 2006-2013 see Authors.txt
 *
 * This file is part of MPC-HC.
 *
 * MPC-HC is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-HC is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include <atlcoll.h>

template<class T>
T& FastTrimRight(T& str)
{
    if (!str.IsEmpty()) {
        T::PCXSTR szStart = str;
        T::PCXSTR szEnd   = szStart + str.GetLength() - 1;
        T::PCXSTR szCur   = szEnd;
        for (; szCur >= szStart; szCur--) {
            if (!T::StrTraits::IsSpace(*szCur)) {
                break;
            }
        }

        if (szCur != szEnd) {
            str.Truncate(int(szCur - szStart + 1));
        }
    }

    return str;
}

template<class T>
T& FastTrim(T& str)
{
    return FastTrimRight(str).TrimLeft();
}
