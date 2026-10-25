#include "json_dot.h"
#include <stdlib.h>
#include <string.h>

/**
 * Get a value by path.
 *
 * @param root - JSON root
 * @param path - dotted path
 * @return the object, NULL if not found
 */
static cJSON *_dot_get(cJSON *root, const char *path)
{
    char copy[256];
    strncpy(copy, path, 256);
    copy[255] = 0;
    char *piece = NULL;
    char *saveptr = NULL;
    while ((NULL != (piece = strtok_r(piece ? NULL : copy, ".", &saveptr))) && cJSON_IsObject(root)) {
        path = NULL;
        root = cJSON_GetObjectItem(root, piece);
    }
    return piece == NULL ? root : NULL;
}

int dot_get_any(cJSON *root, const char *path, cJSON **out)
{
    cJSON *obj = _dot_get(root, path);
    if (!obj) {
        return DOT_GET_NOTEXIST;
    }
    if (cJSON_IsNull(obj)) {
        return DOT_GET_NULL;
    }
    *out = obj;
    return DOT_GET_OK;
}

int dot_get_string(cJSON *root, const char *path, const char **out)
{
    cJSON *obj = _dot_get(root, path);
    if (!obj) {
        return DOT_GET_NOTEXIST;
    }
    if (cJSON_IsNull(obj)) {
        return DOT_GET_NULL;
    }
    if (cJSON_IsString(obj)) {
        *out = obj->valuestring;
        return DOT_GET_OK;
    }
    return DOT_GET_BADTYPE;
}

int dot_get_int(cJSON *root, const char *path, int *out)
{
    cJSON *obj = _dot_get(root, path);
    if (!obj) {
        return DOT_GET_NOTEXIST;
    }
    if (cJSON_IsNull(obj)) {
        return DOT_GET_NULL;
    }
    if (cJSON_IsNumber(obj)) {
        *out = obj->valueint;
        return DOT_GET_OK;
    }
    if (cJSON_IsBool(obj)) {
        *out = (int) cJSON_IsTrue(obj);
        return DOT_GET_OK;
    }
    return DOT_GET_BADTYPE;
}

int dot_get_bool(cJSON *root, const char *path, bool *out)
{
    cJSON *obj = _dot_get(root, path);
    if (!obj) {
        return DOT_GET_NOTEXIST;
    }
    if (cJSON_IsNull(obj)) {
        return DOT_GET_NULL;
    }
    if (cJSON_IsBool(obj)) {
        *out = cJSON_IsTrue(obj);
        return DOT_GET_OK;
    }
    if (cJSON_IsNumber(obj)) {
        *out = obj->valueint != 0;
        return DOT_GET_OK;
    }
    return DOT_GET_BADTYPE;
}

int dot_get_object(cJSON *root, const char *path, cJSON **out)
{
    cJSON *obj = _dot_get(root, path);
    if (!obj) {
        return DOT_GET_NOTEXIST;
    }
    if (cJSON_IsNull(obj)) {
        return DOT_GET_NULL;
    }
    if (cJSON_IsObject(obj)) {
        *out = obj;
        return DOT_GET_OK;
    }
    return DOT_GET_BADTYPE;
}

int dot_get_array(cJSON *root, const char *path, cJSON **out)
{
    cJSON *obj = _dot_get(root, path);
    if (!obj) {
        return DOT_GET_NOTEXIST;
    }
    if (cJSON_IsNull(obj)) {
        return DOT_GET_NULL;
    }
    if (cJSON_IsArray(obj)) {
        *out = obj;
        return DOT_GET_OK;
    }
    return DOT_GET_BADTYPE;
}
