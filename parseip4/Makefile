.SUFFIXES:

FLAGS=$(CXXFLAGS) -Wall -Wextra -pedantic -std=c++11 -Wno-implicit-fallthrough -Wno-overflow -march=native

ALL=unittest benchmark

all: $(ALL)

DEPS=common.h naive.cpp naive.h sse.h sse.cpp \
        sse_parse_aux_v2.inl \
        sse_parse_aux_v3.inl \
        sse_parse_aux_v4.inl \
        sse_parse_aux_v5.inl

sse_parse_aux_v2.inl: generator_v2.py
	python3 generator_v2.py > sse_parse_aux_v2.inl

sse_parse_aux_v3.inl: generator_v3.py
	python3 generator_v3.py > sse_parse_aux_v3.inl

sse_parse_aux_v4.inl: generator_v4.py
	python3 generator_v4.py > sse_parse_aux_v4.inl

sse_parse_aux_v5.inl: generator_v5.py
	python3 generator_v5.py > sse_parse_aux_v5.inl

unittest: unittest.cpp $(DEPS)
	$(CXX) $(FLAGS) $< -o $@

benchmark: benchmark.cpp $(DEPS)
	$(CXX) $(FLAGS) -O2 $< -o $@