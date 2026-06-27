#include "Halcon_Def.h"
#include "Halcon_SoftwarePackage.h"
#include "HalconCpp.h"
#include <mutex>
using namespace HalconCpp;

// ============================================================
// Async Thread Pool — lazy init on first logger creation
// ============================================================

static std::once_flag g_spdlog_tp_init_flag;

static void EnsureThreadPool()
{
    std::call_once(g_spdlog_tp_init_flag, []() {
        // queue_size=8192, thread_count=1 (足够视觉应用场景)
        spdlog::init_thread_pool(8192, 1);
    });
}

Herror Hspdlog_init_thread_pool(Hproc_handle proc_handle)
{
    Hcpar queue_size;
    Hcpar thread_count;
    HGetSPar(proc_handle, 1, LONG_PAR, &queue_size, 1);
    HGetSPar(proc_handle, 2, LONG_PAR, &thread_count, 1);

    std::call_once(g_spdlog_tp_init_flag, [&]() {
        spdlog::init_thread_pool((size_t)queue_size.par.l, (size_t)thread_count.par.l);
    });

    return H_MSG_TRUE;
}

// ============================================================
// Logger Creation
// ============================================================

Herror Hspdlog_basic_logger_mt(Hproc_handle proc_handle)
{
    Hcpar name;
    Hcpar filename;
    Hcpar truncate;
    HAllocStringMem(proc_handle, 512);
    HGetSPar(proc_handle, 1, STRING_PAR, &name, 1);
    HGetSPar(proc_handle, 2, STRING_PAR, &filename, 1);
    HGetSPar(proc_handle, 3, LONG_PAR, &truncate, 1);

    Def_OUTSpdlogObject(1, pUserData);

    try
    {
        EnsureThreadPool();
        auto logger = spdlog::basic_logger_mt<spdlog::async_factory>(name.par.s, filename.par.s, truncate.par.l != 0);
        SpdlogSetLogger(*pUserData, logger);
    }
    catch (const spdlog::spdlog_ex &)
    {
        return H__LINE__ * 10000;
    }

    return H_MSG_TRUE;
}

Herror Hspdlog_rotating_logger_mt(Hproc_handle proc_handle)
{
    Hcpar name;
    Hcpar filename;
    Hcpar max_file_size;
    Hcpar max_files;
    HAllocStringMem(proc_handle, 512);
    HGetSPar(proc_handle, 1, STRING_PAR, &name, 1);
    HGetSPar(proc_handle, 2, STRING_PAR, &filename, 1);
    HGetSPar(proc_handle, 3, LONG_PAR, &max_file_size, 1);
    HGetSPar(proc_handle, 4, LONG_PAR, &max_files, 1);

    Def_OUTSpdlogObject(1, pUserData);

    try
    {
        EnsureThreadPool();
        auto logger = spdlog::rotating_logger_mt<spdlog::async_factory>(name.par.s, filename.par.s,
                                                  (size_t)max_file_size.par.l,
                                                  (size_t)max_files.par.l);
        SpdlogSetLogger(*pUserData, logger);
    }
    catch (const spdlog::spdlog_ex &)
    {
        return H__LINE__ * 10000;
    }

    return H_MSG_TRUE;
}

Herror Hspdlog_daily_logger_mt(Hproc_handle proc_handle)
{
    Hcpar name;
    Hcpar filename;
    Hcpar hour;
    Hcpar minute;
    HAllocStringMem(proc_handle, 512);
    HGetSPar(proc_handle, 1, STRING_PAR, &name, 1);
    HGetSPar(proc_handle, 2, STRING_PAR, &filename, 1);
    HGetSPar(proc_handle, 3, LONG_PAR, &hour, 1);
    HGetSPar(proc_handle, 4, LONG_PAR, &minute, 1);

    Def_OUTSpdlogObject(1, pUserData);

    try
    {
        EnsureThreadPool();
        auto logger = spdlog::daily_logger_mt<spdlog::async_factory>(name.par.s, filename.par.s,
                                               (int)hour.par.l, (int)minute.par.l);
        SpdlogSetLogger(*pUserData, logger);
    }
    catch (const spdlog::spdlog_ex &)
    {
        return H__LINE__ * 10000;
    }

    return H_MSG_TRUE;
}

Herror Hspdlog_stdout_color_mt(Hproc_handle proc_handle)
{
    Hcpar name;
    HAllocStringMem(proc_handle, 128);
    HGetSPar(proc_handle, 1, STRING_PAR, &name, 1);

    Def_OUTSpdlogObject(1, pUserData);

    try
    {
        EnsureThreadPool();
        auto logger = spdlog::stdout_color_mt<spdlog::async_factory>(name.par.s);
        SpdlogSetLogger(*pUserData, logger);
    }
    catch (const spdlog::spdlog_ex &)
    {
        return H__LINE__ * 10000;
    }

    return H_MSG_TRUE;
}

Herror Hspdlog_get(Hproc_handle proc_handle)
{
    Hcpar name;
    HAllocStringMem(proc_handle, 128);
    HGetSPar(proc_handle, 1, STRING_PAR, &name, 1);

    auto logger = spdlog::get(name.par.s);
    if (!logger)
    {
        return H__LINE__ * 10000;
    }

    Def_OUTSpdlogObject(1, pUserData);
    SpdlogSetLogger(*pUserData, logger);

    return H_MSG_TRUE;
}

// ============================================================
// Logging
// ============================================================

Herror Hspdlog_log(Hproc_handle proc_handle)
{
    Hcpar level;
    Hcpar msg;
    HAllocStringMem(proc_handle, 32768);
    Def_INSpdlogObject(1, pUserData);
    HGetSPar(proc_handle, 2, LONG_PAR, &level, 1);
    HGetSPar(proc_handle, 3, STRING_PAR, &msg, 1);

    auto logger = SpdlogGetLogger(pUserData);
    if (!logger)
    {
        return H__LINE__ * 10000;
    }

    logger->log(static_cast<spdlog::level::level_enum>(level.par.l), msg.par.s);
    return H_MSG_TRUE;
}

Herror Hspdlog_trace(Hproc_handle proc_handle)
{
    Hcpar msg;
    HAllocStringMem(proc_handle, 32768);
    Def_INSpdlogObject(1, pUserData);
    HGetSPar(proc_handle, 2, STRING_PAR, &msg, 1);

    auto logger = SpdlogGetLogger(pUserData);
    if (!logger)
    {
        return H__LINE__ * 10000;
    }

    logger->trace(msg.par.s);
    return H_MSG_TRUE;
}

Herror Hspdlog_debug(Hproc_handle proc_handle)
{
    Hcpar msg;
    HAllocStringMem(proc_handle, 32768);
    Def_INSpdlogObject(1, pUserData);
    HGetSPar(proc_handle, 2, STRING_PAR, &msg, 1);

    auto logger = SpdlogGetLogger(pUserData);
    if (!logger)
    {
        return H__LINE__ * 10000;
    }

    logger->debug(msg.par.s);
    return H_MSG_TRUE;
}

Herror Hspdlog_info(Hproc_handle proc_handle)
{
    Hcpar msg;
    HAllocStringMem(proc_handle, 32768);
    Def_INSpdlogObject(1, pUserData);
    HGetSPar(proc_handle, 2, STRING_PAR, &msg, 1);

    auto logger = SpdlogGetLogger(pUserData);
    if (!logger)
    {
        return H__LINE__ * 10000;
    }

    logger->info(msg.par.s);
    return H_MSG_TRUE;
}

Herror Hspdlog_warn(Hproc_handle proc_handle)
{
    Hcpar msg;
    HAllocStringMem(proc_handle, 32768);
    Def_INSpdlogObject(1, pUserData);
    HGetSPar(proc_handle, 2, STRING_PAR, &msg, 1);

    auto logger = SpdlogGetLogger(pUserData);
    if (!logger)
    {
        return H__LINE__ * 10000;
    }

    logger->warn(msg.par.s);
    return H_MSG_TRUE;
}

Herror Hspdlog_err(Hproc_handle proc_handle)
{
    Hcpar msg;
    HAllocStringMem(proc_handle, 32768);
    Def_INSpdlogObject(1, pUserData);
    HGetSPar(proc_handle, 2, STRING_PAR, &msg, 1);

    auto logger = SpdlogGetLogger(pUserData);
    if (!logger)
    {
        return H__LINE__ * 10000;
    }

    logger->error(msg.par.s);
    return H_MSG_TRUE;
}

Herror Hspdlog_critical(Hproc_handle proc_handle)
{
    Hcpar msg;
    HAllocStringMem(proc_handle, 32768);
    Def_INSpdlogObject(1, pUserData);
    HGetSPar(proc_handle, 2, STRING_PAR, &msg, 1);

    auto logger = SpdlogGetLogger(pUserData);
    if (!logger)
    {
        return H__LINE__ * 10000;
    }

    logger->critical(msg.par.s);
    return H_MSG_TRUE;
}

// ============================================================
// Logger Configuration
// ============================================================

Herror Hspdlog_set_level(Hproc_handle proc_handle)
{
    Hcpar level;
    Def_INSpdlogObject(1, pUserData);
    HGetSPar(proc_handle, 2, LONG_PAR, &level, 1);

    auto logger = SpdlogGetLogger(pUserData);
    if (!logger)
    {
        return H__LINE__ * 10000;
    }

    logger->set_level(static_cast<spdlog::level::level_enum>(level.par.l));
    return H_MSG_TRUE;
}

Herror Hspdlog_set_pattern(Hproc_handle proc_handle)
{
    Hcpar pattern;
    HAllocStringMem(proc_handle, 1024);
    Def_INSpdlogObject(1, pUserData);
    HGetSPar(proc_handle, 2, STRING_PAR, &pattern, 1);

    auto logger = SpdlogGetLogger(pUserData);
    if (!logger)
    {
        return H__LINE__ * 10000;
    }

    logger->set_pattern(pattern.par.s);
    return H_MSG_TRUE;
}

Herror Hspdlog_flush(Hproc_handle proc_handle)
{
    Def_INSpdlogObject(1, pUserData);

    auto logger = SpdlogGetLogger(pUserData);
    if (!logger)
    {
        return H__LINE__ * 10000;
    }

    logger->flush();
    return H_MSG_TRUE;
}

Herror Hspdlog_flush_on(Hproc_handle proc_handle)
{
    Hcpar level;
    Def_INSpdlogObject(1, pUserData);
    HGetSPar(proc_handle, 2, LONG_PAR, &level, 1);

    auto logger = SpdlogGetLogger(pUserData);
    if (!logger)
    {
        return H__LINE__ * 10000;
    }

    logger->flush_on(static_cast<spdlog::level::level_enum>(level.par.l));
    return H_MSG_TRUE;
}

// ============================================================
// Global Operations
// ============================================================

Herror Hspdlog_drop(Hproc_handle proc_handle)
{
    Hcpar name;
    HAllocStringMem(proc_handle, 128);
    HGetSPar(proc_handle, 1, STRING_PAR, &name, 1);

    spdlog::drop(name.par.s);
    return H_MSG_TRUE;
}

Herror Hspdlog_drop_all(Hproc_handle proc_handle)
{
    spdlog::drop_all();
    return H_MSG_TRUE;
}

Herror Hspdlog_shutdown(Hproc_handle proc_handle)
{
    spdlog::shutdown();
    return H_MSG_TRUE;
}
