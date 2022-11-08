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

#include <thread>
#include <vector>

namespace bhc {

using GENCFG = CfgSel<@BHCGENRUN @, @BHCGENINFL @, @BHCGENSSP @>;

template void FieldModesWorker<GENCFG, @BHCGENO3D @, @BHCGENR3D @>(
    bhcParams<@BHCGENO3D @, @BHCGENR3D @> &params,
    bhcOutputs<@BHCGENO3D @, @BHCGENR3D @> &outputs)
{
    try {
        while(true) {
            int32_t job = sharedJobID++;
            RayInitInfo rinit;
            if(!GetJobIndices<@BHCGENO3D @>(rinit, job, params.Pos, params.Angles)) break;

            MainFieldModes<GENCFG, @BHCGENO3D @, @BHCGENR3D @>(
                rinit, outputs.uAllSources, params.Bdry, params.bdinfo, params.refl,
                params.ssp, params.Pos, params.Angles, params.freqinfo, params.Beam,
                params.beaminfo, outputs.eigen, outputs.arrinfo);
        }
    } catch(const std::exception &e) {
        std::lock_guard<std::mutex> lock(exceptionMutex);
        exceptionStr += std::string(e.what()) + "\n";
    }
}

template void RunFieldModesImpl<GENCFG, @BHCGENO3D @, @BHCGENR3D @>(
    bhcParams<@BHCGENO3D @, @BHCGENR3D @> &params,
    bhcOutputs<@BHCGENO3D @, @BHCGENR3D @> &outputs, uint32_t cores)
{
    std::vector<std::thread> threads;
    for(uint32_t i = 0; i < cores; ++i)
        threads.push_back(std::thread(
            FieldModesWorker<@BHCGENO3D @, @BHCGENR3D @>, std::ref(params),
            std::ref(outputs)));
    for(uint32_t i = 0; i < cores; ++i) threads[i].join();
}

} // namespace bhc
