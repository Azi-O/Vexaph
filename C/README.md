
```markdown
# Vexaph

Chess engine written in C (bitboard + Magic + NNUE + UCI).

ELO: 36

---

## Installation

```bash
git clone https://github.com/Azi-O/Vexaph
cd Vexaph
```

---

Build

Linux / Ubuntu / Debian

```bash
sudo apt update
sudo apt install build-essential cmake
make
# or
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

Termux (Android)

```bash
pkg update
pkg install clang make cmake
make
```

macOS

```bash
xcode-select --install
make
```

The binary after building is located at: build/vexaph

---

Usage

Run directly (UCI):

```bash
./build/vexaph
```

Or load it into a UCI-compatible GUI (Arena, CuteChess, BanksiaGUI...).

Example UCI commands:

```
uci
isready
position startpos
go depth 20
quit
```

The file nn-190f102a22c3.nnue must be in the same directory as the binary (or set via setoption EvalFile).

```