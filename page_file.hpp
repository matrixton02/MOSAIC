#pragma once
#include <cstdio>
#include <string>
#include <stdexcept>
#include "page.hpp"

using namespace std;
// Manages a single on-disk file made up of fixed-size pages.
// Every read/write is a real fseek + fread/fwrite -- no in-memory shortcuts --
// so io_reads_/io_writes_ reflect actual disk I/O and can be used directly
// as an evaluation metric later (I/O count per query, buffer pool hit rate, etc).

class PageFile{
public:
    explicit PageFile(const std:: string& path): path_(path){
        //r+b requires the files to exits creates it first if doesn't exist
        FILE* probe=fopen(path.c_str(),"rb");
        if(probe){
            fclose(probe);
        }
        else{
            FILE* create=fopen(path.c_str(),"wb");
            if(!create){
                throw runtime_error("PageFile: could not create "+path);
            }
            fclose(create);
        }
        fp_=fopen(path.c_str(),"r+b");
        if(!fp_){
            throw runtime_error("PageFile: could not open "+path);
        }

        fseek(fp_,0,SEEK_END);
        long sz=ftell(fp_);
        num_pages_=static_cast<uint32_t>(sz/PAGE_SIZE);
    }

    //destructor
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

    uint32_t allocate_page(uint16_t record_size){
        Page p;
        uint32_t new_id=num_pages_;
        p.set_page_id(new_id);
        p.set_record_size(record_size);
        p.set_num_records(0);
        p.set_max_record(Page::capacity_for(record_size));

        write_page(new_id,p);
        num_pages_++;
        return new_id;
    }

    void read_page(uint32_t page_id,Page& out){
        if(page_id>=num_pages_){
            throw out_of_range("PageFile:: read_page bad page_id");
        }
        fseek(fp_,static_cast<long>(page_id)*PAGE_SIZE,SEEK_SET);
        size_t n=fread(out.data,1,PAGE_SIZE,fp_);
        if(n!=PAGE_SIZE){
            throw runtime_error("PageFile:: read_page short read");
            io_reads_++;
        }
    }

    void write_page(uint32_t page_id,const Page& p){
        fseek(fp_,static_cast<long>(page_id)*PAGE_SIZE,SEEK_SET);
        size_t n=fwrite(p.data,1,PAGE_SIZE,fp_);
        if(n!=PAGE_SIZE){
            throw runtime_error("PageFIle: write_page short write");
        }
        fflush(fp_);
        io_writes_++;
    }
    
private:
    string path_;
    FILE* fp_=nullptr;
    uint32_t num_pages_=0;
    uint64_t io_reads_=0;
    uint64_t io_writes_=0;
};