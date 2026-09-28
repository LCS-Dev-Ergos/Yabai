#ifndef CORE_TYPES_H
#define CORE_TYPES_H

// Shared platform types used by area interfaces, without daemon definitions.
#ifdef __OBJC__
#include <Carbon/Carbon.h>
#include <Cocoa/Cocoa.h>
#include <ApplicationServices/ApplicationServices.h>
#include <CoreVideo/CoreVideo.h>
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <regex.h>
#include <pthread.h>
#include <semaphore.h>

#include "misc/macros.h"
#include "misc/color.h"
#include "misc/hashtable.h"
#include "osax/common.h"

#endif
