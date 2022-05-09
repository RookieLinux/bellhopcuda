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
#pragma once
#include "common.hpp"
#include "step.hpp"
#include "reflect.hpp"

namespace bhc {

/**
 * Calculates the distances to the boundaries
 * Formula differs from JKPS because code uses outward pointing normals
 * 
 * rayx: ray coordinate
 * Topx, Botx: top, bottom coordinate
 * Topn, Botn: top, bottom normal vector (outward)
 * DistTop, DistBot: distance (normal to bdry) from the ray to top, bottom boundary
*/
template<bool R3D> HOST_DEVICE inline void Distances(const VEC23<R3D> &rayx, 
    const VEC23<R3D> &Topx, const VEC23<R3D> &Botx,
    const VEC23<R3D> &Topn, const VEC23<R3D> &Botn, real &DistTop, real &DistBot)
{
    VEC23<R3D> dTop = rayx - Topx; // vector pointing from top    bdry to ray
    VEC23<R3D> dBot = rayx - Botx; // vector pointing from bottom bdry to ray
    DistTop = -glm::dot(Topn, dTop);
    DistBot = -glm::dot(Botn, dBot);
}

/**
 * LP: Pulled out ray update loop initialization. Returns whether to continue
 * with the ray trace. Only call for valid ialpha w.r.t. Angles->iSingleAlpha.
 * Original comments follow.
 * 
 * DistBegTop etc.: Distances from ray beginning, end to top and bottom
 */
template<bool O3D, bool R3D> HOST_DEVICE inline bool RayInit(
    RayInitInfo<O3D> &rinit,
    rayPt<R3D> &point0, vec2 &gradc, real &DistBegTop, real &DistBegBot, 
    Origin<O3D, R3D> &org, SSPSegState &iSeg, BdryState<O3D> &bds, BdryType &Bdry,
    const BdryType *ConstBdry, const BdryInfo<O3D> *bdinfo,
    const SSPStructure *ssp, const Position *Pos, const AnglesStructure *Angles,
    const FreqInfo *freqinfo, const BeamStructure *Beam, const BeamInfo *beaminfo)
{
    if(    rinit.isz < 0    || rinit.isz >= Pos->NSz 
        || rinit.ialpha < 0 || rinit.ialpha >= Angles->Nalpha
        || (O3D && (
            rinit.isx < 0   || rinit.isx >= Pos->NSx ||
            rinit.isy < 0   || rinit.isy >= Pos->NSy ||
            rinit.ibeta < 0 || rinit.ibeta >= Angles->Nbeta
    )){
        printf("Invalid ray init indexes!\n");
        bail();
    }
    
    // LP: This part from BellhopCore
    
    if constexpr(O3D){
        rinit.xs = vec3(Pos->Sx[rinit.isx], Pos->Sy[rinit.isy], Pos->Sz[rinit.isz]);
    }else{
        rinit.xs = vec2(FL(0.0), Pos->Sz[isrc]); // x-y [LP: r-z] coordinate of the source
    }
    real alpha = Angles->alpha[rinit.ialpha]; // initial angle
    real beta = Angles->beta[rinit.ibeta];
    rinit.SrcDeclAngle = RadDeg * alpha; // take-off declination angle in degrees
    rinit.SrcAzimAngle = RadDeg * beta; // take-off azimuthal   angle in degrees
    if constexpr(O3D && !R3D){
        org.xs = rinit.xs;
        org.tradial = vec2(STD::cos(beta), STD::sin(beta));
    }
    
    float omega = FL(2.0) * REAL_PI * freqinfo->freq0;
    iSeg.x = iSeg.y = iSeg.z = iSeg.r = 0;
    
    bool TODO_x_rcvrMat_init;
    
    VEC23<O3D> tinit;
    if constexpr(O3D){
        tinit = vec3(FL(0.0), FL(0.0), FL(1.0));
    }else{
        tinit = vec2(STD::cos(alpha), STD::sin(alpha));
    }
    SSPOutputs<O3D> o;
    EvaluateSSP<O3D, R3D>(rinit.xs, tinit, o, org, ssp, iSeg);
    gradc = o.gradc;
    
    bool TODO_PickEpsilon;
    
    if constexpr(!O3D){
        // Are there enough beams?
        real DalphaOpt = STD::sqrt(o.ccpx.real() / (FL(6.0) * freqinfo->freq0 * Pos->Rr[Pos->NRr-1]));
        int32_t NalphaOpt = 2 + (int)((Angles->alpha[Angles->Nalpha-1] - Angles->alpha[0]) / DalphaOpt);
        
        if(Beam->RunType[0] == 'C' && Angles->Nalpha < NalphaOpt && ialpha == 0){
            printf("Warning in " BHC_PROGRAMNAME " : Too few beams\nNalpha should be at least = %d\n", NalphaOpt);
        }
    }
    
    int32_t ibp = BinarySearchLEQ(beaminfo->SrcBmPat, beaminfo->NSBPPts, 2, 0, SrcDeclAngle);
    // LP: Our function won't ever go outside the table, but we need to limit it
    // to 2 from the end.
    ibp = bhc::min(ibp, beaminfo->NSBPPts-2);
    
    // linear interpolation to get amplitude
    real s = (SrcDeclAngle - beaminfo->SrcBmPat[2*ibp+0]) / (beaminfo->SrcBmPat[2*(ibp+1)+0] - beaminfo->SrcBmPat[2*ibp+0]);
    float Amp0 = (FL(1.0) - s) * beaminfo->SrcBmPat[2*ibp+1] + s * beaminfo->SrcBmPat[2*(ibp+1)+1]; // initial amplitude
    
    // Lloyd mirror pattern for semi-coherent option
    if(Beam->RunType[0] == 'S')
        Amp0 *= STD::sqrt(FL(2.0)) * STD::abs(STD::sin(omega / o.ccpx.real() * DEP(xs) * STD::sin(alpha)));
        
    // LP: This part from TraceRay
    
    VEC23<R3D> tinit2;
    if constexpr(R3D){
        tinit2 = vec3(STD::cos(alpha) * STD::cos(beta), STD::cos(alpha) * STD::sin(beta), STD::sin(alpha));
    }else if constexpr(O3D){
        tinit2 = vec2(STD::cos(alpha), STD::sin(alpha));
    }else{
        tinit2 = tinit;
    }
    if constexpr(O3D && !R3D){
        point0.x     = vec2(RL(0.0), xs.z);
    }else{
        point0.x     = xs;
    }
    point0.c         = o.ccpx.real();
    point0.t         = tinit2 / o.ccpx.real();
    point0.tau       = cpx(FL(0.0), FL(0.0));
    point0.Amp       = Amp0;
    point0.Phase     = FL(0.0);
    point0.NumTopBnc = 0;
    point0.NumBotBnc = 0;
    if constexpr(R3D){
        point0.p     = mat2x2(FL(1.0)); // LP: identity
        point0.q     = mat2x2(FL(0.0)); // LP: zero matrix
        point0.DetQ  = DEBUG_LARGEVAL; //epsilon.x * epsilon.y // LP: commented out
        point0.phi   = FL(0.0);
    }else{
        point0.p     = vec2(FL(1.0), FL(0.0));
        point0.q     = vec2(FL(0.0), FL(1.0));
    }
    
    if constexpr(!O3D){
        // second component of qv is not used in geometric beam tracing
        // set I.C. to 0 in hopes of saving run time
        if(Beam->RunType[1] == 'G') point0.q = vec2(FL(0.0), FL(0.0));
    }
    
    Bdry = *ConstBdry;
    if constexpr(O3D){
        bds.top.Iseg.x = bds.top.Iseg.y = 0;
        bds.bot.Iseg.x = bds.bot.Iseg.y = 0;
    }else{
        bds.top.Iseg = bds.bot.Iseg = 0;
    }
    GetBdrySeg<O3D>(rinit.xs, point0.t, bds.top, &bdinfo->top, true ); // identify the top    segment above the source
    GetBdrySeg<O3D>(rinit.xs, point0.t, bds.bot, &bdinfo->bot, false); // identify the bottom segment below the source
    
    Distances<R3D>(point0.x, bds.top.x, bds.bot.x, bds.top.n, bds.bot.n, DistBegTop, DistBegBot);
    
    if(DistBegTop <= FL(0.0) || DistBegBot <= FL(0.0)){
        printf("Terminating the ray trace because the source is on or outside the boundaries\n");
        return false; // source must be within the medium
    }
    
    return true;
}

/**
 * LP: Pulled out contents of ray update loop. Returns the number of ray steps
 * taken (i.e. normally 1, or 2 if reflected).
 */
template<bool O3D, bool R3D> HOST_DEVICE inline int32_t RayUpdate(
    const rayPt<R3D> &point0, rayPt<R3D> &point1, rayPt<R3D> &point2,
    real &DistEndTop, real &DistEndBot,
    int32_t &iSmallStepCtr, const Origin<O3D, R3D> &org, SSPSegState &iSeg,
    BdryState<O3D> &bds, BdryType &Bdry, const BdryInfo<O3D> *bdinfo, const ReflectionInfo *refl,
    const SSPStructure *ssp, const FreqInfo *freqinfo, const BeamStructure *Beam)
{
    int32_t numRaySteps = 1;
    bool topRefl, botRefl;
    Step<O3D, R3D>(point0, point1, bds, Beam, org, ssp, iSeg, iSmallStepCtr, topRefl, botRefl);
    /*
    if(point0.x == point1.x){
        printf("Ray did not move from (%g,%g), bailing\n", point0.x.x, point0.x.y);
        bail();
    }
    */
    
    VEC23<O3D> x_o = RayToOceanX(point1.x, org);
    VEC23<O3D> t_o = RayToOceanT(point1.t, org);
    GetBdrySeg<O3D>(x_o, t_o, bds.top, &bdinfo->top, true);
    GetBdrySeg<O3D>(x_o, t_o, bds.bot, &bdinfo->bot, false);
    
    // Reflections?
    // Tests that ray at step is is inside, and ray at step is+1 is outside
    // to detect only a crossing from inside to outside
    // DistBeg is the distance at point0, which is saved
    // DistEnd is the distance at point1, which needs to be calculated
    Distances<R3D>(x_o, bds.top.x, bds.bot.x, bds.top.n, bds.bot.n, DistEndTop, DistEndBot);
    
    // LP: Merging these cases is important for GPU performance.
    if(topRefl || botRefl){
        // printf(topRefl ? "Top reflecting\n" : "Bottom reflecting\n");
        const BdryInfoTopBot<O3D> &bdi = topRefl ? bdinfo->top : bdinfo->bot;
        const BdryStateTopBot<O3D> &bdstb = topRefl ? bds.top : bds.bot;
        const HSInfo &hs = topRefl ? Bdry.Top.hs : Bdry.Bot.hs;
        const ReflectionInfoTopBot &refltb = topRefl ? refl->top : refl->bot;
        ReflCurvature<O3D> rcurv;
        VEC23<R3D> tInt(RL(0.0));
        VEC23<O3D> nInt;
        
        if constexpr(O3D){
            // LP: FORTRAN actually checks if the whole string is just "C", not just the first char
            if(bdi.type[0] == 'C'){
                real s1 = (x_o.x - bdstb.x) / (bdstb.lSeg.x.max - bdstb.lSeg.x.min);
                real s2 = (x_o.y - bdstb.y) / (bdstb.lSeg.y.max - bdstb.lSeg.y.min);
                real m1 = FL(1.0) - s1;
                real m2 = FL(1.0) - s2;
                
                BdryPtFull<true> *bd00 = &bdi.bd[(bdstb.Iseg.x  )*bdi.NPts.y+bdstb.Iseg.y  ];
                BdryPtFull<true> *bd01 = &bdi.bd[(bdstb.Iseg.x  )*bdi.NPts.y+bdstb.Iseg.y+1];
                BdryPtFull<true> *bd10 = &bdi.bd[(bdstb.Iseg.x+1)*bdi.NPts.y+bdstb.Iseg.y  ];
                BdryPtFull<true> *bd11 = &bdi.bd[(bdstb.Iseg.x+1)*bdi.NPts.y+bdstb.Iseg.y+1];
                
                nInt = bd00->Noden * m1 * m2 +
                       bd10->Noden * s1 * m2 +
                       bd11->Noden * s1 * s2 +
                       bd01->Noden * m1 * s2;
                rcurv.z_xx = bd00->z_xx;
                rcurv.z_xy = bd00->z_xy;
                rcurv.z_yy = bd00->z_yy;
                
                rcurv.kappa_xx = bd00->kappa_xx;
                rcurv.kappa_xy = bd00->kappa_xy;
                rcurv.kappa_yy = bd00->kappa_yy;
            }else{
                nInt = bdstb.n;
                rcurv.z_xx = rcurv.z_xy = rcurv.z_yy = FL(0.0);
                rcurv.kappa_xx = rcurv.kappa_xy = rcurv.kappa_yy = FL(0.0);
            }
        }else{
            BdryPtFull<false> *bd0 = &bdi.bd[bdstb.Iseg];
            BdryPtFull<false> *bd1 = &bd0[1]; // LP: next segment
            // LP: FORTRAN actually checks if the whole string is just "C", not just the first char
            if(bdi.type[0] == 'C'){
                real sss = glm::dot(point1.x - bdstb.x, bd0->t) / bd0->Len; // proportional distance along segment
                nInt = (FL(1.0) - sss) * bd0->Noden + sss * bd1->Noden;
                tInt = (FL(1.0) - sss) * bd0->Nodet + sss * bd1->Nodet;
            }else{
                nInt = bd0->n; // normal is constant in a segment
                tInt = bd0->t;
            }
            rcurv.kappa = bd0->kappa;
        }
        
        Reflect<O3D, R3D>(point1, point2, hs, topRefl, tInt, nInt, rcurv,
            freqinfo->freq0, refltb, Beam, org, ssp, iSeg);
        //Incrementing bounce count moved to Reflect
        numRaySteps = 2;
        x_o = RayToOceanX(point2.x, org);
        Distances<R3D>(x_o, bds.top.x, bds.bot.x, bds.top.n, bds.bot.n, DistEndTop, DistEndBot);
    }
    
    return numRaySteps;
}

/**
 * Has the ray left the box, lost its energy, escaped the boundaries, or 
 * exceeded storage limit?
 * [LP: 2D-3D only:]
 * this should be modified to have a single box
 * no need to test point.x.x, for instance, against several limits; calculate one limit in advance
 * LP: Also updates DistBegTop, DistBegBot.
 */
template<bool O3D, bool R3D> HOST_DEVICE inline bool RayTerminate(
    const rayPt<R3D> &point,
    int32_t &Nsteps, int32_t is, const int32_t &iSmallStepCtr,
    real &DistBegTop, real &DistBegBot, const real &DistEndTop, const real &DistEndBot,
    const Origin<O3D, R3D> &org, const BdryInfo<O3D> *bdinfo, const BeamStructure *Beam
    )
{
    bool leftbox, escapedboundaries, backward, toomanysmallsteps;
    bool escaped0bdry, escapedNbdry;
    if constexpr(O3D){
        vec3 x_o = RayToOceanX(point.x, org);
        leftbox = STD::abs(x_o.x - org.xs.x) > Beam->Box.x ||
                  STD::abs(x_o.y - org.xs.y) > Beam->Box.y ||
                  STD::abs(x_o.z - org.xs.z) > Beam->Box.z;
        escaped0bdry = 
            x.x < bhc::max(bdinfo->bot.bd[0].x.x, bdinfo->top.bd[0].x.x) ||
            x.y < bhc::max(bdinfo->bot.bd[0].x.y, bdinfo->top.bd[0].x.y);
        escapedNbdry =
            x.x > bhc::max(bdinfo->bot.bd[(bdinfo->bot.NPts.x-1)*bdinfo->bot.NPts.y].x.x, 
                           bdinfo->top.bd[(bdinfo->top.NPts.x-1)*bdinfo->top.NPts.y].x.x) ||
            x.y > bhc::max(bdinfo->bot.bd[bdinfo->bot.NPts.y-1].x.y, 
                           bdinfo->top.bd[bdinfo->top.NPts.y-1].x.y);
        escapedboundaries = escaped0bdry || escapedNbdry;
        toomanysmallsteps = iSmallStepCtr > 50;
    }else{
        leftbox = STD::abs(point.x.x) > Beam->Box.r ||
                  STD::abs(point.x.y) > Beam->Box.z;
        escaped0bdry = escapedNbdry = false;
        escapedboundaries = (DistBegTop < FL(0.0) && DistEndTop < FL(0.0)) ||
                            (DistBegBot < FL(0.0) && DistEndBot < FL(0.0));
        toomanysmallsteps = false; // LP: Simply never checked in 2D.
    }
    bool lostenergy = point.Amp < FL(0.005);
    if constexpr(O3D && !R3D){
        backward = point.t.x < FL(0.0); // kills off a backward traveling ray
    }else{
        backward = false; // LP: Commented out for 2D, absent for 3D as would not make sense.
    }
    if(leftbox || lostenergy || escapedboundaries || backward){
        /*
        if(leftbox){
            printf("Ray left beam box (%g,%g)\n", Beam->Box.r, Beam->Box.z);
        }else if(escapedboundaries){
            printf("Ray escaped boundaries DistBegTop %g DistEndTop %g DistBegBot %g DistEndBot %g\n",
                DistBegTop, DistEndTop, DistBegBot, DistEndBot);
        }else if(lostenergy){
            printf("Ray energy dropped to %g\n", point.Amp);
        }else if(backward){
            printf("Ray is going backwards\n");
        }
        */
        if(O3D && escaped0bdry){
            // LP: 2D-3D and 3D: If escapes the boundary only to the negative
            // side, stop without including the current step.
            Nsteps = is;
        }else{
            Nsteps = is + 1;
        }
        return true;
    }else if(is >= MaxN - 3){
        printf("Warning in TraceRay: Insufficient storage for ray trajectory\n");
        Nsteps = is;
        return true;
    }
    
    DistBegTop = DistEndTop;
    DistBegBot = DistEndBot;
    return false;
}

}
