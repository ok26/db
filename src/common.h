#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

// Size of every page in the database file
#define PAGE_SIZE 4096

// Page 0 is the database header (see pager.h) which no other page can ever
// point to, so page id 0 is used as the "null" page id in page pointers.
#define NULL_PAGE_ID 0

// Status code returned by every operation that can fail
typedef enum DBStatus {
    DB_OK = 0,
    DB_NOT_FOUND,   // The key/record does not exist
    DB_ERR_IO,      // Reading or writing the database file failed
    DB_ERR_NOMEM,   // Out of memory
    DB_ERR_FULL,    // Every buffer frame is pinned, or the file reached its max size
    DB_ERR_CORRUPT, // The database file contains something that is not valid
    DB_ERR_INVALID, // Invalid argument or misuse of the API
} DBStatus;

static inline const char *db_status_str(DBStatus status) {
    switch (status) {
        case DB_OK:          return "ok";
        case DB_NOT_FOUND:   return "not found";
        case DB_ERR_IO:      return "i/o error";
        case DB_ERR_NOMEM:   return "out of memory";
        case DB_ERR_FULL:    return "full";
        case DB_ERR_CORRUPT: return "corrupt database";
        case DB_ERR_INVALID: return "invalid argument";
    }
    return "unknown status";
}

#endif
