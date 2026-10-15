# db

A small SQLite-like database written from scratch in C, without any
dependencies, to learn how databases work. Performance is not a goal, but
it should be able to do similar things in reasonable time.

The plan is to build it bottom up:

1. **Pager**: reads and writes fixed size pages of the database file ✅
2. **Buffer manager**: keeps pages cached in memory ✅
3. **B+tree**: maps keys to records stored in the pages ✅
4. VM: executes instructions that operate on the B+trees
5. SQL: compiles SQL into VM instructions

## Building and testing

```sh
make                  # builds build/main
make run              # starts a small shell on db/main.db
make test             # builds and runs all tests
make test TEST=bpt_test
make test SANITIZE=1  # all tests with AddressSanitizer + UndefinedBehaviorSanitizer
```

The randomized tests use a fixed seed, another one can be given with
`TEST_SEED=123 make test`.

The shell (`build/main [file]`) supports `insert <key> <value>`, `get <key>`,
`delete <key>`, `range <low> <high>`, `stats`, `print` and `quit`.

## Architecture

```
            bptree.c            uint32 key -> record, (key, RID) pairs in leaves
               |      \
               |   record_manager.c   records in slotted data pages / overflow pages
               |      /
         buffer_manager.c       page cache with pin/unpin, page allocation
               |
            pager.c             on-disk format of every page type, file I/O
```

Every layer only talks to the one below it, and only the pager touches the
file. All functions that can fail return a `DBStatus` (see `src/common.h`).

### Pager (`src/pager.h`)

The file is a sequence of 4096 byte pages. Page 0 is the database header
(magic, page count, freelist, and a few "meta" slots for the layers above).
Every other page starts with a type byte:

| Type | Content |
| --- | --- |
| B+tree page | an internal node or a leaf |
| Data page | a slotted page with records |
| Overflow page | part of a record that is too large for a data page |
| Free page | an unused page in the freelist |

The exact byte layout of each type is documented at the top of
`src/pager.h`. In memory pages are decoded into the structs of that header
(`Page`, `BPTPage`, `DataPage`, ...). Pages are validated when they are
decoded, so a corrupt file results in `DB_ERR_CORRUPT` instead of a crash.

### Buffer manager (`src/buffer_manager.h`)

A fixed number of frames, each holding one decoded page. Pages are *pinned*
while they are used and can only be evicted once they are unpinned again.
The least recently used unpinned page is evicted first, and written back to
the file if it was modified (marked dirty).

```c
Page *page;
DBStatus rc = buffer_manager_get_page(bm, page_id, &page);  // pins the page
if (rc != DB_OK) return rc;
page->bpt.num_keys++;
buffer_manager_mark_dirty(bm, page);
buffer_manager_unpin(bm, page);  // `page` must not be used after this
```

It also allocates pages (`buffer_manager_new_page`) and frees them
(`buffer_manager_free_page`). Freed pages are kept in a linked list that is
stored in the free pages themselves, and are reused before the file grows.

### Record manager (`src/record_manager.h`)

Stores variable sized records and returns a record id `RID = (page_id,
slot_id)`. Data pages are slotted pages: a slot directory at the start of the
page points to records stored from the end of the page. Deleting a record
leaves a hole which is reclaimed by compacting the page when needed, the
slot ids (and therefore RIDs) of the other records never change.

Records larger than a data page are stored in a chain of overflow pages.
Data pages with free space are kept in a doubly linked list so that new
records fill up existing pages first. Empty data pages are freed.

### B+tree (`src/bptree.h`)

Maps `uint32_t` keys to records. Leaves store `(key, RID)` pairs and are
linked for range queries. Supports insert (or replace), get, delete (with
borrowing from and merging with siblings), range queries, and destroying a
whole tree. `bpt_verify` checks every invariant of a tree and is used
heavily by the tests.

The root page of a tree never changes (like in SQLite), so the root page id
can be stored to open the tree again later, e.g. in a header meta slot or
later in a catalog table.

The order of the tree is as large as fits in a page (408 entries per leaf,
512 children per internal node). It can be lowered at compile time, which
the tests use to get very deep trees with few keys:

```sh
gcc -DBPT_MAX_ENTRIES_LEAF=3 -DBPT_MAX_CHILDREN=4 ...
```

## Limitations / next steps

- Keys are `uint32_t` only. Indexes on other column types will need
  variable length keys in the B+tree.
- There is no journal or WAL, so a crash or I/O error in the middle of an
  operation can leave the file inconsistent. Writes only happen when pages
  are evicted or flushed, which makes adding a journal possible later.
- Single threaded, no locking.
- The record format with typed values (planned in `src/pager.h`) belongs to
  the VM step.
