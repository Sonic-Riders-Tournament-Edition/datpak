#include "memstream.hpp"

memstream::memstream(const uint8_t *p, size_t l) :
		std::istream(&_buffer),
		_buffer(p, l) {
	rdbuf(&_buffer);
}