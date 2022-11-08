/*
bellhopcxx / bellhopcuda - C++/CUDA port of BELLHOP underwater acoustics simulator
Copyright (C) 2021-2022 The Regents of the University of California
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
#include "run.hpp"

namespace bhc {

#define NUM_THREADS 256
#define LAUNCH_BOUNDS __launch_bounds__(NUM_THREADS, 1)

using GENCFG = CfgSel<@BHCGENRUN @, @BHCGENINFL @, @BHCGENSSP @>;

template<typename CFG, bool O3D, bool R3D> __global__ void LAUNCH_BOUNDS
FieldModesKernel(bhcParams<O3D, R3D> params, bhcOutputs<O3D, R3D> outputs);

template __global__ void LAUNCH_BOUNDS
FieldModesKernel<GENCFG, @BHCGENO3D @, @BHCGENR3D @>(
    bhcParams<@BHCGENO3D @, @BHCGENR3D @> params,
    bhcOutputs<@BHCGENO3D @, @BHCGENR3D @> outputs)
{
    for(int32_t job = blockIdx.x * blockDim.x + threadIdx.x; true;
        job += gridDim.x * blockDim.x) {
        RayInitInfo rinit;
        if(!GetJobIndices<@BHCGENO3D @>(rinit, job, params.Pos, params.Angles)) break;

        MainFieldModes<GENCFG, @BHCGENO3D @, @BHCGENR3D @>(
            rinit, outputs.uAllSources, params.Bdry, params.bdinfo, params.refl,
            params.ssp, params.Pos, params.Angles, params.freqinfo, params.Beam,
            params.beaminfo, outputs.eigen, outputs.arrinfo);
    }
}

template void RunFieldModesImpl<GENCFG, @BHCGENO3D @, @BHCGENR3D @>(
    bhcParams<@BHCGENO3D @, @BHCGENR3D @> &params,
    bhcOutputs<@BHCGENO3D @, @BHCGENR3D @> &outputs, uint32_t cores)
{
    IGNORE_UNUSED(cores);
    FieldModesKernel<GENCFG, @BHCGENO3D @, @BHCGENR3D @>
        <<<d_multiprocs, NUM_THREADS>>>(params, outputs);
    syncAndCheckKernelErrors("FieldModesKernel<@BHCGENRUN@, @BHCGENINFL@, @BHCGENSSP@, "
                             "@BHCGENO3D@, @BHCGENR3D@>");
}

} // namespace bhc
