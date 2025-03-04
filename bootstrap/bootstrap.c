#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/net/http.h>
#include <psp2/sysmodule.h>
#include <psp2/appmgr.h>
#include <psp2/shellutil.h>
#include <psp2/types.h>
#include "compress.h"
#include "molecule_logo.h"
#include "../build/version.c"
#include "../plugin/henkaku.h"

// Константы
#define INSTALL_ATTEMPTS 3
#define TAIHEN_CONFIG_FILE "ux0:tai/config.txt"
#define TAIHEN_RECOVERY_CONFIG_FILE "ur0:tai/config.txt"
#define TAIHEN_SKPRX_FILE "ur0:tai/taihen.skprx"
#define HENKAKU_SKPRX_FILE "ur0:tai/henkaku.skprx"
#define HENKAKU_SUPRX_FILE "ur0:tai/henkaku.suprx"

// Логгирование
#undef LOG
#if RELEASE
#define LOG(...)
#else
#define LOG sceClibPrintf
#endif

// Консольные данные
typedef struct {
    int X, Y;
    uint32_t fg_color;
    void *base;
} ConsoleData;

ConsoleData cui_data;

// Графические константы
enum {
    FRAMEBUFFER_WIDTH = 960,
    FRAMEBUFFER_HEIGHT = 544,
    FRAMEBUFFER_LINE_SIZE = 960,
    FRAMEBUFFER_SIZE = 2 * 1024 * 1024,
    FRAMEBUFFER_ALIGNMENT = 256 * 1024,
    PROGRESS_BAR_WIDTH = 400,
    PROGRESS_BAR_HEIGHT = 10,
};

// Вывод в консоль с логгированием
#define PRINT_CONSOLE(fmt, ...) do { \
    psvDebugScreenPrintf(cui_data.base, &cui_data.X, &cui_data.Y, fmt, ##__VA_ARGS__); \
    LOG(fmt, ##__VA_ARGS__); \
} while (0);

// HTTP-шаблон
static int g_http_template;

// Инициализация графики
static void init_graphics() {
    int block = sceKernelAllocMemBlock("display", 
        SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 
        FRAMEBUFFER_SIZE, NULL);
    if (block < 0) {
        PRINT_CONSOLE("Ошибка инициализации графики: 0x%x\n", block);
        sceKernelExitProcess(block);
    }
    int ret = sceKernelGetMemBlockBase(block, &cui_data.base);
    if (ret < 0) {
        PRINT_CONSOLE("Ошибка получения базы памяти: 0x%x\n", ret);
        sceKernelExitProcess(ret);
    }
}

// Очистка экрана
static void clear_screen() {
    // Заполнение черным цветом
    sceClibMemset(cui_data.base, 0, FRAMEBUFFER_SIZE);

    // Рисование логотипа
    decompress((char *)molecule_logo.data, cui_data.base, 
        sizeof(molecule_logo.data), molecule_logo.size);

    // Восстановление позиции курсора
    cui_data.X = 0;
    cui_data.Y = 0;
}

// Вспомогательная функция для CRC-проверки
static bool verify_file_crc(const char *path, uint32_t expected_crc) {
    uint32_t actual_crc = crc32_file(path);
    if (actual_crc != expected_crc) {
        PRINT_CONSOLE("CRC32 для %s не совпадает: ожидалось 0x%x, получено 0x%x\n", 
            path, expected_crc, actual_crc);
        return false;
    }
    return true;
}

// Установка taiHEN
int install_taihen(const char *pkg_url_prefix) {
    char url[512], dst[512];

    // Создание директорий
    mkdirs("ur0:tai");

    // Скачивание файлов
    sceIoRemove(TAIHEN_SKPRX_FILE);
    sceIoRemove(HENKAKU_SKPRX_FILE);
    sceIoRemove(HENKAKU_SUPRX_FILE);

    download_file_from_url(pkg_url_prefix, "taihen.skprx", TAIHEN_SKPRX_FILE);
    download_file_from_url(pkg_url_prefix, "henkaku.skprx", HENKAKU_SKPRX_FILE);
    download_file_from_url(pkg_url_prefix, "henkaku.suprx", HENKAKU_SUPRX_FILE);

    // Обновление конфигурации
    if (!exists(TAIHEN_CONFIG_FILE)) {
        write_taihen_config(TAIHEN_CONFIG_FILE, 0);
    }

    if (!exists(TAIHEN_RECOVERY_CONFIG_FILE)) {
        write_taihen_config(TAIHEN_RECOVERY_CONFIG_FILE, 1);
    }

    // Проверка наличия файлов
    return (exists(TAIHEN_SKPRX_FILE) && 
            exists(HENKAKU_SKPRX_FILE) && 
            exists(HENKAKU_SUPRX_FILE)) ? 0 : -1;
}

// Основная функция установки
int module_start(SceSize argc, const void *args) {
    init_graphics();
    init_modules();

    // Основной цикл установки
    for (int try = 0; try < INSTALL_ATTEMPTS; try++) {
        // Проверка версий taiHEN
        if (!verify_taihen()) {
            install_taihen(PKG_URL_PREFIX);
        }

        // Проверка molecularShell
        if (!exists("ux0:app/MLCL00001/eboot.bin") || 
            crc32_file("ux0:app/MLCL00001/eboot.bin") != VITASHELL_CRC32) {
            install_pkg(PKG_URL_PREFIX);
        }

        // Обновление конфигурации
        update_taihen_config();

        // Финальная проверка
        if (verify_taihen() && 
            verify_file_crc("ux0:app/MLCL00001/eboot.bin", VITASHELL_CRC32)) {
            break;
        }
    }

    // Завершение
    PRINT_CONSOLE("Установка завершена успешно!\n");
    sceKernelDelayThread(3 * 1000 * 1000);
    sceKernelExitProcess(0);
}
