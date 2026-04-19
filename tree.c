// tree.c — Tree object serialization and construction
//
// PROVIDED functions: get_file_mode, tree_parse, tree_serialize
// TODO functions: tree_from_index
//
// Binary tree format (per entry, concatenated with no separators):
// "<mode-as-ascii-octal> <n>\0<32-byte-binary-hash>"

#include "tree.h"
#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

// ─── Mode Constants ─────────────────────────────────────────────────────────

#define MODE_FILE 0100644
#define MODE_EXEC 0100755
#define MODE_DIR  0040000

// ─── PROVIDED ───────────────────────────────────────────────────────────────

uint32_t get_file_mode(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return 0;
    if (S_ISDIR(st.st_mode)) return MODE_DIR;
    if (st.st_mode & S_IXUSR) return MODE_EXEC;
    return MODE_FILE;
}

int tree_parse(const void *data, size_t len, Tree *tree_out) {
    tree_out->count = 0;
    const uint8_t *ptr = (const uint8_t *)data;
    const uint8_t *end = ptr + len;

    while (ptr < end && tree_out->count < MAX_TREE_ENTRIES) {
        TreeEntry *entry = &tree_out->entries[tree_out->count];

        const uint8_t *space = memchr(ptr, ' ', end - ptr);
        if (!space) return -1;

        char mode_str[16] = {0};
        size_t mode_len = space - ptr;
        if (mode_len >= sizeof(mode_str)) return -1;
        memcpy(mode_str, ptr, mode_len);
        entry->mode = strtol(mode_str, NULL, 8);
        ptr = space + 1;

        const uint8_t *null_byte = memchr(ptr, '\0', end - ptr);
        if (!null_byte) return -1;
        size_t name_len = null_byte - ptr;
        if (name_len >= sizeof(entry->name)) return -1;
        memcpy(entry->name, ptr, name_len);
        entry->name[name_len] = '\0';
        ptr = null_byte + 1;

        if (ptr + HASH_SIZE > end) return -1;
        memcpy(entry->hash.hash, ptr, HASH_SIZE);
        ptr += HASH_SIZE;

        tree_out->count++;
    }
    return 0;
}

static int compare_tree_entries(const void *a, const void *b) {
    return strcmp(((const TreeEntry *)a)->name, ((const TreeEntry *)b)->name);
}

int tree_serialize(const Tree *tree, void **data_out, size_t *len_out) {
    size_t max_size = tree->count * 296;
    uint8_t *buffer = malloc(max_size);
    if (!buffer) return -1;

    Tree sorted_tree = *tree;
    qsort(sorted_tree.entries, sorted_tree.count, sizeof(TreeEntry), compare_tree_entries);

    size_t offset = 0;
    for (int i = 0; i < sorted_tree.count; i++) {
        const TreeEntry *entry = &sorted_tree.entries[i];
        int written = sprintf((char *)buffer + offset, "%o %s", entry->mode, entry->name);
        offset += written + 1; // +1 for the null terminator
        memcpy(buffer + offset, entry->hash.hash, HASH_SIZE);
        offset += HASH_SIZE;
    }

    *data_out = buffer;
    *len_out = offset;
    return 0;
}

// ─── IMPLEMENTED ─────────────────────────────────────────────────────────────

// Forward declaration for object_write (implemented in object.c)
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

// Helper: write one level of the tree. Called recursively for subdirectories.
// entries[] is an array of IndexEntry pointers for files at this prefix depth.
// prefix is the directory prefix up to this level (e.g., "src/").
static int write_tree_level(IndexEntry **entries, int count, const char *prefix, ObjectID *id_out) {
    Tree tree;
    tree.count = 0;

    int i = 0;
    while (i < count) {
        const char *rel_path = entries[i]->path + strlen(prefix);

        // Find if there's a '/' — meaning this entry is in a subdirectory
        char *slash = strchr(rel_path, '/');

        if (!slash) {
            // It's a file directly at this level - add as blob entry
            TreeEntry *te = &tree.entries[tree.count];
            te->mode = entries[i]->mode;
            strncpy(te->name, rel_path, sizeof(te->name) - 1);
            te->name[sizeof(te->name) - 1] = '\0';
            te->hash = entries[i]->id;
            tree.count++;
            i++;
        } else {
            // It's in a subdirectory — group all entries sharing this prefix and recurse
            size_t dir_name_len = slash - rel_path;
            char dir_name[256];
            strncpy(dir_name, rel_path, dir_name_len);
            dir_name[dir_name_len] = '\0';

            // Build new prefix for the subdir
            char new_prefix[512];
            snprintf(new_prefix, sizeof(new_prefix), "%s%s/", prefix, dir_name);

            // Collect all entries under this subdirectory
            int j = i;
            while (j < count && strncmp(entries[j]->path + strlen(prefix), dir_name, dir_name_len) == 0
                   && (entries[j]->path + strlen(prefix))[dir_name_len] == '/') {
                j++;
            }

            // Recursively write the subtree
            ObjectID subtree_id;
            if (write_tree_level(entries + i, j - i, new_prefix, &subtree_id) != 0)
                return -1;

            TreeEntry *te = &tree.entries[tree.count];
            te->mode = MODE_DIR;
            strncpy(te->name, dir_name, sizeof(te->name) - 1);
            te->name[sizeof(te->name) - 1] = '\0';
            te->hash = subtree_id;
            tree.count++;

            i = j;
        }
    }

    // Serialize and write this tree object
    void *tree_data;
    size_t tree_len;
    if (tree_serialize(&tree, &tree_data, &tree_len) != 0) return -1;

    int rc = object_write(OBJ_TREE, tree_data, tree_len, id_out);
    free(tree_data);
    return rc;
}

int tree_from_index(ObjectID *id_out) {
    Index index;
    index.count = 0;

    if (index_load(&index) != 0) return -1;
    if (index.count == 0) {
        // Empty index — write an empty tree
        Tree empty;
        empty.count = 0;
        void *tree_data;
        size_t tree_len;
        if (tree_serialize(&empty, &tree_data, &tree_len) != 0) return -1;
        int rc = object_write(OBJ_TREE, tree_data, tree_len, id_out);
        free(tree_data);
        return rc;
    }

    // Build an array of pointers sorted by path (index_load sorts already)
    IndexEntry *ptrs[MAX_INDEX_ENTRIES];
    for (int i = 0; i < index.count; i++) {
        ptrs[i] = &index.entries[i];
    }

    return write_tree_level(ptrs, index.count, "", id_out);
}
