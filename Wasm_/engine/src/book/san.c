#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "book/book.h"

static int piece_type_of(char c) {
  switch (c) {
    case 'N': return 1;
    case 'B': return 2;
    case 'R': return 3;
    case 'Q': return 4;
    case 'K': return 5;
    default: return 0;
  }
}

static bool is_legal(Board *b, int move) {
  if (!make_move(b, move)) return false;
  unmake_move(b);
  return true;
}

int tokenize_moves(const char *text, char ***out_tokens, char **arena_out) {
  size_t len = strlen(text);
  char *arena = malloc(len + 1);
  memcpy(arena, text, len + 1);
  int cap = 16, n = 0;
  char **toks = malloc(sizeof(char *) * (size_t)cap);
  char *save = NULL;
  for (char *raw = strtok_r(arena, " \t\r\n\f\v", &save); raw; raw = strtok_r(NULL, " \t\r\n\f\v", &save)) {
    size_t i = 0;
    while (isdigit((unsigned char)raw[i])) i++;
    size_t j = i;
    while (raw[j] == '.') j++;
    if (i > 0 && j > i && raw[j] == '\0') continue;
    char *t = raw;
    if (i > 0 && j > i) t = raw + j;
    if (*t == '\0') continue;
    if (!strcmp(t, "1-0") || !strcmp(t, "0-1") || !strcmp(t, "1/2-1/2") || !strcmp(t, "*")) continue;
    if (n == cap) { cap *= 2; toks = realloc(toks, sizeof(char *) * (size_t)cap); }
    toks[n++] = t;
  }
  *out_tokens = toks;
  *arena_out = arena;
  return n;
}

int parse_san(Board *b, const char *token) {
  char san[32];
  size_t len = strlen(token);
  if (len >= sizeof(san)) return 0;
  memcpy(san, token, len + 1);
  while (len > 0 && (san[len - 1] == '+' || san[len - 1] == '#' || san[len - 1] == '!' || san[len - 1] == '?')) san[--len] = 0;
  for (size_t i = 0; i < len; i++) if (san[i] == '0') san[i] = 'O';

  MoveList list;
  generate_moves(b, &list);

  if (!strcmp(san, "O-O") || !strcmp(san, "O-O-O")) {
    int file = !strcmp(san, "O-O") ? 6 : 2;
    for (int i = 0; i < list.count; i++) {
      int mv = list.moves[i];
      if ((mv & MOVE_CASTLING) == 0 || ((mv >> 6) & 7) != file) continue;
      if (is_legal(b, mv)) return mv;
    }
    return 0;
  }

  size_t start = 0, end = len;
  int type = 0;
  if (len > 0 && strchr("NBRQK", san[0])) { type = piece_type_of(san[0]); start = 1; }
  int promo_type = 0;
  if (end > start && strchr("NBRQ", san[end - 1])) {
    promo_type = piece_type_of(san[end - 1]);
    end--;
    if (end > start && san[end - 1] == '=') end--;
  }
  if (end < start + 2) return 0;
  char tf = san[end - 2], tr = san[end - 1];
  if (tf < 'a' || tf > 'h' || tr < '1' || tr > '8') return 0;
  int src_file = -1, src_row = -1;
  bool capture = false;
  size_t i = start, pe = end - 2;
  if (i < pe && san[i] >= 'a' && san[i] <= 'h') src_file = san[i++] - 'a';
  if (i < pe && san[i] >= '1' && san[i] <= '8') src_row = 8 - (san[i++] - '0');
  if (i < pe && san[i] == 'x') { capture = true; i++; }
  if (i != pe) return 0;
  int dst = (8 - (tr - '0')) * 8 + (tf - 'a');
  if (promo_type != 0 && type != 0) return 0;

  int found = 0;
  for (int k = 0; k < list.count; k++) {
    int mv = list.moves[k];
    if (((mv >> 6) & 63) != dst) continue;
    if (PIECE_TYPE[(mv >> 12) & 15] != type) continue;
    if (mv & MOVE_CASTLING) continue;
    int promo = (mv >> 16) & 15;
    if (promo != 0 ? (promo % 6) != promo_type : promo_type != 0) continue;
    int src = mv & 63;
    if (src_file >= 0 && (src & 7) != src_file) continue;
    if (src_row >= 0 && (src >> 3) != src_row) continue;
    if (type == 0 && (((mv & (MOVE_CAPTURE | MOVE_ENPASSANT)) != 0) != capture)) continue;
    if (!is_legal(b, mv)) continue;
    if (found != 0) return 0;
    found = mv;
  }
  return found;
}
