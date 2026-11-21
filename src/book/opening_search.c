#include <math.h>
#include <string.h>
#include "book/book.h"
#include "brain/eval.h"

const BookOptions BOOK_DEFAULTS = {130, 0.7, 40, 0.06, 12, 100};

int book_rank_candidates(Board *b, const BookPosition *pos, const BookOptions *opt, BookCandidate *cands) {
  int nc = 0;
  for (int i = 0; i < pos->n; i++) {
    const BookEdge *edge = &pos->edges[i];
    int move = parse_move(b, edge->uci);
    if (move == 0) continue;
    if (!make_move(b, move)) continue;
    int score = -evaluate(b);
    unmake_move(b);
    BookCandidate *c = &cands[nc++];
    c->move = move;
    snprintf(c->uci, sizeof(c->uci), "%s", edge->uci);
    c->lines = edge->lines;
    c->quality = edge->quality;
    c->reach = edge->reach;
    c->ends = edge->ends;
    c->score = score;
    c->weight = 0;
    c->prob = 0;
  }
  if (nc == 0) return 0;

  int best = cands[0].score;
  for (int i = 1; i < nc; i++) if (cands[i].score > best) best = cands[i].score;

  int nk = 0;
  for (int i = 0; i < nc; i++) {
    if (cands[i].score >= best - opt->margin) cands[nk++] = cands[i];
  }
  for (int i = 0; i < nk; i++) {
    BookCandidate *c = &cands[i];
    double depth_factor = 1 + opt->reach_bonus * (c->reach < opt->reach_cap ? c->reach : opt->reach_cap);
    c->weight = sqrt(c->quality) * depth_factor * exp((c->score - best) / opt->eval_scale);
  }

  double v = opt->variety < 0 ? 0 : (opt->variety > 1 ? 1 : opt->variety);
  double exponent = 1 + (1 - v) * 3;
  double total = 0;
  for (int i = 0; i < nk; i++) total += pow(cands[i].weight, exponent);
  for (int i = 0; i < nk; i++) cands[i].prob = pow(cands[i].weight, exponent) / total;

  for (int i = 1; i < nk; i++) {
    BookCandidate key = cands[i];
    int j = i - 1;
    while (j >= 0 && cands[j].prob < key.prob) {
      cands[j + 1] = cands[j];
      j--;
    }
    cands[j + 1] = key;
  }
  return nk;
}

bool select_book_move(Board *b, const OpeningBook *book, const BookOptions *opt, double (*rng)(void *), void *ud, BookSelection *out) {
  if (b->hist_ply >= opt->max_ply) return false;
  const BookPosition *pos = opening_book_edges_at(book, b->hash_key);
  if (!pos) return false;
  int nk = book_rank_candidates(b, pos, opt, out->cands);
  if (nk == 0) return false;
  out->ncands = nk;

  int pick = 0;
  if (opt->variety > 0 && nk > 1) {
    double r = rng(ud);
    for (int i = 0; i < nk; i++) {
      r -= out->cands[i].prob;
      if (r <= 0) { pick = i; break; }
    }
  }
  out->found = true;
  out->move = out->cands[pick].move;
  memcpy(out->uci, out->cands[pick].uci, sizeof(out->uci));
  out->score = out->cands[pick].score;
  return true;
}

static double zero_rng(void *ud) { (void)ud; return 0; }

int book_line(Board *b, const OpeningBook *book, int first_move, int length, const BookOptions *opt, int *line) {
  int n = 0;
  line[n++] = first_move;
  int applied = 0;
  if (!make_move(b, first_move)) return n;
  applied++;
  BookOptions o = *opt;
  o.variety = 0;
  static BookSelection sel;
  while (n < length) {
    sel.found = false;
    if (!select_book_move(b, book, &o, zero_rng, NULL, &sel)) break;
    if (!make_move(b, sel.move)) break;
    applied++;
    line[n++] = sel.move;
  }
  for (int i = 0; i < applied; i++) unmake_move(b);
  return n;
}
