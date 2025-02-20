/***********************************************************************************************************************************
Test PostgreSQL Interface
***********************************************************************************************************************************/
#include "storage/posix/storage.h"

#include "common/harnessPostgres.h"

/***********************************************************************************************************************************
Test Run
***********************************************************************************************************************************/
static void
testRun(void)
{
    FUNCTION_HARNESS_VOID();

    Storage *storageTest = storagePosixNewP(TEST_PATH_STR, .write = true);

    // *****************************************************************************************************************************
    if (testBegin("pgVersionFromStr() and pgVersionToStr()"))
    {
        TEST_ERROR(pgVersionFromStr(STRDEF("9.3.4")), AssertError, "version 9.3.4 format is invalid");
        TEST_ERROR(pgVersionFromStr(STRDEF("abc")), AssertError, "version abc format is invalid");
        TEST_ERROR(pgVersionFromStr(NULL), AssertError, "assertion 'version != NULL' failed");

        TEST_RESULT_INT(pgVersionFromStr(STRDEF("10")), PG_VERSION_10, "valid pg version 10");
        TEST_RESULT_INT(pgVersionFromStr(STRDEF("9.6")), 90600, "valid pg version 9.6");

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_RESULT_STR_Z(pgVersionToStr(PG_VERSION_11), "11", "infoPgVersionToString 11");
        TEST_RESULT_STR_Z(pgVersionToStr(PG_VERSION_96), "9.6", "infoPgVersionToString 9.6");
        TEST_RESULT_STR_Z(pgVersionToStr(93456), "9.34", "infoPgVersionToString 93456");
    }

    // *****************************************************************************************************************************
    if (testBegin("pgDbIs*()"))
    {
        TEST_RESULT_BOOL(pgDbIsTemplate(STRDEF("template0")), true, "template0 is template");
        TEST_RESULT_BOOL(pgDbIsTemplate(STRDEF("template1")), true, "template1 is template");
        TEST_RESULT_BOOL(pgDbIsTemplate(STRDEF("postgres")), false, "postgres is not template");

        TEST_RESULT_BOOL(pgDbIsSystem(STRDEF("postgres")), true, "postgres is system");
        TEST_RESULT_BOOL(pgDbIsSystem(STRDEF("template0")), true, "template0 is system");
        TEST_RESULT_BOOL(pgDbIsSystem(STRDEF("app")), false, "app is not system");

        TEST_RESULT_BOOL(pgDbIsSystemId(16383), true, "16383 is system");
        TEST_RESULT_BOOL(pgDbIsSystemId(16384), false, "16384 is not system");
    }

    // *****************************************************************************************************************************
    if (testBegin("pgControlVersion()"))
    {
        TEST_ERROR(pgControlVersion(70300), AssertError, "invalid PostgreSQL version 70300");
        TEST_RESULT_UINT(pgControlVersion(PG_VERSION_93), 937, "9.3 control version");
        TEST_RESULT_UINT(pgControlVersion(PG_VERSION_11), 1100, "11 control version");
    }

    // *****************************************************************************************************************************
    if (testBegin("pgControlFromBuffer() and pgControlFromFile()"))
    {
        // Sanity test to ensure PG_VERSION_MAX has been updated
        // -------------------------------------------------------------------------------------------------------------------------
        TEST_RESULT_UINT(pgInterface[0].version, PG_VERSION_MAX, "check max version");

        // -------------------------------------------------------------------------------------------------------------------------
        // Create a bogus control file
        Buffer *result = bufNew(HRN_PG_CONTROL_SIZE);
        memset(bufPtr(result), 0, bufSize(result));
        bufUsedSet(result, bufSize(result));

        *(PgControlCommon *)bufPtr(result) = (PgControlCommon)
        {
            .controlVersion = 501,
            .catalogVersion = 19780101,
        };

        TEST_ERROR(
            pgControlFromBuffer(result), VersionNotSupportedError,
            "unexpected control version = 501 and catalog version = 19780101\nHINT: is this version of PostgreSQL supported?");

        // -------------------------------------------------------------------------------------------------------------------------
        HRN_PG_CONTROL_PUT(
            storageTest, PG_VERSION_11, .systemId = 0xFACEFACE, .checkpoint = 0xEEFFEEFFAABBAABB, .timeline = 47,
            .walSegmentSize = 1024 * 1024);

        PgControl info = {0};
        TEST_ASSIGN(info, pgControlFromFile(storageTest), "get control info v11");
        TEST_RESULT_UINT(info.systemId, 0xFACEFACE, "   check system id");
        TEST_RESULT_UINT(info.version, PG_VERSION_11, "   check version");
        TEST_RESULT_UINT(info.catalogVersion, 201809051, "   check catalog version");
        TEST_RESULT_UINT(info.checkpoint, 0xEEFFEEFFAABBAABB, "check checkpoint");
        TEST_RESULT_UINT(info.timeline, 47, "check timeline");

        // -------------------------------------------------------------------------------------------------------------------------
        HRN_PG_CONTROL_PUT(storageTest, PG_VERSION_93, .walSegmentSize = 1024 * 1024);

        TEST_ERROR(
            pgControlFromFile(storageTest), FormatError, "wal segment size is 1048576 but must be 16777216 for PostgreSQL <= 10");

        // -------------------------------------------------------------------------------------------------------------------------
        HRN_PG_CONTROL_PUT(storageTest, PG_VERSION_95, .pageSize = 32 * 1024);

        TEST_ERROR(pgControlFromFile(storageTest), FormatError, "page size is 32768 but must be 8192");

        // -------------------------------------------------------------------------------------------------------------------------
        HRN_PG_CONTROL_PUT(
            storageTest, PG_VERSION_93, .systemId = 0xEFEFEFEFEF, .catalogVersion = hrnPgCatalogVersion(PG_VERSION_93),
            .checkpoint = 0xAABBAABBEEFFEEFF, .timeline = 88);

        TEST_ASSIGN(info, pgControlFromFile(storageTest), "get control info v90");
        TEST_RESULT_UINT(info.systemId, 0xEFEFEFEFEF, "   check system id");
        TEST_RESULT_UINT(info.version, PG_VERSION_93, "   check version");
        TEST_RESULT_UINT(info.catalogVersion, 201306121, "   check catalog version");
        TEST_RESULT_UINT(info.checkpoint, 0xAABBAABBEEFFEEFF, "check checkpoint");
        TEST_RESULT_UINT(info.timeline, 88, "check timeline");
    }

    // *****************************************************************************************************************************
    if (testBegin("pgLsnFromStr(), pgLsnToStr(), pgLsnToWalSegment(), pg*FromWalSegment(), and pgLsnRangeToWalSegmentList()"))
    {
        TEST_RESULT_UINT(pgLsnFromStr(STRDEF("1/1")), 0x0000000100000001, "lsn to string");
        TEST_RESULT_UINT(pgLsnFromStr(STRDEF("ffffffff/ffffffff")), 0xFFFFFFFFFFFFFFFF, "lsn to string");
        TEST_RESULT_UINT(pgLsnFromStr(STRDEF("ffffffff/aaaaaaaa")), 0xFFFFFFFFAAAAAAAA, "lsn to string");

        TEST_RESULT_STR_Z(pgLsnToStr(0xFFFFFFFFAAAAAAAA), "ffffffff/aaaaaaaa", "string to lsn");
        TEST_RESULT_STR_Z(pgLsnToStr(0x0000000000000000), "0/0", "string to lsn");
        TEST_RESULT_STR_Z(pgLsnToStr(0x0000000100000002), "1/2", "string to lsn");

        TEST_RESULT_STR_Z(pgLsnToWalSegment(1, 0xFFFFFFFFAAAAAAAA, 0x1000000), "00000001FFFFFFFF000000AA", "lsn to wal segment");
        TEST_RESULT_STR_Z(pgLsnToWalSegment(1, 0xFFFFFFFFAAAAAAAA, 0x40000000), "00000001FFFFFFFF00000002", "lsn to wal segment");
        TEST_RESULT_STR_Z(pgLsnToWalSegment(1, 0xFFFFFFFF40000000, 0x40000000), "00000001FFFFFFFF00000001", "lsn to wal segment");

        TEST_RESULT_UINT(pgTimelineFromWalSegment(STRDEF("00000001FFFFFFFF000000AA")), 1, "timeline 1");
        TEST_RESULT_UINT(pgTimelineFromWalSegment(STRDEF("F000000FFFFFFFFF000000AA")), 0xF000000F, "timeline F000000F");

        TEST_RESULT_STRLST_Z(
            pgLsnRangeToWalSegmentList(
                2, pgLsnFromStr(STRDEF("1/FD000000")), pgLsnFromStr(STRDEF("2/60")), 16 * 1024 * 1024),
            "0000000200000001000000FD\n0000000200000001000000FE\n0000000200000001000000FF\n000000020000000200000000\n",
            "get range");
        TEST_RESULT_STRLST_Z(
            pgLsnRangeToWalSegmentList(
                2, pgLsnFromStr(STRDEF("A/800")), pgLsnFromStr(STRDEF("B/C0000000")), 1024 * 1024 * 1024),
            "000000020000000A00000000\n000000020000000A00000001\n000000020000000A00000002\n000000020000000A00000003\n"
            "000000020000000B00000000\n000000020000000B00000001\n000000020000000B00000002\n000000020000000B00000003\n",
            "get range >= 11/1GB");
        TEST_RESULT_STRLST_Z(
            pgLsnRangeToWalSegmentList(
                3, pgLsnFromStr(STRDEF("7/FFEFFFFF")), pgLsnFromStr(STRDEF("8/001AAAAA")), 1024 * 1024),
            "000000030000000700000FFE\n000000030000000700000FFF\n000000030000000800000000\n000000030000000800000001\n",
            "get range >= 11/1MB");
    }

    // *****************************************************************************************************************************
    if (testBegin("pgLsnName(), pgTablespaceId(), pgWalName(), pgWalPath(), and pgXactPath()"))
    {
        TEST_RESULT_STR_Z(pgLsnName(PG_VERSION_96), "location", "check location name");
        TEST_RESULT_STR_Z(pgLsnName(PG_VERSION_10), "lsn", "check lsn name");

        TEST_RESULT_STR_Z(pgTablespaceId(PG_VERSION_93, 201306121), "PG_9.3_201306121", "check 9.3 tablespace id");
        TEST_RESULT_STR_Z(pgTablespaceId(PG_VERSION_94, 999999999), "PG_9.4_999999999", "check 9.4 tablespace id");

        TEST_RESULT_STR_Z(pgWalName(PG_VERSION_96), "xlog", "check xlog name");
        TEST_RESULT_STR_Z(pgWalName(PG_VERSION_10), "wal", "check wal name");

        TEST_RESULT_STR_Z(pgWalPath(PG_VERSION_96), "pg_xlog", "check xlog path");
        TEST_RESULT_STR_Z(pgWalPath(PG_VERSION_10), "pg_wal", "check wal path");

        TEST_RESULT_STR_Z(pgXactPath(PG_VERSION_96), "pg_clog", "check pg_clog name");
        TEST_RESULT_STR_Z(pgXactPath(PG_VERSION_10), "pg_xact", "check pg_xact name");
    }

    // *****************************************************************************************************************************
    if (testBegin("pgPageChecksum()"))
    {
        unsigned char page[PG_PAGE_SIZE_DEFAULT];
        memset(page, 0xFF, PG_PAGE_SIZE_DEFAULT);

        TEST_RESULT_UINT(pgPageChecksum(page, 0), TEST_BIG_ENDIAN() ? 0xF55E : 0x0E1C, "check 0xFF filled page, block 0");
        TEST_RESULT_UINT(pgPageChecksum(page, 999), TEST_BIG_ENDIAN() ? 0xF1B9 : 0x0EC3, "check 0xFF filled page, block 999");
    }

    // *****************************************************************************************************************************
    if (testBegin("pgWalFromBuffer() and pgWalFromFile()"))
    {
        const String *walFile = STRDEF(TEST_PATH "/0000000F0000000F0000000F");

        // Create a bogus control file, initially not in long format)
        // -------------------------------------------------------------------------------------------------------------------------
        Buffer *result = bufNew((size_t)16 * 1024 * 1024);
        memset(bufPtr(result), 0, bufSize(result));
        bufUsedSet(result, bufSize(result));

        *(PgWalCommon *)bufPtr(result) = (PgWalCommon){.magic = 777};

        TEST_ERROR(pgWalFromBuffer(result), FormatError, "first page header in WAL file is expected to be in long format");

        // Add the long flag so that the version will now error
        // -------------------------------------------------------------------------------------------------------------------------
        ((PgWalCommon *)bufPtr(result))->flag = PG_WAL_LONG_HEADER;

        TEST_ERROR(
            pgWalFromBuffer(result), VersionNotSupportedError,
            "unexpected WAL magic 777\n"
            "HINT: is this version of PostgreSQL supported?");

        // -------------------------------------------------------------------------------------------------------------------------
        memset(bufPtr(result), 0, bufSize(result));
        hrnPgWalToBuffer(
            (PgWal){.version = PG_VERSION_11, .systemId = 0xECAFECAF, .size = PG_WAL_SEGMENT_SIZE_DEFAULT * 2}, result);
        storagePutP(storageNewWriteP(storageTest, walFile), result);

        PgWal info = {0};
        TEST_ASSIGN(info, pgWalFromFile(walFile, storageTest), "get wal info v11");
        TEST_RESULT_UINT(info.systemId, 0xECAFECAF, "   check system id");
        TEST_RESULT_UINT(info.version, PG_VERSION_11, "   check version");
        TEST_RESULT_UINT(info.size, PG_WAL_SEGMENT_SIZE_DEFAULT * 2, "   check size");

        // -------------------------------------------------------------------------------------------------------------------------
        memset(bufPtr(result), 0, bufSize(result));
        hrnPgWalToBuffer(
            (PgWal){.version = PG_VERSION_96, .systemId = 0xEAEAEAEA, .size = PG_WAL_SEGMENT_SIZE_DEFAULT * 2}, result);

        TEST_ERROR(pgWalFromBuffer(result), FormatError, "wal segment size is 33554432 but must be 16777216 for PostgreSQL <= 10");

        // -------------------------------------------------------------------------------------------------------------------------
        memset(bufPtr(result), 0, bufSize(result));
        hrnPgWalToBuffer((PgWal){.version = PG_VERSION_93, .systemId = 0xEAEAEAEA, .size = PG_WAL_SEGMENT_SIZE_DEFAULT}, result);
        storagePutP(storageNewWriteP(storageTest, walFile), result);

        TEST_ASSIGN(info, pgWalFromFile(walFile, storageTest), "get wal info v9.3");
        TEST_RESULT_UINT(info.systemId, 0xEAEAEAEA, "   check system id");
        TEST_RESULT_UINT(info.version, PG_VERSION_93, "   check version");
        TEST_RESULT_UINT(info.size, PG_WAL_SEGMENT_SIZE_DEFAULT, "   check size");
    }

    // *****************************************************************************************************************************
    if (testBegin("pgControlToLog()"))
    {
        char logBuf[STACK_TRACE_PARAM_MAX];

        PgControl pgControl =
        {
            .version = PG_VERSION_11,
            .systemId = 0xEFEFEFEFEF,
            .walSegmentSize = 16 * 1024 * 1024,
            .pageChecksum = true
        };

        TEST_RESULT_VOID(FUNCTION_LOG_OBJECT_FORMAT(&pgControl, pgControlToLog, logBuf, sizeof(logBuf)), "pgControlToLog");
        TEST_RESULT_Z(
            logBuf, "{version: 110000, systemId: 1030522662895, walSegmentSize: 16777216, pageChecksum: true}", "check log");
    }

    // *****************************************************************************************************************************
    if (testBegin("pgWalToLog()"))
    {
        char logBuf[STACK_TRACE_PARAM_MAX];

        PgWal pgWal =
        {
            .version = PG_VERSION_10,
            .systemId = 0xFEFEFEFEFE
        };

        TEST_RESULT_VOID(FUNCTION_LOG_OBJECT_FORMAT(&pgWal, pgWalToLog, logBuf, sizeof(logBuf)), "pgWalToLog");
        TEST_RESULT_Z(logBuf, "{version: 100000, systemId: 1095199817470}", "check log");
    }

    FUNCTION_HARNESS_RETURN_VOID();
}
