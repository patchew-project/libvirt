#ifndef __TEST_DRIVER_H__
# define __TEST_DRIVER_H__

# define MAX_CPUS 128

struct _testCell {
    unsigned long mem;
    unsigned long freeMem;
    int numCpus;
    virCapsHostNUMACellCPU cpus[MAX_CPUS];
};
typedef struct _testCell testCell;
typedef struct _testCell *testCellPtr;

# define MAX_CELLS 128

struct _testAuth {
    char *username;
    char *password;
};
typedef struct _testAuth testAuth;
typedef struct _testAuth *testAuthPtr;

struct _testDriver {
    virMutex lock;

    virNodeInfo nodeInfo;
    virInterfaceObjList ifaces;
    bool transaction_running;
    virInterfaceObjList backupIfaces;
    virStoragePoolObjList pools;
    virNodeDeviceObjList devs;
    int numCells;
    testCell cells[MAX_CELLS];
    size_t numAuths;
    testAuthPtr auths;

    /* virAtomic access only */
    volatile int nextDomID;

    /* immutable pointer, immutable object after being initialized with
     * testBuildCapabilities */
    virCapsPtr caps;

    /* immutable pointer, immutable object */
    virDomainXMLOptionPtr xmlopt;

    /* immutable pointer, self-locking APIs */
    virDomainObjListPtr domains;
    virNetworkObjListPtr networks;
    virObjectEventStatePtr eventState;
};
typedef struct _testDriver testDriver;
typedef testDriver *testDriverPtr;

# define VIR_FROM_THIS VIR_FROM_TEST

extern unsigned long long defaultPoolAlloc;

extern unsigned long long defaultPoolCap;

void testDriverLock(testDriverPtr driver);
void testDriverUnlock(testDriverPtr driver);

int testStoragePoolObjSetDefaults(virStoragePoolObjPtr pool);

void testObjectEventQueue(testDriverPtr driver,
                          virObjectEventPtr event);

#endif /* __TEST_DRIVER_H__ */
