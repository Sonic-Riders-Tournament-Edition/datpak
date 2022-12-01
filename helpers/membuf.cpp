//
// Created by ggonz on 9/30/2022.
//

#include "membuf.hpp"

membuf::membuf(const uint8_t *p, std::size_t l) {
	setg((char*)p, (char*)p, (char*)p + l);
}