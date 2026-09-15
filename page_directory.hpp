#pragma once
#include <cstdint>
#include <vector>
#include <algorithm>

using namespace std;

/*Maps key ranges to page IDs, kept sorted by min_key.
This is intentionally the simplest possible implementation: a sorted array
searched with binary search. That's a deliberate placeholder -- this class
has the exact interface (lookup / range_lookup) that will later be backed by
a real B+ tree, and later still by the learned index (RMI/PGM). Swapping the
implementation behind PageDirectory is the whole point of the project; the
call sites in Table never need to change.*/

struct DirectoryEntry{
    int64_t min_key;
    int64_t max_key;
    uint32_t page_id;
};

class PageDirectory{
public:
    // Called once per page as it fills up / is finalized during bulk load.
    void add_entry(int64_t min_key,int64_t max_key,uint32_t page_id){
        entries_.push_back({min_key,max_key,page_id});
    }

    // Must be called after bulk-loading all entries and before querying,
    // since lookups assume entries_ is sorted by min_key.
    void finalize(){
        sort(entries_.begin(),entries_.end(),
        [](const DirectoryEntry& a,const DirectoryEntry& b){
            return a.min_key<b.min_key;
        });
    }

    size_t size() const {
        return entries_.size();
    }

    // Point lookup: which single page could contain this key?
    // Returns -1 if no page's range contains it.
    int64_t lookup(int64_t key) const {
        auto it=upper_bound(entries_.begin(),entries_.end(),key,
            [](int64_t k,const DirectoryEntry& e){
                return k<e.min_key;
            });
        if(it==entries_.begin()){
            return -1;
        }
        --it;
        if(key>=it->min_key && key<=it->max_key){
            return static_cast<int64_t>(it->page_id);
        }
        return -1;
    }

    // Range lookup: all pages whose [min_key, max_key] overlaps [low, high].
    // find first entry whose max_key >= low, then walk forward while
    // min_key <= high
    vector<uint32_t> range_lookuo(int64_t low,int64_t high){
        vector<uint32_t> result;

        auto it=lower_bound(entries_.begin(),entries_.end(),low,
            [](int64_t k,const DirectoryEntry& e){
                return e.max_key<k;
            });
        
        for(; it!=entries_.end() && it->min_key<=high;it++){
            result.push_back(it->page_id);
        }
        return result;
    }

    const vector<DirectoryEntry>& enteries(){
        return entries_;
    }

private:
    vector<DirectoryEntry> entries_;
};