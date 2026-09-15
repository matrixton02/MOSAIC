#pragma once
#include "page_directory.hpp"
#include "page_file.hpp"
#include <cstring>
#include <string>
#include <stdexcept>

class Table{
public:
private:

    PageFile file_;
    PageDirectory dir_;
    uint16_t payload_size_;
    uint16_t record_size_;

    uint32_t current_page_id=UINT32_MAX;
    Page current_page_;
    int64_t current_min_key_=0;
    int64_t current_max_key_=0;
};