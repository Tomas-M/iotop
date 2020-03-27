#include "iotop.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct xxxid_stats_arr *arr_alloc(void)
{
    struct xxxid_stats_arr *a;

    a = calloc(1, sizeof *a);

    if (!a)
        return NULL;

    a->arr = calloc(PROC_LIST_SZ_INC, sizeof *a->arr);
    if (!a->arr)
    {
        free(a);
        return NULL;
    }
    return a;
}

static int arr_resize(struct xxxid_stats_arr *a, int newsize)
{
    struct xxxid_stats **t;

    if (!a)
        return EINVAL;
    if (a->size >= newsize)
        return 0;

    newsize = (newsize + PROC_LIST_SZ_INC - 1) / PROC_LIST_SZ_INC;
    newsize *= PROC_LIST_SZ_INC;
    t = realloc(a->arr, newsize * sizeof(struct xxxid_stats *));
    if (!t)
        return ENOMEM;

    a->arr = t;
    a->size = newsize;
    return 0;
}

int arr_add(struct xxxid_stats_arr *pa, struct xxxid_stats *ps)
{
    int a = -1;
    int i,s,e;
    pid_t r;
    int res;

    if (!pa)
        return EINVAL;
    if (!ps)
        return EINVAL;

    res = arr_resize(pa, pa->length + 1);
    if (res)
        return res;

    if (pa->sor)
    {
        free(pa->sor);
        pa->sor = NULL;
    }

    s = 0;
    e = pa->length;
    for (;;)
    {
        if (e - s < 5)
        {
            for (i = s; i < e; i++)
            {
                r = ps->tid - pa->arr[i]->tid;
                if (!r)
                    return EINVAL; // can't add duplicate
                if (r < 0)
                    break;
            }
            a = i;
            break;
        }
        else
        {
            i = s + (e - s) / 2;
            r = ps->tid - pa->arr[i]->tid;
            if (!r)
                return EINVAL; // can't add duplicate
            if (r < 0)
                e = i;
            else
                s = i + 1;
        }
    }

    // add at position a
    if (a != pa->length)
        memmove(pa->arr + a + 1, pa->arr + a, (pa->length - a) * sizeof *pa->arr);
    pa->arr[a] = ps;
    pa->length++;

    return 0; // SUCCESS
}

struct xxxid_stats *arr_find(struct xxxid_stats_arr *pa, pid_t tid)
{
    int i, s, e, r;

    if (!pa)
        return NULL;

    s = 0;
    e = pa->length;
    for (;;)
    {
        if (e - s < 5)
        {
            for (i = s; i < e; i++)
            {
                r = tid - pa->arr[i]->tid;
                if (!r)
                    return pa->arr[i];
                if (r < 0)
                    break;
            }
            return NULL;
        }
        else
        {
            i = s + (e - s) / 2;
            r = tid - pa->arr[i]->tid;
            if (!r)
                return pa->arr[i];
            if (r < 0)
                e = i;
            else
                s = i + 1;
        }
    }
}

void arr_free(struct xxxid_stats_arr *pa)
{
    int i;

    if (!pa)
        return;
    if (pa->arr)
    {
        for (i = 0; i < pa->length; i++)
            free_stats(pa->arr[i]);
        free(pa->arr);
    }
    if (pa->sor)
        free(pa->sor);
    free(pa);
}


void arr_sort(struct xxxid_stats_arr *pa, int (*cb)(const void *a, const void *b, void *arg), void *arg)
{
    if (!pa)
        return;
    if (pa->sor)
        free(pa->sor);
    pa->sor = NULL;
    if (!pa->length)
        return;
    pa->sor = calloc(pa->length, sizeof *pa->arr);
    if (!pa->sor)
        return;

    memcpy(pa->sor, pa->arr, pa->length * sizeof *pa->arr);
    qsort_r(pa->sor, pa->length, sizeof *pa->sor,cb,arg);
}

