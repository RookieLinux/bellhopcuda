#pragma once
#include "trace.hpp"
#include "influence.hpp"

/**
 * Returns whether the job should continue.
 * `is` changed to `isrc` because `is` is used for steps
 */
HOST_DEVICE inline bool GetJobIndices(int32_t &isrc, int32_t &ialpha, int32_t job,
    const Position *Pos, const AnglesStructure *Angles)
{
    if(Angles->iSingle_alpha >= 0){
        isrc = ray;
        ialpha = Angles->iSingle_alpha;
    }else{
        isrc = ray / Angles->Nalpha;
        ialpha = ray % Angles->Nalpha;
    }
    return (isrc < Pos->NSz);
}

/**
 * Main ray tracing function for ray path output mode.
 */
HOST_DEVICE inline void MainRayMode(int32_t isrc, int32_t ialpha, real &SrcDeclAngle,
    ray2DPt *ray2D, int32_t &Nsteps,
    const BdryType *ConstBdry, const BdryInfo *bdinfo, const ReflectionInfo *refl,
    const SSPStructure *ssp, const Position *Pos, const AnglesStructure *Angles,
    const FreqInfo *freqinfo, const BeamStructure *Beam, const BeamInfo *beaminfo)
{
    real DistBegTop, DistEndTop, DistBegBot, DistEndBot;
    int32_t IsegTop, IsegBot, iSegz, iSegr;
    vec2 gradc, rTopSeg, rBotSeg;
    BdryType Bdry;
    
    if(!RayInit(isrc, ialpha, SrcDeclAngle, ray2D[0], gradc,
        DistBegTop, DistBegBot, IsegTop, IsegBot, rTopSeg, rBotSeg, iSegz, iSegr,
        Bdry, ConstBdry, bdinfo, refl, ssp, Pos, Angles, freqinfo, Beam, beaminfo))
    {
        Nsteps = 1;
        return;
    }
    
    int32_t iSmallStepCtr = 0;
    int32_t is = 0; // index for a step along the ray
    
    for(int32_t istep = 0; istep<MaxN-1; ++istep){
        is += RayUpdate(ray2D[is], ray2D[is+1], ray2D[is+2], 
            DistBegTop, DistBegBot, DistEndTop, DistEndBot,
            IsegTop, IsegBot, rTopSeg, rBotSeg, iSmallStepCtr, iSegz, iSegr,
            Bdry, bdinfo, refl, ssp, freqinfo, Beam);
        if(RayTerminate(ray2D[is], Nsteps, is, DistBegTop, DistBegBot,
            DistEndTop, DistEndBot, Beam)) break;
    }
}

/**
 * for a TL calculation, allocate space for the pressure matrix
 */
inline void InitTLMode(cpx *&uAllSources, const Position *Pos,
    const BeamStructure *Beam)
{
    int32_t NRz_per_range = Compute_NRz_per_range(Pos, Beam);
    size_t n = Pos->NSz * NRz_per_range * Pos->NRr;
    uAllSources = allocate<cpx>(n);
    memset(uAllSources, 0, n * sizeof(cpx));
}

/**
 * LP: Write TL results
 */
inline void FinalizeTLMode(cpx *&uAllSources, DirectOFile &SHDFile)
{
    for(int32_t isrc=0; isrc<Pos->NSz; ++isrc){
        cpx ccpx;
        int32_t iSegz = 0, iSegr = 0;
        EvaluateSSPCOnly(vec2(RC(0.0), Pos->Sz[isrc]), ccpx, freqinfo->freq0, ssp, iSegz, iSegr);
        ScalePressure(Angles->Dalpha, ccpx.real(), Pos->Rr, 
            &uAllSources[isrc * NRz_per_range * Pos->NRr], 
            NRz_per_range, Pos->NRr, Beam->RunType, freqinfo->freq0);
        int32_t IRec = 10 + NRz_per_range * isrc;
        for(int32_t Irz1 = 0; Irz1 < NRz_per_range; ++Irz1){
            SHDFile.rec(IRec);
            for(int32_t r=0; r < Pos->NRr; ++r){
                DOFWRITEV(SHDFile, uAllSources[(isrc * NRz_per_range + Irz1) * Pos->NRr + r]);
            }
            ++IRec;
        }
    }
    deallocate(uAllSources);
}

/**
 * Main ray tracing function for TL / field output modes.
 */
HOST_DEVICE inline void MainTLMode(int32_t isrc, int32_t ialpha, real &SrcDeclAngle,
    cpx *uAllSources,
    const BdryType *ConstBdry, const BdryInfo *bdinfo, const ReflectionInfo *refl,
    const SSPStructure *ssp, const Position *Pos, const AnglesStructure *Angles,
    const FreqInfo *freqinfo, const BeamStructure *Beam, const BeamInfo *beaminfo)
{
    real DistBegTop, DistEndTop, DistBegBot, DistEndBot;
    int32_t IsegTop, IsegBot, iSegz, iSegr;
    vec2 gradc, rTopSeg, rBotSeg;
    BdryType Bdry;
    
    ray2DPt point0, point1, point2;
    InfluenceRayInfo inflray;
    
    if(!RayInit(isrc, ialpha, SrcDeclAngle, point0, gradc,
        DistBegTop, DistBegBot, IsegTop, IsegBot, rTopSeg, rBotSeg, iSegz, iSegr,
        Bdry, ConstBdry, bdinfo, refl, ssp, Pos, Angles, freqinfo, Beam, beaminfo)) return;
    
    Init_Influence(inflray, point0, Angles->alpha[ialpha], gradc, Pos, Angles, freqinfo, Beam);
    
    cpx *u = &uAllSources[isrc * inflray.NRz_per_range * Pos->NRr];
    int32_t iSmallStepCtr = 0;
    int32_t is = 0; // index for a step along the ray
    
    for(int32_t istep = 0; istep<MaxN-1; ++istep){
        int32_t dStep = RayUpdate(point0, point1, point2, 
            DistBegTop, DistBegBot, DistEndTop, DistEndBot,
            IsegTop, IsegBot, rTopSeg, rBotSeg, iSmallStepCtr, iSegz, iSegr,
            Bdry, bdinfo, refl, ssp, freqinfo, Beam);
        Step_Influence(point0, point1, inflray, is, u, ConstBdry, ssp, iSegz, iSegr, Pos, Beam);
        if(dStep == 2){
            Step_Influence(point1, point2, inflray, is, u, ConstBdry, ssp, iSegz, iSegr, Pos, Beam);
            point0 = point2;
        }else if(dStep == 1){
            point0 = point1;
        }else{
            print("Invalid dstep: %d\n", dstep);
            bail();
        }
        is += dStep;
        if(RayTerminate(point0, Nsteps, is, DistBegTop, DistBegBot,
            DistEndTop, DistEndBot, Beam)) break;
    }
}

//TODO 
/*

switch(Beam->RunType[0]){
case 'A':
case 'a':
    arrinfo->MaxNArr = std::max(ArrivalsStorage / (inflinfo->NRz_per_range * Pos->NRr), MinNArr);
    PRTFile << "\n( Maximum # of arrivals = " << arrinfo->MaxNArr << ")\n";
    break;
default:
    arrinfo->MaxNArr = 1;
    arrinfo->arr = allocate<TODO>(inflinfo->NRz_per_range * Pos->NRr * 1);
    arrinfo->NArr = allocate<int32_t>(inflinfo->NRz_per_range * Pos->NRr);
}

memset(arrinfo->NArr, 0, inflinfo->NRz_per_range * Pos->NRr * sizeof(int32_t));

switch(Beam->RunType[0]){

case 'A':
case 'a':
    // Arrivals calculation, zero out arrival matrix
    memset(arrinfo->Narr, 0, TODO);
    break;
}
*/
