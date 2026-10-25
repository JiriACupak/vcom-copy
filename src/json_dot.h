/**
 * Helper functions for JSON parsing.
 *
 * The `_dot_get` family of functions return a status code and, if successful,
 * the read value through the passed pointer.
 *
 * `path` is a dot-separated object path, e.g. csp.address to get the address from `{"csp":{"address":7}}`.
 *
 * Created on 2021/09/28.
 */

#ifndef VCOM_JSON_DOT_H
#define VCOM_JSON_DOT_H

#include <cJSON.h>
#include <stdbool.h>

#define DOT_GET_OK 0
#define DOT_GET_NOTEXIST (-2)
#define DOT_GET_NULL (-3)
#define DOT_GET_BADTYPE (-4)

int dot_get_any(cJSON *root, const char * path, cJSON **out);
int dot_get_string(cJSON *root, const char * path, const char **out);
int dot_get_int(cJSON *root, const char * path, int *out);
int dot_get_bool(cJSON *root, const char * path, bool *out);
int dot_get_object(cJSON *root, const char * path, cJSON **out);
int dot_get_array(cJSON *root, const char * path, cJSON **out);

#endif //VCOM_JSON_DOT_H
