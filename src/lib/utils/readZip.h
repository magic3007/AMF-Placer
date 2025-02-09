/**
 * @file readZip.h
 * @author Tingyuan LIANG (tliang@connect.ust.hk)
 * @brief
 * @version 0.1
 * @date 2021-10-02
 *
 * @copyright Copyright (c) 2021 Reconfiguration Computing Systems Lab, The Hong Kong University of Science and
 * Technology. All rights reserved.
 *
 */

#ifndef _READZIP
#define _READZIP

#include <cstdio>
#include <iostream>

// create a FILEBUF to read the unzip file pipe

struct FILEbuf : std::streambuf
{
    FILEbuf(FILE *fp) : fp_(fp)
    {
    }
    int underflow()
    {
        if (this->gptr() == this->egptr())
        {
            int size = fread(this->buffer_, 1, int(s_size), this->fp_);
            if (0 < size)
            {
                this->setg(this->buffer_, this->buffer_, this->buffer_ + size);
            }
        }
        return this->gptr() == this->egptr() ? traits_type::eof() : traits_type::to_int_type(*gptr());
    }
    FILE *fp_;
    enum
    {
        s_size = 1024
    };
    char buffer_[s_size];
};

#endif
