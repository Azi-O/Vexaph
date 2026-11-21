Vexaph C

Vexaph C is a modern, lightweight, fast chess engine written in C. It uses bitboards to represent the board, Magic Bitboards for move generation, an advanced search engine, NNUE evaluation, full UCI support, and builds to WebAssembly (WASM) for running on the web.

Estimated Elo: 2950 - 3050

---

Features

· Board & Move Generation (src/core/):
  · 64-bit bitboards (bitboard.c, board.c).
  · Magic Bitboards for ultra-fast sliding piece move generation (magic_data.c, movegen.c).
  · Zobrist Hashing for position identification (zobrist.c).
· Search & Evaluation (src/brain/):
  · Alpha-Beta search with Transposition Tables (tt.c), Static Exchange Evaluation (see.c), and Move Ordering (movepick.c).
  · NNUE evaluation (nnue.c, using nn-190f102a22c3.nnue).
  · Handcrafted fallback evaluation & endgame knowledge: pawn structure (eval_pawn.c), piece-square tables (eval_tables.c), KPK endgame tablebases (kpk.c).
  · Time management (timeman.c).
· Opening Book & ECO (src/book/, data/eco/):
  · Opening book manager (book_manager.c, opening_book.c, opening_search.c).
  · SAN parser (san.c).
  · Full ECO database support (ecoA.json → ecoE.json & eco_interpolated.json via eco_loader.c).
· Interface & Portability (src/interface/, src/platform/):
  · Standard UCI protocol, compatible with Arena, CuteChess, BanksiaGUI, etc. (uci.c).
  · WebAssembly API to run the engine directly in the browser (wasm_api.c).

---

Project Structure

```text
vexaph_c/
├── CMakeLists.txt           # CMake build configuration
├── Makefile                 # GNU Make build script
├── nn-190f102a22c3.nnue     # NNUE weights (~47.7 MB)
├── data/
│   └── eco/                 # ECO opening classification JSON data
├── include/                 # Headers (.h) mirroring src/
│   ├── book/
│   ├── brain/
│   ├── core/
│   ├── interface/
│   └── platform/
├── src/                     # C source code
│   ├── main.c               # Entry point
│   ├── book/                # Opening book & ECO
│   ├── brain/               # Search, NNUE, HCE, TT, SEE, time management
│   ├── core/                # Bitboards, magic bitboards, board state, movegen
│   ├── interface/           # UCI & WebAssembly bindings
│   └── platform/            # OS abstraction layer
├── tests/                   # Unit tests & Perft
│   ├── perft_main.c         # Move generation speed & accuracy test
│   └── test_main.c          # Engine test suite
└── tools/                   # Benchmark & evaluation utilities
    ├── bench_time.c         # Search speed & NPS benchmark
    └── eval_check.c         # Static evaluation check
```

---

Build

Requirements

· C11 compiler (gcc, clang, or MSVC)
· CMake (3.10+) or GNU Make
· (Optional) Emscripten (emcc) if building WASM (wasm_api.c)

Using CMake (recommended)

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
```

Using Makefile

```bash
make
```

---

Usage

Running via UCI

After building, run the executable from the terminal or load it into any UCI-compatible chess GUI:

```bash
./vexaph_c
```

Example UCI commands:

```text
uci
isready
setoption name EvalFile value nn-190f102a22c3.nnue
position startpos moves e2e4 e7e5
go depth 15
quit
```

Running Tests & Benchmarks

Verify move generation with Perft or run the test suite:

```bash
./perft_main
./test_main
```

Benchmark search speed:

```bash
./bench_time
```