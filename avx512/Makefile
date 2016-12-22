.PHONY: clean

FLAGS=-std=c++11 -O3 -Wall -Wextra -pedantic
ALL=avx512f-alignr avx512cd-conflict-demo

all: $(ALL)

avx512f-alignr: avx512f-alignr.cpp
	$(CXX) $(FLAGS) -mavx512f $^ -o $@

avx512cd-conflict-demo: avx512cd-conflict-demo.cpp
	$(CXX) $(FLAGS) -mavx512cd $^ -o $@

run: avx512f-alignr
	sde -cnl -- ./$^

clean:
	rm -f $(ALL)
