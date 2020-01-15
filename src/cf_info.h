#pragma once

#include "type.h"
#include "str.h"
#include "prop_encoder.h"
//#include "rset.h"

//For many purpose. Will be removed ultimately
extern index_t residue;
extern vid_t _global_vcount;
//Required for exiting from the streaming computation
extern index_t _edge_count;
extern int _dir, _persist, _source;

extern void* alloc_buf();

inline char* gstrdup(const char* str) 
{
    return strdup(str);
}
/////////////////////////////////
//One relationship or label
class pinfo_t {
    public:
    char*        p_name;
    char*        p_longname;
    propid_t     cf_id;
    propid_t     local_id;
};

enum filter_fn_t {
    fn_out = 0,//simple lookup 
    fn_ein, //simple inward lookup
    //More coming soon such as regex
};

enum gtype_t {
    etype = 0,
    egraph,
    evlabel, 
};

enum egraph_t {
    eADJ,
    eSNB
};

class prop_encoder_t;

//Column Family
class cfinfo_t {
 public:
    pinfo_t**     col_info;
    propid_t      col_count;
    propid_t      cf_id;
    str_t         mem;

    prop_encoder_t* prop_encoder;

    gtype_t     gtype;
    egraph_t    egtype;
    
    sflag_t     flag1;
    sflag_t     flag2;
    uint8_t     flag1_count;
    uint8_t     flag2_count;
        
    //threads
    pthread_t   snap_thread;
    bool        snap_create;
    
    pthread_t   w_thread;
    int         wtf;   //edge log file
    
    string     snapfile;
    FILE*      snap_f;
   
   private:
    list_head  snapshot;

 public: 
    cfinfo_t(gtype_t type = evlabel);   
    
    void create_wthread();
    static void* w_func(void* arg);
    virtual void reg_edgelog() {assert(0);}
    void create_snapthread(bool a_snap_create);
    static void* snap_func(void* arg);
    
    virtual status_t create_snapshot();
    inline snapshot_t* get_snapshot() {
        if (list_empty(&snapshot)) {
            return 0;
        } else { 
            return ((snapshot_t*) snapshot.next)->take_ref();
        }
    }
    void read_snapshot();
    void write_snapshot();
    
    void new_snapshot(index_t snap_marker);

 public:
    void create_columns(propid_t prop_count);
    void add_column(pinfo_t* prop_info, const char* longname, const char* shortname);

    //edges and vertex properties
    virtual status_t batch_update(const string& src, const string& dst, propid_t pid = 0);
    virtual status_t batch_update(const string& src, const string& dst, const char* property_str);
    
    //For heavy weight edges.
    virtual status_t batch_update(const string& src, const string& dst, propid_t pid, 
                          propid_t count, prop_pair_t* prop_pair, int del = 0);
    
    virtual index_t create_marker(index_t marker) {return marker;};    
    virtual index_t update_marker() {return 0;};    
    virtual void prep_graph_baseline(egraph_t egraph_type=eADJ);
    virtual void waitfor_archive(index_t marker = 0);
    virtual void make_graph_baseline();
    virtual status_t write_edgelog();
    virtual void compress_graph_baseline();
    virtual void store_graph_baseline(bool clean = false);
    virtual void read_graph_baseline();
    virtual void file_open(const string& filename, bool trunc);
    
    inline char* alloc_str(index_t size, index_t& offset) {
        return mem.alloc_str(size, offset);
    }
    
    inline index_t copy_str(const char* value) {
        return mem.copy_str(value);
    }
    inline  char* get_str(index_t offset) {
        return mem.get_ptr(offset);
    }

    void setup_str(index_t size) {
        mem.setup(size);
    }

    //Graph specific
    virtual void add_edge_property(const char* longname, prop_encoder_t* prop_encoder);
    
    //virtual status_t transform(srset_t* iset, srset_t* oset, direction_t direction);
    //virtual status_t transform_withfilter(srset_t* iset, srset_t* oset, direction_t direction, filter_info_t* filter_info);
    //virtual status_t extend(srset_t* iset, srset_t* oset, direction_t direction);
    
    //label specific
    virtual status_t filter(sid_t sid, univ_t value, filter_fn_t fn);
    virtual void print_raw_dst(tid_t tid, vid_t vid, propid_t pid = 0);
    virtual status_t get_encoded_value(const char* value, univ_t* univ);
};
