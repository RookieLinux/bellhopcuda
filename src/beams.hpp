/*
bellhopcxx / bellhopcuda - C++/CUDA port of BELLHOP underwater acoustics simulator
Copyright (C) 2021-2023 The Regents of the University of California
c/o Jules Jaffe team at SIO / UCSD, jjaffe@ucsd.edu
Based on BELLHOP, which is Copyright (C) 1983-2020 Michael B. Porter

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/
#pragma once
#include "common.hpp"
#include "runtype.hpp"

namespace bhc {

template<bool O3D> inline HOST_DEVICE VEC23<O3D> BeamBoxCenter(const VEC23<O3D> &xs)
{
    VEC23<O3D> ret = xs;
    // box is centered at z=0
    DEP(ret) = RL(0.0);
    return ret;
}

template<bool O3D, int DIM> inline HOST_DEVICE bool IsOutsideBeamBoxDim(
    const VEC23<O3D> &x, const BeamStructure<O3D> *Beam, const VEC23<O3D> &xs)
{
    static_assert(DIM >= 0 && DIM <= ZDIM<O3D>(), "Invalid use of IsOutsideBoxDim!");
    // LP: In 2D, source range is always 0.
    return STD::abs(x[DIM] - BeamBoxCenter<O3D>(xs)[DIM]) >= Beam->Box[DIM];
}

} // namespace bhc
