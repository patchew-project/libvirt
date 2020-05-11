#include <config.h>

#include <unistd.h>

#include "internal.h"
#include "viralloc.h"
#include "testutils.h"
#include "virutil.h"

static const char* diskNames[] = {
    "sda",  "sdb",  "sdc",  "sdd",  "sde",  "sdf",  "sdg",  "sdh",  "sdi",  "sdj",  "sdk",  "sdl",  "sdm",  "sdn",  "sdo",  "sdp",  "sdq",  "sdr",  "sds",  "sdt",  "sdu",  "sdv",  "sdw",  "sdx",  "sdy",  "sdz",
    "sdaa", "sdab", "sdac", "sdad", "sdae", "sdaf", "sdag", "sdah", "sdai", "sdaj", "sdak", "sdal", "sdam", "sdan", "sdao", "sdap", "sdaq", "sdar", "sdas", "sdat", "sdau", "sdav", "sdaw", "sdax", "sday", "sdaz",
    "sdba", "sdbb", "sdbc", "sdbd", "sdbe", "sdbf", "sdbg", "sdbh", "sdbi", "sdbj", "sdbk", "sdbl", "sdbm", "sdbn", "sdbo", "sdbp", "sdbq", "sdbr", "sdbs", "sdbt", "sdbu", "sdbv", "sdbw", "sdbx", "sdby", "sdbz",
    "sdca", "sdcb", "sdcc", "sdcd", "sdce", "sdcf", "sdcg", "sdch", "sdci", "sdcj", "sdck", "sdcl", "sdcm", "sdcn", "sdco", "sdcp", "sdcq", "sdcr", "sdcs", "sdct", "sdcu", "sdcv", "sdcw", "sdcx", "sdcy", "sdcz",
    "sdda", "sddb", "sddc", "sddd", "sdde", "sddf", "sddg", "sddh", "sddi", "sddj", "sddk", "sddl", "sddm", "sddn", "sddo", "sddp", "sddq", "sddr", "sdds", "sddt", "sddu", "sddv", "sddw", "sddx", "sddy", "sddz",
    "sdea", "sdeb", "sdec", "sded", "sdee", "sdef", "sdeg", "sdeh", "sdei", "sdej", "sdek", "sdel", "sdem", "sden", "sdeo", "sdep", "sdeq", "sder", "sdes", "sdet", "sdeu", "sdev", "sdew", "sdex", "sdey", "sdez",
    "sdfa", "sdfb", "sdfc", "sdfd", "sdfe", "sdff", "sdfg", "sdfh", "sdfi", "sdfj", "sdfk", "sdfl", "sdfm", "sdfn", "sdfo", "sdfp", "sdfq", "sdfr", "sdfs", "sdft", "sdfu", "sdfv", "sdfw", "sdfx", "sdfy", "sdfz",
    "sdga", "sdgb", "sdgc", "sdgd", "sdge", "sdgf", "sdgg", "sdgh", "sdgi", "sdgj", "sdgk", "sdgl", "sdgm", "sdgn", "sdgo", "sdgp", "sdgq", "sdgr", "sdgs", "sdgt", "sdgu", "sdgv", "sdgw", "sdgx", "sdgy", "sdgz",
    "sdha", "sdhb", "sdhc", "sdhd", "sdhe", "sdhf", "sdhg", "sdhh", "sdhi", "sdhj", "sdhk", "sdhl", "sdhm", "sdhn", "sdho", "sdhp", "sdhq", "sdhr", "sdhs", "sdht", "sdhu", "sdhv", "sdhw", "sdhx", "sdhy", "sdhz",
    "sdia", "sdib", "sdic", "sdid", "sdie", "sdif", "sdig", "sdih", "sdii", "sdij", "sdik", "sdil", "sdim", "sdin", "sdio", "sdip", "sdiq", "sdir", "sdis", "sdit", "sdiu", "sdiv", "sdiw", "sdix", "sdiy", "sdiz"
};

struct testDiskName
{
    const char *name;
    int idx;
    int partition;
};

static struct testDiskName diskNamesPart[] = {
    {"sda0",          0,           0},
    {"sdb10",         1,          10},
    {"sdc2147483647", 2,  2147483647},
};

static const char* diskNamesInvalid[] = {
    "sda00", "sda01", "sdb-1",
    "vd2"
};

static int
testIndexToDiskName(const void *data G_GNUC_UNUSED)
{
    size_t i;
    char *diskName = NULL;

    for (i = 0; i < G_N_ELEMENTS(diskNames); ++i) {
        VIR_FREE(diskName);

        diskName = virIndexToDiskName(i, "sd");

        if (STRNEQ(diskNames[i], diskName)) {
            virTestDifference(stderr, diskNames[i], diskName);
            VIR_FREE(diskName);

            return -1;
        }
    }

    VIR_FREE(diskName);

    return 0;
}



static int
testDiskNameToIndex(const void *data G_GNUC_UNUSED)
{
    size_t i;
    int idx;
    char *diskName = NULL;

    for (i = 0; i < 100000; ++i) {
        VIR_FREE(diskName);

        diskName = virIndexToDiskName(i, "sd");
        idx = virDiskNameToIndex(diskName);

        if (idx < 0 || idx != i) {
            VIR_TEST_DEBUG("\nExpect [%zu]", i);
            VIR_TEST_DEBUG("Actual [%d]", idx);

            VIR_FREE(diskName);

            return -1;
        }
    }

    VIR_FREE(diskName);

    return 0;
}



static int
testDiskNameParse(const void *data G_GNUC_UNUSED)
{
    size_t i;
    int idx;
    int partition;
    struct testDiskName *disk = NULL;

    for (i = 0; i < G_N_ELEMENTS(diskNamesPart); ++i) {
        disk = &diskNamesPart[i];
        if (virDiskNameParse(disk->name, &idx, &partition))
            return -1;

        if (disk->idx != idx) {
            VIR_TEST_DEBUG("\nExpect [%d]", disk->idx);
            VIR_TEST_DEBUG("Actual [%d]", idx);
            return -1;
        }

        if (disk->partition != partition) {
            VIR_TEST_DEBUG("\nExpect [%d]", disk->partition);
            VIR_TEST_DEBUG("Actual [%d]", partition);
            return -1;
        }
    }

    for (i = 0; i < G_N_ELEMENTS(diskNamesInvalid); ++i) {
        if (!virDiskNameParse(diskNamesInvalid[i], &idx, &partition)) {
            VIR_TEST_DEBUG("Should Fail [%s]", diskNamesInvalid[i]);
            return -1;
        }
    }

    return 0;
}



struct testVersionString
{
    const char *string;
    bool allowMissing;
    int result;
    unsigned long version;
};

static struct testVersionString versions[] = {
    { "2.6.38-8-generic", false,  0, 1000000 * 2 + 1000 * 6 + 38 },
    { "3.0-1-virtual",    true,   0, 1000000 * 3 + 1000 * 0 + 0 },
    { "5",                true,   0, 1000000 * 5 + 1000 * 0 + 0 },
    { "4.1.0",            false,  0, 1000000 * 4 + 1000 * 1 + 0 },
    { "12.345.678",       false,  0, 1000000 * 12 + 1000 * 345 + 678 },
    { "1.234.5678",       false, -1, 0 },
    { "1.2345.678",       false, -1, 0 },
    { "12345.6.78",       false, -1, 0 },
    { "123456789",        true,  -1, 0 },
    { "3.0-2-virtual",    false, -1, 0 },
    { "no-number-here",   false, -1, 0 },
};

static int
testParseVersionString(const void *data G_GNUC_UNUSED)
{
    int result;
    size_t i;
    unsigned long version;

    for (i = 0; i < G_N_ELEMENTS(versions); ++i) {
        result = virParseVersionString(versions[i].string, &version,
                                       versions[i].allowMissing);

        if (result != versions[i].result) {
            VIR_TEST_DEBUG("\nVersion string [%s]", versions[i].string);
            VIR_TEST_DEBUG("Expect result [%d]", versions[i].result);
            VIR_TEST_DEBUG("Actual result [%d]", result);

            return -1;
        }

        if (result < 0)
            continue;

        if (version != versions[i].version) {
            VIR_TEST_DEBUG("\nVersion string [%s]", versions[i].string);
            VIR_TEST_DEBUG("Expect version [%lu]", versions[i].version);
            VIR_TEST_DEBUG("Actual version [%lu]", version);

            return -1;
        }
    }

    return 0;
}



struct testRoundData {
    unsigned int input;
    unsigned int output;
};

static struct testRoundData roundData[] = {
    { 0, 0 },
    { 1, 1 },
    { 1000, 1024 },
    { 1024, 1024 },
    { 1025, 2048 },
    { UINT_MAX, 0 },
};

static int
testRoundValueToPowerOfTwo(const void *data G_GNUC_UNUSED)
{
    unsigned int result;
    size_t i;

    for (i = 0; i < G_N_ELEMENTS(roundData); i++) {
        result = VIR_ROUND_UP_POWER_OF_TWO(roundData[i].input);
        if (roundData[i].output != result) {
            VIR_TEST_DEBUG("\nInput number [%u]", roundData[i].input);
            VIR_TEST_DEBUG("Expected number [%u]", roundData[i].output);
            VIR_TEST_DEBUG("Actual number [%u]", result);

            return -1;
        }
    }

    return 0;
}


#define TEST_OVERFLOW(var, val, expect) \
    tmp = val; \
    if (VIR_ASSIGN_IS_OVERFLOW(var, tmp) != expect) { \
        fprintf(stderr, "\noverflow check failed: " \
                "var: " #var " val: " #val "\n"); \
        return -1; \
    }

static int
testOverflowCheckMacro(const void *data G_GNUC_UNUSED)
{
    long long tmp;
    uint8_t luchar;
    int8_t lchar;

    TEST_OVERFLOW(luchar, 254, false);
    TEST_OVERFLOW(luchar, 255, false);
    TEST_OVERFLOW(luchar, 256, true);
    TEST_OVERFLOW(luchar, 767, true);

    TEST_OVERFLOW(lchar, 127, false);
    TEST_OVERFLOW(lchar, -128, false);
    TEST_OVERFLOW(lchar, -129, true);
    TEST_OVERFLOW(lchar, 128, true);

    return 0;
}


struct testKernelCmdlineGetValueData
{
    const char *cmdline;
    const char *arg;
    int rc;
    const char *val;
    size_t next;
};

static struct testKernelCmdlineGetValueData kEntries[] = {
    { "arg1 arg2 arg3=val1",                            "arg4",           1, NULL,                19 },
    { "arg1=val1 arg2 arg3=val3 arg4",                  "arg2",           0, NULL,                14 },
    { "arg1=val1 arg2 arg3=val3 arg4",                  "arg3",           0, "val3",              24 },
    { "arg1=val1 arg2 arg-3=val3 arg4",                 "arg_3",          0, "val3",              25 },
    { "arg1=val1 arg2 arg_3=val3 arg4",                 "arg-3",          0, "val3",              25 },
    { "arg1=val1 arg2 arg_3=val3 arg4",                 "arg_3",          0, "val3",              25 },
    { "arg1=val1 arg2 arg-3=val3 arg4",                 "arg-3",          0, "val3",              25 },
    { "arg1=val1 arg2=\"value with spaces\" arg3=val3", "arg2",           0, "value with spaces", 34 },
    { "arg1=val1 arg2=\"value with spaces\" arg3=val3", "arg3",           0, "val3",              44 },
    { "arg1=val1 \"arg2=value with spaces\" arg3=val3", "arg2",           0, "value with spaces", 34 },
    { "arg1=val1 \"arg2=value with spaces\" arg3=val3", "arg3",           0, "val3",              44 },
    { "arg1=val1 arg2=\"val\"ue arg3",                  "arg2",           0, "val\"ue",           22 },
    { "arg1=val1 arg2=\"val\"ue arg3\" escaped=val2\"", "arg3\" escaped", 0, "val2",              42 },
    { "arg1=val1 arg2longer=someval arg2=val2 arg3",    "arg2",           0, "val2",              38 },
};

static int
testKernelCmdlineGetValue(const void *data G_GNUC_UNUSED)
{
    int rc;
    char *val = NULL;
    const char *next;
    size_t i;

    for (i = 0; i < G_N_ELEMENTS(kEntries); ++i) {
        VIR_FREE(val);

        rc = virKernelCmdlineGetValue(kEntries[i].arg, kEntries[i].cmdline,
                                      &val, &next);

        if (rc != kEntries[i].rc || STRNEQ_NULLABLE(val, kEntries[i].val) ||
            (next - kEntries[i].cmdline) != kEntries[i].next) {
            VIR_TEST_DEBUG("\nKernel cmdline [%s]", kEntries[i].cmdline);
            VIR_TEST_DEBUG("Kernel argument [%s]", kEntries[i].arg);
            VIR_TEST_DEBUG("Expect rc [%d]", kEntries[i].rc);
            VIR_TEST_DEBUG("Actual rc [%d]", rc);
            VIR_TEST_DEBUG("Expect value [%s]", kEntries[i].val);
            VIR_TEST_DEBUG("Actual value [%s]", val);
            VIR_TEST_DEBUG("Expect next index [%lu]", kEntries[i].next);
            VIR_TEST_DEBUG("Actual next index [%lu]",
                           (size_t)(next - kEntries[i].cmdline));

            VIR_FREE(val);

            return -1;
        }
    }

    VIR_FREE(val);

    return 0;
}


struct testKernelCmdlineMatchData
{
    const char *cmdline;
    const char *arg;
    const char *values[2];
    virKernelCmdlineFlags flags;
    bool result;
};

static struct testKernelCmdlineMatchData kMatchEntries[] = {
    {"arg1 myarg=no arg2=val2 myarg=yes arg4=val4 myarg=no arg5", "myarg", {"1", "y"},    VIR_KERNEL_CMDLINE_FLAGS_SEARCH_STICKY | VIR_KERNEL_CMDLINE_FLAGS_CMP_EQ,     false },
    {"arg1 myarg=no arg2=val2 myarg=yes arg4=val4 myarg=no arg5", "myarg", {"on", "yes"}, VIR_KERNEL_CMDLINE_FLAGS_SEARCH_STICKY | VIR_KERNEL_CMDLINE_FLAGS_CMP_EQ,     true },
    {"arg1 myarg=no arg2=val2 myarg=yes arg4=val4 myarg=no arg5", "myarg", {"1", "y"},    VIR_KERNEL_CMDLINE_FLAGS_SEARCH_STICKY | VIR_KERNEL_CMDLINE_FLAGS_CMP_PREFIX, true },
    {"arg1 myarg=no arg2=val2 myarg=yes arg4=val4 myarg=no arg5", "myarg", {"a", "b"},    VIR_KERNEL_CMDLINE_FLAGS_SEARCH_STICKY | VIR_KERNEL_CMDLINE_FLAGS_CMP_PREFIX, false },
    {"arg1 myarg=no arg2=val2 myarg=yes arg4=val4 myarg=no arg5", "myarg", {"on", "yes"}, VIR_KERNEL_CMDLINE_FLAGS_SEARCH_LAST | VIR_KERNEL_CMDLINE_FLAGS_CMP_EQ,       false},
    {"arg1 myarg=no arg2=val2 myarg=yes arg4=val4 myarg=no arg5", "myarg", {"1", "y"},    VIR_KERNEL_CMDLINE_FLAGS_SEARCH_LAST | VIR_KERNEL_CMDLINE_FLAGS_CMP_PREFIX,   false},
    {"arg1 myarg=no arg2=val2 arg4=val4 myarg=yes arg5",          "myarg", {"on", "yes"}, VIR_KERNEL_CMDLINE_FLAGS_SEARCH_LAST | VIR_KERNEL_CMDLINE_FLAGS_CMP_EQ,       true },
    {"arg1 myarg=no arg2=val2 arg4=val4 myarg=yes arg5",          "myarg", {"1", "y"},    VIR_KERNEL_CMDLINE_FLAGS_SEARCH_LAST | VIR_KERNEL_CMDLINE_FLAGS_CMP_PREFIX,   true },
    {"arg1 myarg=no arg2=val2 arg4=val4 myarg arg5",              "myarg", {NULL, NULL},  VIR_KERNEL_CMDLINE_FLAGS_SEARCH_LAST,                                         true },
    {"arg1 myarg arg2=val2 arg4=val4 myarg=yes arg5",             "myarg", {NULL, NULL},  VIR_KERNEL_CMDLINE_FLAGS_SEARCH_STICKY,                                       true },
    {"arg1 myarg arg2=val2 arg4=val4 myarg=yes arg5",             "myarg", {NULL, NULL},  VIR_KERNEL_CMDLINE_FLAGS_SEARCH_LAST,                                         false },
};


static int
testKernelCmdlineMatchParam(const void *data G_GNUC_UNUSED)
{
    bool result;
    size_t i, lenValues;

    for (i = 0; i < G_N_ELEMENTS(kMatchEntries); ++i) {
        if (kMatchEntries[i].values[0] == NULL)
            lenValues = 0;
        else
            lenValues = G_N_ELEMENTS(kMatchEntries[i].values);

        result = virKernelCmdlineMatchParam(kMatchEntries[i].cmdline,
                                            kMatchEntries[i].arg,
                                            kMatchEntries[i].values,
                                            lenValues,
                                            kMatchEntries[i].flags);

        if (result != kMatchEntries[i].result) {
            VIR_TEST_DEBUG("\nKernel cmdline [%s]", kMatchEntries[i].cmdline);
            VIR_TEST_DEBUG("Kernel argument [%s]", kMatchEntries[i].arg);
            VIR_TEST_DEBUG("Kernel values [%s] [%s]", kMatchEntries[i].values[0],
                           kMatchEntries[i].values[1]);
            if (kMatchEntries[i].flags & VIR_KERNEL_CMDLINE_FLAGS_CMP_PREFIX)
                VIR_TEST_DEBUG("Flag [VIR_KERNEL_CMDLINE_FLAGS_CMP_PREFIX]");
            if (kMatchEntries[i].flags & VIR_KERNEL_CMDLINE_FLAGS_CMP_EQ)
                VIR_TEST_DEBUG("Flag [VIR_KERNEL_CMDLINE_FLAGS_CMP_EQ]");
            if (kMatchEntries[i].flags & VIR_KERNEL_CMDLINE_FLAGS_SEARCH_STICKY)
                VIR_TEST_DEBUG("Flag [VIR_KERNEL_CMDLINE_FLAGS_SEARCH_STICKY]");
            if (kMatchEntries[i].flags & VIR_KERNEL_CMDLINE_FLAGS_SEARCH_LAST)
                VIR_TEST_DEBUG("Flag [VIR_KERNEL_CMDLINE_FLAGS_SEARCH_LAST]");
            VIR_TEST_DEBUG("Expect result [%d]", kMatchEntries[i].result);
            VIR_TEST_DEBUG("Actual result [%d]", result);

            return -1;
        }
    }

    return 0;
}


static int
mymain(void)
{
    int result = 0;

    virTestQuiesceLibvirtErrors(true);

#define DO_TEST(_name) \
        do { \
            if (virTestRun("Util "#_name, test##_name, \
                           NULL) < 0) { \
                result = -1; \
            } \
        } while (0)

    DO_TEST(IndexToDiskName);
    DO_TEST(DiskNameToIndex);
    DO_TEST(DiskNameParse);
    DO_TEST(ParseVersionString);
    DO_TEST(RoundValueToPowerOfTwo);
    DO_TEST(OverflowCheckMacro);
    DO_TEST(KernelCmdlineGetValue);
    DO_TEST(KernelCmdlineMatchParam);

    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN(mymain)
