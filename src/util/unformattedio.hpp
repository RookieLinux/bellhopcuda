/*
bellhopcxx / bellhopcuda - C++/CUDA port of BELLHOP(3D) underwater acoustics simulator
Copyright (C) 2021-2023 The Regents of the University of California
Marine Physical Lab at Scripps Oceanography, c/o Jules Jaffe, jjaffe@ucsd.edu
Based on BELLHOP / BELLHOP3D, which is Copyright (C) 1983-2022 Michael B. Porter

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

#ifndef _BHC_INCLUDING_COMPONENTS_
#error "Must be included from common_setup.hpp!"
#endif

namespace bhc {

/**
 * C++ emulation of FORTRAN unformatted output (binary). Each FORTRAN write
 * statement defines a record, and the record length is encoded as an int32_t
 * at the beginning and end of each record.
 */
class UnformattedOFile {
public:
    UnformattedOFile(bhcInternal *internal) : _internal(internal), recstart(-1), recl(-1)
    {}
    ~UnformattedOFile()
    {
        if(ostr.is_open()) {
            FinishRecord();
            ostr.close();
        }
    }

    void open(const std::string &path) { ostr.open(path, std::ios::binary); }

    bool good() { return ostr.good() && ostr.is_open(); }

    void rec()
    {
        FinishRecord();
        int32_t zero = 0;
        ostr.write((const char *)&zero, 4);
    }

    template<typename T> void write(T v)
    {
        if(recstart < 0) {
            ExternalError(_internal, "Missing record in UnformattedOFile!");
        }
        ostr.write((const char *)&v, sizeof(T));
        recl += (int32_t)sizeof(T);
    }

    template<typename T> void write(T *arr, size_t n)
    {
        if(recstart < 0) {
            ExternalError(_internal, "Missing record in UnformattedOFile!");
        }
        for(size_t i = 0; i < n; ++i) ostr.write((const char *)&arr[i], sizeof(T));
        recl += (int32_t)(n * sizeof(T));
    }

private:
    void FinishRecord()
    {
        if(recstart < 0 || recl < 0) {
            recstart = 0;
            recl     = 0;
            return;
        }
        ostr.seekp(recstart);
        ostr.write((const char *)&recl, 4);
        ostr.seekp(recstart + 4 + recl);
        ostr.write((const char *)&recl, 4);
        recstart += recl + 8;
        recl = 0;
    }

    bhcInternal *_internal;
    std::ofstream ostr;
    int32_t recstart;
    int32_t recl;
};

} // namespace bhc
