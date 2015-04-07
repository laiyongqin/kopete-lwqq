#include "qq_types.h"
#include "smemory.h"
#include "utility.h"

#include <sys/stat.h>

#define LOCAL_HASH_JS(buf)  (snprintf(buf,sizeof(buf),"%s"LWQQ_PATH_SEP"hash.js",\
            lwdb_get_config_dir()),buf)
#define GLOBAL_HASH_JS(buf) (snprintf(buf,sizeof(buf),"%s"LWQQ_PATH_SEP"hash.js",\
            GLOBAL_DATADIR),buf)

TABLE_BEGIN_LONG(qq_shengxiao_to_str, const char*,LwqqShengxiao , "")
    TR(LWQQ_MOUTH,_("Mouth"))     TR(LWQQ_CATTLE,_("Cattle"))
    TR(LWQQ_TIGER,_("Tiger"))    TR(LWQQ_RABBIT,_("Rabbit"))
    TR(LWQQ_DRAGON,_("Dragon"))   TR(LWQQ_SNACK,_("Snack"))
    TR(LWQQ_HORSE,_("Horse"))    TR(LWQQ_SHEEP,_("Sheep"))
    TR(LWQQ_MONKEY,_("Monkey"))   TR(LWQQ_CHOOK,_("Chook"))
    TR(LWQQ_DOG,_("Dog"))        TR(LWQQ_PIG,_("Pig"))
TABLE_END()

TABLE_BEGIN_LONG(qq_blood_to_str, const char*,LwqqBloodType , "")
    TR(LWQQ_BLOOD_A,_("A"))   TR(LWQQ_BLOOD_B,_("B"))
    TR(LWQQ_BLOOD_O,_("O")) TR(LWQQ_BLOOD_AB,_("AB"))
    TR(LWQQ_BLOOD_OTHER,_("Other"))
TABLE_END()

TABLE_BEGIN_LONG(qq_constel_to_str,const char*,LwqqConstel ,"")
    TR(LWQQ_AQUARIUS,_("Aquarius"))      
    TR(LWQQ_PISCES,_("Pisces"))  
    TR(LWQQ_ARIES,_("Aries"))            
    TR(LWQQ_TAURUS,_("Taurus"))
    TR(LWQQ_GEMINI,_("Gemini"))          
    TR(LWQQ_CANCER,_("Cancer"))  
    TR(LWQQ_LEO,_("Leo"))                
    TR(LWQQ_VIRGO,_("Virgo"))
    TR(LWQQ_LIBRA,_("Libra"))            
    TR(LWQQ_SCORPIO,_("Scorpio"))
    TR(LWQQ_SAGITTARIUS,_("Sagittarius"))
    TR(LWQQ_CAPRICORNUS,_("Capricornus"))
TABLE_END()

TABLE_BEGIN_LONG(qq_gender_to_str,const char*,LwqqGender,"")
    TR(LWQQ_FEMALE,_("Female"))
    TR(LWQQ_MALE,_("Male"))
TABLE_END()

TABLE_BEGIN_LONG(qq_client_to_str,const char*,LwqqClientType,"")
    TR(LWQQ_CLIENT_PC,_("Desktop"))
    TR(LWQQ_CLIENT_MOBILE,_("Phone"))
    TR(LWQQ_CLIENT_WEBQQ,_("WebQQ"))
    TR(LWQQ_CLIENT_QQFORPAD,_("PadQQ"))
TABLE_END()

const char* qq_status_to_str(LwqqStatus status)
{
    if(status == LWQQ_STATUS_ONLINE) return "available";
    return lwqq_status_to_str(status);
}
LwqqStatus qq_status_from_str(const char* str)
{
    if(str==NULL) return LWQQ_STATUS_LOGOUT;
    if(strcmp(str,"available")==0) return LWQQ_STATUS_ONLINE;
    return lwqq_status_from_str(str);
}

const char* qq_level_to_str(int level)
{
    static const char* symbol[] = {"♔","⚙","☾","☆"};
    static const int number[] = {64,16,4,1};
    static char buf[128];
    int l = level;
    int repeat;
    int i,j;
    memset(buf,0,sizeof(buf));
    for(i=0;i<4;i++){
        repeat = l/number[i];
        l=l%number[i];
        for(j=0;j<repeat;j++){
            strcat(buf,symbol[i]);
        }
    }
    format_append(buf,"(%d)",level);
    return buf;
}



int did_dispatch(void* param)
{
    LwqqCommand *d = (LwqqCommand *)param;
    vp_do(*d,NULL);
    s_free(d);
    return 0;
}

void qq_dispatch(LwqqCommand cmd)
{
    LwqqCommand* d = (LwqqCommand*)s_malloc0(sizeof(*d));
    *d = cmd;
    did_dispatch(d);
}

#ifdef WITH_MOZJS
static char* hash_with_local_file(const char* uin,const char* ptwebqq,lwqq_js_t* js)
{
    char path[512] = {0};
    if(access(LOCAL_HASH_JS(path), F_OK)!=0)
        if(access(GLOBAL_HASH_JS(path),F_OK)!=0)
            return NULL;
    lwqq_jso_t* obj = lwqq_js_load(js, path);
    char* res = NULL;

    res = lwqq_js_hash(uin, ptwebqq, js);
    lwqq_js_unload(js, obj);

    return res;
}

static char* hash_with_remote_file(const char* uin,const char* ptwebqq,void* js)
{
    //github.com is too slow
    const char* url = "http://pidginlwqq.sinaapp.com/hash.js";
    LwqqErrorCode ec = qq_download(url, "hash.js", lwdb_get_config_dir());
    if(ec){
        lwqq_log(LOG_ERROR,"Could not download JS From %s",url);
    }
    return hash_with_local_file(uin, ptwebqq, js);
}

static char* hash_with_db_url(const char* uin,const char* ptwebqq,qq_account* ac)
{
    const char* url = lwdb_userdb_read(ac->db, "hash.js");
    if(url == NULL) return NULL;
    if(qq_download(url,"hash.js",lwdb_get_config_dir())==LWQQ_EC_ERROR) return NULL;
    return hash_with_local_file(uin, ptwebqq, ac->js);
}
#endif
qq_account* qq_account_new(char *username, char *password)
{
    qq_account* ac = (qq_account*)malloc(sizeof(qq_account));
    ac->magic = QQ_MAGIC;
    ac->flag = (lwflags)0;

    
    ac->qq = lwqq_client_new(username, password);
    ac->js = lwqq_js_init();
#ifdef WITH_MOZJS
    fprintf(stderr, "WITH_MOZJS\n");
    lwqq_hash_add_entry(ac->qq, "hash_local", (LwqqHashFunc)hash_with_local_file,  ac->js);
    lwqq_hash_add_entry(ac->qq, "hash_url",   (LwqqHashFunc)hash_with_remote_file, ac->js);
    lwqq_hash_add_entry(ac->qq, "hash_db",    (LwqqHashFunc)hash_with_db_url,      ac);
#endif
    ac->font.family = s_strdup("宋体");
    ac->font.size = 12;
    ac->font.style = 0;

    ac->qq->dispatch = qq_dispatch;
    
    return ac;
}
void qq_account_free(qq_account* ac)
{  
    lwqq_js_close(ac->js);
    //s_free(ac->recent_group_name);
    s_free(ac->font.family);
#if QQ_USE_FAST_INDEX
    g_hash_table_destroy(ac->fast_index.qqnum_index);
    g_hash_table_destroy(ac->fast_index.uin_index);
#endif

}


LwqqBuddy* find_buddy_by_qqnumber(LwqqClient* lc,const char* qqnum)
{
    return lwqq_buddy_find_buddy_by_qqnumber(lc, qqnum);
}
LwqqGroup* find_group_by_qqnumber(LwqqClient* lc,const char* qqnum)
{
    LwqqGroup* group;
    LIST_FOREACH(group,&lc->groups,entries) {
        if(!group->account) continue;
        if(strcmp(group->account,qqnum)==0)
            return group;
    }
    return NULL;
}

LwqqBuddy* find_buddy_by_uin(LwqqClient* lc,const char* uin)
{
    return lwqq_buddy_find_buddy_by_uin(lc, uin);
}
LwqqGroup* find_group_by_gid(LwqqClient* lc,const char* gid)
{
    return lwqq_group_find_group_by_gid(lc, gid);
}
void vp_func_4pl(CALLBACK_FUNC func,vp_list* vp,void* q)
{
    typedef void (*f)(void*,void*,void*,void*,long);
    if( q ){
        va_list* va = q;
        vp_init(*vp,sizeof(void*)*4+sizeof(long));
        vp_dump(*vp,*va,void*);
        vp_dump(*vp,*va,void*);
        vp_dump(*vp,*va,void*);
        vp_dump(*vp,*va,void*);
        vp_dump(*vp,*va,long);
        return ;
    }
    void* p1 = vp_arg(*vp,void*);
    void* p2 = vp_arg(*vp,void*);
    void* p3 = vp_arg(*vp,void*);
    void* p4 = vp_arg(*vp,void*);
    long p5 = vp_arg(*vp,long);
    ((f)func)(p1,p2,p3,p4,p5);
}

LwqqErrorCode qq_download(const char* url,const char* file,const char* dir)
{
    LwqqHttpRequest* req = lwqq_http_request_new(url);
    req->do_request(req,0,NULL);
    if(req->http_code != 200) return LWQQ_EC_ERROR;
    if(!req->response) return LWQQ_EC_ERROR;

    char path[2048];
    snprintf(path,sizeof(path),"%s/%s",dir,file);
    char* content = req->response;
    FILE* f;
    f = fopen(path, "w");
    if(!f) mkdir(dir,0755);
    if(!f) return LWQQ_EC_ERROR;
    fwrite(content, 1, strlen(content), f);
    fclose(f);
    lwqq_http_request_free(req);
    return LWQQ_EC_OK;
}
