#ifndef VEXAPH_BOOK_H
#define VEXAPH_BOOK_H

#include <stdio.h>
#include "core/core.h"

#define ECO_FILE_COUNT 6
extern const char *const ECO_FILES[ECO_FILE_COUNT];

typedef struct {
  char *fen;
  char eco[8];
  char *name;
  char *moves;
  char src[40];
  bool interpolated;
  bool root;
  char scid[16];
  int file;
} EcoRecord;

typedef struct {
  char file[40];
  bool ok;
  int total;
  int accepted;
  int rejected;
  char error[192];
} EcoFileStatus;

typedef struct {
  EcoRecord *records;
  int count;
  int cap;
  EcoFileStatus statuses[ECO_FILE_COUNT];
  int rejected;
  int duplicates;
} EcoCollection;

char *eco_read_file(const char *path, size_t *len);
void eco_collect(const char *texts[ECO_FILE_COUNT], EcoCollection *out);
void eco_collection_free(EcoCollection *c);

typedef struct {
  char uci[8];
  int lines;
  double quality;
  int reach;
  int ends;
} BookEdge;

typedef struct {
  U64 hash;
  BookEdge *edges;
  int n, cap;
} BookPosition;

typedef struct {
  BookPosition *pos;
  int npos, cap;
  int *index;
  int index_cap;
  int edge_count;
} OpeningBook;

typedef struct {
  U64 hash;
  char eco[8];
  char *name;
  char *moves;
  char src[40];
  int ply;
  int priority;
} NameInfo;

typedef struct {
  NameInfo *items;
  int n, cap;
  int *index;
  int index_cap;
} NameMap;

typedef struct {
  int records;
  int indexed;
  int illegal_count;
  int fen_mismatch_count;
  int positions;
  int edges;
  int classified;
} BookReport;

typedef struct {
  char eco[8];
  const char *name;
  const char *moves;
  int ply;
  int matched_ply;
  bool exact;
  bool found;
} OpeningInfo;

typedef struct {
  double margin;
  double variety;
  double max_ply;
  double reach_bonus;
  double reach_cap;
  double eval_scale;
} BookOptions;

typedef struct {
  int move;
  char uci[8];
  int lines;
  double quality;
  int reach;
  int ends;
  int score;
  double weight;
  double prob;
} BookCandidate;

typedef struct {
  bool found;
  int move;
  char uci[8];
  int score;
  BookCandidate cands[MAX_MOVES];
  int ncands;
} BookSelection;

typedef struct {
  bool found;
  int move;
  char uci[8];
  int score;
  int line[4];
  int line_len;
  BookCandidate cands[MAX_MOVES];
  int ncands;
  OpeningInfo opening;
} BookProbe;

typedef struct {
  bool ready;
  int load_ms;
  EcoFileStatus files[ECO_FILE_COUNT];
  int nfiles;
  int records;
  int indexed;
  int illegal;
  int fen_mismatch;
  int positions;
  int edges;
  int classified;
  int duplicates;
  int rejected;
  int missing;
  char missing_names[ECO_FILE_COUNT][40];
} BookStats;

typedef struct {
  bool enabled;
  BookOptions options;
  OpeningBook book;
  NameMap names;
  BookReport report;
  bool has_book;
  bool has_classifier;
  EcoFileStatus files[ECO_FILE_COUNT];
  int nfiles;
  int rejected;
  int duplicates;
  int load_ms;
  uint64_t rng_state;
} BookManager;

int tokenize_moves(const char *text, char ***out_tokens, char **arena);
int parse_san(Board *b, const char *token);

void opening_book_init(OpeningBook *b);
void opening_book_free(OpeningBook *b);
const BookPosition *opening_book_edges_at(const OpeningBook *b, U64 hash);
void name_map_init(NameMap *m);
void name_map_free(NameMap *m);
const NameInfo *name_map_get(const NameMap *m, U64 hash);
double record_quality(const EcoRecord *r);
void build_opening_book(const EcoRecord *records, int n, OpeningBook *book, NameMap *names, BookReport *report);

bool classifier_track(const NameMap *names, const Board *b, OpeningInfo *out);
bool classifier_classify(const NameMap *names, const Board *b, OpeningInfo *out);

extern const BookOptions BOOK_DEFAULTS;
int book_rank_candidates(Board *b, const BookPosition *pos, const BookOptions *opt, BookCandidate *cands);
bool select_book_move(Board *b, const OpeningBook *book, const BookOptions *opt, double (*rng)(void *), void *ud, BookSelection *out);
int book_line(Board *b, const OpeningBook *book, int first_move, int length, const BookOptions *opt, int *line);

void book_manager_init(BookManager *m);
void book_manager_free(BookManager *m);
bool book_manager_is_ready(const BookManager *m);
void book_manager_load_texts(BookManager *m, const char *texts[ECO_FILE_COUNT]);
BookStats book_manager_load_dir(BookManager *m, const char *dir);
void book_manager_configure(BookManager *m, int enabled, double variety, bool has_variety);
bool book_manager_usable(const BookManager *m);
bool book_manager_probe(BookManager *m, Board *b, BookProbe *out);
bool book_manager_track(const BookManager *m, const Board *b, OpeningInfo *out);
BookStats book_manager_stats(const BookManager *m);
const char *book_find_eco_dir(const char *hint, const char *argv0);

extern BookManager g_book;

#endif
