#pragma once

#include <list>
#include <map>
#include <unordered_map>
#include <functional>

#include <sys/types.h>
#include <sys/time.h>
#include <stdarg.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <inttypes.h>

namespace rrr {

/**
 * @brief Configure fd as non blocking or blocking
 * 
 * @param fd file descriptor to be configured
 * @param nonblocking if true fd will be configured non blocking else read calls will be blocked.
 * @return int see fnctl return
 */
int set_nonblocking(int fd, bool nonblocking);
/**
 * @brief find next open port from kernel
 * 
 * @return int open port.
 */
int find_open_port();
/**
 * @brief Get the host name.
 * 
 * @return std::string 
 */
std::string get_host_name();

} // namespace rrr
