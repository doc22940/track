/* ------------------------------------------------------------ */
/*
HTTrack Website Copier, Offline Browser for Windows and Unix
Copyright (C) 1998-2014 Xavier Roche and other contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.

Important notes:

- We hereby ask people using this source NOT to use it in purpose of grabbing
emails addresses, or collecting any other private information on persons.
This would disgrace our work, and spoil the many hours we spent on it.

Please visit our Website: http://www.httrack.com
*/

/* ------------------------------------------------------------ */
/* File: Main source                                            */
/* Author: Xavier Roche                                         */
/* ------------------------------------------------------------ */

/* Internal engine bytecode */
#define HTS_INTERNAL_BYTECODE

#include <fcntl.h>
#include <ctype.h>

/* File defs */
#include "htscore.h"

/* specific definitions */
#include "htsbase.h"
#include "htsnet.h"
#include "htsbauth.h"
#include "htsmd5.h"
#include "htsindex.h"

/* external modules */
#include "htsmodules.h"

// htswrap_add
#include "htswrap.h"

// parser
#include "htsparse.h"

/* Cache */
#include "htszlib.h"

/* Charset handling */
#include "htscharset.h"

/* Dynamic typed arrays */
#include "htsarrays.h"

/* END specific definitions */

/* external modules */
extern int hts_parse_externals(htsmoduleStruct * str);
extern void htspe_init(void);

/* debug */
#if DEBUG_SHOWTYPES
char REG[32768] = "\n";
#endif
#if NSDEBUG
int nsocDEBUG = 0;
#endif

//
#define _CLRSCR printf("\33[m\33[2J");
#define _GOTOXY(X,Y) printf("\33[" X ";" Y "f");

#if DEBUG_CHECKINT
#define _CHECKINT_FAIL(a) printf("\n%s\n",a); fflush(stdout); abort();
#define _CHECKINT(obj_ptr,message) \
   if (obj_ptr) {\
     if (( * ((char*) (obj_ptr)) != 0) || ( * ((char*) (((char*) (obj_ptr)) + sizeof(*(obj_ptr))-1)) != 0)) {\
       char msg[1100];\
       if (( * ((char*) (obj_ptr)) != 0) && ( * ((char*) (((char*) (obj_ptr)) + sizeof(*(obj_ptr))-1)) != 0))\
         sprintf(msg,"* PANIC: Integrity error (structure crushed)  in: %s",message);\
       else if ( * ((char*) (obj_ptr)) != 0)\
         sprintf(msg,"* PANIC: Integrity error (start of structure) in: %s",message);\
       else\
         sprintf(msg,"* PANIC: Integrity error (end of structure)   in: %s",message);\
       _CHECKINT_FAIL(msg);\
     }\
   } else {\
     char msg[1100];\
     sprintf(msg,"* PANIC: NULL pointer in: %s",message);\
     _CHECKINT_FAIL(msg);\
   }
#endif

#if DEBUG_HASH
  // longest hash chain?
int longest_hash[3] = { 0, 0, 0 }, hashnumber = 0;
#endif

// Début de httpmirror, routines annexes

// au cas où nous devons quitter rapidement xhttpmirror (plus de mémoire, etc)
// note: partir de liens_max.. vers 0.. sinon erreur de violation de mémoire: les liens suivants
// ne sont plus à nous.. agh! [dur celui-là]
#define HTMLCHECK_UNINIT { \
hts_log_print(opt, LOG_INFO, "engine: end"); \
RUN_CALLBACK0(opt, end); \
}

#define XH_extuninit do { \
  HTMLCHECK_UNINIT \
  hts_record_free(opt); \
  if (filters && filters[0]) { \
  freet(filters[0]); filters[0]=NULL; \
  } \
  if (filters) { \
  freet(filters); filters=NULL; \
  } \
  back_delete_all(opt,&cache,sback); \
  back_free(&sback); \
  checkrobots_free(&robots);\
  if (cache.use) { freet(cache.use); cache.use=NULL; } \
  if (cache.dat) { fclose(cache.dat); cache.dat=NULL; }  \
  if (cache.ndx) { fclose(cache.ndx); cache.ndx=NULL; } \
  if (cache.zipOutput) { \
    zipClose(cache.zipOutput, "Created by HTTrack Website Copier/"HTTRACK_VERSION); \
    cache.zipOutput = NULL; \
  } \
  if (cache.zipInput) { \
    unzClose(cache.zipInput); \
    cache.zipInput = NULL; \
  } \
  if (cache.olddat) { fclose(cache.olddat); cache.olddat=NULL; } \
  if (cache.lst) { fclose(cache.lst); cache.lst=opt->state.strc.lst=NULL; } \
  if (cache.txt) { fclose(cache.txt); cache.txt=NULL; } \
  if (opt->log != NULL) fflush(opt->log); \
  if (makestat_fp) { fclose(makestat_fp); makestat_fp=NULL; } \
  if (maketrack_fp){ fclose(maketrack_fp); maketrack_fp=NULL; } \
  if (opt->accept_cookie) cookie_save(opt->cookie,fconcat(OPT_GET_BUFF(opt),OPT_GET_BUFF_SIZE(opt),StringBuff(opt->path_log),"cookies.txt")); \
  if (makeindex_fp) { fclose(makeindex_fp); makeindex_fp=NULL; } \
  if (cache_hashtable) { coucal_delete(&cache_hashtable); } \
  if (cache_tests)     { coucal_delete(&cache_tests); } \
  if (template_header) { freet(template_header); template_header=NULL; } \
  if (template_body)   { freet(template_body); template_body=NULL; } \
  if (template_footer) { freet(template_footer); template_footer=NULL; } \
  hash_free(&hash); \
  clearCallbacks(&opt->state.callbacks); \
  /*structcheck_init(-1);*/ \
} while(0)
#define XH_uninit do { XH_extuninit; if (r.adr) { freet(r.adr); r.adr=NULL; } } while(0)

struct lien_buffers {
  /* Main array of pointers. 
     This is the real "lien_url **liens" pointer base. */
  TypedArray(lien_url*) ptr;
  /* String pool, chunked. */
  char *string_buffer;
  size_t string_buffer_size;
  size_t string_buffer_capa;
  TypedArray(char*) string_buffers;
  /* Structure list, chunked. */
  lien_url *lien_buffer;
  size_t lien_buffer_size;
  size_t lien_buffer_capa;
  TypedArray(lien_url*) lien_buffers;
};

// duplicate a string, or return NULL upon error (out-of-memory)
static char* hts_record_link_strdup_(httrackp *opt, const char *s) {
  static const size_t block_capa = 32768;
  lien_buffers *const liensbuf = opt->liensbuf;
  const size_t len = strlen(s) + 1;  /* including terminating \0 */
  char *s_dup;

  assertf(liensbuf != NULL);
  assertf(len < block_capa);

  // not enough capacity ? then create a new chunk
  if (len + liensbuf->string_buffer_size > liensbuf->string_buffer_capa) {
    // backup current block pointer for later free
    if (liensbuf->string_buffer != NULL) {
      TypedArrayAdd(liensbuf->string_buffers, liensbuf->string_buffer);
      liensbuf->string_buffer = NULL;
      liensbuf->string_buffer_size = 0;
    }

    // Double capacity for each new chained block
    liensbuf->string_buffer_capa = 
      liensbuf->string_buffer_capa < block_capa 
      ? block_capa : liensbuf->string_buffer_capa * 2;

    liensbuf->string_buffer = malloct(liensbuf->string_buffer_capa);
    if (liensbuf->string_buffer == NULL) {
      hts_record_assert_memory_failed(liensbuf->string_buffer_capa);
    }
    liensbuf->string_buffer_size = 0;

    hts_log_print(opt, LOG_DEBUG,
      "reallocated %d new bytes of strings room",
      (int) liensbuf->string_buffer_capa);
  }

  assertf(len + liensbuf->string_buffer_size <= liensbuf->string_buffer_capa);
  s_dup = &liensbuf->string_buffer[liensbuf->string_buffer_size];
  memcpy(s_dup, s, len);
  liensbuf->string_buffer_size += len;
  
  return s_dup;
}

static char* hts_record_link_strdup(httrackp *opt, const char *s) {
  assertf(opt != NULL);
  assertf(s != NULL);
  return hts_record_link_strdup_(opt, s);
}

size_t hts_record_link_latest(httrackp *opt) {
  lien_buffers *const liensbuf = opt->liensbuf;

  assertf(TypedArraySize(liensbuf->ptr) != 0);
  return TypedArraySize(liensbuf->ptr) - 1;
}

// returns a new zeroed lien_url entry, 
// or (size_t) -1 upon error (out-of-memory)
// the returned index is the osset within opt->liens[]
static size_t hts_record_link_alloc(httrackp *opt) {
  static const size_t block_capa = 256;
  lien_buffers *const liensbuf = opt->liensbuf;
  lien_url *link;

  assertf(opt != NULL);
  assertf(liensbuf != NULL);

  // Limit the number of links
  if (opt->maxlink > 0 && TypedArraySize(liensbuf->ptr) >= opt->maxlink) {
    return (size_t) -1;
  }

  // Create a new chunk of lien_url[]
  // There are references to item pointers, so we can not just realloc()
  if (liensbuf->lien_buffer_size == liensbuf->lien_buffer_capa) {
    size_t capa_bytes;

    if (liensbuf->lien_buffer != NULL) {
      TypedArrayAdd(liensbuf->lien_buffers, liensbuf->lien_buffer);
      liensbuf->lien_buffer_size = 0;
    }

    // Double capacity for each new chained block
    liensbuf->lien_buffer_capa = 
      liensbuf->lien_buffer_capa < block_capa 
      ? block_capa : liensbuf->lien_buffer_capa * 2;

    capa_bytes = liensbuf->lien_buffer_capa*sizeof(*liensbuf->lien_buffer);
    liensbuf->lien_buffer = (lien_url*) malloct(capa_bytes);
    if (liensbuf->lien_buffer == NULL) {
      hts_record_assert_memory_failed(capa_bytes);
    }
    liensbuf->lien_buffer_size = 0;

    hts_log_print(opt, LOG_DEBUG, "reallocated %d new link placeholders",
      (int) liensbuf->lien_buffer_capa);
  }

  // Take next lien_url item
  assertf(liensbuf->lien_buffer_size < liensbuf->lien_buffer_capa);
  link = &liensbuf->lien_buffer[liensbuf->lien_buffer_size++];
  memset(link, 0, sizeof(*link));

  // Add new lien_url pointer to the array of links
  TypedArrayAdd(liensbuf->ptr, link);

  // Ensure we have a guard NULL
  TypedArrayAdd(liensbuf->ptr, NULL);
  TypedArraySize(liensbuf->ptr)--;

  // Update pointer as it may have changed,
  // and update heap top index
  opt->liens = TypedArrayElts(liensbuf->ptr);
  assertf(TypedArraySize(liensbuf->ptr) != 0);
  assertf(TypedArraySize(liensbuf->ptr) < ( (unsigned int) -1 ) / 2);
  opt->lien_tot = (int) TypedArraySize(liensbuf->ptr);

  // return tail
  return hts_record_link_latest(opt);
}

void hts_record_init(httrackp *opt) {
  if (opt->liensbuf == NULL) {
    opt->liensbuf = calloct(sizeof(*opt->liensbuf), 1);
    if (opt->liensbuf == NULL) {
      hts_record_assert_memory_failed(sizeof(*opt->liensbuf));
    }
  }
}

// wipe records
void hts_record_free(httrackp *opt) {
  lien_buffers *const liensbuf = opt->liensbuf;

  if (liensbuf != NULL) {
    size_t i;

    TypedArrayFree(liensbuf->ptr);

    if (liensbuf->string_buffer != NULL) {
      freet(liensbuf->string_buffer);
      liensbuf->string_buffer = NULL;
      liensbuf->string_buffer_size = 0;
      liensbuf->string_buffer_capa = 0;
    }

    for(i = 0 ; i < TypedArraySize(liensbuf->string_buffers) ; i++) {
      freet(TypedArrayNth(liensbuf->string_buffers, i));
      TypedArrayNth(liensbuf->string_buffers, i) = NULL;
    }
    TypedArrayFree(liensbuf->string_buffers);

    if (liensbuf->lien_buffer != NULL) {
      freet(liensbuf->lien_buffer);
      liensbuf->lien_buffer = NULL;
    }

    for(i = 0 ; i < TypedArraySize(liensbuf->lien_buffers) ; i++) {
      freet(TypedArrayNth(liensbuf->lien_buffers, i));
      TypedArrayNth(liensbuf->lien_buffers, i) = NULL;
    }
    TypedArrayFree(liensbuf->lien_buffers);

    freet(opt->liensbuf);
    opt->liensbuf = NULL;
  }

  opt->liens = NULL;  // no longer defined
}

// adds a new link and returns a non-zero value upon success
static int hts_record_link_(httrackp * opt,
                            const char *address, const char *file, const char *save,
                            const char *ref_address, const char *ref_file,
                            const char *codebase) {
  // create a new entry
  const size_t lien_tot = hts_record_link_alloc(opt);
  lien_url*const link = lien_tot != (size_t) -1 ? opt->liens[lien_tot] : NULL;
  if (link == NULL) {
    return 0;
  }

  // record string fields
  if ( (link->adr = hts_record_link_strdup(opt, address)) == NULL
    || (link->fil = hts_record_link_strdup(opt, file)) == NULL
    || (link->sav = hts_record_link_strdup(opt, save)) == NULL
    || (link->former_adr = hts_record_link_strdup(opt, ref_address)) == NULL
    || (link->former_fil = hts_record_link_strdup(opt, ref_file)) == NULL
    ) {
    return 0;
  }

  // record codebase for java classes
  if (codebase != NULL) {
    const size_t len = strlen(file);
    if (len > 6 && strncmp(&file[len - 6], ".class", 6) == 0) {
      if ((link->cod = hts_record_link_strdup(opt, codebase)) == NULL) {
        return 0;
      }
    }
  }

  // add entry in the hashtables
  hash_write(opt->hash, lien_tot);

  // success
  return 1;
}

int hts_record_link(httrackp * opt,
                    const char *address, const char *file, const char *save,
                    const char *ref_address, const char *ref_file,
                    const char *codebase) {
  const int success = 
    hts_record_link_(opt, address, file, save, ref_address, ref_file, codebase);
  if (!success) {
    hts_log_print(opt, LOG_PANIC, "Too many links (links=%ld, limit=%ld)", 
                  (long int) heap_top_index(), (long int) opt->maxlink);
    hts_log_print(opt, LOG_INFO,
      "To avoid that: use #L option for more links (example: -#L1000000, or -#L0 to disable)");
  }
  return success;
}

void hts_invalidate_link(httrackp * opt, int lpos) {
  /* devalidate entry  */
  opt->liens[lpos]->pass2 = -1;
}


#define HT_INDEX_END do { \
if (!makeindex_done) { \
if (makeindex_fp) { \
  char BIGSTK tempo[1024]; \
  if (makeindex_links == 1) { \
    char BIGSTK link_escaped[HTS_URLMAXSIZE*2]; \
    escape_uri_utf(makeindex_firstlink, link_escaped, sizeof(link_escaped)); \
    sprintf(tempo,"<meta HTTP-EQUIV=\"Refresh\" CONTENT=\"0; URL=%s\">"CRLF, link_escaped); \
  } else \
    tempo[0]='\0'; \
    hts_template_format(makeindex_fp,template_footer, \
    "<!-- Mirror and index made by HTTrack Website Copier/"HTTRACK_VERSION" "HTTRACK_AFF_AUTHORS" -->", \
    tempo, /* EOF */ NULL \
    ); \
  fflush(makeindex_fp); \
  fclose(makeindex_fp);  /* à ne pas oublier sinon on passe une nuit blanche */  \
  makeindex_fp=NULL; \
  usercommand(opt,0,NULL,fconcat(OPT_GET_BUFF(opt),OPT_GET_BUFF_SIZE(opt),StringBuff(opt->path_html_utf8),"index.html"),"","");  \
} \
} \
makeindex_done=1;    /* ok c'est fait */  \
} while(0)

/* does it look like XML ? (SVG et al.) */
static int look_like_xml(const char *s) {
  return strncmp(s, "<?xml", 5) == 0
    || strncmp(s, "<!-- ", 5) == 0
    || strncmp(s, "<svg ", 5) == 0
    ;
}

// Début de httpmirror, robot
// url1 peut être multiple
int httpmirror(char *url1, httrackp * opt) {
  char *primary = NULL;         // première page, contenant les liens à scanner
  hash_struct hash;             // système de hachage, accélère la recherche dans les liens
  hash_struct *const hashptr = &hash;
  t_cookie BIGSTK cookie;       // gestion des cookies

  //char* tab_alloc=NULL;
  int ptr;                      // pointeur actuel sur les liens

  //
  int numero_passe = 0;         // deux passes pour html puis images
  struct_back *sback = NULL;
  htsblk BIGSTK r;              // retour de certaines fonctions

  // pour les stats, nombre de fichiers & octets écrits
  LLint stat_fragment = 0;      // pour la fragmentation

  //TStamp istat_timestart;   // départ pour calcul instantanné
  //
  TStamp last_info_shell = 0;
  int info_shell = 0;

  // filtres
  char **filters = NULL;

  //int filter_max=0;
  int filptr = 0;

  //
  int makeindex_done = 0;       // lorsque l'index sera fait
  FILE *makeindex_fp = NULL;
  int makeindex_links = 0;
  char BIGSTK makeindex_firstlink[HTS_URLMAXSIZE * 2];

  // statistiques (mode #Z)
  FILE *makestat_fp = NULL;     // fichier de stats taux transfert
  FILE *maketrack_fp = NULL;    // idem pour le tracking
  TStamp makestat_time = 0;     // attente (secondes)
  LLint makestat_total = 0;     // repère du nombre d'octets transférés depuis denrière stat
  int makestat_lnk = 0;         // idem, pour le nombre de liens

  //
  char BIGSTK codebase[HTS_URLMAXSIZE * 2];     // base pour applet java
  char BIGSTK base[HTS_URLMAXSIZE * 2]; // base pour les autres fichiers

  //
  cache_back BIGSTK cache;
  robots_wizard BIGSTK robots;  // gestion robots.txt
  coucal cache_hashtable = NULL;
  coucal cache_tests = NULL;

  //
  char *template_header = NULL, *template_body = NULL, *template_footer = NULL;

  //
  codebase[0] = '\0';
  base[0] = '\0';
  //
  cookie.auth.next = NULL;
  cookie.auth.auth[0] = cookie.auth.prefix[0] = '\0';
  //

  // noter heure actuelle de départ en secondes
  memset(&HTS_STAT, 0, sizeof(HTS_STAT));
  HTS_STAT.stat_timestart = time_local();
  //istat_timestart=stat_timestart;
  HTS_STAT.istat_timestart[0] = HTS_STAT.istat_timestart[1] = mtime_local();
  /* reset stats */
  HTS_STAT.HTS_TOTAL_RECV = 0;
  HTS_STAT.istat_bytes[0] = HTS_STAT.istat_bytes[1] = 0;
  if (opt->shell) {
    last_info_shell = HTS_STAT.stat_timestart;
  }
  if ((opt->makestat) || (opt->maketrack)) {
    makestat_time = HTS_STAT.stat_timestart;
  }
  // init external modules
  htspe_init();

  // initialiser cookie
  if (opt->accept_cookie) {
    opt->cookie = &cookie;
    cookie.max_len = 30000;     // max len
    strcpybuff(cookie.data, "");
    // Charger cookies.txt par défaut ou cookies.txt du miroir
    cookie_load(opt->cookie, StringBuff(opt->path_log), "cookies.txt");
    cookie_load(opt->cookie, "", "cookies.txt");
  } else
    opt->cookie = NULL;

  // initialiser exit_xh
  opt->state.exit_xh = 0;       // sortir prématurément (var globale)

  // initialiser usercommand
  usercommand(opt, opt->sys_com_exec, StringBuff(opt->sys_com), "", "", "");

  // initialiser structcheck
  // structcheck_init(1);

  // initialiser verif_backblue
  verif_backblue(opt, NULL);
  verif_external(opt, 0, 0);
  verif_external(opt, 1, 0);

  // et templates html
  template_header =
    readfile_or(fconcat
                (OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt), StringBuff(opt->path_bin),
                 "templates/index-header.html"), HTS_INDEX_HEADER);
  template_body =
    readfile_or(fconcat
                (OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt), StringBuff(opt->path_bin),
                 "templates/index-body.html"), HTS_INDEX_BODY);
  template_footer =
    readfile_or(fconcat
                (OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt), StringBuff(opt->path_bin),
                 "templates/index-footer.html"), HTS_INDEX_FOOTER);

  // initialiser mimedefs
  //get_userhttptype(opt,1,StringBuff(opt->mimedefs),NULL);

  // Initialiser indexation
  if (opt->kindex)
    index_init(StringBuff(opt->path_html));

  // effacer bloc cache
  memset(&cache, 0, sizeof(cache_back));
  cache.type = opt->cache;      // cache?
  cache.errlog = cache.log = opt->log;  // err log?
  cache.ptr_ant = cache.ptr_last = 0;   // pointeur pour anticiper

  // initialiser hash cache
  cache_hashtable = coucal_new(0);
  cache_tests = coucal_new(0);
  if (cache_hashtable == NULL || cache_tests == NULL) {
    printf("PANIC! : Not enough memory [%d]\n", __LINE__);
    filters[0] = NULL;          // uniquement a cause du warning de XH_extuninit
    XH_extuninit;
    return 0;
  }
  hts_set_hash_handler(cache_hashtable, opt);
  hts_set_hash_handler(cache_tests, opt);
  coucal_set_name(cache_hashtable, "cache_hashtable");
  coucal_set_name(cache_tests, "cache_tests");
  coucal_value_is_malloc(cache_tests, 1);      /* malloc */
  cache.hashtable = (void *) cache_hashtable;   /* copy backcache hash */
  cache.cached_tests = (void *) cache_tests;    /* copy of cache_tests */

  // robots.txt
  strcpybuff(robots.adr, "!");  // dummy
  robots.token[0] = '\0';
  robots.next = NULL;           // suivant
  opt->robotsptr = &robots;

  // effacer filters
  opt->maxfilter = maximum(opt->maxfilter, 128);
  if (filters_init(&filters, opt->maxfilter, 0) == 0) {
    printf("PANIC! : Not enough memory [%d]\n", __LINE__);
    XH_extuninit;
    return 0;
  }
  opt->filters.filters = &filters;
  //
  opt->filters.filptr = &filptr;
  //opt->filters.filter_max=&filter_max;

  // hash table
  opt->hash = &hash;

  // initialize link heap
  hts_record_init(opt);

  // initialiser ptr et lien_tot
  ptr = 0;

  // initialiser hachage
  hash_init(opt, &hash, opt->urlhack);
  // note: we need a cast because of the const
  hash.liens = (const lien_url *const*const*) &opt->liens;

  // copier adresse(s) dans liste des adresses
  {
    char *a = url1;
    int primary_len = 8192;

    if (StringNotEmpty(opt->filelist)) {
      primary_len += max(0, fsize(StringBuff(opt->filelist)) * 2);
    }
    primary_len += (int) strlen(url1) * 2;

    // création de la première page, qui contient les liens de base à scanner
    // c'est plus propre et plus logique que d'entrer à la main les liens dans la pile
    // on bénéficie ainsi des vérifications et des tests du robot pour les liens "primaires"
    primary = (char *) malloct(primary_len);
    if (primary) {
      primary[0] = '\0';
    } else {
      printf("PANIC! : Not enough memory [%d]\n", __LINE__);
      XH_extuninit;
      return 0;
    }

    while(*a) {
      int i;
      int joker = 0;

      // vérifier qu'il n'y a pas de * dans l'url
      if (*a == '+')
        joker = 1;
      else if (*a == '-')
        joker = 1;

      if (joker) {              // joker ou filters
        //char* p;
        char BIGSTK tempo[HTS_URLMAXSIZE * 2];
        int type;
        int plus = 0;

        // noter joker (dans b)
        if (*a == '+') {        // champ +
          type = 1;
          plus = 1;
          a++;
        } else if (*a == '-') { // champ forbidden[]
          type = 0;
          a++;
        } else {                // champ + avec joker sans doute
          type = 1;
        }

        // recopier prochaine chaine (+ ou -)
        i = 0;
        while((*a != 0) && (!isspace((unsigned char) *a))) {
          tempo[i++] = *a;
          a++;
        }
        tempo[i++] = '\0';
        while(isspace((unsigned char) *a)) {
          a++;
        }

        // sauter les + sans rien après..
        if (strnotempty(tempo)) {
          if ((plus == 0) && (type == 1)) {     // implicite: *www.edf.fr par exemple
            if (tempo[strlen(tempo) - 1] != '*') {
              strcatbuff(tempo, "*");   // ajouter un *
            }
          }
          if (type)
            strcpybuff(filters[filptr], "+");
          else
            strcpybuff(filters[filptr], "-");
          strcatbuff(filters[filptr], tempo);
          filptr++;

          /* sanity check */
          if (filptr + 1 >= opt->maxfilter) {
            opt->maxfilter += HTS_FILTERSINC;
            if (filters_init(&filters, opt->maxfilter, HTS_FILTERSINC) == 0) {
              printf("PANIC! : Too many filters : >%d [%d]\n", filptr,
                     __LINE__);
              hts_log_print(opt, LOG_PANIC,
                            "Too many filters, giving up..(>%d)", filptr);
              hts_log_print(opt, LOG_NOTICE,
                            "To avoid that: use #F option for more filters (example: -#F5000)");
              XH_extuninit;
              return 0;
            }
            //opt->filters.filters=filters;
          }

        }

      } else {                  // adresse normale
        char BIGSTK url[HTS_URLMAXSIZE * 2];

        // prochaine adresse
        i = 0;
        while((*a != 0) && (!isspace((unsigned char) *a))) {
          url[i++] = *a;
          a++;
        }
        while(isspace((unsigned char) *a)) {
          a++;
        }
        url[i++] = '\0';

        //strcatbuff(primary,"<PRIMARY=\"");
        if (strstr(url, ":/") == NULL)
          strcatbuff(primary, "http://");
        strcatbuff(primary, url);
        //strcatbuff(primary,"\">");
        strcatbuff(primary, "\n");
      }
    }                           // while

    /* load URL file list */
    /* OPTIMIZED for fast load */
    if (StringNotEmpty(opt->filelist)) {
      char *filelist_buff = NULL;
      const size_t filelist_sz = off_t_to_size_t(fsize(StringBuff(opt->filelist)));

      if (filelist_sz != (size_t) -1) {
        FILE *fp = fopen(StringBuff(opt->filelist), "rb");

        if (fp) {
          filelist_buff = malloct(filelist_sz + 1);
          if (filelist_buff) {
            if (fread(filelist_buff, 1, filelist_sz, fp) != filelist_sz) {
              freet(filelist_buff);
              filelist_buff = NULL;
            } else {
              *(filelist_buff + filelist_sz) = '\0';
            }
          }
          fclose(fp);
        }
      }

      if (filelist_buff) {
        int filelist_ptr = 0;
        int n = 0;
        char BIGSTK line[HTS_URLMAXSIZE * 2];
        char *primary_ptr = primary + strlen(primary);

        while(filelist_ptr < filelist_sz) {
          int count =
            binput(filelist_buff + filelist_ptr, line, HTS_URLMAXSIZE);
          filelist_ptr += count;
          if (count && line[0]) {
            n++;
            if (strstr(line, ":/") == NULL) {
              strcpybuff(primary_ptr, "http://");
              primary_ptr += strlen(primary_ptr);
            }
            strcpybuff(primary_ptr, line);
            primary_ptr += strlen(primary_ptr);
            strcpybuff(primary_ptr, "\n");
            primary_ptr += 1;
          }
        }
        // fclose(fp);
        hts_log_print(opt, LOG_NOTICE, "%d links added from %s", n,
                      StringBuff(opt->filelist));

        // Free buffer
        freet(filelist_buff);
      } else {
        hts_log_print(opt, LOG_ERROR, "Could not include URL list: %s",
                      StringBuff(opt->filelist));
      }
    }

    // lien primaire
    if (!hts_record_link(opt, "primary", "/primary",
                        fslash(OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt),
                        fconcat(OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt),
                        StringBuff(opt->path_html_utf8), "index.html")),
                        "", "", NULL)) {
      XH_extuninit;             // désallocation mémoire & buffers
      return 0;
    }
    heap_top()->testmode = 0;      // pas mode test
    heap_top()->link_import = 0;   // pas mode import
    heap_top()->depth = opt->depth + 1;    // lien de priorité maximale
    heap_top()->pass2 = 0; // 1ère passe
    heap_top()->retry = opt->retry;        // lien de priorité maximale
    heap_top()->premier = heap_top_index();        // premier lien, objet-père=objet              
    heap_top()->precedent = heap_top_index();      // lien précédent

    // Initialiser cache
    {
      opt->state._hts_in_html_parsing = 4;
      if (!RUN_CALLBACK7(opt, loop, NULL, 0, 0, 0, opt->lien_tot, 0, NULL)) {
        opt->state.exit_xh = 1; // exit requested
      }
      cache_init(&cache, opt);
      opt->state._hts_in_html_parsing = 0;
    }

  }

#if BDEBUG==3
  {
    int i;

    for(i = 0; i < lien_tot; i++) {
      printf("%d>%s%s as %s\n", i, heap(i)->adr, heap(i)->fil, heap(i)->sav);
    }
    for(i = 0; i < filptr; i++) {
      printf("%d>filters=%s\n", i, filters[i]);
    }
  }
#endif

  // backing
  //soc_max=opt->maxsoc;
  if (opt->maxsoc > 0) {
#if BDEBUG==2
    _CLRSCR;
#endif
    // Nombre de fichiers HTML pouvant être présents en mémoire de manière simultannée
    // On prévoit large: les fichiers HTML ne prennent que peu de place en mémoire, et les
    // fichiers non html sont sauvés en direct sur disque.
    // --> 1024 entrées + 32 entrées par socket en supplément
    sback = back_new(opt, opt->maxsoc * 32 + 1024);
    if (sback == NULL) {
      hts_log_print(opt, LOG_PANIC,
                    "Not enough memory, can not allocate %d bytes",
                    (int) ((opt->maxsoc + 1) * sizeof(lien_back)));
      return 0;
    }
  }
  // statistiques
  if (opt->makestat) {
    makestat_fp =
      fopen(fconcat
            (OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt), StringBuff(opt->path_log), "hts-stats.txt"),
            "wb");
    if (makestat_fp != NULL) {
      fprintf(makestat_fp, "HTTrack statistics report, every minutes" LF LF);
      fflush(makestat_fp);
    }
  }
  // tracking -- débuggage
  if (opt->maketrack) {
    maketrack_fp =
      fopen(fconcat
            (OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt), StringBuff(opt->path_log), "hts-track.txt"),
            "wb");
    if (maketrack_fp != NULL) {
      fprintf(maketrack_fp, "HTTrack tracking report, every minutes" LF LF);
      fflush(maketrack_fp);
    }
  }
  // on n'a pas de liens!! (exemple: httrack www.* impossible sans départ..)
  if (opt->lien_tot <= 0) {
    hts_log_print(opt, LOG_ERROR,
                  "You MUST specify at least one complete URL, and not only wildcards!");
  }

  /* Send options to callback functions */
  RUN_CALLBACK0(opt, chopt);

  // attendre une certaine heure..
  if (opt->waittime > 0) {
    int rollover = 0;
    int ok = 0;

    {
      TStamp tl = 0;
      time_t tt;
      struct tm *A;

      tt = time(NULL);
      A = localtime(&tt);
      tl += A->tm_sec;
      tl += A->tm_min * 60;
      tl += A->tm_hour * 60 * 60;
      if (tl > opt->waittime)   // attendre minuit
        rollover = 1;
    }

    // attendre..
    opt->state._hts_in_html_parsing = 5;
    do {
      TStamp tl = 0;
      time_t tt;
      struct tm *A;

      tt = time(NULL);
      A = localtime(&tt);
      tl += A->tm_sec;
      tl += A->tm_min * 60;
      tl += A->tm_hour * 60 * 60;

      if (rollover) {
        if (tl <= opt->waittime)
          rollover = 0;         // attendre heure
      } else {
        if (tl > opt->waittime)
          ok = 1;               // ok!
      }

      {
        int r;

        if (rollover)
          r =
            RUN_CALLBACK7(opt, loop, sback->lnk, sback->count, 0, 0, opt->lien_tot,
                          (int) (opt->waittime - tl + 24 * 3600), NULL);
        else
          r =
            RUN_CALLBACK7(opt, loop, sback->lnk, sback->count, 0, 0, opt->lien_tot,
                          (int) (opt->waittime - tl), NULL);
        if (!r) {
          opt->state.exit_xh = 1;       // exit requested
          ok = 1;
        } else
          Sleep(100);
      }

    } while(!ok);
    opt->state._hts_in_html_parsing = 0;

    // note: recopie de plus haut
    // noter heure actuelle de départ en secondes
    HTS_STAT.stat_timestart = time_local();
    if (opt->shell) {
      last_info_shell = HTS_STAT.stat_timestart;
    }
    if ((opt->makestat) || (opt->maketrack)) {
      makestat_time = HTS_STAT.stat_timestart;
    }

  }
  /* Info for wrappers */
  hts_log_print(opt, LOG_INFO, "engine: start");
  if (!RUN_CALLBACK0(opt, start)) {
    XH_extuninit;
    return 1;
  }
  // ------------------------------------------------------------

  // ------------------------------------------------------------
  // Boucle générale de parcours des liens
  // ------------------------------------------------------------
  do {
    int error = 0;              // si error alors sauter
    int store_errpage = 0;      // c'est une erreur mais on enregistre le html
    int is_binary = 0;          // is a binary file
    int is_loaded_from_file = 0;        // has been loaded from a file (implies is_write=1)
    char BIGSTK loc[HTS_URLMAXSIZE * 2];        // adresse de relocation

    // Ici on charge le fichier (html, gif..) en mémoire
    // Les HTMLs sont traités (si leur priorité est suffisante)

    // effacer r
    hts_init_htsblk(&r);
    //memset(&r, 0, sizeof(htsblk)); r.soc=INVALID_SOCKET;
    r.location = loc;           // en cas d'erreur 3xx (moved)
    // recopier proxy
    if ((r.req.proxy.active = opt->proxy.active)) {
      if (StringBuff(opt->proxy.bindhost) != NULL)
        r.req.proxy.bindhost = StringBuff(opt->proxy.bindhost);
      if (StringBuff(opt->proxy.name) != NULL)
        r.req.proxy.name = StringBuff(opt->proxy.name);
      r.req.proxy.port = opt->proxy.port;
    }
    // et user-agent
    r.req.user_agent = StringBuff(opt->user_agent);
    r.req.referer = StringBuff(opt->referer);
    r.req.from = StringBuff(opt->from);
    r.req.lang_iso = StringBuff(opt->lang_iso);
    r.req.accept = StringBuff(opt->accept);
    r.req.headers = StringBuff(opt->headers);
    r.req.user_agent_send = opt->user_agent_send;

    if (!error) {

      // Skip empty/invalid/done in background
      if (heap(ptr)) {
        while(ptr < opt->lien_tot
              && (heap(ptr))
              && ((((urladr() != NULL) ? (urladr()) : (" "))[0] == '!')
                  || (((urlfil() != NULL) ? (urlfil()) : (" "))[0] == '\0')
                  || ((heap(ptr)->pass2 == -1))
              )
          ) {                   // sauter si lien annulé (ou fil vide)
          if (heap(ptr) != NULL && heap(ptr)->pass2 == -1) {
            hts_log_print(opt, LOG_DEBUG, "link #%d is ready, skipping: %s%s..",
                          ptr, ((urladr() != NULL) ? (urladr()) : (" ")),
                          ((urlfil() != NULL) ? (urlfil()) : (" ")));
          } else {
            hts_log_print(opt, LOG_DEBUG,
                          "link #%d seems ready, skipping: %s%s..", ptr,
                          ((urladr() != NULL) ? (urladr()) : (" ")),
                          ((urlfil() != NULL) ? (urlfil()) : (" ")));
          }
          // remove from stats
          if (heap(ptr)->pass2 == -1) {
            HTS_STAT.stat_background--;
          }
          ptr++;
        }
        // We're done!
        if (ptr == opt->lien_tot) {
          goto jump_if_done;
        }
      }
      if (heap(ptr) != NULL) { // on a qq chose à récupérer?

        hts_log_print(opt, LOG_DEBUG, "Wait get: %s%s", urladr(), urlfil());
#if DEBUG_ROBOTS
        if (strcmp(urlfil(), "/robots.txt") == 0) {
          printf("robots.txt detected\n");
        }
#endif
        // ------------------------------------------------------------
        // DEBUT --RECUPERATION LIEN---
        if (ptr == 0) {         // premier lien à parcourir: lien primaire construit avant
          r.adr = primary;
          primary = NULL;
          r.statuscode = HTTP_OK;
          r.size = strlen(r.adr);
          r.soc = INVALID_SOCKET;
          strcpybuff(r.contenttype, "text/html");
          /*} else if (opt->maxsoc<=0) {   // fichiers 1 à 1 en attente (pas de backing)
             // charger le fichier en mémoire tout bêtement
             r=xhttpget(urladr(),urlfil());
             //
           */
        } else {                // backing, multiples sockets
          /*
           **************************************
           Get the next link, waiting for other files, handling external callbacks
           */
          {
            char BIGSTK buff_err_msg[1024];
            htsmoduleStruct BIGSTK str;
            htsmoduleStructExtended BIGSTK stre;

            buff_err_msg[0] = '\0';
            memset(&str, 0, sizeof(str));
            memset(&stre, 0, sizeof(stre));
            /* */
            str.err_msg = buff_err_msg;
            str.filename = savename();
            str.mime = r.contenttype;
            str.url_host = urladr();
            str.url_file = urlfil();
            str.size = (const int) r.size;
            /* */
            str.addLink = htsAddLink;
            /* */
            str.opt = opt;
            str.sback = sback;
            str.cache = &cache;
            str.hashptr = hashptr;
            str.numero_passe = numero_passe;
            /* */
            str.ptr_ = &ptr;
            /* */
            str.page_charset_ = NULL;
            /* */
            /* */
            stre.r_ = &r;
            /* */
            stre.error_ = &error;
            stre.exit_xh_ = &opt->state.exit_xh;
            stre.store_errpage_ = &store_errpage;
            /* */
            stre.base = base;
            stre.codebase = codebase;
            /* */
            stre.filters_ = &filters;
            stre.filptr_ = &filptr;
            stre.robots_ = &robots;
            stre.hash_ = &hash;
            /* */
            stre.makeindex_done_ = &makeindex_done;
            stre.makeindex_fp_ = &makeindex_fp;
            stre.makeindex_links_ = &makeindex_links;
            stre.makeindex_firstlink_ = makeindex_firstlink;
            /* */
            stre.template_header_ = template_header;
            stre.template_body_ = template_body;
            stre.template_footer_ = template_footer;
            /* */
            stre.stat_fragment_ = &stat_fragment;
            stre.makestat_time = makestat_time;
            stre.makestat_fp = makestat_fp;
            stre.makestat_total_ = &makestat_total;
            stre.makestat_lnk_ = &makestat_lnk;
            stre.maketrack_fp = maketrack_fp;
            /* FUNCTION DEPENDANT */
            stre.loc_ = loc;
            stre.last_info_shell_ = &last_info_shell;
            stre.info_shell_ = &info_shell;

            /* Parse */
            switch (hts_mirror_wait_for_next_file(&str, &stre)) {
            case -1:
              XH_uninit;
              return -1;
              break;
            case 2:
              // Jump to 'continue'
              // This is one of the very very rare cases where goto
              // is acceptable
              // A supplemental flag and if( ) { } would be really messy
              goto jump_if_done;
            }

          }

        }
        // FIN --RECUPERATION LIEN--- 
        // ------------------------------------------------------------

      } else {                  // lien vide..
        hts_log_print(opt, LOG_WARNING, "Warning, link #%d empty", ptr);
        error = 1;
        goto jump_if_done;
      }                         // test si url existe (non vide!)

      // ---tester taille a posteriori---
      // tester r.adr
      if (!error) {
        // erreur, pas de fichier chargé:
        if ((!r.adr) && (r.is_write == 0)
            && (r.statuscode != 301)
            && (r.statuscode != 302)
            && (r.statuscode != 303)
            && (r.statuscode != 307)
            && (r.statuscode != 412)
            && (r.statuscode != 416)
          ) {
          // error=1;

          // peut être que le fichier était trop gros?
          if ((istoobig
               (opt, r.totalsize, opt->maxfile_html, opt->maxfile_nonhtml,
                r.contenttype))
              ||
              (istoobig
               (opt, r.totalsize, opt->maxfile_html, opt->maxfile_nonhtml,
                r.contenttype))) {
            error = 0;
            hts_log_print(opt, LOG_WARNING,
                          "Big file cancelled according to user's preferences: %s%s",
                          urladr(), urlfil());
          }
          // // // error=1;    // ne pas traiter la suite -- euhh si finalement..
        }
      }
      // ---fin tester taille a posteriori---    

      // -------------------- 
      // BOGUS MIME TYPE HACK
      // Check if we have a bogus MIME type
      // example: 
      // Content-type="text/html"
      // and 
      // Content-disposition="foo.jpg"
      // --------------------
      if (!error) {
        if (r.statuscode == HTTP_OK) {  // OK (ou 304 en backing)
          if (r.adr) {          // Written file
            // Buggy SVG (Smiling Spectre)
            if (strcmp(r.contenttype, "image/svg+xml") == 0 && !look_like_xml(r.adr)) {
              // patch it
              strcpybuff(r.contenttype, "application/octet-stream");
              is_binary = 1;
            }
            else if ((is_hypertext_mime(opt, r.contenttype, urlfil()))
                /* Is HTML or Js, .. */
                /* NO - real media is real media, not HTML */
                /*|| (may_be_hypertext_mime(r.contenttype, urlfil()) && (r.adr) ) */
                /* Is real media, .. */
              ) {
              if (strnotempty(r.cdispo)) {      // Content-disposition set!
                if (ishtml(opt, savename()) == 0) {       // Non HTML!!
                  // patch it!
                  strcpybuff(r.contenttype, "application/octet-stream");
                }
              }
            }
          }
        }

        /* Load file if necessary and decode. */
#define LOAD_IN_MEMORY_IF_NECESSARY() do { \
        if (  \
          may_be_hypertext_mime(opt,r.contenttype, urlfil())   /* Is HTML or Js, .. */ \
          && (heap(ptr)->depth>0)            /* Depth > 0 (recurse depth) */ \
          && (r.adr==NULL)        /* HTML Data exists */ \
          && (!store_errpage)     /* Not an html error page */ \
          && (savename()[0]!='\0')  /* Output filename exists */ \
          )  \
        { \
          is_loaded_from_file = 1; \
          r.adr = readfile2(savename(), &r.size); \
          if (r.adr != NULL) { \
            hts_log_print(opt, LOG_INFO, "File successfully loaded for parsing: %s%s (%d bytes)",urladr(),urlfil(),(int)r.size); \
          } else { \
            hts_log_print(opt, LOG_ERROR, "File could not be loaded for parsing: %s%s",urladr(),urlfil()); \
          } \
        } \
} while(0)
        /* Load file and decode if necessary, before content-binary check. (3.43) */
        LOAD_IN_MEMORY_IF_NECESSARY();

        // ------------------------------------
        // BOGUS MIME TYPE HACK II (the revenge)
        // Check if we have a bogus MIME type
        if (HTTP_IS_OK(r.statuscode) && (is_hypertext_mime(opt, r.contenttype, urlfil())  /* Is HTML or Js, .. */
                                         ||may_be_hypertext_mime(opt, r.contenttype, urlfil()))   /* Is real media, .. */
          ) {

          /* Convert charset to UTF-8 - NOT! (what about links ? remote server side will have troubles with converted names) */
          //if (r.adr != NULL && r.size != 0 && opt->convert_utf8) {
          //  char *charset;
          //  char *pos;
          //  if (r.charset[0] != '\0') {
          //    charset = strdup(r.charset);
          //  } else {
          //    charset = hts_getCharsetFromMeta(r.adr, r.size);
          //  }
          //  if (charset != NULL) {
          //    char *const utf8 = hts_convertStringToUTF8(r.adr, r.size, charset);
          //    /* Use new buffer */
          //    if (utf8 != NULL) {
          //      freet(r.adr);
          //      r.size = strlen(utf8);
          //      r.adr = utf8;
          //      /* New UTF-8 charset */
          //      r.charset[0] = '\0';
          //      strcpy(r.charset, "utf-8");
          //    }
          //    /* Free charset */
          //    free(charset);
          //  }
          //}

          /* Check bogus chars */
          if ((r.adr) && (r.size)) {
            unsigned int map[256];
            int i;
            unsigned int nspec = 0;

            map_characters((unsigned char *) r.adr, (unsigned int) r.size,
                           &map[0]);
            for(i = 1; i < 32; i++) {   //  null chars ignored..
              if (!is_realspace(i)
                  && i != 27    /* Damn you ISO2022-xx! */
                ) {
                nspec += map[i];
              }
            }
            /* On-the-fly UCS2 to UTF-8 conversion (note: UCS2 should never be used on the net) */
            if (map[0] > r.size / 10 && r.size % 2 == 0
                &&
                ((((unsigned char) r.adr[0]) == 0xff
                  && ((unsigned char) r.adr[1]) == 0xfe)
                 || (((unsigned char) r.adr[0]) == 0xfe
                     && ((unsigned char) r.adr[1]) == 0xff)
                )
              ) {
#define CH_ADD(c) do {															\
	if (new_offs + 1 > new_capa) {										\
		new_capa *= 2;																	\
		new_adr = (unsigned char*) realloct(new_adr,    \
		                                    new_capa); 	\
		assertf(new_adr != NULL);												\
	}																									\
	new_adr[new_offs++] = (unsigned char) (c);        \
} while(0)
#define CH_ADD_RNG1(c, r, o) do {                   \
	CH_ADD( (c) / (r) + (o) );                        \
	c = (c) % (r);                                    \
} while(0)
#define CH_ADD_RNG0(c, o) do {                      \
	CH_ADD_RNG1(c, 1, o); 	 													\
} while(0)
#define CH_ADD_RNG2(c, r, r2, o) do {               \
	CH_ADD_RNG1(c, (r) * (r2), o);	 									\
} while(0)
              int new_capa = (int) (r.size / 2 + 1);
              int new_offs = 0;
              unsigned char *prev_adr = (unsigned char *) r.adr;
              unsigned char *new_adr = (unsigned char *) malloct(new_capa);
              int i;
              int swap = (((unsigned char) r.adr[0]) == 0xff);

              assertf(new_adr != NULL);
              /* 
                 See http://www.unicode.org/reports/tr28/tr28-3.html#conformance 
                 U+0000..U+007F 00..7F       
                 U+0080..U+07FF C2..DF  80..BF      
                 U+0800..U+0FFF E0      A0..BF  80..BF    
                 U+1000..U+CFFF E1..EC  80..BF  80..BF    
                 U+D000..U+D7FF ED      80..9F  80..BF    
                 U+D800..U+DFFF
                 U+E000..U+FFFF EE..EF  80..BF  80..BF    
               */
              for(i = 0; i < r.size / 2; i++) {
                unsigned short int unic = 0;

                if (swap)
                  unic = prev_adr[i * 2] + (prev_adr[i * 2 + 1] << 8);
                else
                  unic = (prev_adr[i * 2] << 8) + prev_adr[i * 2 + 1];
                if (unic <= 0x7F) {
                  /* U+0000..U+007F 00..7F      */
                  CH_ADD_RNG0(unic, 0x00);
                } else if (unic <= 0x07FF) {
                  /* U+0080..U+07FF C2..DF  80..BF */
                  unic -= 0x0080;
                  CH_ADD_RNG1(unic, 0xbf - 0x80 + 1, 0xc2);
                  CH_ADD_RNG0(unic, 0x80);
                } else if (unic <= 0x0FFF) {
                  /* U+0800..U+0FFF E0      A0..BF  80..BF */
                  unic -= 0x0800;
                  CH_ADD_RNG2(unic, 0xbf - 0x80 + 1, 0xbf - 0xa0 + 1, 0xe0);
                  CH_ADD_RNG1(unic, 0xbf - 0x80 + 1, 0xa0);
                  CH_ADD_RNG0(unic, 0x80);
                } else if (unic <= 0xCFFF) {
                  /* U+1000..U+CFFF E1..EC  80..BF  80..BF */
                  unic -= 0x1000;
                  CH_ADD_RNG2(unic, 0xbf - 0x80 + 1, 0xbf - 0x80 + 1, 0xe1);
                  CH_ADD_RNG1(unic, 0xbf - 0x80 + 1, 0x80);
                  CH_ADD_RNG0(unic, 0x80);
                } else if (unic <= 0xD7FF) {
                  /* U+D000..U+D7FF ED      80..9F  80..BF */
                  unic -= 0xD000;
                  CH_ADD_RNG2(unic, 0xbf - 0x80 + 1, 0x9f - 0x80 + 1, 0xed);
                  CH_ADD_RNG1(unic, 0xbf - 0x80 + 1, 0x80);
                  CH_ADD_RNG0(unic, 0x80);
                } else if (unic <= 0xDFFF) {
                  /* U+D800..U+DFFF */
                  CH_ADD('?');
                  /* ill-formed */
                } else {        /* if (unic <= 0xFFFF) */

                  /* U+E000..U+FFFF EE..EF  80..BF  80..BF */
                  unic -= 0xE000;
                  CH_ADD_RNG2(unic, 0xbf - 0x80 + 1, 0xbf - 0x80 + 1, 0xee);
                  CH_ADD_RNG1(unic, 0xbf - 0x80 + 1, 0x80);
                  CH_ADD_RNG0(unic, 0x80);
                }
              }
              hts_log_print(opt, LOG_WARNING,
                            "File %s%s converted from UCS2 to UTF-8 (old size: %d bytes, new size: %d bytes)",
                            urladr(), urlfil(), (int) r.size, new_offs);
              freet(r.adr);
              r.adr = NULL;
              r.size = new_offs;
              CH_ADD(0);
              r.adr = (char *) new_adr;
#undef CH_ADD
#undef CH_ADD_RNG0
#undef CH_ADD_RNG1
#undef CH_ADD_RNG2
            } else if ((nspec > r.size / 100) && (nspec > 10)) {        // too many special characters
              is_binary = 1;
              strcpybuff(r.contenttype, "application/octet-stream");
              hts_log_print(opt, LOG_WARNING,
                            "File not parsed, looks like binary: %s%s", urladr(),
                            urlfil());
            }

            /* This hack allows you to avoid problems with parsing '\0' characters  */
            if (!is_binary) {
              for(i = 0; i < r.size; i++) {
                if (r.adr[i] == '\0')
                  r.adr[i] = ' ';
              }
            }

          }

        }
      }
      // MOVED IN back_finalize()
      //
      // -------------------- 
      // REAL MEDIA HACK
      // Check if we have to load locally the file
      // --------------------
      //if (!error) {
      //  if (r.statuscode == HTTP_OK) {    // OK (ou 304 en backing)
      //    if (r.adr==NULL) {    // Written file
      //      if (may_be_hypertext_mime(r.contenttype, urlfil())) {   // to parse!
      //        LLint sz;
      //        sz=fsize_utf8(savename());
      //        if (sz>0) {   // ok, exists!
      //          if (sz < 8192) {   // ok, small file --> to parse!
      //            FILE* fp=FOPEN(savename(),"rb");
      //            if (fp) {
      //              r.adr=malloct(sz + 1);
      //              if (r.adr) {
      //                if (fread(r.adr,1,sz,fp) == sz) {
      //                  r.size=sz;
      //                                                r.adr[sz] = '\0';
      //                                                r.is_write = 0;
      //                } else {
      //                  freet(r.adr);
      //                  r.size=0;
      //                  r.adr = NULL;
      //                  r.statuscode=STATUSCODE_INVALID;
      //                  strcpybuff(r.msg, ".RAM read error");
      //                }
      //                fclose(fp);
      //                fp=NULL;
      //                // remove (temporary) file!
      //                remove(savename());
      //              }
      //              if (fp)
      //                fclose(fp);
      //            }
      //          }
      //        }
      //      }
      //    }
      //  }
      //}
      // EN OF REAL MEDIA HACK

      // ---stockage en cache---
      // stocker dans le cache?
      /*
         if (!error) {
         if (ptr>0) {
         if (heap(ptr)) {
         xxcache_mayadd(opt,&cache,&r,urladr(),urlfil(),savename());
         } else
         error=1;
         }
         }
       */
      // ---fin stockage en cache---

      /*
       **************************************
       Check "Moved permanently" and other similar errors, retrying URLs if necessary and handling
       redirect pages.
       */
      if (!error) {
        char BIGSTK buff_err_msg[1024];
        htsmoduleStruct BIGSTK str;
        htsmoduleStructExtended BIGSTK stre;

        buff_err_msg[0] = '\0';
        memset(&str, 0, sizeof(str));
        memset(&stre, 0, sizeof(stre));
        /* */
        str.err_msg = buff_err_msg;
        str.filename = savename();
        str.mime = r.contenttype;
        str.url_host = urladr();
        str.url_file = urlfil();
        str.size = (int) r.size;
        /* */
        str.addLink = htsAddLink;
        /* */
        str.opt = opt;
        str.sback = sback;
        str.cache = &cache;
        str.hashptr = hashptr;
        str.numero_passe = numero_passe;
        /* */
        str.ptr_ = &ptr;
        /* */
        str.page_charset_ = NULL;
        /* */
        /* */
        stre.r_ = &r;
        /* */
        stre.error_ = &error;
        stre.exit_xh_ = &opt->state.exit_xh;
        stre.store_errpage_ = &store_errpage;
        /* */
        stre.base = base;
        stre.codebase = codebase;
        /* */
        stre.filters_ = &filters;
        stre.filptr_ = &filptr;
        stre.robots_ = &robots;
        stre.hash_ = &hash;
        /* */
        stre.makeindex_done_ = &makeindex_done;
        stre.makeindex_fp_ = &makeindex_fp;
        stre.makeindex_links_ = &makeindex_links;
        stre.makeindex_firstlink_ = makeindex_firstlink;
        /* */
        stre.template_header_ = template_header;
        stre.template_body_ = template_body;
        stre.template_footer_ = template_footer;
        /* */
        stre.stat_fragment_ = &stat_fragment;
        stre.makestat_time = makestat_time;
        stre.makestat_fp = makestat_fp;
        stre.makestat_total_ = &makestat_total;
        stre.makestat_lnk_ = &makestat_lnk;
        stre.maketrack_fp = maketrack_fp;

        /* Parse */
        if (hts_mirror_check_moved(&str, &stre) != 0) {
          XH_uninit;
          return -1;
        }

      }

    }                           // if !error

    if (!error) {
#if DEBUG_SHOWTYPES
      if (strstr(REG, r.contenttype) == NULL) {
        strcatbuff(REG, r.contenttype);
        strcatbuff(REG, "\n");
        printf("%s\n", r.contenttype);
        io_flush;
      }
#endif

      /* Load file and decode if necessary, after redirect check. */
      LOAD_IN_MEMORY_IF_NECESSARY();

      // ------------------------------------------------------
      // ok, fichier chargé localement
      // ------------------------------------------------------

      // Vérificateur d'intégrité
#if DEBUG_CHECKINT
      {
        int i;

        for(i = 0; i < sback->count; i++) {
          char si[256];

          sprintf(si, "Test global après back_wait, index %d", i);
          _CHECKINT(&back[i], si)
        }
      }
#endif

      /* info: updated */
      /*
         if (ptr>0) {
         // "mis à jour"
         if ((!r.notmodified) && (opt->is_update) && (!store_errpage)) {    // page modifiée
         if (strnotempty(savename())) {
         HTS_STAT.stat_updated_files++;
         //if ((opt->debug>0) && (opt->log!=NULL)) {
         hts_log_print(opt, LOG_INFO, "File updated: %s%s",urladr(),urlfil());
         }
         } else {
         if (!store_errpage) {
         hts_log_print(opt, LOG_INFO, "File recorded: %s%s",urladr(),urlfil());
         }
         }
         }
       */

      // ------------------------------------------------------
      // traitement (parsing)
      // ------------------------------------------------------

      // traiter
      if (!is_binary && ((is_hypertext_mime(opt, r.contenttype, urlfil()))        /* Is HTML or Js, .. */
                         ||(may_be_hypertext_mime(opt, r.contenttype, urlfil()) && r.adr != NULL) /* Is real media, .. */
          )
          && (heap(ptr)->depth > 0)    /* Depth > 0 (recurse depth) */
          &&(r.adr != NULL)     /* HTML Data exists */
          &&(r.size > 0)        /* And not empty */
          &&(!store_errpage)    /* Not an html error page */
          &&(savename()[0] != '\0')       /* Output filename exists */
        ) {                     // ne traiter que le html si autorisé
        // -- -- -- --
        // Parsing HTML
        if (!error) {
          char page_charset[32];

          /* Remove file if being processed */
          if (is_loaded_from_file) {
            (void) unlink(fconv(OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt), savename()));
            is_loaded_from_file = 0;
          }

          /* Detect charset to convert links into proper UTF8 filenames */
          page_charset[0] = '\0';
          if (opt->convert_utf8) {
            /* HTTP charset is prioritary over meta */
            if (r.charset[0] != '\0') {
              if (strlen(r.charset) < sizeof(page_charset)) {
                strcpy(page_charset, r.charset);
              }
            }
            /* Attempt to find a meta charset */
            else if (is_html_mime_type(r.contenttype)) {
              char *const charset = hts_getCharsetFromMeta(r.adr, r.size);

              if (charset != NULL && strlen(charset) < sizeof(page_charset)) {
                strcpy(page_charset, charset);
              }
              if (charset != NULL)
                free(charset);
            }
            /* Could not detect charset: could it be UTF-8 ? */
            /* No, we can not do that: browsers do not do it 
               (and it would break links). */
            //if (page_charset[0] == '\0') {
            //  if (is_unicode_utf8(r.adr, r.size)) {
            //    strcpy(page_charset, "utf-8");
            //  }
            //}
            /* Could not detect charset */
            if (page_charset[0] == '\0') {
              hts_log_print(opt, LOG_INFO,
                            "Warning: could not detect encoding for: %s%s",
                            urladr(), urlfil());
              /* Fallback to ISO-8859-1 (~== identity) ; accents will look weird */
              strcpy(page_charset, "iso-8859-1");
            }
          }

          /* Info for wrappers */
          hts_log_print(opt, LOG_INFO, "engine: check-html: %s%s", urladr(),
                        urlfil());
          {
            char BIGSTK buff_err_msg[1024];
            htsmoduleStruct BIGSTK str;
            htsmoduleStructExtended BIGSTK stre;

            buff_err_msg[0] = '\0';
            memset(&str, 0, sizeof(str));
            memset(&stre, 0, sizeof(stre));
            /* */
            str.err_msg = buff_err_msg;
            str.filename = savename();
            str.mime = r.contenttype;
            str.url_host = urladr();
            str.url_file = urlfil();
            str.size = (int) r.size;
            /* */
            str.addLink = htsAddLink;
            /* */
            str.opt = opt;
            str.sback = sback;
            str.cache = &cache;
            str.hashptr = hashptr;
            str.numero_passe = numero_passe;
            /* */
            str.ptr_ = &ptr;
            /* */
            str.page_charset_ = page_charset[0] != '\0' ? page_charset : NULL;
            /* */
            /* */
            stre.r_ = &r;
            /* */
            stre.error_ = &error;
            stre.exit_xh_ = &opt->state.exit_xh;
            stre.store_errpage_ = &store_errpage;
            /* */
            stre.base = base;
            stre.codebase = codebase;
            /* */
            stre.filters_ = &filters;
            stre.filptr_ = &filptr;
            stre.robots_ = &robots;
            stre.hash_ = &hash;
            /* */
            stre.makeindex_done_ = &makeindex_done;
            stre.makeindex_fp_ = &makeindex_fp;
            stre.makeindex_links_ = &makeindex_links;
            stre.makeindex_firstlink_ = makeindex_firstlink;
            /* */
            stre.template_header_ = template_header;
            stre.template_body_ = template_body;
            stre.template_footer_ = template_footer;
            /* */
            stre.stat_fragment_ = &stat_fragment;
            stre.makestat_time = makestat_time;
            stre.makestat_fp = makestat_fp;
            stre.makestat_total_ = &makestat_total;
            stre.makestat_lnk_ = &makestat_lnk;
            stre.maketrack_fp = maketrack_fp;

            /* Parse */
            if (htsparse(&str, &stre) != 0) {
              XH_uninit;
              return -1;
            }

            // I'll have to segment this part
// #include "htsparse.c"

          }
        }
        // Fin parsing HTML
        // -- -- -- --

      }                         // si text/html
      // -- -- --
      else {                    // sauver fichier quelconque
        // -- -- --
        // sauver fichier

        /* En cas d'erreur, vérifier que fichier d'erreur existe */
        if (strnotempty(savename()) == 0) {       // chemin de sauvegarde existant
          if (strcmp(urlfil(), "/robots.txt") == 0) {     // pas robots.txt
            if (store_errpage) {        // c'est une page d'erreur
              int create_html_warning = 0;
              int create_gif_warning = 0;

              switch (ishtml(opt, urlfil())) {    /* pas fichier html */
              case 0:          /* non html */
                {
                  char buff[256];

                  guess_httptype(opt, buff, urlfil());
                  if (strcmp(buff, "image/gif") == 0)
                    create_gif_warning = 1;
                }
                break;
              case 1:          /* html */
                if (!r.adr) {
                }
                break;
              default:         /* don't know.. */
                break;
              }
              /* Créer message d'erreur ? */
              if (create_html_warning) {
                char *adr =
                  (char *) malloct(strlen(HTS_DATA_ERROR_HTML) + 1100);
                hts_log_print(opt, LOG_INFO, "Creating HTML warning file (%s)",
                              r.msg);
                if (adr) {
                  if (r.adr) {
                    freet(r.adr);
                    r.adr = NULL;
                  }
                  sprintf(adr, HTS_DATA_ERROR_HTML, r.msg);
                  r.adr = adr;
                }
              } else if (create_gif_warning) {
                char *adr = (char *) malloct(HTS_DATA_UNKNOWN_GIF_LEN);

                hts_log_print(opt, LOG_INFO, "Creating GIF dummy file (%s)",
                              r.msg);
                if (r.adr) {
                  freet(r.adr);
                  r.adr = NULL;
                }
                memcpy(adr, HTS_DATA_UNKNOWN_GIF, HTS_DATA_UNKNOWN_GIF_LEN);
                r.adr = adr;
              }
            }
          }
        }

        if (strnotempty(savename()) == 0) {       // pas de chemin de sauvegarde
          if (strcmp(urlfil(), "/robots.txt") == 0) {     // robots.txt
            if (r.adr) {
              int bptr = 0;
              char BIGSTK line[1024];
              char BIGSTK buff[8192];
              char BIGSTK infobuff[8192];
              int record = 0;

              line[0] = '\0';
              buff[0] = '\0';
              infobuff[0] = '\0';
              //
#if DEBUG_ROBOTS
              printf("robots.txt dump:\n%s\n", r.adr);
#endif
              do {
                char *comm;
                int llen;

                bptr += binput(r.adr + bptr, line, sizeof(line) - 2);
                /* strip comment */
                comm = strchr(line, '#');
                if (comm != NULL) {
                  *comm = '\0';
                }
                /* strip spaces */
                llen = (int) strlen(line);
                while(llen > 0 && is_realspace(line[llen - 1])) {
                  line[llen - 1] = '\0';
                  llen--;
                }
                if (strfield(line, "user-agent:")) {
                  char *a;

                  a = line + 11;
                  while(is_realspace(*a))
                    a++;        // sauter espace(s)
                  if (*a == '*') {
                    if (record != 2)
                      record = 1;       // c pour nous
                  } else if (strfield(a, "httrack") || strfield(a, "winhttrack")
                             || strfield(a, "webhttrack")) {
                    buff[0] = '\0';     // re-enregistrer
                    infobuff[0] = '\0';
                    record = 2; // locked
#if DEBUG_ROBOTS
                    printf("explicit disallow for httrack\n");
#endif
                  } else
                    record = 0;
                } else if (record) {
                  if (strfield(line, "disallow:")) {
                    char *a = line + 9;

                    while(is_realspace(*a))
                      a++;      // sauter espace(s)
                    if (strnotempty(a)) {
#ifdef IGNORE_RESTRICTIVE_ROBOTS
                      if (strcmp(a, "/") != 0 || opt->robots >= 3)
#endif
                      {         /* ignoring disallow: / */
                        if ((strlen(buff) + strlen(a) + 8) < sizeof(buff)) {
                          strcatbuff(buff, a);
                          strcatbuff(buff, "\n");
                          if ((strlen(infobuff) + strlen(a) + 8) <
                              sizeof(infobuff)) {
                            if (strnotempty(infobuff))
                              strcatbuff(infobuff, ", ");
                            strcatbuff(infobuff, a);
                          }
                        }
                      }
#ifdef IGNORE_RESTRICTIVE_ROBOTS
                      else {
                        hts_log_print(opt, LOG_NOTICE,
                                      "Note: %s robots.txt rules are too restrictive, ignoring /",
                                      urladr());
                      }
#endif
                    }
                  }
                }
              } while((bptr < r.size) && (strlen(buff) < (sizeof(buff) - 32)));
              if (strnotempty(buff)) {
                checkrobots_set(&robots, urladr(), buff);
                hts_log_print(opt, LOG_INFO,
                              "Note: robots.txt forbidden links for %s are: %s",
                              urladr(), infobuff);
                hts_log_print(opt, LOG_NOTICE,
                              "Note: due to %s remote robots.txt rules, links beginning with these path will be forbidden: %s (see in the options to disable this)",
                              urladr(), infobuff);
              }
            }
          }
        } else if (r.is_write) {        // déja sauvé sur disque
          /*
             if (!ishttperror(r.statuscode))
             HTS_STAT.stat_files++;
             HTS_STAT.stat_bytes+=r.size;
           */
          //printf("ok......\n");
        } else {
          // Si on doit sauver une page HTML sans la scanner, cela signifie que le niveau de
          // récursion nous en empêche
          // Dans ce cas on met un fichier indiquant ce fait
          // Si par la suite on doit retraiter ce fichier avec un niveau de récursion plus
          // fort, on supprimera le readme, et on scannera le fichier html!
          // note: sauté si store_errpage (càd si page d'erreur, non à scanner!)
          if ((is_hypertext_mime(opt, r.contenttype, urlfil())) && (!store_errpage) && (r.size > 0)) {    // c'est du html!!
            char BIGSTK tempo[HTS_URLMAXSIZE * 2];
            FILE *fp;

            tempo[0] = '\0';
            strcpybuff(tempo, savename());
            strcatbuff(tempo, ".readme");

#if HTS_DOSNAME
            // remplacer / par des slash arrière
            {
              int i = 0;

              while(tempo[i]) {
                if (tempo[i] == '/')
                  tempo[i] = '\\';
                i++;
              }
            }
            // a partir d'ici le slash devient antislash
#endif

            if ((fp = FOPEN(tempo, "wb")) != NULL) {
              fprintf(fp,
                      "Info-file generated by HTTrack Website Copier "
                      HTTRACK_VERSION "%s" CRLF "" CRLF,
                      hts_get_version_info(opt));
              fprintf(fp, "The file %s has not been scanned by HTS" CRLF,
                      savename());
              fprintf(fp,
                      "Some links contained in it may be unreachable locally."
                      CRLF);
              fprintf(fp,
                      "If you want to get these files, you have to set an upper recurse level, ");
              fprintf(fp, "and to rescan the URL." CRLF);
              fclose(fp);
#ifndef _WIN32
              chmod(tempo, HTS_ACCESS_FILE);
#endif
              usercommand(opt, 0, NULL, fconv(OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt), tempo), "",
                          "");
            }

            hts_log_print(opt, LOG_WARNING,
                          "Warning: store %s without scan: %s", r.contenttype,
                          savename());
          } else {
            if ((opt->getmode & 2) != 0) {      // ok autorisé
              hts_log_print(opt, LOG_DEBUG, "Store %s: %s", r.contenttype,
                            savename());
            } else {            // lien non autorisé! (ex: cgi-bin en html)
              hts_log_print(opt, LOG_DEBUG,
                            "non-html file ignored after upload at %s : %s",
                            urladr(), urlfil());
              if (r.adr) {
                freet(r.adr);
                r.adr = NULL;
              }
            }
          }

          //printf("extern=%s\n",r.contenttype);

          // ATTENTION C'EST ICI QU'ON SAUVE LE FICHIER!!          
          if (r.adr != NULL || r.size == 0) {
            file_notify(opt, urladr(), urlfil(), savename(), 1, 1, r.notmodified);
            if (filesave(opt, r.adr, (int) r.size, savename(), urladr(), urlfil()) !=
                0) {
              int fcheck;

              if ((fcheck = check_fatal_io_errno())) {
                hts_log_print(opt, LOG_ERROR,
                              "Mirror aborted: disk full or filesystem problems");
                opt->state.exit_xh = -1;        /* fatal error */
              }
              hts_log_print(opt, LOG_ERROR | LOG_ERRNO,
                            "Unable to save file %s", savename());
            } else {
              /*
                 if (!ishttperror(r.statuscode))
                 HTS_STAT.stat_files++;
                 HTS_STAT.stat_bytes+=r.size;
               */
            }
          }

        }

        /* Parsing of other media types (java, ram..) */
        /*
           if (strfield2(r.contenttype,"audio/x-pn-realaudio")) {
           hts_log_print(opt, LOG_DEBUG, "(Real Media): parsing %s",savename());
           if (fexist(savename())) {   // ok, existe bien!
           FILE* fp=FOPEN(savename(),"r+b");
           if (fp) {
           if (!fseek(fp,0,SEEK_SET)) {
           char BIGSTK line[HTS_URLMAXSIZE*2];
           linput(fp,line,HTS_URLMAXSIZE);
           if (strnotempty(line)) {
           hts_log_print(opt, LOG_DEBUG, "(Real Media): detected %s",line);
           }
           }
           fclose(fp);
           }
           }
           } else */

        /* External modules */
        if (opt->parsejava && (opt->parsejava & HTSPARSE_NO_CLASS) == 0
            && fexist(savename())) {
          char BIGSTK buff_err_msg[1024];
          htsmoduleStruct BIGSTK str;

          buff_err_msg[0] = '\0';
          memset(&str, 0, sizeof(str));
          /* */
          str.err_msg = buff_err_msg;
          str.filename = savename();
          str.mime = r.contenttype;
          str.url_host = urladr();
          str.url_file = urlfil();
          str.size = (int) r.size;
          /* */
          str.addLink = htsAddLink;
          /* */
          str.opt = opt;
          str.sback = sback;
          str.cache = &cache;
          str.hashptr = hashptr;
          str.numero_passe = numero_passe;
          str.ptr_ = &ptr;

          /* Parse if recognized */
          switch (hts_parse_externals(&str)) {
          case 1:
            hts_log_print(opt, LOG_DEBUG,
                          "(External module): parsed successfully %s",
                          savename());
            break;
          case 0:
            hts_log_print(opt, LOG_DEBUG,
                          "(External module): couldn't parse successfully %s : %s",
                          savename(), str.err_msg);
            break;
          }
        }

      }                         // text/html ou autre

      /* Post-processing */
      if (fexist(savename())) {
        usercommand(opt, 0, NULL, savename(), urladr(), urlfil());
      }

    }                           // if !error

  jump_if_done:
    // libérer les liens
    if (r.adr) {
      freet(r.adr);
      r.adr = NULL;
    }                           // libérer la mémoire!

    // prochain lien
    ptr++;

    // faut-il sauter le(s) lien(s) suivant(s)? (fichiers images à passer après les html)
    if (opt->getmode & 4) {     // sauver les non html après
      // sauter les fichiers selon la passe
      if (!numero_passe) {
        while((ptr < opt->lien_tot) ? (heap(ptr)->pass2) : 0)
          ptr++;
      } else {
        while((ptr < opt->lien_tot) ? (!heap(ptr)->pass2) : 0)
          ptr++;
      }
      if (ptr >= opt->lien_tot) {    // fin de boucle
        if (!numero_passe) {    // première boucle
          hts_log_print(opt, LOG_DEBUG, "Now getting non-html files...");
          numero_passe = 1;     // seconde boucle
          ptr = 0;
          // prochain pass2
          while((ptr < opt->lien_tot) ? (!heap(ptr)->pass2) : 0)
            ptr++;

          //printf("first link==%d\n");

        }
      }
    }
    // copy abort state if necessary from outside
    //if (!exit_xh && opt->state.exit_xh) {
    //  exit_xh=opt->state.exit_xh;
    //}
    // a-t-on dépassé le quota?
    if (!back_checkmirror(opt)) {
      ptr = opt->lien_tot;
    } else if (opt->state.exit_xh) {    // sortir
      if (opt->state.exit_xh == 1) {
        hts_log_print(opt, LOG_ERROR, "Exit requested by shell or user");
      } else {
        hts_log_print(opt, LOG_ERROR, "Exit requested by engine");
      }
      ptr = opt->lien_tot;
    }
  } while(ptr < opt->lien_tot);
  //
  //
  //

  /*
     Ensure the index is being closed
   */
  HT_INDEX_END;

  /* 
     updating-a-remotely-deteted-website hack
     no much data transferred, no data saved
     <no files successfulyl saved>
     we assume that something was bad (no connection)
     just backup old cache and restore everything
   */
  if ((HTS_STAT.stat_files <= 0)
      && (HTS_STAT.HTS_TOTAL_RECV < 32768)      /* should be fine */
    ) {
    hts_log_print(opt, LOG_NOTICE,
                  "No data seems to have been transferred during this session! : restoring previous one!");
    XH_uninit;
    if ((fexist
         (fconcat
          (OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt), StringBuff(opt->path_log), "hts-cache/old.dat")))
        &&
        (fexist
         (fconcat
          (OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt), StringBuff(opt->path_log),
           "hts-cache/old.ndx")))) {
      remove(fconcat
             (OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt), StringBuff(opt->path_log),
              "hts-cache/new.dat"));
      remove(fconcat
             (OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt), StringBuff(opt->path_log),
              "hts-cache/new.ndx"));
      remove(fconcat
             (OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt), StringBuff(opt->path_log),
              "hts-cache/new.lst"));
      remove(fconcat
             (OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt), StringBuff(opt->path_log),
              "hts-cache/new.txt"));
      rename(fconcat
             (OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt), StringBuff(opt->path_log),
              "hts-cache/old.dat"), fconcat(OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt),
                                            StringBuff(opt->path_log),
                                            "hts-cache/new.dat"));
      rename(fconcat
             (OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt), StringBuff(opt->path_log),
              "hts-cache/old.ndx"), fconcat(OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt),
                                            StringBuff(opt->path_log),
                                            "hts-cache/new.ndx"));
      rename(fconcat
             (OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt), StringBuff(opt->path_log),
              "hts-cache/old.lst"), fconcat(OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt),
                                            StringBuff(opt->path_log),
                                            "hts-cache/new.lst"));
      rename(fconcat
             (OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt), StringBuff(opt->path_log),
              "hts-cache/old.txt"), fconcat(OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt),
                                            StringBuff(opt->path_log),
                                            "hts-cache/new.txt"));
    }
    opt->state.exit_xh = 2;     /* interrupted (no connection detected) */
    return 1;
  }
  // info text  
  if (cache.txt) {
    fclose(cache.txt);
    cache.txt = NULL;
  }
  // purger!
  if (cache.lst) {
    fclose(cache.lst);
    cache.lst = opt->state.strc.lst = NULL;
    if (opt->delete_old) {
      FILE *old_lst, *new_lst;

      //
      opt->state._hts_in_html_parsing = 3;
      //
      old_lst =
        fopen(fconcat
              (OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt), StringBuff(opt->path_log),
               "hts-cache/old.lst"), "rb");
      if (old_lst) {
        const size_t sz =
          off_t_to_size_t(fsize(fconcat
                (OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt), StringBuff(opt->path_log),
                 "hts-cache/new.lst")));
        new_lst =
          fopen(fconcat
                (OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt), StringBuff(opt->path_log),
                 "hts-cache/new.lst"), "rb");
        if (new_lst != NULL && sz != (size_t) -1) {
          char *adr = (char *) malloct(sz);

          if (adr) {
            if (fread(adr, 1, sz, new_lst) == sz) {
              char line[1100];
              int purge = 0;

              while(!feof(old_lst)) {
                linput(old_lst, line, 1000);
                if (!strstr(adr, line)) {       // fichier non trouvé dans le nouveau?
                  char BIGSTK file[HTS_URLMAXSIZE * 2];

                  strcpybuff(file, StringBuff(opt->path_html));
                  strcatbuff(file, line + 1);
                  file[strlen(file) - 1] = '\0';
                  if (fexist(file)) {   // toujours sur disque: virer
                    hts_log_print(opt, LOG_INFO, "Purging %s", file);
                    remove(file);
                    purge = 1;
                  }
                }
              }
              {
                fseek(old_lst, 0, SEEK_SET);
                while(!feof(old_lst)) {
                  linput(old_lst, line, 1000);
                  while(strnotempty(line) && (line[strlen(line) - 1] != '/')
                        && (line[strlen(line) - 1] != '\\')) {
                    line[strlen(line) - 1] = '\0';
                  }
                  if (strnotempty(line))
                    line[strlen(line) - 1] = '\0';
                  if (strnotempty(line))
                    if (!strstr(adr, line)) {   // non trouvé?
                      char BIGSTK file[HTS_URLMAXSIZE * 2];

                      strcpybuff(file, StringBuff(opt->path_html));
                      strcatbuff(file, line + 1);
                      while((strnotempty(file)) && (rmdir(file) == 0)) {        // ok, éliminé (existait)
                        purge = 1;
                        if (opt->log) {
                          hts_log_print(opt, LOG_INFO, "Purging directory %s/",
                                        file);
                          while(strnotempty(file)
                                && (file[strlen(file) - 1] != '/')
                                && (file[strlen(file) - 1] != '\\')) {
                            file[strlen(file) - 1] = '\0';
                          }
                          if (strnotempty(file))
                            file[strlen(file) - 1] = '\0';
                        }
                      }
                    }
                }
              }
              //
              if (!purge) {
                hts_log_print(opt, LOG_INFO, "No files purged");
              }
            }
            freet(adr);
          }
          fclose(new_lst);
        }
        fclose(old_lst);
      }
      //
      opt->state._hts_in_html_parsing = 0;
    }
  }
  // fin purge!

  // Indexation
  if (opt->kindex)
    index_finish(StringBuff(opt->path_html), opt->kindex);

  // afficher résumé dans log
  if (opt->log != NULL) {
    char BIGSTK finalInfo[8192];
    int error = fspc(opt, NULL, "error");
    int warning = fspc(opt, NULL, "warning");
    int info = fspc(opt, NULL, "info");
    char BIGSTK htstime[256];
    char BIGSTK infoupdated[256];

    // int n=(int) (stat_loaded/(time_local()-HTS_STAT.stat_timestart));
    LLint n =
      (LLint) (HTS_STAT.HTS_TOTAL_RECV /
               (max(1, time_local() - HTS_STAT.stat_timestart)));

    sec2str(htstime, time_local() - HTS_STAT.stat_timestart);
    //sprintf(finalInfo + strlen(finalInfo),LF"HTS-mirror complete in %s : %d links scanned, %d files written (%d bytes overall) [%d bytes received at %d bytes/sec]"LF,htstime,lien_tot-1,HTS_STAT.stat_files,stat_bytes,stat_loaded,n);
    infoupdated[0] = '\0';
    if (opt->is_update) {
      if (HTS_STAT.stat_updated_files > 0) {
        sprintf(infoupdated, ", %d files updated",
                (int) HTS_STAT.stat_updated_files);
      } else {
        sprintf(infoupdated, ", no files updated");
      }
    }
    finalInfo[0] = '\0';
    sprintf(finalInfo + strlen(finalInfo),
            "HTTrack Website Copier/" HTTRACK_VERSION
            " mirror complete in %s : " "%d links scanned, %d files written ("
            LLintP " bytes overall)%s " "[" LLintP " bytes received at " LLintP
            " bytes/sec]", htstime, (int) opt->lien_tot - 1,
            (int) HTS_STAT.stat_files, (LLint) HTS_STAT.stat_bytes, infoupdated,
            (LLint) HTS_STAT.HTS_TOTAL_RECV, (LLint) n);

    if (HTS_STAT.total_packed > 0 && HTS_STAT.total_unpacked > 0) {
      int packed_ratio =
        (int) ((LLint) (HTS_STAT.total_packed * 100) / HTS_STAT.total_unpacked);
      sprintf(finalInfo + strlen(finalInfo),
              ", " LLintP
              " bytes transferred using HTTP compression in %d files, ratio %d%%",
              (LLint) HTS_STAT.total_unpacked, HTS_STAT.total_packedfiles,
              (int) packed_ratio);
    }
    if (!opt->nokeepalive && HTS_STAT.stat_sockid > 0
        && HTS_STAT.stat_nrequests > HTS_STAT.stat_sockid) {
      int rq = (HTS_STAT.stat_nrequests * 10) / HTS_STAT.stat_sockid;

      sprintf(finalInfo + strlen(finalInfo), ", %d.%d requests per connection",
              rq / 10, rq % 10);
    }
    sprintf(finalInfo + strlen(finalInfo), LF);
    if (error)
      sprintf(finalInfo + strlen(finalInfo),
              "(%d errors, %d warnings, %d messages)" LF, error, warning, info);
    else
      sprintf(finalInfo + strlen(finalInfo),
              "(No errors, %d warnings, %d messages)" LF, warning, info);

    // Log
    fprintf(opt->log, LF "%s", finalInfo);

    // Close ZIP
    if (cache.zipOutput) {
      zipClose(cache.zipOutput, finalInfo);
      cache.zipOutput = NULL;
    }
  }
  // fin afficher résumé dans log

  // ending
  usercommand(opt, 0, NULL, NULL, NULL, NULL);

  // désallocation mémoire & buffers
  XH_uninit;

  return 1;                     // OK
}

// version 2 pour le reste

// Estimate transfer rate
// a little bit complex, but not too much
/*
  .. : idle
  ^  : event

  ----|----|----|----|----|----|----|----|---->
   1    2    3    4    5    6    7    8    9   time (seconds)
  ----|----|----|----|----|----|----|----|---->
  ^........^.........^.........^.........^.... timer 0
  ----^.........^.........^.........^......... timer 1
           0    1    0    1    0    1    0     timer N sets its statistics
      *         *         *         *          timer 0 resync timer 1

  Therefore, each seconds, we resync the transfer rate with 2-seconds

*/
int engine_stats(void) {
#if 0
  static FILE *debug_fp = NULL; /* ok */

  if (!debug_fp)
    debug_fp = fopen("esstat.txt", "wb");
#endif
  HTS_STAT.stat_nsocket = HTS_STAT.stat_errors = HTS_STAT.nbk = 0;
  HTS_STAT.nb = 0;
  if (HTS_STAT.HTS_TOTAL_RECV > 2048) {
    TStamp cdif = mtime_local();
    int i;

    for(i = 0; i < 2; i++) {
      if ((cdif - HTS_STAT.istat_timestart[i]) >= 2000) {
        TStamp dif;

#if 0
        fprintf(debug_fp, "set timer %d\n", i);
        fflush(debug_fp);
#endif
        dif = cdif - HTS_STAT.istat_timestart[i];
        if ((TStamp) (dif / 1000) > 0) {
          LLint byt = (HTS_STAT.HTS_TOTAL_RECV - HTS_STAT.istat_bytes[i]);

          HTS_STAT.rate = (LLint) ((TStamp) ((TStamp) byt / (dif / 1000)));
          HTS_STAT.istat_idlasttimer = i;       // this timer recently sets the stats
          //
          HTS_STAT.istat_bytes[i] = HTS_STAT.HTS_TOTAL_RECV;
          HTS_STAT.istat_timestart[i] = cdif;
        }
        return 1;               /* refreshed */
      }
    }

    // resynchronization between timer 0 (master) and 1 (slave)
    // timer #0 resync timer #1 when reaching 1 second limit
    if (HTS_STAT.istat_reference01 != HTS_STAT.istat_timestart[0]) {
      if ((cdif - HTS_STAT.istat_timestart[0]) >= 1000) {
#if 0
        fprintf(debug_fp, "resync timer 1\n");
        fflush(debug_fp);
#endif
        HTS_STAT.istat_bytes[1] = HTS_STAT.HTS_TOTAL_RECV;
        HTS_STAT.istat_timestart[1] = cdif;
        HTS_STAT.istat_reference01 = HTS_STAT.istat_timestart[0];
      }
    }

  }
  return 0;
}

#define _FILTERS     (*opt->filters.filters)
#define _FILTERS_PTR (opt->filters.filptr)
#define _ROBOTS      ((robots_wizard*)opt->robotsptr)

// bannir host (trop lent etc)
void host_ban(httrackp * opt, int ptr,
              struct_back * sback, const char *host) {
  lien_back *const back = sback->lnk;
  const int back_max = sback->count;

  //int l;
  int i;

  if (host[0] == '!')
    return;                     // erreur.. déja cancellé.. bizarre.. devrait pas arriver

  /* sanity check */
  if (*_FILTERS_PTR + 1 >= opt->maxfilter) {
    opt->maxfilter += HTS_FILTERSINC;
    if (filters_init(&_FILTERS, opt->maxfilter, HTS_FILTERSINC) == 0) {
      printf("PANIC! : Too many filters : >%d [%d]\n", *_FILTERS_PTR, __LINE__);
      hts_log_print(opt, LOG_PANIC, "Too many filters, giving up..(>%d)",
                    *_FILTERS_PTR);
      hts_log_print(opt, LOG_INFO,
                    "To avoid that: use #F option for more filters (example: -#F5000)");
      assertf("too many filters - giving up" == NULL);
    }
  }
  // interdire host
  assertf((*_FILTERS_PTR) < opt->maxfilter);
  if (*_FILTERS_PTR < opt->maxfilter) {
    strcpybuff(_FILTERS[*_FILTERS_PTR], "-");
    strcatbuff(_FILTERS[*_FILTERS_PTR], host);
    strcatbuff(_FILTERS[*_FILTERS_PTR], "/*");  // host/ * interdit
    (*_FILTERS_PTR)++;
  }
  // oups
  if (strlen(host) <= 1) {      // euhh?? longueur <= 1
    if (strcmp(host, "file://")) {
      //## if (host[0]!=lOCAL_CHAR) {  // pas local
      hts_log_print(opt, LOG_PANIC,
                    "PANIC! HostCancel detected memory leaks [char %d]",
                    host[0]);
      return;                   // purée
    }
  }
  // couper connexion
  for(i = 0; i < back_max; i++) {
    if (back[i].status >= 0)    // réception OU prêt
      if (strfield2(back[i].url_adr, host)) {
#if HTS_DEBUG_CLOSESOCK
        DEBUG_W("host control: deletehttp\n");
#endif
        back[i].status = 0;     // terminé
        back_set_finished(sback, i);
        if (back[i].r.soc != INVALID_SOCKET)
          deletehttp(&back[i].r);
        back[i].r.soc = INVALID_SOCKET;
        back[i].r.statuscode = STATUSCODE_TIMEOUT;      // timeout (peu importe si c'est un traffic jam)
        strcpybuff(back[i].r.msg, "Link Cancelled by host control");
        hts_log_print(opt, LOG_DEBUG, "Shutdown: %s%s", back[i].url_adr,
                      back[i].url_fil);
      }
  }

  // effacer liens
  //l=strlen(host);
  for(i = 0; i < opt->lien_tot; i++) {
    //if (heap(i)->adr_len==l) {    // même taille de chaîne
    // Calcul de taille sécurisée
    if (heap(i)) {
      if (heap(i)->adr) {
        int l = 0;

        while((heap(i)->adr[l]) && (l < 1020))
          l++;
        if ((l > 0) && (l < 1020)) {    // sécurité
          if (strfield2(jump_identification_const(heap(i)->adr), host)) {    // host
            hts_log_print(opt, LOG_DEBUG, "Cancel: %s%s", heap(i)->adr,
                          heap(i)->fil);
            hts_invalidate_link(opt, i);  // invalidate hashtable entry
            // on efface pas le hash, because si on rencontre le lien, reverif sav..
          }
        } else {
          if (opt->log != NULL) {
            char dmp[1040];

            dmp[0] = '\0';
            strncatbuff(dmp, heap(i)->adr, 1024);
            hts_log_print(opt, LOG_WARNING,
                          "WARNING! HostCancel detected memory leaks [len %d at %d]",
                          l, i);
            hts_log_print(opt, LOG_WARNING,
                          "dump 1024 bytes (address %p): " LF "%s",
                          heap(i)->adr, dmp);
          }
        }
      } else {
        hts_log_print(opt, LOG_WARNING,
                      "WARNING! HostCancel detected memory leaks [adr at %d]",
                      i);
      }
    } else {
      hts_log_print(opt, LOG_WARNING,
                    "WARNING! HostCancel detected memory leaks [null at %d]",
                    i);
    }
    //}
  }
}

int filters_init(char ***ptrfilters, int maxfilter, int filterinc) {
  char **filters = *ptrfilters;
  int filter_max = maximum(maxfilter, 128);

  if (filters == NULL) {
    filters = (char **) malloct(sizeof(char *) * (filter_max + 2));
    memset(filters, 0, sizeof(char *) * (filter_max + 2));      // filters[0] == 0
  } else {
    filters = (char **) realloct(filters, sizeof(char *) * (filter_max + 2));
  }
  if (filters) {
    if (filters[0] == NULL) {
      filters[0] =
        (char *) malloct(sizeof(char) * (filter_max + 2) *
                         (HTS_URLMAXSIZE * 2));
      memset(filters[0], 0,
             sizeof(char) * (filter_max + 2) * (HTS_URLMAXSIZE * 2));
    } else {
      filters[0] =
        (char *) realloct(filters[0],
                          sizeof(char) * (filter_max +
                                          2) * (HTS_URLMAXSIZE * 2));
    }
    if (filters[0] == NULL) {
      freet(filters);
      filters = NULL;
    }
  }
  if (filters != NULL) {
    int i;
    int from;

    if (filterinc == 0)
      from = 0;
    else
      from = filter_max - filterinc;
    for(i = 0; i <= filter_max; i++) {  // PLUS UN (sécurité)
      filters[i] = filters[0] + i * (HTS_URLMAXSIZE * 2);
    }
    for(i = from; i <= filter_max; i++) {       // PLUS UN (sécurité)
      filters[i][0] = '\0';     // clear
    }
  }
  *ptrfilters = filters;
  return (filters != NULL) ? filter_max : 0;
}

static int mkdir_compat(const char *pathname) {
#ifdef _WIN32
  return mkdir(pathname);
#else
  return mkdir(pathname, HTS_ACCESS_FOLDER);
#endif
}

/* path must end with "/" or with the finename (/tmp/bar/ or /tmp/bar/foo.zip) */
/* Note: preserve errno */
HTSEXT_API int dir_exists(const char *path) {
  const int err = errno;
  STRUCT_STAT st;
  char BIGSTK file[HTS_URLMAXSIZE * 2];
  int i = 0;

  if (strnotempty(path) == 0) {
    return 0;
  }
  if (strlen(path) > HTS_URLMAXSIZE) {
    return 0;
  }

  /* Get a copy */
  strcpybuff(file, path);
#ifdef _WIN32
  /* To system name */
  for(i = 0; file[i] != 0; i++) {
    if (file[i] == '/') {
      file[i] = PATH_SEPARATOR;
    }
  }
#endif
  /* Get prefix (note: file can not be empty here) */
  for(i = (int) strlen(file) - 1; i > 0 && file[i] != PATH_SEPARATOR; i--) ;
  for(; i > 0 && file[i] == PATH_SEPARATOR; i--) ;
  file[i + 1] = '\0';

  /* Check the final dir */
  if (STAT(file, &st) == 0 && S_ISDIR(st.st_mode)) {
    return 1;                   /* EXISTS */
  }
  errno = err;
  return 0;                     /* DOES NOT EXIST */
}

/* path must end with "/" or with the finename (/tmp/bar/ or /tmp/bar/foo.zip) */
/* Note: *not* UTF-8 */
HTSEXT_API int structcheck(const char *path) {
  struct stat st;
  char BIGSTK tmpbuf[HTS_URLMAXSIZE * 2];
  char BIGSTK file[HTS_URLMAXSIZE * 2];
  int i = 0;
  int npaths;

  if (strnotempty(path) == 0)
    return 0;
  if (strlen(path) > HTS_URLMAXSIZE) {
    errno = EINVAL;
    return -1;
  }

  /* Get a copy */
  strcpybuff(file, path);
#ifdef _WIN32
  /* To system name */
  for(i = 0; file[i] != 0; i++) {
    if (file[i] == '/') {
      file[i] = PATH_SEPARATOR;
    }
  }
#endif
  /* Get prefix (note: file can not be empty here) */
  for(i = (int) strlen(file) - 1; i > 0 && file[i] != PATH_SEPARATOR; i--) ;
  for(; i > 0 && file[i] == PATH_SEPARATOR; i--) ;
  file[i + 1] = '\0';

  /* First check the final dir */
  if (stat(file, &st) == 0 && S_ISDIR(st.st_mode)) {
    return 0;                   /* OK */
  }

  /* Start from the beginning */
  i = 0;

  /* Skip irrelevant part (the root slash, or the drive path) */
#ifdef _WIN32
  if (file[0] != 0 && file[1] == ':') { /* f:\ */
    i += 2;
    if (file[i] == PATH_SEPARATOR) {    /* f:\ */
      i++;
    }
  } else if (file[0] == PATH_SEPARATOR && file[1] == PATH_SEPARATOR) {  /* \\mch */
    int j;

    i += 2;
    // skip machine and first dir (share) or we'll have troubles (TomZ)
    for(j = 0; j < 2; j++) {
      // skip segment
      for(; file[i] != '\0' && file[i] != PATH_SEPARATOR; i++) ;
      // skip final separator
      if (file[i] == PATH_SEPARATOR) {  /* f:\ */
        i++;
      }
    }
  }
#endif

  /* Check paths */
  for(npaths = 1;; npaths++) {
    char end_char;

    /* Go to next path */

    /* Skip separator(s) */
    for(; file[i] == PATH_SEPARATOR; i++) ;
    /* Next separator */
    for(; file[i] != 0 && file[i] != PATH_SEPARATOR; i++) ;

    /* Check */
    end_char = file[i];
    if (end_char != 0) {
      file[i] = '\0';
    }
    if (stat(file, &st) == 0) { /* Something exists */
      if (!S_ISDIR(st.st_mode)) {
#if HTS_REMOVE_ANNOYING_INDEX
        if (S_ISREG(st.st_mode)) {      /* Regular file in place ; move it and create directory */
          sprintf(tmpbuf, "%s.txt", file);
          if (rename(file, tmpbuf) != 0) {      /* Can't rename regular file */
            return -1;
          }
          if (mkdir_compat(file) != 0) {        /* Can't create directory */
            return -1;
          }
        }
#else
#error Not implemented
#endif
      }
    } else {                    /* Nothing exists ; create directory */
      if (mkdir_compat(file) != 0) {    /* Can't create directory */
        return -1;
      }
    }
    if (end_char == 0) {        /* End */
      break;
    } else {
      file[i] = end_char;       /* Restore / */
    }
  }
  return 0;
}

/* path must end with "/" or with the finename (/tmp/bar/ or /tmp/bar/foo.zip) */
/* Note: UTF-8 */
HTSEXT_API int structcheck_utf8(const char *path) {
  STRUCT_STAT st;
  char BIGSTK tmpbuf[HTS_URLMAXSIZE * 2];
  char BIGSTK file[HTS_URLMAXSIZE * 2];
  int i = 0;
  int npaths;

  if (strnotempty(path) == 0)
    return 0;
  if (strlen(path) > HTS_URLMAXSIZE) {
    errno = EINVAL;
    return -1;
  }

  /* Get a copy */
  strcpybuff(file, path);
#ifdef _WIN32
  /* To system name */
  for(i = 0; file[i] != 0; i++) {
    if (file[i] == '/') {
      file[i] = PATH_SEPARATOR;
    }
  }
#endif
  /* Get prefix (note: file can not be empty here) */
  for(i = (int) strlen(file) - 1; i > 0 && file[i] != PATH_SEPARATOR; i--) ;
  for(; i > 0 && file[i] == PATH_SEPARATOR; i--) ;
  file[i + 1] = '\0';

  /* First check the final dir */
  if (STAT(file, &st) == 0 && S_ISDIR(st.st_mode)) {
    return 0;                   /* OK */
  }

  /* Start from the beginning */
  i = 0;

  /* Skip irrelevant part (the root slash, or the drive path) */
#ifdef _WIN32
  if (file[0] != 0 && file[1] == ':') { /* f:\ */
    i += 2;
    if (file[i] == PATH_SEPARATOR) {    /* f:\ */
      i++;
    }
  } else if (file[0] == PATH_SEPARATOR && file[1] == PATH_SEPARATOR) {  /* \\mch */
    int j;

    i += 2;
    // skip machine and first dir (share) or we'll have troubles (TomZ)
    for(j = 0; j < 2; j++) {
      // skip segment
      for(; file[i] != '\0' && file[i] != PATH_SEPARATOR; i++) ;
      // skip final separator
      if (file[i] == PATH_SEPARATOR) {  /* f:\ */
        i++;
      }
    }
  }
#endif

  /* Check paths */
  for(npaths = 1;; npaths++) {
    char end_char;

    /* Go to next path */

    /* Skip separator(s) */
    for(; file[i] == PATH_SEPARATOR; i++) ;
    /* Next separator */
    for(; file[i] != 0 && file[i] != PATH_SEPARATOR; i++) ;

    /* Check */
    end_char = file[i];
    if (end_char != 0) {
      file[i] = '\0';
    }
    if (STAT(file, &st) == 0) { /* Something exists */
      if (!S_ISDIR(st.st_mode)) {
#if HTS_REMOVE_ANNOYING_INDEX
        if (S_ISREG(st.st_mode)) {      /* Regular file in place ; move it and create directory */
          sprintf(tmpbuf, "%s.txt", file);
          if (RENAME(file, tmpbuf) != 0) {      /* Can't rename regular file */
            return -1;
          }
          if (MKDIR(file) != 0) {       /* Can't create directory */
            return -1;
          }
        }
#else
#error Not implemented
#endif
      }
    } else {                    /* Nothing exists ; create directory */
      if (MKDIR(file) != 0) {   /* Can't create directory */
        return -1;
      }
    }
    if (end_char == 0) {        /* End */
      break;
    } else {
      file[i] = end_char;       /* Restore / */
    }
  }
  return 0;
}

// sauver un fichier
int filesave(httrackp * opt, const char *adr, int len, const char *s,
             const char *url_adr, const char *url_fil) {
  FILE *fp;

  // écrire le fichier
  if ((fp = filecreate(&opt->state.strc, s)) != NULL) {
    int nl = 0;

    if (len > 0) {
      nl = (int) fwrite(adr, 1, len, fp);
    }
    fclose(fp);
    if (nl != len)              // erreur
      return -1;
  } else
    return -1;

  return 0;
}

/* We should stop */
int check_fatal_io_errno(void) {
  switch (errno) {
#ifdef EMFILE
  case EMFILE:                 /* Too many open files */
#endif
#ifdef ENOSPC
  case ENOSPC:                 /* No space left on device */
#endif
#ifdef EROFS
  case EROFS:                  /* Read-only file system */
#endif
    return 1;
    break;
  }
  return 0;
}

// ouvrir un fichier (avec chemin Un*x)
/* Note: utf-8 */
FILE *filecreate(filenote_strc * strc, const char *s) {
  char BIGSTK fname[HTS_URLMAXSIZE * 2];
  FILE *fp;
  int last_errno = 0;

  fname[0] = '\0';

  // noter lst
  if (strc != NULL) {
    filenote(strc, s, NULL);
  }

  strcpybuff(fname, s);
#if HTS_DOSNAME
  // remplacer / par des slash arrière
  {
    int i = 0;

    while(fname[i]) {
      if (fname[i] == '/')
        fname[i] = '\\';
      i++;
    }
  }
  // a partir d'ici le slash devient antislash
#endif

  /* Try to open the file */
  fp = FOPEN(fname, "wb");

  /* Error ? Check the directory structure and retry. */
  if (fp == NULL) {
    last_errno = errno;
    if (structcheck_utf8(s) != 0) {
      last_errno = errno;
    } else {
      last_errno = 0;
    }
    fp = FOPEN(fname, "wb");
  }
  if (fp == NULL && last_errno != 0) {
    errno = last_errno;
  }
#ifndef _WIN32
  if (fp != NULL)
    chmod(fname, HTS_ACCESS_FILE);
#endif
  return fp;
}

// ouvrir un fichier (avec chemin Un*x)
FILE *fileappend(filenote_strc * strc, const char *s) {
  char BIGSTK fname[HTS_URLMAXSIZE * 2];
  FILE *fp;

  fname[0] = '\0';

  // noter lst
  filenote(strc, s, NULL);

  // if (*s=='/') strcpybuff(fname,s+1); else strcpybuff(fname,s);    // pas de / (root!!) // ** SIIIIIII!!! à cause de -O <path>
  strcpybuff(fname, s);

#if HTS_DOSNAME
  // remplacer / par des slash arrière
  {
    int i = 0;

    while(fname[i]) {
      if (fname[i] == '/')
        fname[i] = '\\';
      i++;
    }
  }
  // a partir d'ici le slash devient antislash
#endif

  // ouvrir
  fp = FOPEN(fname, "ab");

#ifndef _WIN32
  if (fp != NULL)
    chmod(fname, HTS_ACCESS_FILE);
#endif

  return fp;
}

// create an empty file
int filecreateempty(filenote_strc * strc, const char *filename) {
  FILE *fp;

  fp = filecreate(strc, filename);      // filenote & co
  if (fp) {
    fclose(fp);
    return 1;
  } else
    return 0;
}

// noter fichier
int filenote(filenote_strc * strc, const char *s, filecreate_params * params) {
  // gestion du fichier liste liste
  if (params) {
    //filecreate_params* p = (filecreate_params*) params;
    strcpybuff(strc->path, params->path);
    strc->lst = params->lst;
    return 0;
  } else if (strc->lst) {
    char BIGSTK savelst[HTS_URLMAXSIZE * 2];
    char catbuff[CATBUFF_SIZE];

    strcpybuff(savelst, fslash(catbuff, sizeof(catbuff), s));
    // couper chemin?
    if (strnotempty(strc->path)) {
      if (strncmp(fslash(catbuff, sizeof(catbuff), strc->path), savelst, strlen(strc->path)) == 0) {     // couper
        strcpybuff(savelst, s + strlen(strc->path));
      }
    }
    fprintf(strc->lst, "[%s]" LF, savelst);
    fflush(strc->lst);
  }
  return 1;
}

/* Note: utf-8 */
void file_notify(httrackp * opt, const char *adr, const char *fil,
                 const char *save, int create, int modify, int not_updated) {
  RUN_CALLBACK6(opt, filesave2, adr, fil, save, create, modify, not_updated);
}

// executer commande utilisateur
static void postprocess_file(httrackp * opt, const char *save, const char *adr,
                             const char *fil);
void usercommand(httrackp * opt, int _exe, const char *_cmd, const char *file,
                 const char *adr, const char *fil) {
  usercommand_strc *strc = &opt->state.usercmd;

  /* Callback */
  if (_exe) {
    strcpybuff(strc->cmd, _cmd);
    if (strnotempty(strc->cmd))
      strc->exe = _exe;
    else
      strc->exe = 0;
  }

  /* post-processing */
  postprocess_file(opt, file, adr, fil);

  if (file != NULL && strnotempty(file)) {
    RUN_CALLBACK1(opt, filesave, file);
  }

  if (strc->exe) {
    if (file != NULL && strnotempty(file)) {
      if (strnotempty(strc->cmd)) {
        usercommand_exe(strc->cmd, file);
      }
    }
  }
}
void usercommand_exe(const char *cmd, const char *file) {
  char BIGSTK temp[8192];
  size_t i;

  temp[0] = '\0';
  //
  for(i = 0; cmd[i] != '\0'; i++) {
    if ((cmd[i] == '$') && (cmd[i + 1] == '0')) {
      strcatbuff(temp, file);
      i++;
    } else {
      char c[2];
      c[0] = cmd[i];
      c[1] = '\0';
      strcatbuff(temp, c);
    }
  }
  if (system(temp) == -1) {
    assertf(!"can not spawn process");
  }
}

static void postprocess_file(httrackp * opt, const char *save, const char *adr,
                             const char *fil) {
  //int first = 0;
  /* MIME-html archive to build */
  if (opt != NULL && opt->mimehtml) {
    if (adr != NULL && strcmp(adr, "primary") == 0) {
      adr = NULL;
    }
    if (save != NULL && opt != NULL && adr != NULL && adr[0]
        && strnotempty(save) && fexist(save)) {
      const char *rsc_save = save;
      const char *rsc_fil = strrchr(fil, '/');
      int n;

      if (rsc_fil == NULL)
        rsc_fil = fil;
      if (strncmp
          (fslash(OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt), save),
           fslash(OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt), 
           StringBuff(opt->path_html_utf8)), (n =
                                                                        (int)
                                                                        strlen
                                                                        (StringBuff
                                                                         (opt->
                                                                          path_html_utf8))))
          == 0) {
        rsc_save += n;
      }

      if (!opt->state.mimehtml_created) {
        //first = 1;
        opt->state.mimefp =
          fopen(fconcat
                (OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt),
                StringBuff(opt->path_html), "index.mht"),
                "wb");
        (void) unlink(fconcat(OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt),
          StringBuff(opt->path_html),
                              "index.eml"));
#ifndef _WIN32
        if (symlink("index.mht",
                    fconcat(OPT_GET_BUFF(opt), OPT_GET_BUFF_SIZE(opt), StringBuff(opt->path_html),
                            "index.eml")) != 0) {
          if (errno != EPERM) {
            hts_log_print(opt, LOG_WARNING | LOG_ERRNO,
              "could not create symbolic link from index.mht to index.eml");
          }
        }
#endif
        if (opt->state.mimefp != NULL) {
          char BIGSTK rndtmp[1024], currtime[256];

          srand((unsigned int) time(NULL));
          time_gmt_rfc822(currtime);
          sprintf(rndtmp, "%d_%d", (int) time(NULL), (int) rand());
          StringRoom(opt->state.mimemid, 256);
          sprintf(StringBuffRW(opt->state.mimemid), "----=_MIMEPart_%s_=----",
                  rndtmp);
          StringSetLength(opt->state.mimemid, -1);
          fprintf(opt->state.mimefp,
                  "From: HTTrack Website Copier <nobody@localhost>\r\n"
                  "Subject: Local mirror\r\n" "Date: %s\r\n"
                  "Message-ID: <httrack_%s@localhost>\r\n"
                  "Content-Type: multipart/related;\r\n"
                  "\tboundary=\"%s\";\r\n" "\ttype=\"text/html\"\r\n"
                  "MIME-Version: 1.0\r\n"
                  "\r\nThis message is a RFC MIME-compliant multipart message.\r\n"
                  "\r\n", currtime, rndtmp, StringBuff(opt->state.mimemid));
          opt->state.mimehtml_created = 1;
        } else {
          opt->state.mimehtml_created = -1;
          hts_log_print(opt, LOG_ERROR, "unable to create index.mht");
        }
      }
      if (opt->state.mimehtml_created == 1 && opt->state.mimefp != NULL) {
        FILE *fp = FOPEN(save, "rb");

        if (fp != NULL) {
          char buff[60 * 100 + 2];
          char mimebuff[256];
          char BIGSTK cid[HTS_URLMAXSIZE * 3];
          size_t len;
          int isHtml = (ishtml(opt, save) == 1);

          mimebuff[0] = '\0';

          /* CID */
          make_content_id(adr, fil, cid, sizeof(cid));

          guess_httptype(opt, mimebuff, save);
          fprintf(opt->state.mimefp, "--%s\r\n",
                  StringBuff(opt->state.mimemid));
          /*if (first)
             fprintf(opt->state.mimefp, "Content-disposition: inline\r\n");
             else */
          fprintf(opt->state.mimefp,
                  "Content-disposition: attachment; filename=\"%s\"\r\n",
                  rsc_save);
          fprintf(opt->state.mimefp,
                  "Content-Type: %s\r\n" "Content-Transfer-Encoding: %s\r\n"
                  /*"Content-Location: http://localhost/%s\r\n" */
                  "Content-ID: <%s>\r\n" "\r\n", mimebuff,
                  isHtml ? "8bit" : "base64"
                  /*, rsc_save */
                  , cid);
          while((len = fread(buff, 1, sizeof(buff) - 2, fp)) > 0) {
            buff[len] = '\0';
            if (!isHtml) {
              char base64buff[60 * 100 * 2];

              code64((unsigned char *) buff, (int) len,
                     (unsigned char *) base64buff, 1);
              fprintf(opt->state.mimefp, "%s", base64buff);
            } else {
              fprintf(opt->state.mimefp, "%s", buff);
            }
          }
          fclose(fp);
          fprintf(opt->state.mimefp, "\r\n\r\n");
        }
      }
    } else if (save == NULL) {
      if (opt->state.mimehtml_created == 1 && opt->state.mimefp != NULL) {
        fprintf(opt->state.mimefp, "--%s--\r\n",
                StringBuff(opt->state.mimemid));
        fclose(opt->state.mimefp);
        opt->state.mimefp = NULL;
      }
    }
  }
}

// écrire n espaces dans fp
int fspc(httrackp * opt, FILE * fp, const char *type) {
  fspc_strc *const strc = (opt != NULL) ? &opt->state.fspc : NULL;

  if (fp != NULL) {
    char s[256];
    time_t tt;
    struct tm *A;

    tt = time(NULL);
    A = localtime(&tt);
    if (A == NULL) {
      int localtime_returned_null = 0;

      assertf(localtime_returned_null);
    }
    strftime(s, 250, "%H:%M:%S", A);
    if (strnotempty(type))
      fprintf(fp, "%s\t%c%s: \t", s, hichar(*type), type + 1);
    else
      fprintf(fp, "%s\t \t", s);
    if (strc != NULL) {
      if (strcmp(type, "warning") == 0)
        strc->warning++;
      else if (strcmp(type, "error") == 0)
        strc->error++;
      else if (strcmp(type, "info") == 0)
        strc->info++;
    }
  } else if (strc == NULL) {
    return 0;
  } else if (!type) {
    strc->error = strc->warning = strc->info = 0;       // reset
  } else if (strcmp(type, "warning") == 0)
    return strc->warning;
  else if (strcmp(type, "error") == 0)
    return strc->error;
  else if (strcmp(type, "info") == 0)
    return strc->info;
  return 0;
}

// vérifier taux de transfert
#if 0
void check_rate(TStamp stat_timestart, int maxrate) {
  // vérifier taux de transfert (pas trop grand?)
  /*
     if (maxrate>0) {
     int r = (int) (HTS_STAT.HTS_TOTAL_RECV/(time_local()-stat_timestart));    // taux actuel de transfert
     HTS_STAT.HTS_TOTAL_RECV_STATE=0;
     if (r>maxrate) {    // taux>taux autorisé
     int taux = (int) (((TStamp) (r - maxrate) * 100) / (TStamp) maxrate);
     if (taux<15)
     HTS_STAT.HTS_TOTAL_RECV_STATE=1;   // ralentir un peu (<15% dépassement)
     else if (taux<50)
     HTS_STAT.HTS_TOTAL_RECV_STATE=2;   // beaucoup (<50% dépassement)
     else
     HTS_STAT.HTS_TOTAL_RECV_STATE=3;   // énormément (>50% dépassement)
     }
     }
   */
}
#endif

// ---
// sous routines liées au moteur et au backing

// supplemental links ready (done) after ptr or ready in background
int backlinks_done(const struct_back * sback,
                   lien_url ** liens, int lien_tot,
                   int ptr) {
  int n = 0;

#if 0
  int i;

  //Links done and stored in cache
  for(i = ptr + 1; i < lien_tot; i++) {
    if (heap(i)) {
      if (heap(i)->pass2 == -1) {
        n++;
      }
    }
  }
#else
  // finalized in background
  n += HTS_STAT.stat_background;
#endif
  n += back_done_incache(sback);
  return n;
}

// remplir backing si moins de max_bytes en mémoire
HTS_INLINE int back_fillmax(struct_back * sback, httrackp * opt,
                            cache_back * cache, int ptr,
                            int numero_passe) {
  if (!opt->state.stop) {
    if (back_incache(sback) < opt->maxcache) {  // pas trop en mémoire?
      return back_fill(sback, opt, cache, ptr, numero_passe);
    }
  }
  return -1;                    /* plus de place */
}

int back_pluggable_sockets_strict(struct_back * sback, httrackp * opt) {
  int n = opt->maxsoc - back_nsoc(sback);

  // connect limiter
  if (n > 0 && opt->maxconn > 0 && HTS_STAT.last_connect > 0) {
    TStamp opTime =
      HTS_STAT.last_request ? HTS_STAT.last_request : HTS_STAT.last_connect;
    TStamp cTime = mtime_local();
    TStamp lap = (cTime - opTime);
    TStamp minLap = (TStamp) (1000.0 / opt->maxconn);

    if (lap < minLap) {
      n = 0;
    } else if (minLap != 0) {
      int nMax = (int) (lap / minLap);

      n = min(n, nMax);
    }
  }

  return n;
}

int back_pluggable_sockets(struct_back * sback, httrackp * opt) {
  int n;

  // ajouter autant de socket qu'on peut ajouter
  n = back_pluggable_sockets_strict(sback, opt);

  // vérifier qu'il restera assez de place pour les tests ensuite (en théorie, 1 entrée libre restante suffirait)
  n = min(n, back_available(sback) - 8);

  // no space left on backing stack - do not back anymore
  if (back_stack_available(sback) <= 2)
    n = 0;

  return n;
}

// remplir backing
int back_fill(struct_back * sback, httrackp * opt, cache_back * cache,
              int ptr, int numero_passe) {
  int n = back_pluggable_sockets(sback, opt);

  if (opt->savename_delayed == 2 && !opt->delayed_cached)       /* cancel (always delayed) */
    return 0;
  if (n > 0) {
    int p;

    if (ptr < cache->ptr_last) {        /* restart (2 scans: first html, then non html) */
      cache->ptr_ant = 0;
    }

    p = ptr + 1;
    /* on a déja parcouru */
    if (p < cache->ptr_ant)
      p = cache->ptr_ant;
    while(p < opt->lien_tot && n > 0 && back_checkmirror(opt)) {
      //while((p<lien_tot) && (n>0) && (p < ptr+opt->maxcache_anticipate)) {
      int ok = 1;

      // on ne met pas le fichier en backing si il doit être traité après ou s'il a déja été traité
      if (heap(p)->pass2) {    // 2è passe
        if (numero_passe != 1)
          ok = 0;
      } else {
        if (numero_passe != 0)
          ok = 0;
      }

      // Why in hell did I do that ?
      //if (ok && heap(p)->sav != NULL && heap(p)->sav[0] != '\0' 
      //  && hash_read(opt->hash,heap(p)->sav,NULL,HASH_STRUCT_FILENAME ) >= 0)     // lookup in liens_record
      //{
      //  ok = 0;
      //}
      if (heap(p)->sav == NULL || heap(p)->sav[0] == '\0'
          || hash_read(opt->hash, heap(p)->sav, NULL, HASH_STRUCT_FILENAME ) < 0) {
        ok = 0;
      }
      // note: si un backing est fini, il reste en mémoire jusqu'à ce que
      // le ptr l'atteigne
      if (ok) {
        if (!back_exist
            (sback, opt, heap(p)->adr, heap(p)->fil, heap(p)->sav)) {
          if (back_add
              (sback, opt, cache, heap(p)->adr, heap(p)->fil, heap(p)->sav,
               heap(heap(p)->precedent)->adr, heap(heap(p)->precedent)->fil,
               heap(p)->testmode) == -1) {
            hts_log_print(opt, LOG_DEBUG,
                          "error: unable to add more links through back_add for back_fill");
#if BDEBUG==1
            printf("error while adding\n");
#endif
            n = 0;              // sortir
          } else {
            n--;
#if BDEBUG==1
            printf("backing: %s%s\n", heap(p)->adr, heap(p)->fil);
#endif
          }
        }
      }
      p++;
    }                           // while
    /* sauver position dernière anticipation */
    cache->ptr_ant = p;
    cache->ptr_last = ptr;
  }
  return 0;
}

// ---

// Poll stdin.. si besoin
#if HTS_POLL
// lecture stdin des caractères disponibles
int read_stdin(char *s, int max) {
  int i = 0;

  while((check_stdin()) && (i < (max - 1)))
    s[i++] = fgetc(stdin);
  s[i] = '\0';
  return i;
}

#ifdef _WIN32
int check_stdin(void) {
  return (_kbhit());
}
#else
int check_flot(T_SOC s) {
  fd_set fds;
  struct timeval tv;

  FD_ZERO(&fds);
  FD_SET((T_SOC) s, &fds);
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  select(s + 1, &fds, NULL, NULL, &tv);
  return FD_ISSET(s, &fds);
}
int check_stdin(void) {
  fflush(stdout);
  fflush(stdin);
  if (check_flot(0))
    return 1;
  return 0;
}
#endif
#endif

int check_sockerror(T_SOC s) {
  fd_set fds;
  struct timeval tv;

  FD_ZERO(&fds);
  FD_SET((T_SOC) s, &fds);
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  select((int) s + 1, NULL, NULL, &fds, &tv);
  return FD_ISSET(s, &fds);
}

/* check incoming data */
int check_sockdata(T_SOC s) {
  fd_set fds;
  struct timeval tv;

  FD_ZERO(&fds);
  FD_SET((T_SOC) s, &fds);
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  select((int) s + 1, &fds, NULL, NULL, &tv);
  return FD_ISSET(s, &fds);
}

// Attente de touche
int ask_continue(httrackp * opt) {
  const char *s;

  s = RUN_CALLBACK1(opt, query2, opt->state.HTbuff);
  if (s) {
    if (strnotempty(s)) {
      if ((strfield2(s, "N")) || (strfield2(s, "NO")) || (strfield2(s, "NON")))
        return 0;
    }
    return 1;
  }
  return 1;
}

// nombre de digits dans un nombre
int nombre_digit(int n) {
  int i = 1;

  while(n >= 10) {
    n /= 10;
    i++;
  }
  return i;
}

// renvoi adresse de la fin du token dans p
// renvoi NULL si la chaine est un token unique
// (PATCHE également la chaine)
// ex: "test" "test2" renvoi adresse sur espace
// flag==1 si chaine comporte des echappements comme \"
char *next_token(char *p, int flag) {
  int detect = 0;
  int quote = 0;

  p--;
  do {
    p++;
    if (flag && (*p == '\\')) { // sauter \x ou \"
      if (quote) {
        char c = '\0';

        if (*(p + 1) == '\\')
          c = '\\';
        else if (*(p + 1) == '"')
          c = '"';
        if (c) {
          char BIGSTK tempo[8192];

          tempo[0] = c;
          tempo[1] = '\0';
          strcatbuff(tempo, p + 2);
          strcpybuff(p, tempo);
        }
      }
    } else if (*p == 34) {      // guillemets (de fin)
      char BIGSTK tempo[8192];

      tempo[0] = '\0';
      strcatbuff(tempo, p + 1);
      strcpybuff(p, tempo);     /* wipe "" */
      p--;
      /* */
      quote = !quote;
    } else if (*p == 32) {
      if (!quote)
        detect = 1;
    } else if (*p == '\0') {
      p = NULL;
      detect = 1;
    }
  } while(!detect);
  return p;
}

static int hts_cancel_file_push_(httrackp * opt, const char *url) {
  if (url != NULL && url[0] != '\0') {
    htsoptstatecancel **cancel;

    /* search for available place to store a new htsoptstatecancel* */
    for(cancel = &opt->state.cancel; *cancel != NULL;
        cancel = &((*cancel)->next)) {
      if (strcmp((*cancel)->url, url) == 0) {
        return 1;               /* already there */
      }
    }
    *cancel = malloct(sizeof(htsoptstatecancel));
    (*cancel)->next = NULL;
    (*cancel)->url = strdupt(url);
    return 0;
  }
  return 1;
}

/* cancel a file (locked) */
HTSEXT_API int hts_cancel_file_push(httrackp * opt, const char *url) {
  int ret;

  hts_mutexlock(&opt->state.lock);
  ret = hts_cancel_file_push_(opt, url);
  hts_mutexrelease(&opt->state.lock);
  return ret;
}

static char *hts_cancel_file_pop_(httrackp * opt) {
  if (opt->state.cancel != NULL) {
    htsoptstatecancel **cancel;
    htsoptstatecancel *ret;

    for(cancel = &opt->state.cancel; (*cancel)->next != NULL;
        cancel = &((*cancel)->next)) ;
    ret = *cancel;
    *cancel = NULL;
    return ret->url;
  }
  return NULL;                  /* no entry */
}

char *hts_cancel_file_pop(httrackp * opt) {
  char *ret;

  hts_mutexlock(&opt->state.lock);
  ret = hts_cancel_file_pop_(opt);
  hts_mutexrelease(&opt->state.lock);
  return ret;
}

HTSEXT_API void hts_cancel_test(httrackp * opt) {
  if (opt->state._hts_in_html_parsing == 2)
    opt->state._hts_cancel = 2;
}
HTSEXT_API void hts_cancel_parsing(httrackp * opt) {
  if (opt->state._hts_in_html_parsing)
    opt->state._hts_cancel = 1;
}

// en train de parser un fichier html? réponse: % effectués
// flag>0 : refresh demandé
HTSEXT_API int hts_is_parsing(httrackp * opt, int flag) {
  if (opt->state._hts_in_html_parsing) {        // parsing?
    if (flag >= 0)
      opt->state._hts_in_html_poll = 1; // faudrait un tit refresh
    return max(opt->state._hts_in_html_done, 1);        // % effectués
  } else {
    return 0;                   // non
  }
}
HTSEXT_API int hts_is_testing(httrackp * opt) { // 0 non 1 test 2 purge
  if (opt->state._hts_in_html_parsing == 2)
    return 1;
  else if (opt->state._hts_in_html_parsing == 3)
    return 2;
  else if (opt->state._hts_in_html_parsing == 4)
    return 3;
  else if (opt->state._hts_in_html_parsing == 5)        // scheduling
    return 4;
  else if (opt->state._hts_in_html_parsing == 6)        // wait for slot
    return 5;
  return 0;
}
HTSEXT_API int hts_is_exiting(httrackp * opt) {
  return opt->state.exit_xh;
}

// message d'erreur?
char *hts_errmsg(httrackp * opt) {
  return opt->state._hts_errmsg;
}

// mode pause transfer
HTSEXT_API int hts_setpause(httrackp * opt, int p) {
  if (p >= 0)
    opt->state._hts_setpause = p;
  return opt->state._hts_setpause;
}

// ask for termination
HTSEXT_API int hts_request_stop(httrackp * opt, int force) {
  if (opt != NULL) {
    hts_log_print(opt, LOG_ERROR, "Exit requested by shell or user");
    hts_mutexlock(&opt->state.lock);
    opt->state.stop = 1;
    hts_mutexrelease(&opt->state.lock);
  }
  return 0;
}

HTSEXT_API int hts_has_stopped(httrackp * opt) {
  int ended;
  hts_mutexlock(&opt->state.lock);
  ended = opt->state.is_ended;
  hts_mutexrelease(&opt->state.lock);
  return ended;
}

// régler en cours de route les paramètres réglables..
// -1 : erreur
//HTSEXT_API int hts_setopt(httrackp* set_opt) {
//  if (set_opt) {
//    httrackp* engine_opt=hts_declareoptbuffer(NULL);
//    if (engine_opt) {
//      //_hts_setopt=opt;
//      copy_htsopt(set_opt,engine_opt);
//    }
//  }
//  return 0;
//}
// ajout d'URL
// -1 : erreur
HTSEXT_API int hts_addurl(httrackp * opt, char **url) {
  if (url)
    opt->state._hts_addurl = url;
  return (opt->state._hts_addurl != NULL);
}
HTSEXT_API int hts_resetaddurl(httrackp * opt) {
  opt->state._hts_addurl = NULL;
  return (opt->state._hts_addurl != NULL);
}

// copier nouveaux paramètres si besoin
HTSEXT_API int copy_htsopt(const httrackp * from, httrackp * to) {
  if (from->maxsite > -1)
    to->maxsite = from->maxsite;

  if (from->maxfile_nonhtml > -1)
    to->maxfile_nonhtml = from->maxfile_nonhtml;

  if (from->maxfile_html > -1)
    to->maxfile_html = from->maxfile_html;

  if (from->maxsoc > 0)
    to->maxsoc = from->maxsoc;

  if (from->nearlink > -1)
    to->nearlink = from->nearlink;

  if (from->timeout > -1)
    to->timeout = from->timeout;

  if (from->rateout > -1)
    to->rateout = from->rateout;

  if (from->maxtime > -1)
    to->maxtime = from->maxtime;

  if (from->maxrate > -1)
    to->maxrate = from->maxrate;

  if (from->maxconn > 0)
    to->maxconn = from->maxconn;

  if (StringNotEmpty(from->user_agent))
    StringCopyS(to->user_agent, from->user_agent);

  if (from->retry > -1)
    to->retry = from->retry;

  if (from->hostcontrol > -1)
    to->hostcontrol = from->hostcontrol;

  if (from->errpage > -1)
    to->errpage = from->errpage;

  if (from->parseall > -1)
    to->parseall = from->parseall;

  // test all: bit 8 de travel
  if (from->travel > -1) {
    if (from->travel & 256)
      to->travel |= 256;
    else
      to->travel &= 255;
  }

  return 0;
}

//

/* External modules callback */
int htsAddLink(htsmoduleStruct * str, char *link) {
  if (link != NULL && str != NULL && link[0] != '\0') {
    ENGINE_LOAD_CONTEXT_BASE();
    /* */
    lien_adrfilsave afs;
    char BIGSTK codebase[HTS_URLMAXSIZE * 2];

    /* */
    int pass_fix, prio_fix;

    /* */
    int forbidden_url = 1;

    codebase[0] = '\0';

    hts_log_print(opt, LOG_DEBUG, "(module): adding link : '%s'", link);
    // recopie de "creer le lien"
    //    

    if (!RUN_CALLBACK1(opt, linkdetected, link)) {
      hts_log_print(opt, LOG_ERROR, "Link %s refused by external wrapper",
                    link);
      return 0;
    }
    if (!RUN_CALLBACK2(opt, linkdetected2, link, NULL)) {
      hts_log_print(opt, LOG_ERROR, "Link %s refused by external wrapper(2)",
                    link);
      return 0;
    }
    // adr = c'est la même
    // fil et save: save2 et fil2
    prio_fix = maximum(heap(ptr)->depth - 1, 0);
    pass_fix = max(heap(ptr)->pass2, numero_passe);
    if (heap(ptr)->cod)
      strcpybuff(codebase, heap(ptr)->cod);    // codebase valable pour tt les classes descendantes
    if (strnotempty(codebase) == 0) {   // pas de codebase, construire
      char *a;

      if (str->relativeToHtmlLink == 0)
        strcpybuff(codebase, heap(ptr)->fil);
      else
        strcpybuff(codebase, heap(heap(ptr)->precedent)->fil);
      a = codebase + strlen(codebase) - 1;
      while((*a) && (*a != '/') && (a > codebase))
        a--;
      if (*a == '/')
        *(a + 1) = '\0';        // couper
    } else {                    // couper http:// éventuel
      if (strfield(codebase, "http://")) {
        char BIGSTK tempo[HTS_URLMAXSIZE * 2];
        char *a = codebase + 7;

        a = strchr(a, '/');     // après host
        if (a) {                // ** msg erreur et vérifier?
          strcpybuff(tempo, a);
          strcpybuff(codebase, tempo);  // couper host
        } else {
          hts_log_print(opt, LOG_ERROR, "Unexpected strstr error in base %s",
                        codebase);
        }
      }
    }

    if (!((int) strlen(codebase) < HTS_URLMAXSIZE)) {   // trop long
      hts_log_print(opt, LOG_ERROR, "Codebase too long, parsing skipped (%s)",
                    codebase);
    }

    {
      char *lien = link;
      int dejafait = 0;

      if (strnotempty(lien) && strlen(lien) < HTS_URLMAXSIZE) {

        // calculer les chemins et noms de sauvegarde
        if (ident_url_relatif(lien, urladr(), codebase, &afs.af) >= 0) { // reformage selon chemin
          int r;
          int set_prio_to = 0;
          int just_test_it = 0;

          forbidden_url =
            hts_acceptlink(opt, ptr, afs.af.adr, afs.af.fil, NULL, NULL, &set_prio_to, &just_test_it);
          hts_log_print(opt, LOG_DEBUG,
                        "result for wizard external module link: %d",
                        forbidden_url);

          /* Link accepted */
          if (!forbidden_url) {
            char BIGSTK tempo[HTS_URLMAXSIZE * 2];
            int a, b;

            tempo[0] = '\0';
            a = opt->savename_type;
            b = opt->savename_83;
            opt->savename_type = 0;
            opt->savename_83 = 0;
            // note: adr,fil peuvent être patchés
            r =
              url_savename(&afs, NULL, NULL, NULL, opt, sback, cache, hashptr, ptr, numero_passe,
                           NULL);
            // resolve unresolved type
            if (r != -1 && forbidden_url == 0 && IS_DELAYED_EXT(afs.save)
              ) {               // pas d'erreur, on continue
              lien_adrfil former;

              former.adr[0] = former.fil[0] = '\0';
              r =
                hts_wait_delayed(str, &afs, NULL, NULL, &former, &forbidden_url);
            }
            // end resolve unresolved type
            opt->savename_type = a;
            opt->savename_83 = b;
            if (r != -1 && !forbidden_url) {
              if (savename()) {
                if (lienrelatif(tempo, afs.save, savename()) == 0) {
                  hts_log_print(opt, LOG_DEBUG,
                                "(module): relative link at %s build with %s and %s: %s",
                                afs.af.adr, afs.save, savename(), tempo);
                  if (str->localLink
                      && str->localLinkSize > (int) strlen(tempo) + 1) {
                    strcpybuff(str->localLink, tempo);
                  }
                }
              }
            }
          }

          if (forbidden_url) {
            hts_log_print(opt, LOG_DEBUG, "(module): file not caught: %s",
                          lien);
            if (str->localLink
                && str->localLinkSize > (int) (strlen(afs.af.adr) + strlen(afs.af.fil) + 8)) {
              str->localLink[0] = '\0';
              if (!link_has_authority(afs.af.adr))
                strcpybuff(str->localLink, "http://");
              strcatbuff(str->localLink, afs.af.adr);
              strcatbuff(str->localLink, afs.af.fil);
            }
            r = -1;
          }
          //
          if (r != -1) {
            hts_log_print(opt, LOG_DEBUG, "(module): %s%s -> %s (base %s)", afs.af.adr,
                          afs.af.fil, afs.save, codebase);

            // modifié par rapport à l'autre version (cf prio_fix notamment et save2)

            // vérifier que le lien n'a pas déja été noté
            // si c'est le cas, alors il faut s'assurer que la priorité associée
            // au fichier est la plus grande des deux priorités
            //
            // On part de la fin et on essaye de se presser (économise temps machine)
            {
              int i = hash_read(hashptr, afs.save, NULL, HASH_STRUCT_FILENAME );    // lecture type 0 (sav)

              if (i >= 0) {
                heap(i)->depth = maximum(heap(i)->depth, prio_fix);
                dejafait = 1;
              }
            }

            if (!dejafait) {
              //
              // >>>> CREER LE LIEN JAVA <<<<

              // enregistrer fichier (MACRO)
              if (!hts_record_link(opt, afs.af.adr, afs.af.fil, afs.save, "", "", "")) {    // erreur, pas de place réservée
                opt->state.exit_xh = -1;        /* fatal error -> exit */
                return 0;
              }
              // mode test?                          
              heap_top()->testmode = 0;    // pas mode test
              heap_top()->link_import = 0; // pas mode import

              // écrire autres paramètres de la structure-lien
              //if (meme_adresse)                                 
              heap_top()->premier = heap(ptr)->premier;
              //else    // sinon l'objet père est le précédent lui même
              //  heap_top()->premier=ptr;

              heap_top()->precedent = ptr;
              // noter la priorité
              if (!set_prio_to)
                heap_top()->depth = prio_fix;
              else
                heap_top()->depth = max(0, min(heap(ptr)->depth - 1, set_prio_to - 1));   // PRIORITE NULLE (catch page)
              heap_top()->pass2 = max(pass_fix, numero_passe);
              heap_top()->retry = opt->retry;

              //strcpybuff(heap_top()->adr,adr);
              //strcpybuff(heap_top()->fil,fil);
              //strcpybuff(heap_top()->sav,save); 
              hts_log_print(opt, LOG_DEBUG, "(module): OK, NOTE: %s%s -> %s",
                            heap_top()->adr, heap_top()->fil,
                            heap_top()->sav);

            }
          }
        }
      }
    }

    /* Apply changes */
    ENGINE_SAVE_CONTEXT_BASE();

    return (forbidden_url == 0);
  }
  return 0;
}

// message copyright interne
void voidf(void) {
  static const char *a;

  a = "" CRLF "" CRLF;
  a = "+-----------------------------------------------+" CRLF;
  a = "|HyperTextTRACKer, Offline Browser Utility      |" CRLF;
  a = "|                      HTTrack Website Copier   |" CRLF;
  a = "|Code:         Windows Interface Xavier Roche   |" CRLF;
  a = "|                    HTS/HTTrack Xavier Roche   |" CRLF;
  a = "|                .class Parser Yann Philippot   |" CRLF;
  a = "|                                               |" CRLF;
  a = "|Tested on:                 Windows95,98,NT,2K  |" CRLF;
  a = "|                           Linux PC            |" CRLF;
  a = "|                           Sun-Solaris 5.6     |" CRLF;
  a = "|                           AIX 4               |" CRLF;
  a = "|                                               |" CRLF;
  a = "|Copyright (C) Xavier Roche and other           |" CRLF;
  a = "|contributors                                   |" CRLF;
  a = "|                                               |" CRLF;
  a = "|Use this program at your own risks!            |" CRLF;
  a = "+-----------------------------------------------+" CRLF;
  a = "" CRLF;
  (void) a;
}

// HTTrack Website Copier Copyright (C) 1998-2014 Xavier Roche and other contributors
//
