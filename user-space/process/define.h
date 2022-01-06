/* SPDX-License-Identifier: GPL-2.0 */
#ifndef HACKERNEL_PROCESS_DEFINE_H
#define HACKERNEL_PROCESS_DEFINE_H

#include "hackernel/util.h"

EXTERN_C_BEGIN

enum {
    PROCESS_A_UNSPEC,
    PROCESS_A_SESSION,

    PROCESS_A_STATUS_CODE,
    PROCESS_A_OP_TYPE,
    PROCESS_A_NAME,
    PROCESS_A_PERM,
    PROCESS_A_ID,
    __PROCESS_A_MAX,
};
#define PROCESS_A_MAX (__PROCESS_A_MAX - 1)

EXTERN_C_END

#endif
