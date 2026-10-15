#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "src/bptree.h"

// A small shell to play with the storage engine until the VM / SQL layer
// exists. The root page id of the tree is kept in a header meta slot so
// that the same tree is opened again the next time.

#define TREE_META_SLOT DB_META_FIRST_USER

static int print_callback(uint32_t key, const void *data, uint32_t size, void *ctx) {
    (void)ctx;
    printf("%u: %.*s\n", key, (int)size, (const char*)data);
    return 0;
}

static void print_help(void) {
    printf("Commands:\n"
           "  insert <key> <value>  insert a key, or replace its value\n"
           "  get <key>\n"
           "  delete <key>\n"
           "  range <low> <high>    print all keys in [low, high]\n"
           "  stats                 check the tree and print statistics\n"
           "  print                 print the structure of the tree\n"
           "  help\n"
           "  quit\n");
}

// Parses an unsigned 32 bit number at *s and moves *s past it
static int parse_u32(char **s, uint32_t *out) {
    while (**s == ' ') (*s)++;
    if (**s < '0' || **s > '9') return 0;

    char *end;
    errno = 0;
    unsigned long v = strtoul(*s, &end, 10);
    if (errno != 0 || v > UINT32_MAX) return 0;
    *out = (uint32_t)v;
    *s = end;
    while (**s == ' ') (*s)++;
    return 1;
}

static void report(DBStatus rc) {
    if (rc == DB_OK) printf("ok\n");
    else printf("error: %s\n", db_status_str(rc));
}

static void run_command(BufferManager *bm, BPTree *bpt, char *line) {
    char *args = line + strcspn(line, " ");
    if (*args) *args++ = '\0';
    uint32_t key, high;

    if (strcmp(line, "insert") == 0 && parse_u32(&args, &key)) {
        report(bpt_insert(bpt, key, args, (uint32_t)strlen(args)));
    }
    else if (strcmp(line, "get") == 0 && parse_u32(&args, &key)) {
        void *data;
        uint32_t size;
        DBStatus rc = bpt_get(bpt, key, &data, &size);
        if (rc == DB_OK) {
            printf("%.*s\n", (int)size, (char*)data);
            free(data);
        }
        else report(rc);
    }
    else if (strcmp(line, "delete") == 0 && parse_u32(&args, &key)) {
        report(bpt_delete(bpt, key));
    }
    else if (strcmp(line, "range") == 0 && parse_u32(&args, &key) &&
            parse_u32(&args, &high)) {
        DBStatus rc = bpt_range_query(bpt, key, high, print_callback, NULL);
        if (rc != DB_OK) report(rc);
    }
    else if (strcmp(line, "stats") == 0) {
        BPTStats tree;
        BufferManagerStats buffer;
        DBStatus rc = bpt_verify(bpt, &tree);
        if (rc != DB_OK) {
            report(rc);
            return;
        }
        buffer_manager_stats(bm, &buffer);
        printf("tree:   %llu keys, height %u, %llu leaves, %llu internal nodes\n",
            (unsigned long long)tree.key_count, tree.height,
            (unsigned long long)tree.leaf_count, (unsigned long long)tree.internal_count);
        printf("file:   %u pages (%u free)\n", buffer.page_count, buffer.free_pages);
        printf("buffer: %u/%u frames used, %llu hits, %llu misses, %llu evictions\n",
            buffer.cached_pages, buffer.frame_count, (unsigned long long)buffer.hits,
            (unsigned long long)buffer.misses, (unsigned long long)buffer.evictions);
    }
    else if (strcmp(line, "print") == 0) {
        DBStatus rc = bpt_print(bpt, stdout);
        if (rc != DB_OK) report(rc);
    }
    else if (strcmp(line, "help") == 0) {
        print_help();
    }
    else if (*line) {
        printf("unknown command, type 'help'\n");
    }
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "db/main.db";

    BufferManager *bm;
    DBStatus rc = buffer_manager_open(path, 0, &bm);
    if (rc != DB_OK) {
        fprintf(stderr, "cannot open %s: %s\n", path, db_status_str(rc));
        return 1;
    }

    BPTree *bpt;
    uint32_t root_page_id = buffer_manager_get_meta(bm, TREE_META_SLOT);
    if (root_page_id == NULL_PAGE_ID) {
        rc = bpt_create(bm, &bpt);
        if (rc == DB_OK) {
            buffer_manager_set_meta(bm, TREE_META_SLOT, bpt_root_page_id(bpt));
        }
    }
    else {
        rc = bpt_open(bm, root_page_id, &bpt);
    }
    if (rc != DB_OK) {
        fprintf(stderr, "cannot open the tree: %s\n", db_status_str(rc));
        buffer_manager_close(bm);
        return 1;
    }

    printf("Opened %s, type 'help' for a list of commands\n", path);
    char line[4096];
    for (;;) {
        printf("db> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) break;
        run_command(bm, bpt, line);
    }

    bpt_close(bpt);
    rc = buffer_manager_close(bm);
    if (rc != DB_OK) {
        fprintf(stderr, "failed to write the database: %s\n", db_status_str(rc));
        return 1;
    }
    return 0;
}
