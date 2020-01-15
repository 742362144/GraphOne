
#include <algorithm>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>

#include "cf_info.h"
#include "graph.h"

double bu_factor = 0.07;
int32_t MAX_BCOUNT = 256;
uint64_t MAX_ECOUNT = (1<<9);
uint64_t MAX_PECOUNT = (MAX_ECOUNT << 1)/3;
index_t  BATCH_SIZE = (1L << 16);//edge batching in edge log
index_t  BATCH_MASK =  0xFFFF;

//In-memory data structure size
index_t  BLOG_SHIFT = 27;
//index_t  BLOG_SIZE = (1L << BLOG_SHIFT); //size of edge log
//index_t  BLOG_MASK = (BLOG_SIZE - 1);

vid_t RANGE_COUNT = 256;
vid_t RANGE_2DSHIFT = 4;
index_t  LOCAL_VUNIT_COUNT = 20;
index_t  LOCAL_DELTA_SIZE = 28;
index_t  DURABLE_SIZE = (1L << 28);//Durable adj-list

//durable data structure buffer size
index_t  W_SIZE = (1L << 12); //Edges to write
index_t  DVT_SIZE = (1L <<24);//durable v-unit 

#ifdef B64
propid_t INVALID_PID = 0xFFFF;
tid_t    INVALID_TID  = 0xFFFFFFFF;
sid_t    INVALID_SID  = 0xFFFFFFFFFFFFFFFF;
#else 
propid_t INVALID_PID = 0xFF;
tid_t    INVALID_TID  = 0xFF;
sid_t    INVALID_SID  = 0xFFFFFFFF;
#endif

degree_t INVALID_DEGREE = 0xFFFFFFFF;

index_t residue = 0;
int THD_COUNT = 2;
vid_t _global_vcount = 0;
index_t _edge_count = -1L;
int _dir = 0;//undirected
int _persist = 0;//no
int _source = 0;//text

using std::swap;

void* alloc_buf()
{
    return calloc(sizeof(edge_t), MAX_ECOUNT);
}

void free_buf(void* buf)
{
    free(buf);
    buf = 0;
}
void cfinfo_t::create_columns(propid_t prop_count)
{
    col_info = new pinfo_t* [prop_count];
    col_count = 0;
}

void cfinfo_t::add_column(pinfo_t* prop_info, const char* longname, const char* shortname)
{
    g->add_property(longname);
    prop_info->p_name = gstrdup(shortname);
    prop_info->p_longname = gstrdup(longname);
    prop_info->cf_id = cf_id;
    prop_info->local_id = col_count;
    
    col_info[col_count] = prop_info;
    ++col_count;
}

void cfinfo_t::add_edge_property(const char* longname, prop_encoder_t* a_prop_encoder)
{
    prop_encoder = a_prop_encoder;
}

cfinfo_t::cfinfo_t(gtype_t type/* = evlabel*/)
{
    INIT_LIST_HEAD(&snapshot);
    gtype = type;
    egtype = eADJ;
    flag1 = 0;
    flag2 = 0;
    flag1_count = 0;
    flag2_count = 0;

    col_info = 0;
    col_count = 0;
    
    snap_thread = 0;
    snap_f = 0;
    wtf = 0;
}

void cfinfo_t::create_wthread()
{
    if (egraph != gtype) {
        return;
    }
    reg_edgelog();
    if (0 != pthread_create(&w_thread, 0, cfinfo_t::w_func, (void*)this)) {
        assert(0);
    }
}

void* cfinfo_t::w_func(void* arg)
{
    cout << "enterting w_func" << endl; 
    
    cfinfo_t* ptr = (cfinfo_t*)(arg);
    
    do {
        ptr->write_edgelog();
        //cout << "Writing w_thd" << endl;
        usleep(10);
    } while(1);

    return 0;
}

void cfinfo_t::create_snapthread(bool snap_thd)
{
    if (egraph != gtype) {
        return;
    }
    snap_create = snap_thd; 

    if (snap_create) {
    if (0 != pthread_create(&snap_thread, 0, cfinfo_t::snap_func, (void*)this)) {
        assert(0);
    }
    }
}

void* cfinfo_t::snap_func(void* arg)
{
    cfinfo_t* ptr = (cfinfo_t*)(arg);

    do {
        ptr->create_marker(0);
    } while (eEndBatch != ptr->create_snapshot());

    return 0;
}

status_t cfinfo_t::create_snapshot()
{
    assert(0);
    return eOK;
}

void cfinfo_t::new_snapshot(index_t snap_marker)
{
    snapshot_t* next = new snapshot_t;
    snapshot_t* last = 0;
    
    if (!list_empty(&snapshot)) {
        last = (snapshot_t*)snapshot.next;
        next->snap_id = last->snap_id + 1;
    } else {
        next->snap_id = 1;//snapshot starts at 1
    }

    next->marker = snap_marker;
    list_add(&next->list, &snapshot);

    //if (last) last->drop_ref();
    snapshot_t* oldest = (snapshot_t*)snapshot.prev;
    while (oldest->snap_id + 5 < next->snap_id) {
        //let us delete some snapshopts
        if (1 == oldest->get_ref()) {
            oldest->drop_ref(); //deleted
            oldest = (snapshot_t*)snapshot.prev;
        } else { break;}
    }
}

void cfinfo_t::read_snapshot()
{
    assert(snap_f != 0);

    off_t size = fsize(snapfile.c_str());
    if (size == -1L) {
        assert(0);
    }
    
    snapid_t count = (size/sizeof(disk_snapshot_t));
    disk_snapshot_t* disk_snapshot = (disk_snapshot_t*)calloc(count, sizeof(disk_snapshot_t));
    fread(disk_snapshot, sizeof(disk_snapshot_t), count, snap_f);
    
    snapshot_t* next = 0;
    next = new snapshot_t;
    next->snap_id = disk_snapshot[count-1].snap_id;
    next->marker = disk_snapshot[count-1].marker;
    list_add(&next->list, &snapshot);
}

void cfinfo_t::write_snapshot()
{
    disk_snapshot_t* disk_snapshot = (disk_snapshot_t*)malloc(sizeof(disk_snapshot_t));
    snapshot_t* last = get_snapshot();
    disk_snapshot->snap_id= last->snap_id;
    disk_snapshot->marker = last->marker;
    fwrite(disk_snapshot, sizeof(disk_snapshot_t), 1, snap_f);
    last->drop_ref();
}

status_t cfinfo_t::batch_update(const string& src, const string& dst, propid_t pid /* = 0*/)
{
    assert(0);
    return  eOK;
}

void cfinfo_t::compress_graph_baseline()
{
    assert(0);
    return;
}

status_t cfinfo_t::batch_update(const string& src, const string& dst, const char* property_str)
{
    assert(0);
    return  eOK;
}
    
status_t cfinfo_t::batch_update(const string& src, const string& dst, propid_t pid, 
                          propid_t count, prop_pair_t* prop_pair, int del /* = 0 */)
{
    //cout << "ignoring edge properties" << endl;
    batch_update(src, dst, pid);
    return eOK;
}

void cfinfo_t::waitfor_archive(index_t marker)
{   
    return;
}

void cfinfo_t::prep_graph_baseline(egraph_t egraph_type)
{   
    return;
}
    
void cfinfo_t::make_graph_baseline()
{
    assert(0);
}

status_t cfinfo_t::write_edgelog()
{
   return eNoWork;
}

void cfinfo_t::store_graph_baseline(bool clean)
{
    assert(0);
}

void cfinfo_t::read_graph_baseline()
{
    assert(0);
}

void cfinfo_t::file_open(const string& filename, bool trunc)
{
    assert(0);
}

/*******label specific **********/
status_t cfinfo_t::filter(sid_t sid, univ_t value, filter_fn_t fn)
{
    assert(0);
    return eOK;
}
    
void cfinfo_t::print_raw_dst(tid_t tid, vid_t vid, propid_t pid /* = 0 */)
{
    assert(0);
}

status_t cfinfo_t::get_encoded_value(const char* value, univ_t* univ)
{
    assert(0);
    return eQueryFail;
}

///****************///
/*status_t cfinfo_t::transform(srset_t* iset, srset_t* oset, direction_t direction)
{
    assert(0);
    return eOK;
}

status_t cfinfo_t::extend(srset_t* iset, srset_t* oset, direction_t direction)
{
    assert(0);
    return eOK;
}
*/

off_t fsize(const string& fname)
{
    struct stat st;
    if (0 == stat(fname.c_str(), &st)) {
        return st.st_size;
    }
    perror("stat issue");
    return -1L;
}

off_t fsize(int fd)
{
    struct stat st;
    if (0 == fstat(fd, &st)) {
        return st.st_size;
    }
    perror("stat issue");
    return -1L;
}

off_t fsize_dir(const string& idir)
{
    struct dirent *ptr;
    DIR *dir;
    string filename;
        
    index_t size = 0;
    index_t total_size = 0;
    

    //allocate accuately
    dir = opendir(idir.c_str());
    
    while (NULL != (ptr = readdir(dir))) {
        if (ptr->d_name[0] == '.') continue;
        filename = idir + "/" + string(ptr->d_name);
        size = fsize(filename);
        total_size += size;
    }
    closedir(dir);
    return total_size;
}
