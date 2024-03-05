#pragma once

#include "basetypes.hpp"
#include "debugging.hpp"
#include "logging.hpp"
#include "misc.hpp"
#include "strop.hpp"
#include "threading.hpp"
class dummy_class {
public:
    dummy_class() {
#ifdef LOG_LEVEL_AS_DEBUG
        rrr::Log::set_level(rrr::Log::DEBUG);
#else
        rrr::Log::set_level(rrr::Log::INFO);
#endif
    }
};
static dummy_class dummy___;
//#include "unittest.hpp"
