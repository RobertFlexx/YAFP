/*
 * yafp - Yet Another Fetch Program
 * Cross-platform system information display
 * Supports: Linux, macOS, FreeBSD, OpenBSD, NetBSD, DragonFlyBSD
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif

#if defined(__NetBSD__)
#define _NETBSD_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <dirent.h>
#include <ctype.h>
#include <time.h>
#include <limits.h>
#include <stdint.h>
#include <fcntl.h>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/statvfs.h>
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
#include <sys/param.h>
#include <sys/mount.h>
#endif

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>

#if defined(__linux__)
#include <sys/sysinfo.h>
#include <utmpx.h>
#define HAVE_UTMPX 1
#define HAVE_SYSINFO 1
#define HAVE_PROC_FS 1

#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/vm_statistics.h>
#include <utmpx.h>
#include <CoreFoundation/CoreFoundation.h>
#define HAVE_UTMPX 1
#define HAVE_SYSCTL 1

#elif defined(__FreeBSD__) || defined(__DragonFly__)
#include <sys/sysctl.h>
#include <sys/vmmeter.h>
#include <vm/vm_param.h>
#include <utmpx.h>
#define HAVE_UTMPX 1
#define HAVE_SYSCTL 1

#elif defined(__OpenBSD__)
#include <sys/sysctl.h>
#include <sys/swap.h>
#include <sys/sensors.h>
#include <machine/cpu.h>
#include <utmp.h>
#define HAVE_UTMP 1
#define HAVE_SYSCTL 1

#elif defined(__NetBSD__)
#include <sys/sysctl.h>
#include <uvm/uvm_extern.h>
#include <utmpx.h>
#define HAVE_UTMPX 1
#define HAVE_SYSCTL 1

#else
#define HAVE_GENERIC_POSIX 1
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define YAFP_VERSION "0.2.0"

static bool g_use_color = true;
static bool g_minimal   = false;

#define LABEL_WIDTH 12

static void die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

static char *xstrdup(const char *s)
{
    if (!s)
        return NULL;
    size_t len = strlen(s);
    char *p = malloc(len + 1);
    if (!p)
        die("malloc");
    memcpy(p, s, len + 1);
    return p;
}

static char *xmalloc(size_t n)
{
    char *p = malloc(n);
    if (!p)
        die("malloc");
    return p;
}

static void rstrip(char *s)
{
    if (!s)
        return;
    size_t len = strlen(s);
    while (len > 0) {
        char c = s[len - 1];
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t')
            s[--len] = '\0';
        else
            break;
    }
}

static char *read_first_line_trim(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    char buf[512];
    if (!fgets(buf, sizeof buf, f)) {
        fclose(f);
        return NULL;
    }
    fclose(f);
    rstrip(buf);
    return xstrdup(buf);
}

static char *run_command_first_line(const char *cmd)
{
    FILE *fp = popen(cmd, "r");
    if (!fp)
        return NULL;
    char buf[512];
    if (!fgets(buf, sizeof buf, fp)) {
        pclose(fp);
        return NULL;
    }
    pclose(fp);
    rstrip(buf);
    if (buf[0] == '\0')
        return NULL;
    return xstrdup(buf);
}

static char *get_username(void)
{
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_name)
        return xstrdup(pw->pw_name);

    const char *u = getenv("USER");
    if (u && *u)
        return xstrdup(u);

    return xstrdup("user");
}

static char *get_hostname_simple(void)
{
    char buf[256];
    if (gethostname(buf, sizeof buf) == 0) {
        buf[sizeof(buf) - 1] = '\0';
        char *dot = strchr(buf, '.');
        if (dot)
            *dot = '\0';
        return xstrdup(buf);
    }
    return xstrdup("localhost");
}

static char *get_os_pretty_name(void)
{
    struct utsname uts;
    if (uname(&uts) != 0)
        return xstrdup("Unknown");

#if defined(__linux__)
    FILE *f = fopen("/etc/os-release", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof line, f)) {
            if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
                char *p = line + 12;
                while (*p == ' ' || *p == '\t')
                    p++;
                if (*p == '"' || *p == '\'') {
                    char quote = *p++;
                    char *end = strchr(p, quote);
                    if (end)
                        *end = '\0';
                } else {
                    rstrip(p);
                }
                fclose(f);
                return xstrdup(p);
            }
        }
        fclose(f);
    }
    
    f = fopen("/etc/lsb-release", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof line, f)) {
            if (strncmp(line, "DISTRIB_DESCRIPTION=", 20) == 0) {
                char *p = line + 20;
                if (*p == '"') {
                    p++;
                    char *end = strchr(p, '"');
                    if (end)
                        *end = '\0';
                } else {
                    rstrip(p);
                }
                fclose(f);
                return xstrdup(p);
            }
        }
        fclose(f);
    }
    return xstrdup("Linux");

#elif defined(__APPLE__)
    char *version = run_command_first_line("sw_vers -productVersion 2>/dev/null");
    char *name = run_command_first_line("sw_vers -productName 2>/dev/null");
    
    if (name && version) {
        char buf[128];
        snprintf(buf, sizeof buf, "%s %s", name, version);
        free(name);
        free(version);
        return xstrdup(buf);
    }
    free(name);
    free(version);
    return xstrdup("macOS");

#elif defined(__FreeBSD__)
    char buf[64];
    snprintf(buf, sizeof buf, "FreeBSD %s", uts.release);
    return xstrdup(buf);

#elif defined(__OpenBSD__)
    char buf[64];
    snprintf(buf, sizeof buf, "OpenBSD %s", uts.release);
    return xstrdup(buf);

#elif defined(__NetBSD__)
    char buf[64];
    snprintf(buf, sizeof buf, "NetBSD %s", uts.release);
    return xstrdup(buf);

#elif defined(__DragonFly__)
    char buf[64];
    snprintf(buf, sizeof buf, "DragonFly %s", uts.release);
    return xstrdup(buf);

#else
    return xstrdup(uts.sysname);
#endif
}

static char *get_host_model(void)
{
#if defined(__linux__)
    char *vendor = read_first_line_trim("/sys/class/dmi/id/sys_vendor");
    char *product = read_first_line_trim("/sys/class/dmi/id/product_name");
    char *result = NULL;

    if (vendor && *vendor && product && *product) {
        char buf[256];
        snprintf(buf, sizeof buf, "%s %s", vendor, product);
        result = xstrdup(buf);
    } else if (product && *product) {
        result = xstrdup(product);
    }

    free(vendor);
    free(product);
    
    if (result)
        return result;
    
    char *model = read_first_line_trim("/sys/firmware/devicetree/base/model");
    if (model)
        return model;
    
    return xstrdup("Unknown");

#elif defined(__APPLE__)
    char model[256];
    size_t len = sizeof(model);
    if (sysctlbyname("hw.model", model, &len, NULL, 0) == 0)
        return xstrdup(model);
    return xstrdup("Apple Mac");

#elif defined(HAVE_SYSCTL)
    char buf[256];
    size_t len = sizeof(buf);
    
#if defined(__FreeBSD__) || defined(__DragonFly__)
    if (sysctlbyname("hw.hv_vendor", buf, &len, NULL, 0) == 0 && len > 0)
        return xstrdup(buf);
#endif
    
    int mib[2] = { CTL_HW, HW_MACHINE };
    len = sizeof(buf);
    if (sysctl(mib, 2, buf, &len, NULL, 0) == 0)
        return xstrdup(buf);
    
    return xstrdup("Unknown");
#else
    return xstrdup("Unknown");
#endif
}

static char *get_kernel_release(void)
{
    struct utsname uts;
    if (uname(&uts) == 0)
        return xstrdup(uts.release);
    return xstrdup("Unknown");
}

static char *get_architecture(void)
{
    struct utsname uts;
    if (uname(&uts) == 0)
        return xstrdup(uts.machine);
    return xstrdup("Unknown");
}

static long get_uptime_seconds(void)
{
#if defined(__linux__)
#if HAVE_SYSINFO
    struct sysinfo si;
    if (sysinfo(&si) == 0)
        return si.uptime;
#endif
    FILE *f = fopen("/proc/uptime", "r");
    if (f) {
        double up = 0.0;
        if (fscanf(f, "%lf", &up) == 1) {
            fclose(f);
            return (long)up;
        }
        fclose(f);
    }
    return -1;

#elif defined(HAVE_SYSCTL)
    struct timeval boottime;
    size_t len = sizeof(boottime);
    int mib[2] = { CTL_KERN, KERN_BOOTTIME };
    
    if (sysctl(mib, 2, &boottime, &len, NULL, 0) == 0) {
        time_t now = time(NULL);
        return (long)(now - boottime.tv_sec);
    }
    return -1;
#else
    return -1;
#endif
}

static char *format_uptime(long *out_secs)
{
    long secs = get_uptime_seconds();
    
    if (secs < 0)
        secs = 0;
    if (out_secs)
        *out_secs = secs;

    long days = secs / 86400;
    secs %= 86400;
    long hours = secs / 3600;
    secs %= 3600;
    long mins = secs / 60;

    char buf[64];
    if (days > 0)
        snprintf(buf, sizeof buf, "%ldd %ldh %ldm", days, hours, mins);
    else if (hours > 0)
        snprintf(buf, sizeof buf, "%ldh %ldm", hours, mins);
    else
        snprintf(buf, sizeof buf, "%ldm", mins);

    return xstrdup(buf);
}

static char *get_run_datetime(time_t *out_now)
{
    time_t now = time(NULL);
    if (out_now)
        *out_now = now;
    if (now == (time_t)-1)
        return xstrdup("Unknown");
    struct tm *tm = localtime(&now);
    if (!tm)
        return xstrdup("Unknown");

    char buf[64];
    if (strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", tm) == 0)
        return xstrdup("Unknown");
    return xstrdup(buf);
}

static char *format_boot_time(time_t now, long uptime_secs)
{
    if (now == (time_t)-1 || uptime_secs <= 0)
        return xstrdup("Unknown");
    time_t boot = now - uptime_secs;
    struct tm *tm = localtime(&boot);
    if (!tm)
        return xstrdup("Unknown");
    char buf[64];
    if (strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", tm) == 0)
        return xstrdup("Unknown");
    return xstrdup(buf);
}

static char *get_shell_name(void)
{
    const char *sh = getenv("SHELL");
    if (!sh || !*sh) {
        struct passwd *pw = getpwuid(getuid());
        if (pw && pw->pw_shell)
            sh = pw->pw_shell;
    }
    
    if (!sh || !*sh)
        return xstrdup("Unknown");

    const char *slash = strrchr(sh, '/');
    if (slash && slash[1] != '\0')
        return xstrdup(slash + 1);

    return xstrdup(sh);
}

static char *get_term_env(void)
{
    const char *t = getenv("TERM");
    if (!t || !*t)
        return xstrdup("Unknown");
    return xstrdup(t);
}

static char *get_tty_size(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        char buf[32];
        snprintf(buf, sizeof buf, "%dx%d", ws.ws_col, ws.ws_row);
        return xstrdup(buf);
    }
    return xstrdup("Unknown");
}

static char *get_tty_path(void)
{
    char *name = ttyname(STDIN_FILENO);
    if (name)
        return xstrdup(name);
    return xstrdup("not a tty");
}

static char *get_de_session(void)
{
    const char *de = getenv("XDG_CURRENT_DESKTOP");
    const char *sess = getenv("DESKTOP_SESSION");

    if (de && *de && sess && *sess) {
        char buf[256];
        snprintf(buf, sizeof buf, "%s (%s)", de, sess);
        return xstrdup(buf);
    }
    if (de && *de)
        return xstrdup(de);
    if (sess && *sess)
        return xstrdup(sess);

#if defined(__APPLE__)
    return xstrdup("Aqua");
#else
    return xstrdup("tty");
#endif
}

static char *get_cpu_info(long *threads_out)
{
    char *model = NULL;
    long threads = 1;

#if defined(__linux__)
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof line, f)) {
            if ((strncasecmp(line, "model name", 10) == 0 ||
                 strncasecmp(line, "Hardware", 8) == 0) && !model) {
                char *p = strchr(line, ':');
                if (p) {
                    p++;
                    while (*p == ' ' || *p == '\t')
                        p++;
                    rstrip(p);
                    model = xstrdup(p);
                }
            } else if (strncasecmp(line, "processor", 9) == 0) {
                threads++;
            }
        }
        fclose(f);
        if (threads > 1)
            threads--;
    }

#elif defined(__APPLE__)
    char brand[256];
    size_t len = sizeof(brand);
    if (sysctlbyname("machdep.cpu.brand_string", brand, &len, NULL, 0) == 0)
        model = xstrdup(brand);
    
    int ncpu;
    len = sizeof(ncpu);
    if (sysctlbyname("hw.logicalcpu", &ncpu, &len, NULL, 0) == 0)
        threads = ncpu;

#elif defined(HAVE_SYSCTL)
    int mib[2];
    size_t len;
    
#if defined(__FreeBSD__) || defined(__DragonFly__)
    char brand[256];
    len = sizeof(brand);
    if (sysctlbyname("hw.model", brand, &len, NULL, 0) == 0)
        model = xstrdup(brand);
#elif defined(__OpenBSD__) || defined(__NetBSD__)
    char brand[256];
    mib[0] = CTL_HW;
    mib[1] = HW_MODEL;
    len = sizeof(brand);
    if (sysctl(mib, 2, brand, &len, NULL, 0) == 0)
        model = xstrdup(brand);
#endif

    mib[0] = CTL_HW;
    mib[1] = HW_NCPU;
    int ncpu;
    len = sizeof(ncpu);
    if (sysctl(mib, 2, &ncpu, &len, NULL, 0) == 0)
        threads = ncpu;
#endif

    if (!model)
        model = xstrdup("Unknown");
    if (threads < 1)
        threads = 1;
    if (threads_out)
        *threads_out = threads;

    char buf[512];
    snprintf(buf, sizeof buf, "%s (%ld %s)",
             model, threads, threads == 1 ? "thread" : "threads");
    free(model);
    return xstrdup(buf);
}

static char *get_cpu_governor(void)
{
#if defined(__linux__)
    char *gov = read_first_line_trim("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
    if (gov)
        return gov;
#endif
    return xstrdup("N/A");
}

static char *get_cpu_temp(void)
{
#if defined(__linux__)
    DIR *dir = opendir("/sys/class/thermal");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strncmp(ent->d_name, "thermal_zone", 12) != 0)
                continue;

            char type_path[PATH_MAX];
            snprintf(type_path, sizeof type_path, "/sys/class/thermal/%s/type", ent->d_name);
            char *type = read_first_line_trim(type_path);
            if (!type)
                continue;

            char lower[128];
            size_t len = strlen(type);
            if (len >= sizeof(lower))
                len = sizeof(lower) - 1;
            for (size_t i = 0; i < len; ++i)
                lower[i] = (char)tolower((unsigned char)type[i]);
            lower[len] = '\0';

            bool is_cpu = strstr(lower, "cpu") ||
                         strstr(lower, "x86_pkg") ||
                         strstr(lower, "coretemp") ||
                         strstr(lower, "k10temp") ||
                         strstr(lower, "acpitz");
            free(type);

            if (is_cpu) {
                char temp_path[PATH_MAX];
                snprintf(temp_path, sizeof temp_path, "/sys/class/thermal/%s/temp", ent->d_name);
                FILE *f = fopen(temp_path, "r");
                if (f) {
                    long value;
                    if (fscanf(f, "%ld", &value) == 1) {
                        fclose(f);
                        closedir(dir);
                        char buf[32];
                        snprintf(buf, sizeof buf, "%.1f°C", value / 1000.0);
                        return xstrdup(buf);
                    }
                    fclose(f);
                }
            }
        }
        closedir(dir);
    }
    
    dir = opendir("/sys/class/hwmon");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_name[0] == '.')
                continue;
            
            char temp_path[PATH_MAX];
            snprintf(temp_path, sizeof temp_path, "/sys/class/hwmon/%s/temp1_input", ent->d_name);
            FILE *f = fopen(temp_path, "r");
            if (f) {
                long value;
                if (fscanf(f, "%ld", &value) == 1) {
                    fclose(f);
                    closedir(dir);
                    char buf[32];
                    snprintf(buf, sizeof buf, "%.1f°C", value / 1000.0);
                    return xstrdup(buf);
                }
                fclose(f);
            }
        }
        closedir(dir);
    }

#elif defined(__FreeBSD__) || defined(__DragonFly__)
    int temp;
    size_t len = sizeof(temp);
    if (sysctlbyname("dev.cpu.0.temperature", &temp, &len, NULL, 0) == 0) {
        char buf[32];
        if (temp > 2000) {
            snprintf(buf, sizeof buf, "%.1f°C", (temp - 2732) / 10.0);
        } else {
            snprintf(buf, sizeof buf, "%d°C", temp);
        }
        return xstrdup(buf);
    }

#elif defined(__OpenBSD__)
    int mib[5] = { CTL_HW, HW_SENSORS, 0, SENSOR_TEMP, 0 };
    struct sensor sens;
    size_t len = sizeof(sens);
    
    for (int dev = 0; dev < 10; dev++) {
        mib[2] = dev;
        for (int idx = 0; idx < 10; idx++) {
            mib[4] = idx;
            if (sysctl(mib, 5, &sens, &len, NULL, 0) == 0) {
                if (sens.status == SENSOR_S_OK) {
                    char buf[32];
                    double celsius = (sens.value - 273150000) / 1000000.0;
                    snprintf(buf, sizeof buf, "%.1f°C", celsius);
                    return xstrdup(buf);
                }
            }
        }
    }

#elif defined(__APPLE__)
    return xstrdup("N/A");
#endif

    return xstrdup("N/A");
}

static char *get_memory_usage(double *out_percent)
{
    unsigned long long total_kb = 0, used_kb = 0;

#if defined(__linux__)
    FILE *f = fopen("/proc/meminfo", "r");
    if (f) {
        unsigned long long avail_kb = 0, buffers_kb = 0, cached_kb = 0;
        char line[256];
        
        while (fgets(line, sizeof line, f)) {
            char key[32];
            unsigned long long value;
            if (sscanf(line, "%31s %llu", key, &value) >= 2) {
                if (strcmp(key, "MemTotal:") == 0)
                    total_kb = value;
                else if (strcmp(key, "MemAvailable:") == 0)
                    avail_kb = value;
                else if (strcmp(key, "Buffers:") == 0)
                    buffers_kb = value;
                else if (strcmp(key, "Cached:") == 0)
                    cached_kb = value;
            }
        }
        fclose(f);
        
        if (avail_kb > 0)
            used_kb = total_kb - avail_kb;
        else
            used_kb = total_kb - buffers_kb - cached_kb;
    }

#elif defined(__APPLE__)
    int64_t physmem;
    size_t len = sizeof(physmem);
    if (sysctlbyname("hw.memsize", &physmem, &len, NULL, 0) == 0)
        total_kb = physmem / 1024;
    
    mach_port_t host = mach_host_self();
    vm_size_t pagesize;
    host_page_size(host, &pagesize);
    
    vm_statistics64_data_t vm_stat;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    
    if (host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&vm_stat, &count) == KERN_SUCCESS) {
        unsigned long long free_pages = vm_stat.free_count + vm_stat.inactive_count;
        unsigned long long free_kb = (free_pages * pagesize) / 1024;
        used_kb = total_kb - free_kb;
    }

#elif defined(__FreeBSD__) || defined(__DragonFly__)
    unsigned long physmem;
    size_t len = sizeof(physmem);
    if (sysctlbyname("hw.physmem", &physmem, &len, NULL, 0) == 0)
        total_kb = physmem / 1024;
    
    unsigned int pagesize;
    len = sizeof(pagesize);
    sysctlbyname("hw.pagesize", &pagesize, &len, NULL, 0);
    
    unsigned int inactive, cache, free_count;
    len = sizeof(inactive);
    sysctlbyname("vm.stats.vm.v_inactive_count", &inactive, &len, NULL, 0);
    len = sizeof(cache);
    sysctlbyname("vm.stats.vm.v_cache_count", &cache, &len, NULL, 0);
    len = sizeof(free_count);
    sysctlbyname("vm.stats.vm.v_free_count", &free_count, &len, NULL, 0);
    
    unsigned long long free_kb = ((unsigned long long)(inactive + cache + free_count) * pagesize) / 1024;
    used_kb = total_kb - free_kb;

#elif defined(__OpenBSD__)
    int mib[2];
    size_t len;
    
    mib[0] = CTL_HW;
    mib[1] = HW_PHYSMEM64;
    int64_t physmem;
    len = sizeof(physmem);
    if (sysctl(mib, 2, &physmem, &len, NULL, 0) == 0)
        total_kb = physmem / 1024;
    
    mib[0] = CTL_VM;
    mib[1] = VM_UVMEXP;
    struct uvmexp uvm;
    len = sizeof(uvm);
    if (sysctl(mib, 2, &uvm, &len, NULL, 0) == 0) {
        unsigned long long free_kb = ((unsigned long long)(uvm.free + uvm.inactive) * uvm.pagesize) / 1024;
        used_kb = total_kb - free_kb;
    }

#elif defined(__NetBSD__)
    int mib[2];
    size_t len;
    
    mib[0] = CTL_HW;
    mib[1] = HW_PHYSMEM64;
    int64_t physmem;
    len = sizeof(physmem);
    if (sysctl(mib, 2, &physmem, &len, NULL, 0) == 0)
        total_kb = physmem / 1024;
    
    struct uvmexp_sysctl uvm;
    mib[0] = CTL_VM;
    mib[1] = VM_UVMEXP2;
    len = sizeof(uvm);
    if (sysctl(mib, 2, &uvm, &len, NULL, 0) == 0) {
        unsigned long long free_kb = ((unsigned long long)(uvm.free + uvm.inactive) * uvm.pagesize) / 1024;
        used_kb = total_kb - free_kb;
    }
#endif

    if (total_kb == 0) {
        if (out_percent)
            *out_percent = -1.0;
        return xstrdup("Unknown");
    }

    double total_mib = total_kb / 1024.0;
    double used_mib = used_kb / 1024.0;
    double percent = (used_mib / total_mib) * 100.0;

    if (out_percent)
        *out_percent = percent;

    char buf[64];
    snprintf(buf, sizeof buf, "%.0fMiB / %.0fMiB (%.0f%%)",
             used_mib, total_mib, percent);
    return xstrdup(buf);
}

static char *get_swap_usage(double *out_percent)
{
    unsigned long long total_kb = 0, used_kb = 0;

#if defined(__linux__)
    FILE *f = fopen("/proc/meminfo", "r");
    if (f) {
        unsigned long long free_kb = 0;
        char line[256];
        
        while (fgets(line, sizeof line, f)) {
            char key[32];
            unsigned long long value;
            if (sscanf(line, "%31s %llu", key, &value) >= 2) {
                if (strcmp(key, "SwapTotal:") == 0)
                    total_kb = value;
                else if (strcmp(key, "SwapFree:") == 0)
                    free_kb = value;
            }
        }
        fclose(f);
        used_kb = total_kb - free_kb;
    }

#elif defined(__APPLE__)
    struct xsw_usage xsu;
    size_t len = sizeof(xsu);
    if (sysctlbyname("vm.swapusage", &xsu, &len, NULL, 0) == 0) {
        total_kb = xsu.xsu_total / 1024;
        used_kb = xsu.xsu_used / 1024;
    }

#elif defined(__FreeBSD__) || defined(__DragonFly__)
    char *output = run_command_first_line("swapctl -sk 2>/dev/null | awk '{print $2, $4}'");
    if (output) {
        sscanf(output, "%llu %llu", &total_kb, &used_kb);
        free(output);
    }

#elif defined(__OpenBSD__)
    int nswap = swapctl(SWAP_NSWAP, NULL, 0);
    if (nswap > 0) {
        struct swapent *sep = xmalloc(nswap * sizeof(struct swapent));
        if (swapctl(SWAP_STATS, sep, nswap) >= 0) {
            for (int i = 0; i < nswap; i++) {
                total_kb += (unsigned long long)sep[i].se_nblks * DEV_BSIZE / 1024;
                used_kb += (unsigned long long)sep[i].se_inuse * DEV_BSIZE / 1024;
            }
        }
        free(sep);
    }

#elif defined(__NetBSD__)
    int nswap = swapctl(SWAP_NSWAP, NULL, 0);
    if (nswap > 0) {
        struct swapent *sep = xmalloc(nswap * sizeof(struct swapent));
        if (swapctl(SWAP_STATS, sep, nswap) >= 0) {
            for (int i = 0; i < nswap; i++) {
                total_kb += (unsigned long long)sep[i].se_nblks * DEV_BSIZE / 1024;
                used_kb += (unsigned long long)sep[i].se_inuse * DEV_BSIZE / 1024;
            }
        }
        free(sep);
    }
#endif

    if (total_kb == 0) {
        if (out_percent)
            *out_percent = -1.0;
        return xstrdup("Disabled");
    }

    double total_mib = total_kb / 1024.0;
    double used_mib = used_kb / 1024.0;
    double percent = (used_mib / total_mib) * 100.0;

    if (out_percent)
        *out_percent = percent;

    char buf[64];
    snprintf(buf, sizeof buf, "%.0fMiB / %.0fMiB (%.0f%%)",
             used_mib, total_mib, percent);
    return xstrdup(buf);
}

static char *get_disk_root_usage(double *out_percent)
{
#if defined(__linux__) || defined(__APPLE__)
    struct statvfs st;
    if (statvfs("/", &st) != 0) {
        if (out_percent)
            *out_percent = -1.0;
        return xstrdup("Unknown");
    }

    unsigned long long total = (unsigned long long)st.f_blocks * st.f_frsize;
    unsigned long long avail = (unsigned long long)st.f_bavail * st.f_frsize;
    unsigned long long used = total - avail;
#else
    struct statfs st;
    if (statfs("/", &st) != 0) {
        if (out_percent)
            *out_percent = -1.0;
        return xstrdup("Unknown");
    }

    unsigned long long total = (unsigned long long)st.f_blocks * st.f_bsize;
    unsigned long long avail = (unsigned long long)st.f_bavail * st.f_bsize;
    unsigned long long used = total - avail;
#endif

    if (total == 0) {
        if (out_percent)
            *out_percent = -1.0;
        return xstrdup("Unknown");
    }

    double total_gib = total / (1024.0 * 1024.0 * 1024.0);
    double used_gib = used / (1024.0 * 1024.0 * 1024.0);
    double percent = (used_gib / total_gib) * 100.0;

    if (out_percent)
        *out_percent = percent;

    char buf[64];
    snprintf(buf, sizeof buf, "%.1fGiB / %.1fGiB (%.0f%%)",
             used_gib, total_gib, percent);
    return xstrdup(buf);
}

static char *get_root_fs_type(void)
{
#if defined(__linux__)
    FILE *f = fopen("/proc/self/mounts", "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof line, f)) {
            char dev[256], mnt[256], type[64];
            if (sscanf(line, "%255s %255s %63s", dev, mnt, type) == 3) {
                if (strcmp(mnt, "/") == 0) {
                    fclose(f);
                    return xstrdup(type);
                }
            }
        }
        fclose(f);
    }
    return xstrdup("Unknown");

#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
    struct statfs st;
    if (statfs("/", &st) == 0)
        return xstrdup(st.f_fstypename);
    return xstrdup("Unknown");
#else
    return xstrdup("Unknown");
#endif
}

static char *get_loadavg(double *one_min)
{
    double loadavg[3];
    if (getloadavg(loadavg, 3) >= 1) {
        if (one_min)
            *one_min = loadavg[0];
        char buf[64];
        snprintf(buf, sizeof buf, "%.2f %.2f %.2f",
                 loadavg[0], loadavg[1], loadavg[2]);
        return xstrdup(buf);
    }
    if (one_min)
        *one_min = -1.0;
    return xstrdup("Unknown");
}

static char *get_network_info(void)
{
    struct ifaddrs *ifaddr = NULL;
    if (getifaddrs(&ifaddr) == -1)
        return xstrdup("Offline");

    char result[128];
    result[0] = '\0';

    for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr)
            continue;
        if (ifa->ifa_addr->sa_family != AF_INET)
            continue;

        const char *name = ifa->ifa_name;
        if (!name)
            continue;
        
        if (strcmp(name, "lo") == 0 || strcmp(name, "lo0") == 0)
            continue;

        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
        struct sockaddr_in *nm = (struct sockaddr_in *)ifa->ifa_netmask;

        char addr[INET_ADDRSTRLEN];
        if (!inet_ntop(AF_INET, &sa->sin_addr, addr, sizeof addr))
            continue;

        int prefix = 0;
        if (nm) {
            uint32_t m = ntohl(nm->sin_addr.s_addr);
            for (int i = 0; i < 32; ++i) {
                if (m & (1u << (31 - i)))
                    prefix++;
                else
                    break;
            }
        }

        snprintf(result, sizeof result, "%s %s/%d", name, addr, prefix);
        break;
    }

    freeifaddrs(ifaddr);

    if (!result[0])
        return xstrdup("Offline");
    return xstrdup(result);
}

static char *get_process_count(void)
{
#if defined(__linux__)
    DIR *dir = opendir("/proc");
    if (!dir)
        return xstrdup("Unknown");

    long count = 0;
    struct dirent *ent;
    
    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;
        bool all_digits = true;
        for (const char *p = name; *p; ++p) {
            if (*p < '0' || *p > '9') {
                all_digits = false;
                break;
            }
        }
        if (all_digits && name[0] != '\0')
            count++;
    }
    closedir(dir);

    char buf[32];
    snprintf(buf, sizeof buf, "%ld", count);
    return xstrdup(buf);

#elif defined(HAVE_SYSCTL)
    int mib[3];
    size_t len;
    
#if defined(__APPLE__)
    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_ALL;
    
    if (sysctl(mib, 3, NULL, &len, NULL, 0) == 0) {
        long count = len / sizeof(struct kinfo_proc);
        char buf[32];
        snprintf(buf, sizeof buf, "%ld", count);
        return xstrdup(buf);
    }
#elif defined(__FreeBSD__) || defined(__DragonFly__)
    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_ALL;
    
    if (sysctl(mib, 3, NULL, &len, NULL, 0) == 0) {
        long count = len / sizeof(struct kinfo_proc);
        char buf[32];
        snprintf(buf, sizeof buf, "%ld", count);
        return xstrdup(buf);
    }
#elif defined(__OpenBSD__) || defined(__NetBSD__)
    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_ALL;
    
    if (sysctl(mib, 3, NULL, &len, NULL, 0) == 0) {
        long count = len / sizeof(struct kinfo_proc);
        char buf[32];
        snprintf(buf, sizeof buf, "%ld", count);
        return xstrdup(buf);
    }
#endif
    return xstrdup("Unknown");
#else
    return xstrdup("Unknown");
#endif
}

static char *get_user_summary(void)
{
    char names[256];
    names[0] = '\0';
    int count = 0;

#if defined(HAVE_UTMPX)
    setutxent();
    struct utmpx *ut;
    
    while ((ut = getutxent()) != NULL) {
        if (ut->ut_type != USER_PROCESS)
            continue;

        char username[sizeof(ut->ut_user) + 1];
        memcpy(username, ut->ut_user, sizeof(ut->ut_user));
        username[sizeof(ut->ut_user)] = '\0';
        
        if (!username[0])
            continue;

        if (strstr(names, username))
            continue;

        if (count < 5) {
            if (names[0] != '\0')
                strncat(names, ", ", sizeof(names) - strlen(names) - 1);
            strncat(names, username, sizeof(names) - strlen(names) - 1);
        }
        count++;
    }
    endutxent();

#elif defined(HAVE_UTMP)
    FILE *f = fopen(_PATH_UTMP, "r");
    if (f) {
        struct utmp ut;
        while (fread(&ut, sizeof(ut), 1, f) == 1) {
            if (ut.ut_name[0] == '\0')
                continue;
            if (ut.ut_line[0] == '\0')
                continue;

            char username[sizeof(ut.ut_name) + 1];
            memcpy(username, ut.ut_name, sizeof(ut.ut_name));
            username[sizeof(ut.ut_name)] = '\0';

            if (strstr(names, username))
                continue;

            if (count < 5) {
                if (names[0] != '\0')
                    strncat(names, ", ", sizeof(names) - strlen(names) - 1);
                strncat(names, username, sizeof(names) - strlen(names) - 1);
            }
            count++;
        }
        fclose(f);
    }
#endif

    if (count <= 0)
        return xstrdup("0");

    char buf[320];
    if (names[0])
        snprintf(buf, sizeof buf, "%d (%s)", count, names);
    else
        snprintf(buf, sizeof buf, "%d", count);
    return xstrdup(buf);
}

static long count_dir_entries(const char *path)
{
    DIR *dir = opendir(path);
    if (!dir)
        return -1;

    long count = 0;
    struct dirent *ent;
    
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] != '.')
            count++;
    }
    closedir(dir);
    return count > 0 ? count : -1;
}

static char *get_package_summary(void)
{
    char buf[256];
    buf[0] = '\0';
    int found = 0;

#if defined(__linux__)
    FILE *f = fopen("/var/lib/dpkg/status", "r");
    if (f) {
        char line[256];
        long count = 0;
        while (fgets(line, sizeof line, f)) {
            if (strncmp(line, "Package:", 8) == 0)
                count++;
        }
        fclose(f);
        if (count > 0) {
            char part[64];
            snprintf(part, sizeof part, "%ld (dpkg)", count);
            strncat(buf, part, sizeof(buf) - strlen(buf) - 1);
            found = 1;
        }
    }

    long pacman = count_dir_entries("/var/lib/pacman/local");
    if (pacman > 0) {
        char part[64];
        snprintf(part, sizeof part, "%ld (pacman)", pacman);
        if (found)
            strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
        strncat(buf, part, sizeof(buf) - strlen(buf) - 1);
        found = 1;
    }

    char *rpm = run_command_first_line("rpm -qa 2>/dev/null | wc -l");
    if (rpm) {
        long count = strtol(rpm, NULL, 10);
        free(rpm);
        if (count > 0) {
            char part[64];
            snprintf(part, sizeof part, "%ld (rpm)", count);
            if (found)
                strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
            strncat(buf, part, sizeof(buf) - strlen(buf) - 1);
            found = 1;
        }
    }

#elif defined(__APPLE__)
    char *brew = run_command_first_line("brew list 2>/dev/null | wc -l");
    if (brew) {
        long count = strtol(brew, NULL, 10);
        free(brew);
        if (count > 0) {
            char part[64];
            snprintf(part, sizeof part, "%ld (brew)", count);
            strncat(buf, part, sizeof(buf) - strlen(buf) - 1);
            found = 1;
        }
    }

#elif defined(__FreeBSD__) || defined(__DragonFly__)
    char *pkg = run_command_first_line("pkg info -q 2>/dev/null | wc -l");
    if (pkg) {
        long count = strtol(pkg, NULL, 10);
        free(pkg);
        if (count > 0) {
            char part[64];
            snprintf(part, sizeof part, "%ld (pkg)", count);
            strncat(buf, part, sizeof(buf) - strlen(buf) - 1);
            found = 1;
        }
    }

#elif defined(__OpenBSD__)
    char *pkg = run_command_first_line("pkg_info 2>/dev/null | wc -l");
    if (pkg) {
        long count = strtol(pkg, NULL, 10);
        free(pkg);
        if (count > 0) {
            char part[64];
            snprintf(part, sizeof part, "%ld (pkg)", count);
            strncat(buf, part, sizeof(buf) - strlen(buf) - 1);
            found = 1;
        }
    }

#elif defined(__NetBSD__)
    char *pkg = run_command_first_line("pkg_info 2>/dev/null | wc -l");
    if (pkg) {
        long count = strtol(pkg, NULL, 10);
        free(pkg);
        if (count > 0) {
            char part[64];
            snprintf(part, sizeof part, "%ld (pkgsrc)", count);
            strncat(buf, part, sizeof(buf) - strlen(buf) - 1);
            found = 1;
        }
    }
#endif

    long flatpak_sys = count_dir_entries("/var/lib/flatpak/app");
    const char *home = getenv("HOME");
    long flatpak_user = -1;
    if (home) {
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/.local/share/flatpak/app", home);
        flatpak_user = count_dir_entries(path);
    }
    long flatpak = 0;
    if (flatpak_sys > 0)
        flatpak += flatpak_sys;
    if (flatpak_user > 0)
        flatpak += flatpak_user;
    if (flatpak > 0) {
        char part[64];
        snprintf(part, sizeof part, "%ld (flatpak)", flatpak);
        if (found)
            strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
        strncat(buf, part, sizeof(buf) - strlen(buf) - 1);
        found = 1;
    }

    if (!found)
        return xstrdup("Unknown");
    return xstrdup(buf);
}

static char *get_battery_status(void)
{
#if defined(__linux__)
    DIR *dir = opendir("/sys/class/power_supply");
    if (!dir)
        return xstrdup("N/A");

    struct dirent *ent;
    char *result = NULL;

    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "BAT", 3) != 0)
            continue;

        char path[PATH_MAX];
        
        snprintf(path, sizeof path, "/sys/class/power_supply/%s/capacity", ent->d_name);
        char *cap = read_first_line_trim(path);
        
        snprintf(path, sizeof path, "/sys/class/power_supply/%s/status", ent->d_name);
        char *status = read_first_line_trim(path);

        if (cap && status) {
            char buf[128];
            snprintf(buf, sizeof buf, "%s%% (%s)", cap, status);
            result = xstrdup(buf);
        } else if (cap) {
            char buf[64];
            snprintf(buf, sizeof buf, "%s%%", cap);
            result = xstrdup(buf);
        }

        free(cap);
        free(status);
        
        if (result)
            break;
    }
    closedir(dir);
    return result ? result : xstrdup("N/A");

#elif defined(__APPLE__)
    char *output = run_command_first_line(
        "pmset -g batt 2>/dev/null | grep -Eo '[0-9]+%.*' | head -1");
    if (output)
        return output;
    return xstrdup("N/A");

#elif defined(__FreeBSD__) || defined(__DragonFly__)
    int life, state;
    size_t len;
    
    len = sizeof(life);
    if (sysctlbyname("hw.acpi.battery.life", &life, &len, NULL, 0) == 0) {
        len = sizeof(state);
        sysctlbyname("hw.acpi.battery.state", &state, &len, NULL, 0);
        
        const char *status;
        if (state == 1)
            status = "Discharging";
        else if (state == 2)
            status = "Charging";
        else
            status = "Unknown";
        
        char buf[64];
        snprintf(buf, sizeof buf, "%d%% (%s)", life, status);
        return xstrdup(buf);
    }
    return xstrdup("N/A");

#elif defined(__OpenBSD__)
    int mib[5] = { CTL_HW, HW_SENSORS, 0, SENSOR_PERCENT, 0 };
    struct sensor sens;
    size_t len = sizeof(sens);
    
    for (int dev = 0; dev < 10; dev++) {
        mib[2] = dev;
        if (sysctl(mib, 5, &sens, &len, NULL, 0) == 0) {
            if (sens.status == SENSOR_S_OK) {
                char buf[32];
                snprintf(buf, sizeof buf, "%.0f%%", sens.value / 1000.0);
                return xstrdup(buf);
            }
        }
    }
    return xstrdup("N/A");

#else
    return xstrdup("N/A");
#endif
}

static char *get_gpu_info(void)
{
#if defined(__linux__)
    DIR *dir = opendir("/sys/class/drm");
    if (!dir)
        return xstrdup("Unknown");

    struct dirent *ent;
    char device_path[PATH_MAX];
    device_path[0] = '\0';

    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "card", 4) == 0 &&
            strchr(ent->d_name, '-') == NULL) {
            snprintf(device_path, sizeof device_path,
                     "/sys/class/drm/%s/device", ent->d_name);
            break;
        }
    }
    closedir(dir);

    if (!device_path[0])
        return xstrdup("Unknown");

    char vendor_path[PATH_MAX];
    snprintf(vendor_path, sizeof vendor_path, "%s/vendor", device_path);
    char *vendor_id = read_first_line_trim(vendor_path);

    const char *vendor_name = "Unknown";
    if (vendor_id) {
        if (strcmp(vendor_id, "0x8086") == 0)
            vendor_name = "Intel";
        else if (strcmp(vendor_id, "0x10de") == 0)
            vendor_name = "NVIDIA";
        else if (strcmp(vendor_id, "0x1002") == 0 || strcmp(vendor_id, "0x1022") == 0)
            vendor_name = "AMD";
        free(vendor_id);
    }

    char buf[64];
    snprintf(buf, sizeof buf, "%s GPU", vendor_name);
    return xstrdup(buf);

#elif defined(__APPLE__)
    char *output = run_command_first_line(
        "system_profiler SPDisplaysDataType 2>/dev/null | grep 'Chipset Model' | head -1 | cut -d: -f2");
    if (output) {
        while (*output == ' ')
            output++;
        return xstrdup(output);
    }
    return xstrdup("Unknown");

#elif defined(__FreeBSD__) || defined(__DragonFly__)
    char *output = run_command_first_line("pciconf -lv 2>/dev/null | grep -A4 'vgapci' | grep device | head -1");
    if (output)
        return output;
    return xstrdup("Unknown");

#elif defined(__OpenBSD__) || defined(__NetBSD__)
    char *output = run_command_first_line("pcidump 2>/dev/null | grep -i 'vga\\|display' | head -1");
    if (output)
        return output;
    return xstrdup("Unknown");

#else
    return xstrdup("Unknown");
#endif
}

static char *get_locale_str(void)
{
    const char *lang = getenv("LC_ALL");
    if (!lang || !*lang)
        lang = getenv("LC_CTYPE");
    if (!lang || !*lang)
        lang = getenv("LANG");
    if (!lang || !*lang)
        return xstrdup("C");
    return xstrdup(lang);
}

static char *get_session_type(void)
{
    const char *xdg = getenv("XDG_SESSION_TYPE");
    if (xdg && *xdg)
        return xstrdup(xdg);

    const char *way = getenv("WAYLAND_DISPLAY");
    if (way && *way)
        return xstrdup("wayland");

    const char *disp = getenv("DISPLAY");
    if (disp && *disp)
        return xstrdup("x11");

#if defined(__APPLE__)
    return xstrdup("aqua");
#else
    return xstrdup("tty");
#endif
}

static char *make_bar(double percent, int width)
{
    if (width < 1)
        width = 10;
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    
    int filled = (int)((percent / 100.0) * width + 0.5);

    char *buf = xmalloc((size_t)width + 3);
    buf[0] = '[';
    for (int i = 0; i < width; ++i)
        buf[1 + i] = (i < filled) ? '=' : ' ';
    buf[1 + width] = ']';
    buf[2 + width] = '\0';

    return buf;
}

static void print_header(const char *user, const char *host)
{
    char buf[256];
    snprintf(buf, sizeof buf, "%s@%s", user, host);

    char ver[128];
    snprintf(ver, sizeof ver, "YAFP v%s — yet another fetch program", YAFP_VERSION);

    if (g_use_color) {
        printf("\033[1;38;5;45m%s\033[0m\n", buf);
        printf("\033[38;5;240m%s\033[0m\n", ver);
    } else {
        puts(buf);
        puts(ver);
    }

    size_t len = strlen(ver);
    if (len < strlen(buf))
        len = strlen(buf);
    if (len < 40)
        len = 40;

    for (size_t i = 0; i < len; ++i)
        putchar('-');
    putchar('\n');
}

static void print_section(const char *name)
{
    if (g_minimal)
        return;

    putchar('\n');
    if (g_use_color)
        printf("\033[38;5;117m[ %s ]\033[0m\n", name);
    else
        printf("[ %s ]\n", name);
}

static void print_kv(const char *label, const char *value)
{
    if (!value)
        value = "";

    if (g_use_color) {
        printf("  \033[38;5;213m%-*s\033[0m : \033[38;5;81m%s\033[0m\n",
               LABEL_WIDTH, label, value);
    } else {
        printf("  %-*s : %s\n", LABEL_WIDTH, label, value);
    }
}

static void print_help(const char *prog)
{
    printf("yafp %s - Yet Another Fetch Program\n\n", YAFP_VERSION);
    printf("Usage: %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  -h, --help       Show this help\n");
    printf("  -v, --version    Show version\n");
    printf("      --no-color   Disable ANSI colors\n");
    printf("      --plain      Plain mode (no colors, full info)\n");
    printf("      --minimal    Only core fields (OS, kernel, uptime, memory)\n\n");
    printf("Supported platforms:\n");
    printf("  Linux, macOS, FreeBSD, OpenBSD, NetBSD, DragonFlyBSD\n");
}

int main(int argc, char **argv)
{
    const char *progname = (argc > 0 && argv[0]) ? argv[0] : "yafp";

    if (getenv("NO_COLOR"))
        g_use_color = false;
    else
        g_use_color = isatty(STDOUT_FILENO) != 0;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_help(progname);
            return 0;
        } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
            printf("yafp %s\n", YAFP_VERSION);
            return 0;
        } else if (strcmp(arg, "--no-color") == 0) {
            g_use_color = false;
        } else if (strcmp(arg, "--plain") == 0) {
            g_use_color = false;
            g_minimal = false;
        } else if (strcmp(arg, "--minimal") == 0) {
            g_minimal = true;
        } else {
            fprintf(stderr, "%s: unknown option '%s'\n", progname, arg);
            fprintf(stderr, "Try '%s --help'\n", progname);
            return 1;
        }
    }

    char *user = get_username();
    char *host = get_hostname_simple();
    char *os_name = get_os_pretty_name();
    char *host_model = get_host_model();
    char *kernel = get_kernel_release();
    char *arch = get_architecture();

    long uptime_secs = 0;
    char *uptime = format_uptime(&uptime_secs);
    time_t now = 0;
    char *run_time = get_run_datetime(&now);
    char *boot_time = format_boot_time(now, uptime_secs);

    double load1 = -1.0;
    char *loadavg = get_loadavg(&load1);

    char *shell = get_shell_name();
    char *term_env = get_term_env();
    char *tty_size = get_tty_size();
    char *tty_path = get_tty_path();
    char *de_session = get_de_session();
    char *session_type = get_session_type();

    long cpu_threads = 0;
    char *cpu_info = get_cpu_info(&cpu_threads);
    char *cpu_gov = get_cpu_governor();
    char *cpu_temp = get_cpu_temp();

    double mem_pct = -1.0, swap_pct = -1.0, disk_pct = -1.0;
    char *mem_usage = get_memory_usage(&mem_pct);
    char *swap_usage = get_swap_usage(&swap_pct);
    char *disk_usage = get_disk_root_usage(&disk_pct);
    char *fs_type = get_root_fs_type();

    char *proc_count = get_process_count();
    char *pkg_summary = get_package_summary();
    char *battery = get_battery_status();
    char *gpu_info = get_gpu_info();
    char *net_info = get_network_info();
    char *locale_str = get_locale_str();
    char *user_summary = get_user_summary();

    if (strcmp(user_summary, "0") == 0) {
        free(user_summary);
        char buf[128];
        snprintf(buf, sizeof buf, "1 (%s)", user);
        user_summary = xstrdup(buf);
    }

    double cpu_pct = -1.0;
    if (cpu_threads < 1)
        cpu_threads = 1;
    if (load1 >= 0.0) {
        cpu_pct = (load1 / (double)cpu_threads) * 100.0;
        if (cpu_pct > 100.0)
            cpu_pct = 100.0;
    }

    char *cpu_bar = (cpu_pct >= 0.0) ? make_bar(cpu_pct, 20) : xstrdup("");
    char *mem_bar = (mem_pct >= 0.0) ? make_bar(mem_pct, 20) : xstrdup("");
    char *swap_bar = (swap_pct >= 0.0) ? make_bar(swap_pct, 20) : xstrdup("");
    char *disk_bar = (disk_pct >= 0.0) ? make_bar(disk_pct, 20) : xstrdup("");

    print_header(user, host);

    if (g_minimal) {
        print_kv("OS", os_name);
        print_kv("Kernel", kernel);
        print_kv("Uptime", uptime);
        print_kv("Memory", mem_usage);
    } else {
        print_section("System");
        print_kv("OS", os_name);
        print_kv("Host", host_model);
        print_kv("Kernel", kernel);
        print_kv("Arch", arch);

        char up_line[160];
        snprintf(up_line, sizeof up_line, "%s (boot %s)", uptime, boot_time);
        print_kv("Uptime", up_line);
        print_kv("Run time", run_time);
        print_kv("Load avg", loadavg);

        if (cpu_bar[0]) {
            char cpu_load_line[160];
            if (cpu_pct >= 0.0)
                snprintf(cpu_load_line, sizeof cpu_load_line, "~%.0f%% %s", cpu_pct, cpu_bar);
            else
                snprintf(cpu_load_line, sizeof cpu_load_line, "%s", cpu_bar);
            print_kv("CPU load", cpu_load_line);
        }

        print_kv("Packages", pkg_summary);
        print_kv("Processes", proc_count);
        print_kv("Users", user_summary);
        print_kv("Locale", locale_str);

        print_section("Hardware");
        print_kv("CPU", cpu_info);
        print_kv("CPU gov", cpu_gov);
        print_kv("CPU temp", cpu_temp);
        print_kv("GPU", gpu_info);

        if (mem_bar[0]) {
            char mem_line[192];
            snprintf(mem_line, sizeof mem_line, "%s %s", mem_usage, mem_bar);
            print_kv("Memory", mem_line);
        } else {
            print_kv("Memory", mem_usage);
        }

        if (swap_bar[0]) {
            char swap_line[192];
            snprintf(swap_line, sizeof swap_line, "%s %s", swap_usage, swap_bar);
            print_kv("Swap", swap_line);
        } else {
            print_kv("Swap", swap_usage);
        }

        if (disk_bar[0]) {
            char disk_line[192];
            snprintf(disk_line, sizeof disk_line, "%s %s", disk_usage, disk_bar);
            print_kv("Disk (/)", disk_line);
        } else {
            print_kv("Disk (/)", disk_usage);
        }

        print_kv("Filesystem", fs_type);
        print_kv("Network", net_info);
        print_kv("Battery", battery);

        print_section("Session");
        print_kv("Shell", shell);
        print_kv("Terminal", term_env);
        print_kv("Session", session_type);
        print_kv("DE / Sess", de_session);

        char tty_line[160];
        snprintf(tty_line, sizeof tty_line, "%s (%s)", tty_path, tty_size);
        print_kv("TTY", tty_line);
    }

    free(user);
    free(host);
    free(os_name);
    free(host_model);
    free(kernel);
    free(arch);
    free(uptime);
    free(run_time);
    free(boot_time);
    free(loadavg);
    free(shell);
    free(term_env);
    free(tty_size);
    free(tty_path);
    free(de_session);
    free(session_type);
    free(cpu_info);
    free(cpu_gov);
    free(cpu_temp);
    free(mem_usage);
    free(swap_usage);
    free(disk_usage);
    free(fs_type);
    free(proc_count);
    free(pkg_summary);
    free(battery);
    free(gpu_info);
    free(net_info);
    free(locale_str);
    free(user_summary);
    free(cpu_bar);
    free(mem_bar);
    free(swap_bar);
    free(disk_bar);

    return 0;
}