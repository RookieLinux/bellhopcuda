#pragma once
#include "common.hpp"

namespace bhc {

/**
 * Optionally reads a vector of source frequencies for a broadband run
 * If the broadband option is not selected, then the input freq (a scalar) is stored in the frequency vector
 */
inline void ReadfreqVec(char BroadbandOption, LDIFile &ENVFile, std::ostream &PRTFile,
    FreqInfo *freqinfo)
{
    freqinfo->Nfreq = 1;
    
    if(BroadbandOption == 'B'){
        LIST(ENVFile); ENVFile.Read(freqinfo->Nfreq);
        PRTFile << "__________________________________________________________________________\n\n\n";
        PRTFile << "Number of frequencies = " << freqinfo->Nfreq << "\n";
        if(freqinfo->Nfreq <= 0){
            std::cout << "Number of frequencies must be positive\n";
            std::abort();
        }
    }
    
    if(freqinfo->freqVec != nullptr) deallocate(freqinfo->freqVec);
    freqinfo->freqVec = allocate<real>(bhc::max(3, freqinfo->Nfreq));
    
    if(BroadbandOption == 'B'){
        PRTFile << "Frequencies (Hz)\n";
        freqinfo->freqVec[2] = FL(-999.9);
        LIST(ENVFile); ENVFile.Read(freqinfo->freqVec, freqinfo->Nfreq);
        SubTab(freqinfo->freqVec, freqinfo->Nfreq);
        EchoVector(freqinfo->freqVec, freqinfo->Nfreq, PRTFile);
    }else{
        freqinfo->freqVec[0] = freqinfo->freq0;
    }
}

/**
 * Read a vector x
 * Description is something like 'receiver ranges'
 * Units       is something like 'km'
 */
template<typename REAL> inline void ReadVector(int32_t &Nx, REAL *&x, std::string Description, 
    std::string Units, LDIFile &ENVFile, std::ostream &PRTFile)
{
    PRTFile << "\n__________________________________________________________________________\n\n";
    LIST(ENVFile); ENVFile.Read(Nx);
    PRTFile << "Number of " << Description << " = " << Nx << "\n";
    
    if(Nx <= 0){
        std::cout << "ReadVector: Number of " << Description << " must be positive\n";
        std::abort();
    }
    
    if(x != nullptr) deallocate(x);
    x = allocate<REAL>(bhc::max(3, Nx));
    
    PRTFile << Description << " (" << Units << ")\n";
    x[2] = FL(-999.9);
    LIST(ENVFile); ENVFile.Read(x, Nx);
    
    SubTab(x, Nx);
    Sort(x, Nx);
    EchoVector(x, Nx, PRTFile);
    
    PRTFile << "\n";
    
    // Vectors in km should be converted to m for internal use
    trim(Units);
    if(Units.length() >= 2){
        if(Units.substr(0, 2) == "km") for(int32_t i=0; i<Nx; ++i) x[i] *= FL(1000.0);
    }
}

/**
 * Read source x-y coordinates
 * 
 * ThreeD: flag indicating whether this is a 3D run
 */
inline void ReadSxSy(bool ThreeD, LDIFile &ENVFile, std::ostream &PRTFile,
    Position *Pos)
{
    if(ThreeD){
        ReadVector(Pos->NSx, Pos->Sx, "source   x-coordinates, Sx", "km", ENVFile, PRTFile);
        ReadVector(Pos->NSy, Pos->Sy, "source   y-coordinates, Sy", "km", ENVFile, PRTFile);
    }else{
        Pos->Sx = allocate<float>(1); Pos->Sy = allocate<float>(1);
        Pos->Sx[0] = FL(0.0);
        Pos->Sy[0] = FL(0.0);
    }
}

/**
 * Reads source and receiver z-coordinates (depths)
 * zMin, zMax: limits for those depths; 
 *     sources and receivers are shifted to be within those limits
 */
inline void ReadSzRz(real zMin, real zMax, LDIFile &ENVFile, std::ostream &PRTFile,
    Position *Pos)
{
    //bool monotonic; //LP: monotonic is a function, this is a name clash
    
    ReadVector(Pos->NSz, Pos->Sz, "Source   depths, Sz", "m", ENVFile, PRTFile);
    ReadVector(Pos->NRz, Pos->Rz, "Receiver depths, Rz", "m", ENVFile, PRTFile);
    
    if(Pos->ws != nullptr){ deallocate(Pos->ws); deallocate(Pos->iSz); }
    Pos->ws = allocate<float>(Pos->NSz); Pos->iSz = allocate<int32_t>(Pos->NSz);
    
    if(Pos->wr != nullptr){ deallocate(Pos->wr); deallocate(Pos->iRz); }
    Pos->wr = allocate<float>(Pos->NRz); Pos->iRz = allocate<int32_t>(Pos->NRz);
    
    // *** Check for Sz/Rz in upper or lower halfspace ***
    
    bool topbdry = false, botbdry = false;
    for(int32_t i=0; i<Pos->NSz; ++i){
        if(Pos->Sz[i] < zMin){
            topbdry = true;
            Pos->Sz[i] = zMin;
        }
        if(Pos->Sz[i] > zMax){
            botbdry = true;
            Pos->Sz[i] = zMax;
        }
    }
    if(topbdry) PRTFile << "Warning in ReadSzRz : Source above or too near the top bdry has been moved down\n";
    if(botbdry) PRTFile << "Warning in ReadSzRz : Source below or too near the bottom bdry has been moved up\n";
    
    topbdry = false; botbdry = false;
    for(int32_t i=0; i<Pos->NRz; ++i){
        if(Pos->Rz[i] < zMin){
            topbdry = true;
            Pos->Rz[i] = zMin;
        }
        if(Pos->Rz[i] > zMax){
            botbdry = true;
            Pos->Rz[i] = zMax;
        }
    }
    if(topbdry) PRTFile << "Warning in ReadSzRz : Receiver above or too near the top bdry has been moved down\n";
    if(botbdry) PRTFile << "Warning in ReadSzRz : Receiver below or too near the bottom bdry has been moved up\n";
    
    /*
    if(!monotonic(Pos->sz, Pos->NSz)){
        std::cout << "SzRzRMod: Source depths are not monotonically increasing\n";
        std::abort();
    }
    if(!monotonic(Pos->rz, Pos->NRz)){
        std::cout << "SzRzRMod: Receiver depths are not monotonically increasing\n";
        std::abort();
    }
    */
}

inline void ReadRcvrRanges(LDIFile &ENVFile, std::ostream &PRTFile,
    Position *Pos)
{
    ReadVector(Pos->NRr, Pos->Rr, "Receiver ranges, Rr", "km", ENVFile, PRTFile);
    
    // calculate range spacing
    Pos->Delta_r = FL(0.0);
    if(Pos->NRr != 1) Pos->Delta_r = Pos->Rr[Pos->NRr-1] - Pos->Rr[Pos->NRr-2];
    
    if(!monotonic(Pos->Rr, Pos->NRr)){
        std::cout << "ReadRcvrRanges: Receiver ranges are not monotonically increasing\n";
        std::abort();
    }
}

inline void ReadRcvrBearings(LDIFile &ENVFile, std::ostream &PRTFile,
    Position *Pos)
{
    ReadVector(Pos->Ntheta, Pos->theta, "receiver bearings, theta", "degrees", ENVFile, PRTFile);
    
    CheckFix360Sweep(Pos->theta, Pos->Ntheta);
    
    // calculate angular spacing
    Pos->Delta_theta = FL(0.0);
    if(Pos->Ntheta != 1) Pos->Delta_theta = Pos->theta[Pos->Ntheta-1] - Pos->theta[Pos->Ntheta-2];
    
    if(!monotonic(Pos->theta, Pos->Ntheta)){
        std::cout << "ReadRcvrBearings: Receiver bearings are not monotonically increasing\n";
        std::abort();
    }
}

}
