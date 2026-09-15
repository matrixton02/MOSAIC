#pragma once
#include "page_directory.hpp"
#include "page_file.hpp"
#include <cstring>
#include <string>
#include <stdexcept>

/*A Table ties a PageFile (physical storage) to a PageDirectory
(key-range lookup) under a fixed record schema.
Schema note: the key is always the first 8 bytes of a record (int64_t).
The caller is responsible for computing that key -- e.g. a raw timestamp
for the 1D case, or a Z-order interleaved key once the multi-dimensional
version is added. Table itself doesn't know or care which; it only needs
int64_t comparability, which is exactly what makes the directory/index
layer swappable later without touching this class.*/


class Table{
public:
    // payload_size = bytes of non-key data per record (e.g. 3 doubles = 24 bytes
    // for energy/time/channel-derived fields you want to store alongside the key).

    Table(const std::string& file_path,uint16_t payload_size): file_(file_path), payload_size_(payload_size), record_size_(sizeof(int64_t)+payload_size){
        if(record_size_>PAGE_SIZE-PAGE_HEADER_SIZE){
            throw runtime_error("Table: record_size is too large for one pass");
        }
    }

    uint16_t record_size() const{
        return record_size_;
    }

    uint32_t num_pages() const{
        return file_.num_pages();
    }

    const PageDirectory& directory() const {
        return dir_;
    }

    PageFile& file(){
        return file_;
    }

    // Bulk-insert path: assumes keys arrive in non-decreasing order (true for
    // sorted bulk load, and roughly true for physics data ingested in
    // acquisition order). Fills pages sequentially and finalizes each page's
    // directory entry as soon as it's full.

    void insert(int64_t key,const uint8_t* payload){
        if(current_page_id_==UINT32_MAX || current_page_.is_full()){
            flush_current_page();
            current_page_id_=file_.allocate_page(record_size_);
            current_page_=Page();
            file_.read_page(current_page_id_,current_page_); // reload header set by allocate_page
            current_min_key_=key;
        }

        vector<uint8_t> record(record_size_);
        memcpy(record.data(),&key,sizeof(int64_t));

        if(payload_size_>0){
            memcpy(record.data() + sizeof(int64_t),payload,payload_size_);
        }

        current_page_.append(record.data());
        current_max_key_=key;
    }

    // Call once after all inserts are done (end of bulk load) to flush the
    // last in-progress page and sort the directory for querying.

    void finalize(){
        flush_current_page();
        dir_.finalize();
    }

    // Fetch a record's payload given its page_id and slot, used after the
    // directory/index has told us which page+slot to look at.

    void read_record(uint32_t page_id, uint16_t slot,int64_t* key_out,uint8_t* payload_out){
        Page p;
        file_.read_page(page_id,p);
        vector<uint8_t> buf(record_size_);
        p.read_record(slot,buf.data());
        memcpy(&key_out,buf.data()+sizeof(int64_t),payload_size_);

        if(payload_size_>0){
            memcpy(payload_out,buf.data()+sizeof(int64_t),payload_size_);
        }
    }

        // Scan every record on a page (used by range queries to filter within a page).
    void scan_page(uint32_t page_id, vector<int64_t>& key_out){
        Page p;
        file_.read_page(page_id,p);
        key_out.clear();
        vector<uint8_t> buf(record_size_);

        for(uint16_t s=0;s<p.num_records();s++){
            p.read_record(s,buf.data());
            int64_t k;
            memcpy(&k,buf.data(),sizeof(int64_t));
            key_out.push_back(k);
        }
    }



private:
    void flush_current_page(){
        if(current_page_id_ != UINT32_MAX && current_page_.num_records()>0){
            file_.write_page(current_page_id_,current_page_);
            dir_.add_entry(current_min_key_,current_max_key_,current_page_id_);
        }
    }
    PageFile file_;
    PageDirectory dir_;
    uint16_t payload_size_;
    uint16_t record_size_;

    uint32_t current_page_id_=UINT32_MAX;
    Page current_page_;
    int64_t current_min_key_=0;
    int64_t current_max_key_=0;
};