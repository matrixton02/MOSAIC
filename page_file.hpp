#pragma once
#include <cstdio>
#include <string>
#include <stdexcept>

using namespace std;
// Manages a single on-disk file made up of fixed-size pages.
// Every read/write is a real fseek + fread/fwrite -- no in-memory shortcuts --
// so io_reads_/io_writes_ reflect actual disk I/O and can be used directly
// as an evaluation metric later (I/O count per query, buffer pool hit rate, etc).

class PageFile{
public:
    ~PageFile(){
        if(fp_){
            fclose(fp_);
        }
    }

    uint32_t num_pages() const {
        return num_pages_;
    }

    uint64_t io_reads() const{
        return io_reads_;
    }

    uint64_t io_writes() const{
        return io_writes_;
    }
    
private:
    string path_;
    FILE* fp_=nullptr;
    uint32_t num_pages_=0;
    uint64_t io_reads_=0;
    uint64_t io_writes_=0;
};