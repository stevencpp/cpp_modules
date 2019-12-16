#/bin/bash
cd "${0%/*}"
mkdir -p build && cd build
COMPILER=$1
NUM_FILES=$2
if [ "$NUM_FILES" = "" ]; then
  NUM_FILES=25
fi
echo ===== benchmarking iostream with $COMPILER =====

if [[ "$COMPILER" == *.exe ]]; then
	export PATH="$PATH:/c/Program Files/LLVM/bin"
	NINJA="C:/Program Files/cpp_modules/bin/ninja"
else
	NINJA="/usr/local/cpp_modules/bin/ninja"
fi

CMAKE_GEN="cmake -G Ninja .. -DCMAKE_CXX_COMPILER=$COMPILER -DNUM_FILES=$NUM_FILES"
REP=4

echo === HEADERS ===
rm -f CMakeCache.txt && $CMAKE_GEN -DUSE=HEADERS
for i in `seq $REP`; do ninja -t clean && time ninja; done

echo === PCH ===
rm -f CMakeCache.txt && $CMAKE_GEN -DUSE=PCH 
for i in `seq $REP`; do ninja -t clean && time ninja; done

echo === HEADER_UNITS ===
rm -f CMakeCache.txt && $CMAKE_GEN -DUSE=HEADER_UNITS
for i in `seq $REP`; do "$NINJA" -t clean && time "$NINJA"; done

echo === MODULES ===
#if [[ "$COMPILER" != clang* ]]; then
rm -f CMakeCache.txt && $CMAKE_GEN -DUSE=MODULES
for i in `seq $REP`; do "$NINJA" -t clean && time "$NINJA"; done
#fi
