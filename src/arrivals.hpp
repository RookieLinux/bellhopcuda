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
#include "jobs.hpp"

namespace bhc {

/**
 * Is this the second step of a pair (on the same ray)?
 * If so, we want to combine the arrivals to conserve space.
 * (test this by seeing if the arrival time is close to the previous one)
 * (also need that the phase is about the same to make sure surface and direct paths are
 * not joined)
 */
template<bool R3D> HOST_DEVICE inline bool IsSecondStepOfPair(
    real omega, real Phase, cpx delay, Arrival *baseArr, int32_t Nt)
{
    // arrivals with essentially the same phase are grouped into one
    const float PhaseTol = /*R3D ? FL(0.5) :*/ FL(0.05); // LP: 0.5 for 2D removed by mbp
                                                         // in 2022 revisions.
    return Nt >= 1 && omega * STD::abs(delay - Cpxf2Cpx(baseArr[Nt - 1].delay)) < PhaseTol
        && STD::abs(baseArr[Nt - 1].Phase - Phase) < PhaseTol;
}

/**
 * Adds the amplitude and delay for an ARRival into a matrix of same.
 * Extra logic included to keep only the strongest arrivals.
 */
template<bool R3D> HOST_DEVICE inline void AddArr(
    int32_t itheta, int32_t id, int32_t ir, real Amp, real omega, real Phase, cpx delay,
    const RayInitInfo &rinit, real RcvrDeclAngle, real RcvrAzimAngle, int32_t NumTopBnc,
    int32_t NumBotBnc, const ArrInfo *arrinfo, const Position *Pos)
{
    size_t base      = GetFieldAddr(rinit.isx, rinit.isy, rinit.isz, itheta, id, ir, Pos);
    Arrival *baseArr = &arrinfo->Arr[base * arrinfo->MaxNArr];
    int32_t *baseNArr = &arrinfo->NArr[base];
    int32_t Nt;

    if(arrinfo->AllowMerging) {
        // LP: BUG: This only checks the last arrival, whereas the first step of the
        // pair could have been placed in previous slots. See the Fortran version readme.

        Nt = *baseNArr; // # of arrivals

        if(!IsSecondStepOfPair<R3D>(omega, Phase, delay, baseArr, Nt)) {
            int32_t iArr;
            if(Nt >= arrinfo->MaxNArr) { // space not available to add an arrival?
                // replace weakest arrival
                iArr         = -1;
                real weakest = Amp;
                for(Nt = 0; Nt < arrinfo->MaxNArr; ++Nt) {
                    if(baseArr[Nt].a < weakest) {
                        weakest = baseArr[Nt].a;
                        iArr    = Nt;
                    }
                }
                if(iArr < 0) return; // LP: current arrival is weaker than all stored
            } else {
                iArr      = Nt;
                *baseNArr = Nt + 1; // # of arrivals
            }
            baseArr[iArr].a            = (float)Amp;                // amplitude
            baseArr[iArr].Phase        = (float)Phase;              // phase
            baseArr[iArr].delay        = Cpx2Cpxf(delay);           // delay time
            baseArr[iArr].SrcDeclAngle = (float)rinit.SrcDeclAngle; // launch angle from
                                                                    // source
            baseArr[iArr].SrcAzimAngle = (float)rinit.SrcAzimAngle; // launch angle from
                                                                    // source
            baseArr[iArr].RcvrDeclAngle = (float)RcvrDeclAngle;     // angle ray reaches
                                                                    // receiver
            baseArr[iArr].RcvrAzimAngle = (float)RcvrAzimAngle;     // angle ray reaches
                                                                    // receiver
            baseArr[iArr].NTopBnc = NumTopBnc; // Number of top    bounces
            baseArr[iArr].NBotBnc = NumBotBnc; //   "       bottom
        } else {                               // not a new ray
            // PhaseArr[<base> + Nt-1] = PhaseArr[<base> + Nt-1] // LP: ???

            // calculate weightings of old ray information vs. new, based on amplitude of
            // the arrival
            float AmpTot = baseArr[Nt - 1].a + (float)Amp;
            float w1     = baseArr[Nt - 1].a / AmpTot;
            float w2     = (float)Amp / AmpTot;

            baseArr[Nt - 1].delay = w1 * baseArr[Nt - 1].delay
                + w2 * Cpx2Cpxf(delay); // weighted sum
            baseArr[Nt - 1].a            = AmpTot;
            baseArr[Nt - 1].SrcDeclAngle = w1 * baseArr[Nt - 1].SrcDeclAngle
                + w2 * (float)rinit.SrcDeclAngle;
            baseArr[Nt - 1].SrcAzimAngle = w1 * baseArr[Nt - 1].SrcAzimAngle
                + w2 * (float)rinit.SrcAzimAngle;
            baseArr[Nt - 1].RcvrDeclAngle = w1 * baseArr[Nt - 1].RcvrDeclAngle
                + w2 * (float)RcvrDeclAngle;
            baseArr[Nt - 1].RcvrAzimAngle = w1 * baseArr[Nt - 1].RcvrAzimAngle
                + w2 * (float)RcvrAzimAngle;
        }

    } else {
        // LP: For multithreading mode, some mutex scheme would be needed to
        // guarantee correct access to previously written data, which would
        // destroy the performance on GPU. So just write the first
        // arrinfo->MaxNArr arrivals and give up.
        Nt = AtomicFetchAdd(baseNArr, 1);
        if(Nt >= arrinfo->MaxNArr) return;
        baseArr[Nt].a             = (float)Amp;                // amplitude
        baseArr[Nt].Phase         = (float)Phase;              // phase
        baseArr[Nt].delay         = Cpx2Cpxf(delay);           // delay time
        baseArr[Nt].SrcDeclAngle  = (float)rinit.SrcDeclAngle; // launch angle from source
        baseArr[Nt].SrcAzimAngle  = (float)rinit.SrcAzimAngle; // launch angle from source
        baseArr[Nt].RcvrDeclAngle = (float)RcvrDeclAngle; // angle ray reaches receiver
        baseArr[Nt].RcvrAzimAngle = (float)RcvrAzimAngle; // angle ray reaches receiver
        baseArr[Nt].NTopBnc       = NumTopBnc;            // Number of top    bounces
        baseArr[Nt].NBotBnc       = NumBotBnc;            //   "       bottom
    }
}

inline void InitArrivalsMode(
    ArrInfo *arrinfo, int32_t maxThreads, bool ThreeD, const Position *Pos,
    PrintFileEmu &PRTFile)
{
    arrinfo->AllowMerging = maxThreads == 1;
    size_t nSrcsRcvrs     = Pos->NRz_per_range * Pos->NRr * Pos->NSz;
    if(ThreeD) nSrcsRcvrs *= Pos->Ntheta * Pos->NSx * Pos->NSy;
    // LP: Having a minimum size is not great if the user specified a maximum
    // amount of memory to use.
    // const size_t MinNArr         = 10;
    // arrinfo->MaxNArr = (int32_t)bhc::max(ArrivalsStorage / nReceivers, MinNArr);
    arrinfo->MaxNArr = arrinfo->ArrMemSize / (nSrcsRcvrs * sizeof(Arrival));
    PRTFile << "\n( Maximum # of arrivals = " << arrinfo->MaxNArr << " )\n";
    checkallocate(arrinfo->Arr, nSrcsRcvrs * (size_t)arrinfo->MaxNArr);
    checkallocate(arrinfo->NArr, nSrcsRcvrs);
    memset(arrinfo->Arr, 0, nSrcsRcvrs * (size_t)arrinfo->MaxNArr * sizeof(Arrival));
    memset(arrinfo->NArr, 0, nSrcsRcvrs * sizeof(int32_t));
}

template<bool O3D, bool R3D> void WriteOutArrivals(
    const Position *Pos, const FreqInfo *freqinfo, const BeamStructure<O3D> *Beam,
    std::string FileRoot, const ArrInfo *arrinfo);
extern template void WriteOutArrivals<false, false>(
    const Position *Pos, const FreqInfo *freqinfo, const BeamStructure<false> *Beam,
    std::string FileRoot, const ArrInfo *arrinfo);
extern template void WriteOutArrivals<true, false>(
    const Position *Pos, const FreqInfo *freqinfo, const BeamStructure<true> *Beam,
    std::string FileRoot, const ArrInfo *arrinfo);
extern template void WriteOutArrivals<true, true>(
    const Position *Pos, const FreqInfo *freqinfo, const BeamStructure<true> *Beam,
    std::string FileRoot, const ArrInfo *arrinfo);

} // namespace bhc
