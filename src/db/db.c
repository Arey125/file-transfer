#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "db.h"

sqlite3 *db;

int exec(char *sql) {
    char *err_msg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        printf("SQL error for \"%s\": %s\n", sql, err_msg);
        sqlite3_free(err_msg);
    }
    return rc;
}

int init_db() {
    int rc = sqlite3_open("fsh.db", &db);
    if (rc != SQLITE_OK) {
        printf("Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return -1;
    }

    char commands[][256] = {
        "PRAGMA foreign_keys = ON",
        "CREATE TABLE IF NOT EXISTS files (id TEXT PRIMARY KEY, name TEXT)",
        "CREATE TABLE IF NOT EXISTS chunks (id TEXT PRIMARY KEY, content "
        "BLOB, ind INTEGER, file_id TEXT, FOREIGN KEY(file_id) REFERENCES files(id))",
    };

    int commands_count = sizeof(commands) / sizeof(commands[0]);
    for (int i = 0; i < commands_count; i++)
        if (exec(commands[i]) != SQLITE_OK)
            return -1;

    return 0;
};

int get_chunk_from_db(char *id, char **chunk) {
    *chunk = malloc(1);
    int len = 0;

    char sql[256];
    sprintf(sql, "select content from chunks where id = '%s'", id);
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        len = sqlite3_column_bytes(stmt, 0);
        *chunk = realloc(*chunk, len);
        memcpy(*chunk, sqlite3_column_blob(stmt, 0), len);
    } else {
        printf("Chunk not found: %s\n", sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);

    return len;
}

char* get_file_list_from_db() {
    char sql[] = "select name from files";
    sqlite3_stmt *stmt;
    int len = 256;
    int pos = 0;
    char *result = malloc(len);
    int rc;

    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int str_len = sqlite3_column_bytes(stmt, 0);
        if (pos + str_len + 1 >= len) {
            len *= 2;
            result = realloc(result, len);
        }
        pos += sprintf(result + pos, "%s\n", sqlite3_column_text(stmt, 0));
    }

    if (rc != SQLITE_DONE) {
        printf("Error while trying to get file names from db: %s\n",
               sqlite3_errmsg(db));
        return NULL;
    }
    result[pos - 1] = '\0';

    sqlite3_finalize(stmt);

    return result;
}

int start_adding_file_to_db(char* name) {
    int rc = exec("BEGIN TRANSACTION");
    if (rc != SQLITE_OK) {
        return -1;
    }
    char sql[256];
    sprintf(sql, "insert into files (id, name) values ('new', '%s')", name);
    rc = exec(sql);
    if (rc != SQLITE_OK) {
        exec("ROLLBACK");
    }
    return rc == SQLITE_OK ? 0 : -1;
}


int add_chunk_to_db(char* id, char* content, int content_len, int ind) {
    char sql[256];
    sprintf(sql, "insert into chunks (id, content, ind, file_id) values ('%s', ?, %d, 'new')", id, ind);
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    int rc = sqlite3_bind_blob(stmt, 1, content, content_len, SQLITE_STATIC);
    if (rc != SQLITE_OK) {
        printf("Could not bind blob: %s", sqlite3_errmsg(db));
        return -1;
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        printf("Could not add chunk: %s", sqlite3_errmsg(db));
        exec("ROLLBACK");
        return -1;
    }

    rc = sqlite3_finalize(stmt);

    if (rc != SQLITE_OK) {
        exec("ROLLBACK");
        return -1;
    }
    return 0;
}

int finish_adding_file_to_db(char* file_id) {
    char commands[3][256];
    sprintf(commands[0], "insert into files (id, name) values ('%s', (select name from files where id = 'new'))", file_id);
    sprintf(commands[1], "UPDATE chunks SET file_id = '%s' WHERE file_id = 'new'", file_id);
    sprintf(commands[2], "DELETE FROM files WHERE id = 'new'");

    for (int i = 0; i < 3; i++) {
        if (exec(commands[i]) != SQLITE_OK) {
            return -1;
        }
    }

    return commit_transaction();
}

int commit_transaction() {
    return exec("COMMIT") == SQLITE_OK ? 0 : -1;
}
