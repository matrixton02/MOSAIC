#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <stdexcept>

constexpr uint32_t PAGE_SIZE=4096; //4KB
constexpr uint32_t PAGE_HEADER_SIZE= 16; // page_id, record_size, num_records, max_records

using namespace std;

// A single physical page as it sits on disk / in the buffer pool.
// Layout: [ header (16 bytes) ][ record 0 ][ record 1 ] ... [ record N-1 ][ unused ]
class Page{
public:
    uint8_t data[PAGE_SIZE];

    Page(){
        memset(data,0,PAGE_SIZE);
    }

    uint32_t page_id() const { 
        return read_u32(0);
    }
    void set_page_id(uint32_t id) {
        write_u32(0, id);
    }

    uint16_t record_size() const{
        return read_u16(4);
    }
    void set_record_size(uint16_t sz){
        write_u16(4,sz);
    }

    uint16_t num_records() const {
        return read_u16(6);
    }
    void set_num_records(uint16_t n){
        write_u16(6,n);
    }

    uint16_t max_records() const {
        return read_u16(8);
    }
    void set_max_record(uint16_t n){
        write_u16(n,8);
    }

    bool is_full() const{
        return num_records()>=max_records();
    }

    static uint16_t capacity_for(uint16_t rec_size){
        return static_cast<uint16_t>((PAGE_SIZE-PAGE_HEADER_SIZE)/rec_size);
    }

    uint16_t append(const uint8_t* record_bytes){
        if(is_full()){
            throw runtime_error("Page: append called on a full page");
        }

        uint16_t slot=num_records();
        uint32_t offset=PAGE_HEADER_SIZE+slot * record_size();
        memcpy(data+offset,record_bytes,record_size());
        set_num_records(slot+1);
        return slot;
    }

    void read_record(uint16_t slot,uint8_t* out_buf) const{
        if(slot>=num_records()){
            throw out_of_range("Page: read_record slot out of range");
        }
        uint32_t offset=PAGE_HEADER_SIZE+slot*record_size();
        memcpy(out_buf,data+offset,record_size());
    }
    

private:
    uint32_t read_u32(uint32_t off) const {
        uint32_t v;
        memcpy(&v,data+off,4);
        return v;
    }
    void write_u32(uint32_t off,uint32_t v){
        memcpy(data+off,&v,4);
    }

    uint16_t read_u16(uint32_t off) const {
        uint16_t v;
        memcpy(&v,data+off,2);
        return v;
    }

    void write_u16(uint32_t off,uint16_t v){
        memcpy(data+off,&v,2);
    }

};