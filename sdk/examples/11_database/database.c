/* database.c - Database usage example
 * Demonstrates the FunDB SQL database engine.
 */

#include "funsos.h"

int main(void)
{
    funsos_window_t win = funsos_create_window(80, 60, 600, 400, "Database Demo");
    funsos_fill_window(win, 0xFFFFFF);

    funsos_color_t black = {0x00, 0x00, 0x00, 0xFF};
    funsos_color_t blue  = {0x00, 0x00, 0xFF, 0xFF};
    funsos_color_t green = {0x00, 0x80, 0x00, 0xFF};

    funsos_draw_text(win, 20, 20, "FunDB Database Demo", blue);

    /* Open database */
    fundb_handle_t db = fundb_open("/var/db/test.db");
    if (db) {
        funsos_draw_text(win, 20, 50, "Database opened", green);

        /* Create a table */
        fundb_column_t columns[] = {
            {"id",   FUNDB_TYPE_INT,  0, 1, 1, 1, 0},
            {"name", FUNDB_TYPE_TEXT, 64, 1, 0, 0, 0},
            {"age",  FUNDB_TYPE_INT,  0, 0, 0, 0, 0},
        };

        int rc = fundb_create_table(db, "users", columns, 3);
        if (rc == 0) {
            funsos_draw_text(win, 20, 80, "Table 'users' created", green);
        }

        /* Insert data */
        fundb_row_t row;
        void *values[] = {(void *)1, (void *)"Alice", (void *)30};
        uint32_t sizes[] = {4, 6, 4};
        uint32_t types[] = {FUNDB_TYPE_INT, FUNDB_TYPE_TEXT, FUNDB_TYPE_INT};
        row.values = values; row.sizes = sizes; row.types = types;

        fundb_insert(db, "users", &row);
        funsos_draw_text(win, 20, 110, "Inserted row: Alice, 30", black);

        /* Query data */
        fundb_result_t *result = fundb_select(db, "users", "*", NULL, "id", 10);
        if (result) {
            funsos_draw_text(win, 20, 140, "Query returned rows", green);
            fundb_free_result(result);
        }

        /* SQL query */
        fundb_result_t *sql_result = fundb_query(db, "SELECT * FROM users WHERE age > 25");
        if (sql_result) {
            funsos_draw_text(win, 20, 170, "SQL query executed", green);
            fundb_free_result(sql_result);
        }

        /* Transaction */
        fundb_begin(db);
        fundb_commit(db);
        funsos_draw_text(win, 20, 200, "Transaction committed", black);

        /* Close database */
        fundb_close(db);
        funsos_draw_text(win, 20, 230, "Database closed", black);
    } else {
        funsos_draw_text(win, 20, 50, "Failed to open database", black);
    }

    /* Event loop */
    funsos_event_t event;
    while (1) {
        if (funsos_wait_event(&event) != 0)
            continue;
        if (event.type == FUNSOS_EVENT_KEY_PRESS && event.key == 0x1B)
            break;
    }

    funsos_destroy_window(win);
    return 0;
}
