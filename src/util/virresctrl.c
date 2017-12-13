/*
 * virresctrl.c:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "virresctrlpriv.h"
#include "c-ctype.h"
#include "count-one-bits.h"
#include "viralloc.h"
#include "virfile.h"
#include "virlog.h"
#include "virobject.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_RESCTRL

VIR_LOG_INIT("util.virresctrl")


/* Common definitions */
#define SYSFS_RESCTRL_PATH "/sys/fs/resctrl"

/* Resctrl is short for Resource Control.  It might be implemented for various
 * resources, but at the time of this writing this is only supported for cache
 * allocation technology (aka CAT).  Hence the reson for leaving 'Cache' out of
 * all the structure and function names for now (can be added later if needed.
 */

/* Our naming for cache types and scopes */
VIR_ENUM_IMPL(virCache, VIR_CACHE_TYPE_LAST,
              "both",
              "code",
              "data")

/*
 * This is the same enum, but for the resctrl naming
 * of the type (L<level><type>)
 */
VIR_ENUM_DECL(virResctrl)
VIR_ENUM_IMPL(virResctrl, VIR_CACHE_TYPE_LAST,
              "",
              "CODE",
              "DATA")


/* Info-related definitions and InfoClass-related functions */
typedef struct _virResctrlInfoPerType virResctrlInfoPerType;
typedef virResctrlInfoPerType *virResctrlInfoPerTypePtr;
struct _virResctrlInfoPerType {
    /* Kernel-provided information */
    char *cbm_mask;
    unsigned int min_cbm_bits;

    /* Our computed information from the above */
    unsigned int bits;
    unsigned int max_cache_id;

    /* In order to be self-sufficient we need size information per cache.
     * Funnily enough, one of the outcomes of the resctrlfs design is that it
     * does not account for different sizes per cache on the same level.  So
     * for the sake of easiness, let's copy that, for now. */
    unsigned long long size;

    /* Information that we will return upon request (this is public struct) as
     * until now all the above is internal to this module */
    virResctrlInfoPerCache control;
};

typedef struct _virResctrlInfoPerLevel virResctrlInfoPerLevel;
typedef virResctrlInfoPerLevel *virResctrlInfoPerLevelPtr;
struct _virResctrlInfoPerLevel {
    virResctrlInfoPerTypePtr *types;
};

struct _virResctrlInfo {
    virObject parent;

    virResctrlInfoPerLevelPtr *levels;
    size_t nlevels;
};

static virClassPtr virResctrlInfoClass;

static void
virResctrlInfoDispose(void *obj)
{
    size_t i = 0;
    size_t j = 0;

    virResctrlInfoPtr resctrl = obj;

    for (i = 0; i < resctrl->nlevels; i++) {
        virResctrlInfoPerLevelPtr level = resctrl->levels[i];

        if (!level)
            continue;

        if (level->types) {
            for (j = 0; j < VIR_CACHE_TYPE_LAST; j++) {
                if (level->types[j])
                    VIR_FREE(level->types[j]->cbm_mask);
                VIR_FREE(level->types[j]);
            }
        }
        VIR_FREE(level->types);
        VIR_FREE(level);
    }

    VIR_FREE(resctrl->levels);
}


static int virResctrlInfoOnceInit(void)
{
    if (!(virResctrlInfoClass = virClassNew(virClassForObject(),
                                            "virResctrlInfo",
                                            sizeof(virResctrlInfo),
                                            virResctrlInfoDispose)))
        return -1;

    return 0;
}


VIR_ONCE_GLOBAL_INIT(virResctrlInfo)


virResctrlInfoPtr
virResctrlInfoNew(void)
{
    if (virResctrlInfoInitialize() < 0)
        return NULL;

    return virObjectNew(virResctrlInfoClass);
}


/* Alloc-related definitions and AllocClass-related functions */
typedef struct _virResctrlAllocPerType virResctrlAllocPerType;
typedef virResctrlAllocPerType *virResctrlAllocPerTypePtr;
struct _virResctrlAllocPerType {
    /* There could be bool saying whether this is set or not, but since everything
     * in virResctrlAlloc (and most of libvirt) goes with pointer arrays we would
     * have to have one more level of allocation anyway, so this stays faithful to
     * the concept */
    unsigned long long **sizes;
    size_t nsizes;

    /* Mask for each cache */
    virBitmapPtr *masks;
    size_t nmasks;
};

typedef struct _virResctrlAllocPerLevel virResctrlAllocPerLevel;
typedef virResctrlAllocPerLevel *virResctrlAllocPerLevelPtr;
struct _virResctrlAllocPerLevel {
    virResctrlAllocPerTypePtr *types; /* Indexed with enum virCacheType */
};

struct _virResctrlAlloc {
    virObject parent;

    virResctrlAllocPerLevelPtr *levels;
    size_t nlevels;

    char *id; /* The identifier (any unique string for now) */
    char *path;
};

static virClassPtr virResctrlAllocClass;

static void
virResctrlAllocDispose(void *obj)
{
    size_t i = 0;
    size_t j = 0;
    size_t k = 0;

    virResctrlAllocPtr resctrl = obj;

    for (i = 0; i < resctrl->nlevels; i++) {
        virResctrlAllocPerLevelPtr level = resctrl->levels[i];

        if (!level)
            continue;

        for (j = 0; j < VIR_CACHE_TYPE_LAST; j++) {
            virResctrlAllocPerTypePtr type = level->types[j];

            if (!type)
                continue;

            for (k = 0; k < type->nsizes; k++)
                VIR_FREE(type->sizes[k]);

            for (k = 0; k < type->nmasks; k++)
                virBitmapFree(type->masks[k]);

            VIR_FREE(type->sizes);
            VIR_FREE(type->masks);
            VIR_FREE(type);
        }
        VIR_FREE(level->types);
        VIR_FREE(level);
    }

    VIR_FREE(resctrl->id);
    VIR_FREE(resctrl->path);
    VIR_FREE(resctrl->levels);
}


static int
virResctrlAllocOnceInit(void)
{
    if (!(virResctrlAllocClass = virClassNew(virClassForObject(),
                                             "virResctrlAlloc",
                                             sizeof(virResctrlAlloc),
                                             virResctrlAllocDispose)))
        return -1;

    return 0;
}


VIR_ONCE_GLOBAL_INIT(virResctrlAlloc)


virResctrlAllocPtr
virResctrlAllocNew(void)
{
    if (virResctrlAllocInitialize() < 0)
        return NULL;

    return virObjectNew(virResctrlAllocClass);
}


/* Common functions */
static int
virResctrlLockInternal(int op)
{
    int fd = open(SYSFS_RESCTRL_PATH, O_DIRECTORY | O_CLOEXEC);

    if (fd < 0) {
        virReportSystemError(errno, "%s", _("Cannot open resctrlfs"));
        return -1;
    }

    if (flock(fd, op) < 0) {
        virReportSystemError(errno, "%s", _("Cannot lock resctrlfs"));
        VIR_FORCE_CLOSE(fd);
        return -1;
    }

    return fd;
}


static inline int
virResctrlLockWrite(void)
{
    return virResctrlLockInternal(LOCK_EX);
}


static int
virResctrlUnlock(int fd)
{
    if (fd == -1)
        return 0;

    /* The lock gets unlocked by closing the fd, which we need to do anyway in
     * order to clean up properly */
    if (VIR_CLOSE(fd) < 0) {
        virReportSystemError(errno, "%s", _("Cannot close resctrlfs"));

        /* Trying to save the already broken */
        if (flock(fd, LOCK_UN) < 0)
            virReportSystemError(errno, "%s", _("Cannot unlock resctrlfs"));
        return -1;
    }

    return 0;
}


/* Info-related functions */
bool
virResctrlInfoIsEmpty(virResctrlInfoPtr resctrl)
{
    size_t i = 0;
    size_t j = 0;

    if (!resctrl)
        return true;

    for (i = 0; i < resctrl->nlevels; i++) {
        virResctrlInfoPerLevelPtr i_level = resctrl->levels[i];

        if (!i_level)
            continue;

        for (j = 0; j < VIR_CACHE_TYPE_LAST; j++) {
            if (i_level->types[j])
                return false;
        }
    }

    return true;
}

int
virResctrlGetInfo(virResctrlInfoPtr resctrl)
{
    DIR *dirp = NULL;
    char *info_path = NULL;
    char *endptr = NULL;
    char *tmp_str = NULL;
    int ret = -1;
    int rv = -1;
    int type = 0;
    struct dirent *ent = NULL;
    unsigned int level = 0;
    virResctrlInfoPerLevelPtr i_level = NULL;
    virResctrlInfoPerTypePtr i_type = NULL;

    rv = virDirOpenIfExists(&dirp, SYSFS_RESCTRL_PATH "/info");
    if (rv <= 0) {
        ret = rv;
        goto cleanup;
    }

    while ((rv = virDirRead(dirp, &ent, SYSFS_RESCTRL_PATH "/info")) > 0) {
        if (ent->d_type != DT_DIR)
            continue;

        if (ent->d_name[0] != 'L')
            continue;

        if (virStrToLong_uip(ent->d_name + 1, &endptr, 10, &level) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("Cannot parse resctrl cache info level"));
            goto cleanup;
        }

        type = virResctrlTypeFromString(endptr);
        if (type < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("Cannot parse resctrl cache info type"));
            goto cleanup;
        }

        if (VIR_ALLOC(i_type) < 0)
            goto cleanup;

        i_type->control.scope = type;

        rv = virFileReadValueUint(&i_type->control.max_allocation,
                                  SYSFS_RESCTRL_PATH "/info/%s/num_closids",
                                  ent->d_name);
        if (rv == -2)
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("Cannot get num_closids from resctrl cache info"));
        if (rv < 0)
            goto cleanup;

        rv = virFileReadValueString(&i_type->cbm_mask,
                                    SYSFS_RESCTRL_PATH
                                    "/info/%s/cbm_mask",
                                    ent->d_name);
        if (rv == -2)
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("Cannot get cbm_mask from resctrl cache info"));
        if (rv < 0)
            goto cleanup;

        rv = virFileReadValueUint(&i_type->min_cbm_bits,
                                  SYSFS_RESCTRL_PATH "/info/%s/min_cbm_bits",
                                  ent->d_name);
        if (rv == -2)
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("Cannot get min_cbm_bits from resctrl cache info"));
        if (rv < 0)
            goto cleanup;

        virStringTrimOptionalNewline(i_type->cbm_mask);

        if (resctrl->nlevels <= level &&
            VIR_EXPAND_N(resctrl->levels, resctrl->nlevels,
                         level - resctrl->nlevels + 1) < 0)
            goto cleanup;

        if (!resctrl->levels[level] &&
            (VIR_ALLOC(resctrl->levels[level]) < 0 ||
             VIR_ALLOC_N(resctrl->levels[level]->types, VIR_CACHE_TYPE_LAST) < 0))
            goto cleanup;
        i_level = resctrl->levels[level];

        if (i_level->types[type]) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Duplicate cache type in resctrlfs for level %u"),
                           level);
            goto cleanup;
        }

        for (tmp_str = i_type->cbm_mask; *tmp_str != '\0'; tmp_str++) {
            if (!c_isxdigit(*tmp_str)) {
                virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                               _("Cannot parse cbm_mask from resctrl cache info"));
                goto cleanup;
            }

            i_type->bits += count_one_bits(virHexToBin(*tmp_str));
        }

        VIR_STEAL_PTR(i_level->types[type], i_type);
    }

    ret = 0;
 cleanup:
    VIR_DIR_CLOSE(dirp);
    VIR_FREE(info_path);
    VIR_FREE(i_type);
    return ret;
}


int
virResctrlInfoGetCache(virResctrlInfoPtr resctrl,
                       unsigned int level,
                       unsigned long long size,
                       size_t *ncontrols,
                       virResctrlInfoPerCachePtr **controls)
{
    virResctrlInfoPerLevelPtr i_level = NULL;
    virResctrlInfoPerTypePtr i_type = NULL;
    size_t i = 0;
    int ret = -1;

    if (virResctrlInfoIsEmpty(resctrl))
        return 0;

    if (level >= resctrl->nlevels)
        return 0;

    i_level = resctrl->levels[level];
    if (!i_level)
        return 0;

    for (i = 0; i < VIR_CACHE_TYPE_LAST; i++) {
        i_type = i_level->types[i];
        if (!i_type)
            continue;

        /* Let's take the opportunity to update our internal information about
         * the cache size */
        if (!i_type->size) {
            i_type->size = size;
            i_type->control.granularity = size / i_type->bits;
            if (i_type->min_cbm_bits != 1)
                i_type->control.min = i_type->min_cbm_bits * i_type->control.granularity;
        } else {
            if (i_type->size != size) {
                virReportError(VIR_ERR_INTERNAL_ERROR,
                               _("Forbidden inconsistency for resctrlfs, "
                                 "level %u caches have different sizes"),
                               level);
                goto error;
            }
            i_type->max_cache_id++;
        }

        if (VIR_EXPAND_N(*controls, *ncontrols, 1) < 0)
            goto error;
        if (VIR_ALLOC((*controls)[*ncontrols - 1]) < 0)
            goto error;

        memcpy((*controls)[*ncontrols - 1], &i_type->control, sizeof(i_type->control));
    }

    ret = 0;
 cleanup:
    return ret;
 error:
    while (*ncontrols)
        VIR_FREE((*controls)[--*ncontrols]);
    VIR_FREE(*controls);
    goto cleanup;
}


/* Alloc-related functions */
bool
virResctrlAllocIsEmpty(virResctrlAllocPtr resctrl)
{
    size_t i = 0;
    size_t j = 0;
    size_t k = 0;

    if (!resctrl)
        return true;

    for (i = 0; i < resctrl->nlevels; i++) {
        virResctrlAllocPerLevelPtr a_level = resctrl->levels[i];

        if (!a_level)
            continue;

        for (j = 0; j < VIR_CACHE_TYPE_LAST; j++) {
            virResctrlAllocPerTypePtr a_type = a_level->types[j];

            if (!a_type)
                continue;

            for (k = 0; k < a_type->nsizes; k++) {
                if (a_type->sizes[k])
                    return false;
            }

            for (k = 0; k < a_type->nmasks; k++) {
                if (a_type->masks[k])
                    return false;
            }
        }
    }

    return true;
}


static virResctrlAllocPerTypePtr
virResctrlAllocGetType(virResctrlAllocPtr resctrl,
                       unsigned int level,
                       virCacheType type)
{
    virResctrlAllocPerLevelPtr a_level = NULL;

    if (resctrl->nlevels <= level &&
        VIR_EXPAND_N(resctrl->levels, resctrl->nlevels, level - resctrl->nlevels + 1) < 0)
        return NULL;

    if (!resctrl->levels[level] &&
        (VIR_ALLOC(resctrl->levels[level]) < 0 ||
         VIR_ALLOC_N(resctrl->levels[level]->types, VIR_CACHE_TYPE_LAST) < 0))
        return NULL;

    a_level = resctrl->levels[level];

    if (!a_level->types[type] && VIR_ALLOC(a_level->types[type]) < 0)
        return NULL;

    return a_level->types[type];
}


int
virResctrlAllocUpdateMask(virResctrlAllocPtr resctrl,
                          unsigned int level,
                          virCacheType type,
                          unsigned int cache,
                          virBitmapPtr mask)
{
    virResctrlAllocPerTypePtr a_type = virResctrlAllocGetType(resctrl, level, type);

    if (!a_type)
        return -1;

    if (a_type->nmasks <= cache &&
        VIR_EXPAND_N(a_type->masks, a_type->nmasks,
                     cache - a_type->nmasks + 1) < 0)
        return -1;

    if (!a_type->masks[cache]) {
        a_type->masks[cache] = virBitmapNew(virBitmapSize(mask));

        if (!a_type->masks[cache])
            return -1;
    }

    return virBitmapCopy(a_type->masks[cache], mask);
}


int
virResctrlAllocUpdateSize(virResctrlAllocPtr resctrl,
                          unsigned int level,
                          virCacheType type,
                          unsigned int cache,
                          unsigned long long size)
{
    virResctrlAllocPerTypePtr a_type = virResctrlAllocGetType(resctrl, level, type);

    if (!a_type)
        return -1;

    if (a_type->nsizes <= cache &&
        VIR_EXPAND_N(a_type->sizes, a_type->nsizes,
                     cache - a_type->nsizes + 1) < 0)
        return -1;

    if (!a_type->sizes[cache] && VIR_ALLOC(a_type->sizes[cache]) < 0)
        return -1;

    *(a_type->sizes[cache]) = size;

    return 0;
}


static bool
virResctrlAllocCheckCollision(virResctrlAllocPtr a,
                              unsigned int level,
                              virCacheType type,
                              unsigned int cache)
{
    virResctrlAllocPerLevelPtr a_level = NULL;
    virResctrlAllocPerTypePtr a_type = NULL;

    if (!a)
        return false;

    if (a->nlevels <= level)
        return false;

    a_level = a->levels[level];

    if (!a_level)
        return false;

    /* All types should be always allocated */
    a_type = a_level->types[VIR_CACHE_TYPE_BOTH];

    /* If there is an allocation for type 'both', there can be no other
     * allocation for the same cache */
    if (a_type && a_type->nsizes > cache && a_type->sizes[cache])
        return true;

    if (type == VIR_CACHE_TYPE_BOTH) {
        a_type = a_level->types[VIR_CACHE_TYPE_CODE];

        if (a_type && a_type->nsizes > cache && a_type->sizes[cache])
            return true;

        a_type = a_level->types[VIR_CACHE_TYPE_DATA];

        if (a_type && a_type->nsizes > cache && a_type->sizes[cache])
            return true;
    } else {
        a_type = a_level->types[type];

        if (a_type && a_type->nsizes > cache && a_type->sizes[cache])
            return true;
    }

    return false;
}


int
virResctrlAllocSetSize(virResctrlAllocPtr resctrl,
                       unsigned int level,
                       virCacheType type,
                       unsigned int cache,
                       unsigned long long size)
{
    if (virResctrlAllocCheckCollision(resctrl, level, type, cache)) {
        virReportError(VIR_ERR_XML_ERROR,
                       _("Colliding cache allocations for cache "
                         "level '%u' id '%u', type '%s'"),
                       level, cache, virCacheTypeToString(type));
        return -1;
    }

    return virResctrlAllocUpdateSize(resctrl, level, type, cache, size);
}


int
virResctrlAllocForeachSize(virResctrlAllocPtr resctrl,
                           virResctrlAllocForeachSizeCallback cb,
                           void *opaque)
{
    int ret = 0;
    unsigned int level = 0;
    unsigned int type = 0;
    unsigned int cache = 0;

    if (!resctrl)
        return 0;

    for (level = 0; level < resctrl->nlevels; level++) {
        virResctrlAllocPerLevelPtr a_level = resctrl->levels[level];

        if (!a_level)
            continue;

        for (type = 0; type < VIR_CACHE_TYPE_LAST; type++) {
            virResctrlAllocPerTypePtr a_type = a_level->types[type];

            if (!a_type)
                continue;

            for (cache = 0; cache < a_type->nsizes; cache++) {
                unsigned long long *size = a_type->sizes[cache];

                if (!size)
                    continue;

                ret = cb(level, type, cache, *size, opaque);
                if (ret < 0)
                    return ret;
            }
        }
    }

    return 0;
}


int
virResctrlAllocSetID(virResctrlAllocPtr alloc,
                     const char *id)
{
    if (!id) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Resctrl allocation 'id' cannot be NULL"));
        return -1;
    }

    return VIR_STRDUP(alloc->id, id);
}


char *
virResctrlAllocFormat(virResctrlAllocPtr resctrl)
{
    virBuffer buf = VIR_BUFFER_INITIALIZER;
    unsigned int level = 0;
    unsigned int type = 0;
    unsigned int cache = 0;

    if (!resctrl)
        return NULL;

    for (level = 0; level < resctrl->nlevels; level++) {
        virResctrlAllocPerLevelPtr a_level = resctrl->levels[level];

        if (!a_level)
            continue;

        for (type = 0; type < VIR_CACHE_TYPE_LAST; type++) {
            virResctrlAllocPerTypePtr a_type = a_level->types[type];

            if (!a_type)
                continue;

            virBufferAsprintf(&buf, "L%u%s:", level, virResctrlTypeToString(type));

            for (cache = 0; cache < a_type->nmasks; cache++) {
                virBitmapPtr mask = a_type->masks[cache];
                char *mask_str = NULL;

                if (!mask)
                    continue;

                mask_str = virBitmapToString(mask, false, true);
                if (!mask_str) {
                    virBufferFreeAndReset(&buf);
                    return NULL;
                }

                virBufferAsprintf(&buf, "%u=%s;", cache, mask_str);
                VIR_FREE(mask_str);
            }

            virBufferTrim(&buf, ";", 1);
            virBufferAddChar(&buf, '\n');
        }
    }

    virBufferCheckError(&buf);
    return virBufferContentAndReset(&buf);
}


static int
virResctrlAllocParseProcessCache(virResctrlAllocPtr resctrl,
                                 unsigned int level,
                                 virCacheType type,
                                 char *cache)
{
    char *tmp = strchr(cache, '=');
    unsigned int cache_id = 0;
    virBitmapPtr mask = NULL;
    int ret = -1;

    if (!tmp)
        return 0;

    *tmp = '\0';
    tmp++;

    if (virStrToLong_uip(cache, NULL, 10, &cache_id) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Invalid cache id '%s'"), cache);
        return -1;
    }

    mask = virBitmapNewString(tmp);
    if (!mask)
        return -1;

    if (virResctrlAllocUpdateMask(resctrl, level, type, cache_id, mask) < 0)
        goto cleanup;

    ret = 0;
 cleanup:
    virBitmapFree(mask);
    return ret;
}


static int
virResctrlAllocParseProcessLine(virResctrlAllocPtr resctrl,
                                char *line)
{
    char **caches = NULL;
    char *tmp = NULL;
    unsigned int level = 0;
    int type = -1;
    size_t ncaches = 0;
    size_t i = 0;
    int ret = -1;

    /* Skip lines that don't concern caches, e.g. MB: etc. */
    if (line[0] != 'L')
        return 0;

    /* And lines that we can't parse too */
    tmp = strchr(line, ':');
    if (!tmp)
        return 0;

    *tmp = '\0';
    tmp++;

    if (virStrToLong_uip(line + 1, &line, 10, &level) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Cannot parse resctrl schema level '%s'"),
                       line + 1);
        return -1;
    }

    type = virResctrlTypeFromString(line);
    if (type < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Cannot parse resctrl schema level '%s'"),
                       line + 1);
        return -1;
    }

    caches = virStringSplitCount(tmp, ";", 0, &ncaches);
    if (!caches)
        return 0;

    for (i = 0; i < ncaches; i++) {
        if (virResctrlAllocParseProcessCache(resctrl, level, type, caches[i]) < 0)
            goto cleanup;
    }

    ret = 0;
 cleanup:
    virStringListFree(caches);
    return ret;
}


static int
virResctrlAllocParse(virResctrlAllocPtr alloc,
                     const char *schemata)
{
    char **lines = NULL;
    size_t nlines = 0;
    size_t i = 0;
    int ret = -1;

    lines = virStringSplitCount(schemata, "\n", 0, &nlines);
    for (i = 0; i < nlines; i++) {
        if (virResctrlAllocParseProcessLine(alloc, lines[i]) < 0)
            goto cleanup;
    }

    ret = 0;
 cleanup:
    virStringListFree(lines);
    return ret;
}


static void
virResctrlAllocSubtractPerType(virResctrlAllocPerTypePtr a,
                               virResctrlAllocPerTypePtr b)
{
    size_t i = 0;

    if (!a || !b)
        return;

    for (i = 0; i < a->nmasks && i < b->nmasks; ++i) {
        if (a->masks[i] && b->masks[i])
            virBitmapSubtract(a->masks[i], b->masks[i]);
    }
}


static void
virResctrlAllocSubtract(virResctrlAllocPtr a,
                        virResctrlAllocPtr b)
{
    size_t i = 0;
    size_t j = 0;

    if (!b)
        return;

    for (i = 0; i < a->nlevels && b->nlevels; ++i) {
        if (a->levels[i] && b->levels[i]) {
            /* Here we rely on all the system allocations to use the same types.
             * We kind of _hope_ it's the case.  If this is left here until the
             * review and someone finds it, then suggest only removing this last
             * sentence. */
            for (j = 0; j < VIR_CACHE_TYPE_LAST; j++) {
                virResctrlAllocSubtractPerType(a->levels[i]->types[j],
                                               b->levels[i]->types[j]);
            }
        }
    }
}


static virResctrlAllocPtr
virResctrlAllocNewFromInfo(virResctrlInfoPtr info)
{
    size_t i = 0;
    size_t j = 0;
    size_t k = 0;
    virResctrlAllocPtr ret = virResctrlAllocNew();
    virBitmapPtr mask = NULL;

    if (!ret)
        return NULL;

    for (i = 0; i < info->nlevels; i++) {
        virResctrlInfoPerLevelPtr i_level = info->levels[i];

        if (!i_level)
            continue;

        for (j = 0; j < VIR_CACHE_TYPE_LAST; j++) {
            virResctrlInfoPerTypePtr i_type = i_level->types[j];

            if (!i_type)
                continue;

            virBitmapFree(mask);
            mask = virBitmapNew(i_type->bits);
            if (!mask)
                goto error;
            virBitmapSetAll(mask);

            for (k = 0; k <= i_type->max_cache_id; k++) {
                if (virResctrlAllocUpdateMask(ret, i, j, k, mask) < 0)
                    goto error;
            }
        }
    }

 cleanup:
    virBitmapFree(mask);
    return ret;
 error:
    virObjectUnref(ret);
    ret = NULL;
    goto cleanup;
}


virResctrlAllocPtr
virResctrlAllocGetFree(virResctrlInfoPtr resctrl)
{
    virResctrlAllocPtr ret = NULL;
    virResctrlAllocPtr alloc = NULL;
    virBitmapPtr mask = NULL;
    struct dirent *ent = NULL;
    DIR *dirp = NULL;
    char *schemata = NULL;
    int rv = -1;

    if (virResctrlInfoIsEmpty(resctrl)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("Resource control is not supported on this host"));
        return NULL;
    }

    ret = virResctrlAllocNewFromInfo(resctrl);
    if (!ret)
        return NULL;

    if (virFileReadValueString(&schemata,
                               SYSFS_RESCTRL_PATH
                               "/schemata") < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Could not read schemata file for the default group"));
        goto error;
    }

    alloc = virResctrlAllocNew();
    if (!alloc)
        goto error;

    if (virResctrlAllocParse(alloc, schemata) < 0)
        goto error;
    if (virResctrlAllocIsEmpty(alloc)) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("No schemata for default resctrlfs group"));
        goto error;
    }
    virResctrlAllocSubtract(ret, alloc);

    if (virDirOpen(&dirp, SYSFS_RESCTRL_PATH) < 0)
        goto error;

    while ((rv = virDirRead(dirp, &ent, SYSFS_RESCTRL_PATH)) > 0) {
        if (ent->d_type != DT_DIR)
            continue;

        if (STREQ(ent->d_name, "info"))
            continue;

        VIR_FREE(schemata);
        if (virFileReadValueString(&schemata,
                                   SYSFS_RESCTRL_PATH
                                   "/%s/schemata",
                                   ent->d_name) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Could not read schemata file for group %s"),
                           ent->d_name);
            goto error;
        }

        virObjectUnref(alloc);
        alloc = virResctrlAllocNew();
        if (!alloc)
            goto error;

        if (virResctrlAllocParse(alloc, schemata) < 0)
            goto error;

        virResctrlAllocSubtract(ret, alloc);

        VIR_FREE(schemata);
    }
    if (rv < 0)
        goto error;

 cleanup:
    virObjectUnref(alloc);
    VIR_DIR_CLOSE(dirp);
    VIR_FREE(schemata);
    virBitmapFree(mask);
    return ret;

 error:
    virObjectUnref(ret);
    ret = NULL;
    goto cleanup;
}


static int
virResctrlAllocMasksAssign(virResctrlInfoPtr r_info,
                           virResctrlAllocPtr alloc)
{
    int ret = -1;
    unsigned int level = 0;
    virResctrlAllocPtr alloc_free = NULL;

    if (!r_info) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("Resource control is not supported on this host"));
        return -1;
    }

    alloc_free = virResctrlAllocGetFree(r_info);
    if (!alloc_free)
        return -1;

    for (level = 0; level < alloc->nlevels; level++) {
        virResctrlAllocPerLevelPtr a_level = alloc->levels[level];
        virResctrlAllocPerLevelPtr f_level = NULL;
        unsigned int type = 0;

        if (!a_level)
            continue;

        if (level < alloc_free->nlevels)
            f_level = alloc_free->levels[level];

        if (!f_level) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("Cache level %d does not support tuning"),
                           level);
            goto cleanup;
        }

        for (type = 0; type < VIR_CACHE_TYPE_LAST; type++) {
            virResctrlAllocPerTypePtr a_type = a_level->types[type];
            virResctrlAllocPerTypePtr f_type = f_level->types[type];
            unsigned int cache = 0;

            if (!a_type)
                continue;

            if (!f_type) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("Cache level %d does not support tuning for "
                                 "scope type '%s'"),
                               level, virCacheTypeToString(type));
                goto cleanup;
            }

            for (cache = 0; cache < a_type->nsizes; cache++) {
                unsigned long long *size = a_type->sizes[cache];
                virBitmapPtr a_mask = NULL;
                virBitmapPtr f_mask = NULL;
                virResctrlInfoPerLevelPtr i_level = r_info->levels[level];
                virResctrlInfoPerTypePtr i_type = i_level->types[type];
                unsigned long long granularity;
                unsigned long long need_bits;
                size_t i = 0;
                ssize_t pos = -1;
                ssize_t last_bits = 0;
                ssize_t last_pos = -1;

                if (!size)
                    continue;

                if (cache >= f_type->nmasks) {
                    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                                   _("Cache with id %u does not exists for level %d"),
                                   cache, level);
                    goto cleanup;
                }

                f_mask = f_type->masks[cache];
                if (!f_mask) {
                    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                                   _("Cache level %d id %u does not support tuning for "
                                     "scope type '%s'"),
                                   level, cache, virCacheTypeToString(type));
                    goto cleanup;
                }

                granularity = i_type->size / i_type->bits;
                need_bits = *size / granularity;

                if (*size % granularity) {
                    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                                   _("Cache allocation of size %llu is not "
                                     "divisible by granularity %llu"),
                                   *size, granularity);
                    goto cleanup;
                }

                if (need_bits < i_type->min_cbm_bits) {
                    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                                   _("Cache allocation of size %llu is smaller "
                                     "than the minimum allowed allocation %llu"),
                                   *size, granularity * i_type->min_cbm_bits);
                    goto cleanup;
                }

                while ((pos = virBitmapNextSetBit(f_mask, pos)) >= 0) {
                    ssize_t pos_clear = virBitmapNextClearBit(f_mask, pos);
                    ssize_t bits;

                    if (pos_clear < 0)
                        pos_clear = virBitmapSize(f_mask);

                    bits = pos_clear - pos;

                    /* Not enough bits, move on and skip all of them */
                    if (bits < need_bits) {
                        pos = pos_clear;
                        continue;
                    }

                    /* This fits perfectly */
                    if (bits == need_bits) {
                        last_pos = pos;
                        break;
                    }

                    /* Remember the smaller region if we already found on before */
                    if (last_pos < 0 || (last_bits && bits < last_bits)) {
                        last_bits = bits;
                        last_pos = pos;
                    }

                    pos = pos_clear;
                }

                if (last_pos < 0) {
                    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                                   _("Not enough room for allocation of "
                                     "%llu bytes for level %u cache %u "
                                     "scope type '%s'"),
                                   *size, level, cache,
                                   virCacheTypeToString(type));
                    goto cleanup;
                }

                a_mask = virBitmapNew(i_type->bits);
                for (i = last_pos; i < last_pos + need_bits; i++) {
                    ignore_value(virBitmapSetBit(a_mask, i));
                    ignore_value(virBitmapClearBit(f_mask, i));
                }

                if (a_type->nmasks <= cache) {
                    if (VIR_EXPAND_N(a_type->masks, a_type->nmasks,
                                     cache - a_type->nmasks + 1) < 0) {
                        virBitmapFree(a_mask);
                        goto cleanup;
                    }
                }
                a_type->masks[cache] = a_mask;
            }
        }
    }

    ret = 0;
 cleanup:
    virObjectUnref(alloc_free);
    return ret;
}


/* This checks if the directory for the alloc exists.  If not it tries to create
 * it and apply appropriate alloc settings. */
int
virResctrlAllocCreate(virResctrlInfoPtr r_info,
                      virResctrlAllocPtr alloc,
                      const char *drivername,
                      const char *machinename)
{
    char *schemata_path = NULL;
    char *alloc_str = NULL;
    int ret = -1;
    int lockfd = -1;

    if (!alloc)
        return 0;

    if (!r_info) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("Resource control is not supported on this host"));
        return -1;
    }

    if (!alloc->path &&
        virAsprintf(&alloc->path, "%s/%s-%s-%s",
                    SYSFS_RESCTRL_PATH, drivername, machinename, alloc->id) < 0)
        return -1;

    /* Check if this allocation was already created */
    if (virFileIsDir(alloc->path))
        return 0;

    if (virFileExists(alloc->path)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Path '%s' for resctrl allocation exists and is not a "
                         "directory"), alloc->path);
        goto cleanup;
    }

    lockfd = virResctrlLockWrite();
    if (lockfd < 0)
        goto cleanup;

    if (virResctrlAllocMasksAssign(r_info, alloc) < 0)
        goto cleanup;

    alloc_str = virResctrlAllocFormat(alloc);
    if (!alloc_str)
        goto cleanup;

    if (virAsprintf(&schemata_path, "%s/schemata", alloc->path) < 0)
        goto cleanup;

    if (virFileMakePath(alloc->path) < 0) {
        virReportSystemError(errno,
                             _("Cannot create resctrl directory '%s'"),
                             alloc->path);
        goto cleanup;
    }

    if (virFileWriteStr(schemata_path, alloc_str, 0) < 0) {
        rmdir(alloc->path);
        virReportSystemError(errno,
                             _("Cannot write into schemata file '%s'"),
                             schemata_path);
        goto cleanup;
    }

    ret = 0;
 cleanup:
    virResctrlUnlock(lockfd);
    VIR_FREE(alloc_str);
    VIR_FREE(schemata_path);
    return ret;
}


int
virResctrlAllocAddPID(virResctrlAllocPtr alloc,
                      pid_t pid)
{
    char *tasks = NULL;
    char *pidstr = NULL;
    int ret = 0;

    if (!alloc->path) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Cannot add pid to non-existing resctrl allocation"));
        return -1;
    }

    if (virAsprintf(&tasks, "%s/tasks", alloc->path) < 0)
        return -1;

    if (virAsprintf(&pidstr, "%lld", (long long int) pid) < 0)
        goto cleanup;

    if (virFileWriteStr(tasks, pidstr, 0) < 0) {
        virReportSystemError(errno,
                             _("Cannot write pid in tasks file '%s'"),
                             tasks);
        goto cleanup;
    }

    ret = 0;
 cleanup:
    VIR_FREE(tasks);
    VIR_FREE(pidstr);
    return ret;
}


int
virResctrlAllocRemove(virResctrlAllocPtr alloc)
{
    int ret = 0;

    if (!alloc->path)
        return 0;

    VIR_DEBUG("Removing resctrl allocation %s", alloc->path);
    if (rmdir(alloc->path) != 0 && errno != ENOENT) {
        ret = -errno;
        VIR_ERROR(_("Unable to remove %s (%d)"), alloc->path, errno);
    }

    return ret;
}
