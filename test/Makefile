CXXFLAGS = -std=c++11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined

test: test_core sync-check
	./test_core

test_core: test_core.cpp ../common/lk201_core.h
	$(CXX) $(CXXFLAGS) -o $@ test_core.cpp

# The sketches carry copies of the core (Arduino can't include ../)
sync:
	cp ../common/lk201_core.h ../lk201/lk201_core.h
	cp ../common/lk201_core.h ../lk201_rp2040/lk201_core.h

sync-check:
	cmp ../common/lk201_core.h ../lk201/lk201_core.h
	cmp ../common/lk201_core.h ../lk201_rp2040/lk201_core.h

clean:
	rm -f test_core

.PHONY: test sync sync-check clean
