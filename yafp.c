#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <pwd.h>
#include <errno.h>
#include <dirent.h>
#include <sys/statvfs.h>
#include <ctype.h>
#include <time.h>
#include <limits.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <utmpx.h>
#include <stdint.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define YAFP_VERSION "0.1.0"

static bool g_use_color  = true;
static bool g_minimal    = false;

#define LABEL_WIDTH 12

// platform specific headers
#ifdef __linux__
#include <sys/sysinfo.h>
#elif defined(__FreeBSD__)
#include <sys/sysctl.h>
#elif defined(__OpenBSD__)
#include <sys/sysctl.h>
#elif defined(__NetBSD__)
#include <sys/sysctl.h>
#endif

// function to get system information
void get_system_info() {
    #ifdef __linux__
        struct sysinfo info;
        if (sysinfo(&info) == 0) {
            printf("Total RAM: %ld MB\n", info.totalram / (1024 * 1024));
        }
    #elif defined(__FreeBSD__)
        struct sysctlinfo info;
        size_t len = sizeof(info);
        if (sysctlbyname("hw.physmem", &info, &len, NULL, 0) == 0) {
            printf("Total RAM: %ld MB\n", info.totalram / (1024 * 1024));
        }
    #elif defined(__OpenBSD__)
        struct sysctlinfo info;
        size_t len = sizeof(info);
        if (sysctlbyname("vm.stats", &info, &len, NULL, 0) == 0) {
            printf("Total RAM: %ld MB\n", info.totalram / (1024 * 1024));
        }
    #elif defined(__NetBSD__)
        struct sysctlinfo info;
        size_t len = sizeof(info);
        if (sysctlbyname("hw.physmem", &info, &len, NULL, 0) == 0) {
            printf("Total RAM: %ld MB\n", info.totalram / (1024 * 1024));
        }
    #else
        printf("Unsupported platform\n");
    #endif
}


/* ---------- tiny helpers ---------- */

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *p = malloc(len + 1);
    if (!p) die("malloc");
    memcpy(p, s, len + 1);
    return p;
}

static void rstrip(char *s) {
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0) {
        char c = s[len - 1];
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t')
            s[--len] = '\0';
        else
            break;
    }
}

static char *read_first_line_trim(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    char buf[256];
    if (!fgets(buf, sizeof buf, f)) {
        fclose(f);
        return NULL;
    }
    fclose(f);
    rstrip(buf);
    return xstrdup(buf);
}

/* ---------- user / host ---------- */

static char *get_username(void) {
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_name)
        return xstrdup(pw->pw_name);

    const char *u = getenv("USER");
    if (u && *u)
        return xstrdup(u);

    return xstrdup("user");
}

static char *get_hostname_simple(void) {
    char buf[256];
    if (gethostname(buf, sizeof buf) == 0) {
        buf[sizeof(buf) - 1] = '\0';
        return xstrdup(buf);
    }
    return xstrdup("host");
}

/* ---------- OS / kernel / arch / host model ---------- */

static char *get_host_model(void) {
    char *vendor  = read_first_line_trim("/sys/class/dmi/id/sys_vendor");
    char *product = read_first_line_trim("/sys/class/dmi/id/product_name");
    char *result = NULL;

    if (vendor && *vendor && product && *product) {
        char buf[256];
        snprintf(buf, sizeof buf, "%s %s", vendor, product);
        result = xstrdup(buf);
    } else if (product && *product) {
        result = xstrdup(product);
    } else {
        result = xstrdup("unknown");
    }

    free(vendor);
    free(product);
    return result;
}

static char *get_os_pretty_name(void) {
    FILE *f = fopen("/etc/os-release", "r");
    if (!f)
        return xstrdup("Linux");

    char *line = NULL;
    size_t cap = 0;
    char *result = NULL;

    while (getline(&line, &cap, f) != -1) {
        if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
            char *p = line + 12;
            while (*p == ' ' || *p == '\t')
                p++;

            if (*p == '"' || *p == '\'') {
                char quote = *p++;
                char *start = p;
                char *end = strchr(start, quote);
                if (end)
                    *end = '\0';
                result = xstrdup(start);
            } else {
                rstrip(p);
                result = xstrdup(p);
            }
            break;
        }
    }

    free(line);
    fclose(f);

    if (!result)
        result = xstrdup("Linux");

    return result;
}

static char *get_kernel_release(void) {
    struct utsname uts;
    if (uname(&uts) == 0)
        return xstrdup(uts.release);
    return xstrdup("unknown");
}

static char *get_architecture(void) {
    struct utsname uts;
    if (uname(&uts) == 0)
        return xstrdup(uts.machine);
    return xstrdup("unknown");
}

/* ---------- uptime, run time, boot time ---------- */

static char *format_uptime(long *out_secs) {
    long secs = 0;
    struct sysinfo info;

    if (sysinfo(&info) == 0) {
        secs = info.uptime;
    } else {
        FILE *f = fopen("/proc/uptime", "r");
        if (f) {
            double up = 0.0;
            if (fscanf(f, "%lf", &up) == 1)
                secs = (long) up;
            fclose(f);
        }
    }

    if (secs < 0) secs = 0;
    if (out_secs) *out_secs = secs;

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

static char *get_run_datetime(time_t *out_now) {
    time_t now = time(NULL);
    if (out_now)
        *out_now = now;
    if (now == (time_t)-1)
        return xstrdup("unknown");
    struct tm *tm = localtime(&now);
    if (!tm)
        return xstrdup("unknown");

    char buf[64];
    if (strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", tm) == 0)
        return xstrdup("unknown");
    return xstrdup(buf);
}

static char *format_boot_time(time_t now, long uptime_secs) {
    if (now == (time_t)-1 || uptime_secs <= 0)
        return xstrdup("unknown");
    time_t boot = now - uptime_secs;
    struct tm *tm = localtime(&boot);
    if (!tm)
        return xstrdup("unknown");
    char buf[64];
    if (strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", tm) == 0)
        return xstrdup("unknown");
    return xstrdup(buf);
}

/* ---------- shell / terminal / tty ---------- */

static char *get_shell_name(void) {
    const char *sh = getenv("SHELL");
    if (!sh || !*sh)
        return xstrdup("unknown");

    const char *slash = strrchr(sh, '/');
    if (slash && slash[1] != '\0')
        return xstrdup(slash + 1);

    return xstrdup(sh);
}

static char *get_term_env(void) {
    const char *t = getenv("TERM");
    if (!t || !*t)
        return xstrdup("unknown");
    return xstrdup(t);
}

static char *get_tty_size(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        char buf[32];
        snprintf(buf, sizeof buf, "%dx%d", ws.ws_col, ws.ws_row);
        return xstrdup(buf);
    }
    return xstrdup("unknown");
}

static char *get_tty_path(void) {
    char *name = ttyname(STDIN_FILENO);
    if (name)
        return xstrdup(name);
    return xstrdup("not a tty");
}

/* ---------- DE / session ---------- */

static char *get_de_session(void) {
    const char *de   = getenv("XDG_CURRENT_DESKTOP");
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
    return xstrdup("tty");
}

/* ---------- CPU / memory ---------- */

static char *get_cpu_info(long *threads_out) {
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) {
        if (threads_out) *threads_out = 1;
        return xstrdup("unknown");
    }

    char line[512];
    char *model = NULL;
    long threads = 0;

    while (fgets(line, sizeof line, f)) {
        if (strncasecmp(line, "model name", 10) == 0 ||
            strncasecmp(line, "Hardware", 8) == 0) {
            if (!model) {
                char *p = strchr(line, ':');
                if (p) {
                    p++;
                    while (*p == ' ' || *p == '\t')
                        p++;
                    rstrip(p);
                    model = xstrdup(p);
                }
            }
            } else if (strncasecmp(line, "processor", 9) == 0) {
                threads++;
            }
    }

    fclose(f);

    if (!model)
        model = xstrdup("unknown");
    if (threads <= 0)
        threads = 1;
    if (threads_out)
        *threads_out = threads;

    char buf[512];
    snprintf(buf, sizeof buf, "%s (%ld thread%s)", model, threads,
             (threads == 1 ? "" : "s"));
    free(model);
    return xstrdup(buf);
}

static char *get_cpu_governor(void) {
    char *gov = read_first_line_trim("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
    if (!gov)
        return xstrdup("unknown");
    return gov;
}

static char *get_cpu_temp(void) {
    DIR *dir = opendir("/sys/class/thermal");
    if (!dir)
        return xstrdup("unknown");

    struct dirent *ent;
    char temp_path[PATH_MAX];
    temp_path[0] = '\0';

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

        if (strstr(lower, "cpu") ||
            strstr(lower, "x86_pkg_temp") ||
            strstr(lower, "package id 0") ||
            strstr(lower, "tctl")) {
            snprintf(temp_path, sizeof temp_path, "/sys/class/thermal/%s/temp", ent->d_name);
        free(type);
        break;
            }

            free(type);
    }

    closedir(dir);

    if (!temp_path[0])
        return xstrdup("unknown");

    FILE *f = fopen(temp_path, "r");
    if (!f)
        return xstrdup("unknown");
    long value = 0;
    if (fscanf(f, "%ld", &value) != 1) {
        fclose(f);
        return xstrdup("unknown");
    }
    fclose(f);

    double c = value / 1000.0;
    char buf[32];
    snprintf(buf, sizeof buf, "%.1f°C", c);
    return xstrdup(buf);
}

static char *get_memory_usage(double *out_percent) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) {
        if (out_percent) *out_percent = -1.0;
        return xstrdup("unknown");
    }

    long long mem_total_kb = 0;
    long long mem_avail_kb = 0;

    char line[256];
    while (fgets(line, sizeof line, f)) {
        char key[32];
        long long value = 0;
        char unit[32] = {0};

        if (sscanf(line, "%31s %lld %31s", key, &value, unit) != 3)
            continue;

        if (strcmp(key, "MemTotal:") == 0)
            mem_total_kb = value;
        else if (strcmp(key, "MemAvailable:") == 0)
            mem_avail_kb = value;
    }

    fclose(f);

    if (mem_total_kb <= 0) {
        if (out_percent) *out_percent = -1.0;
        return xstrdup("unknown");
    }
    if (mem_avail_kb <= 0)
        mem_avail_kb = mem_total_kb / 2;

    long long used_kb  = mem_total_kb - mem_avail_kb;
    double total_mib   = (double) mem_total_kb / 1024.0;
    double used_mib    = (double) used_kb / 1024.0;

    if (total_mib <= 0.0) {
        if (out_percent) *out_percent = -1.0;
        return xstrdup("unknown");
    }

    double percent = used_mib * 100.0 / total_mib;
    if (out_percent)
        *out_percent = percent;

    char buf[64];
    snprintf(buf, sizeof buf, "%.0fMiB / %.0fMiB (%.0f%%)",
             used_mib, total_mib, percent);
    return xstrdup(buf);
}

static char *get_swap_usage(double *out_percent) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) {
        if (out_percent) *out_percent = -1.0;
        return xstrdup("unknown");
    }

    long long swap_total_kb = 0;
    long long swap_free_kb  = -1;

    char line[256];
    while (fgets(line, sizeof line, f)) {
        char key[32];
        long long value = 0;
        char unit[32] = {0};

        if (sscanf(line, "%31s %lld %31s", key, &value, unit) != 3)
            continue;

        if (strcmp(key, "SwapTotal:") == 0)
            swap_total_kb = value;
        else if (strcmp(key, "SwapFree:") == 0)
            swap_free_kb = value;
    }

    fclose(f);

    if (swap_total_kb <= 0) {
        if (out_percent) *out_percent = -1.0;
        return xstrdup("disabled");
    }

    if (swap_free_kb < 0)
        swap_free_kb = 0;

    long long used_kb = swap_total_kb - swap_free_kb;
    double total_mib  = (double) swap_total_kb / 1024.0;
    double used_mib   = (double) used_kb / 1024.0;
    double percent    = (total_mib > 0.0) ? used_mib * 100.0 / total_mib : 0.0;

    if (out_percent)
        *out_percent = percent;

    char buf[64];
    snprintf(buf, sizeof buf, "%.0fMiB / %.0fMiB (%.0f%%)",
             used_mib, total_mib, percent);
    return xstrdup(buf);
}

/* ---------- disk + fs type ---------- */

static char *get_disk_root_usage(double *out_percent) {
    struct statvfs st;
    if (statvfs("/", &st) != 0) {
        if (out_percent) *out_percent = -1.0;
        return xstrdup("unknown");
    }

    unsigned long long total = (unsigned long long) st.f_blocks * st.f_frsize;
    unsigned long long used  = (unsigned long long) (st.f_blocks - st.f_bfree) * st.f_frsize;

    if (total == 0) {
        if (out_percent) *out_percent = -1.0;
        return xstrdup("unknown");
    }

    double total_gib = (double) total / (1024.0 * 1024.0 * 1024.0);
    double used_gib  = (double) used  / (1024.0 * 1024.0 * 1024.0);
    double percent   = used_gib * 100.0 / total_gib;

    if (out_percent)
        *out_percent = percent;

    char buf[64];
    snprintf(buf, sizeof buf, "%.1fGiB / %.1fGiB (%.0f%%)",
             used_gib, total_gib, percent);
    return xstrdup(buf);
}

static char *get_root_fs_type(void) {
    FILE *f = fopen("/proc/self/mounts", "r");
    if (!f)
        return xstrdup("unknown");

    char line[512];
    char dev[256], mnt[256], type[64];

    while (fgets(line, sizeof line, f)) {
        dev[0] = mnt[0] = type[0] = '\0';
        if (sscanf(line, "%255s %255s %63s", dev, mnt, type) != 3)
            continue;
        if (strcmp(mnt, "/") == 0) {
            fclose(f);
            return xstrdup(type);
        }
    }

    fclose(f);
    return xstrdup("unknown");
}

static char *get_root_mount_device(void) {
    FILE *f = fopen("/proc/self/mounts", "r");
    if (!f)
        return xstrdup("unknown");

    char line[512];
    char dev[256], mnt[256];
    while (fgets(line, sizeof line, f)) {
        dev[0] = mnt[0] = '\0';
        if (sscanf(line, "%255s %255s", dev, mnt) != 2)
            continue;
        if (strcmp(mnt, "/") == 0) {
            fclose(f);
            return xstrdup(dev);
        }
    }

    fclose(f);
    return xstrdup("unknown");
}

static char *guess_block_name_from_dev(const char *dev) {
    if (!dev || strncmp(dev, "/dev/", 5) != 0)
        return NULL;
    const char *name = dev + 5;

    DIR *dir = opendir("/sys/block");
    if (!dir)
        return NULL;

    struct dirent *ent;
    char *match = NULL;

    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;
        if (strstr(name, ent->d_name) != NULL) {
            match = xstrdup(ent->d_name);
            break;
        }
    }

    closedir(dir);
    return match;
}

static char *get_root_disk_type(void) {
    char *dev = get_root_mount_device();
    char *block = guess_block_name_from_dev(dev);
    char buf[128];

    if (!dev || strcmp(dev, "unknown") == 0 || !block) {
        snprintf(buf, sizeof buf, "unknown");
        if (dev) free(dev);
        if (block) free(block);
        return xstrdup(buf);
    }

    const char *type = NULL;

    if (strncmp(dev, "/dev/nvme", 9) == 0) {
        type = "NVMe SSD";
    } else if (strncmp(dev, "/dev/mmc", 8) == 0) {
        type = "eMMC/SD";
    } else if (strncmp(dev, "/dev/sd", 7) == 0 ||
        strncmp(dev, "/dev/vd", 7) == 0 ||
        strncmp(dev, "/dev/hd", 7) == 0) {
        char path[256];
    snprintf(path, sizeof path, "/sys/block/%s/queue/rotational", block);
    FILE *f = fopen(path, "r");
    if (f) {
        int rot = -1;
        if (fscanf(f, "%d", &rot) == 1) {
            if (rot == 0)
                type = "SSD";
            else if (rot == 1)
                type = "HDD";
        }
        fclose(f);
    }
        }

        if (!type) {
            if (strcmp(dev, "overlay") == 0)
                snprintf(buf, sizeof buf, "virtual (overlay)");
            else
                snprintf(buf, sizeof buf, "unknown (%s)", block);
        } else {
            snprintf(buf, sizeof buf, "%s (%s)", type, block);
        }

        free(dev);
        free(block);
        return xstrdup(buf);
}

/* ---------- battery ---------- */

static char *get_battery_status(void) {
    DIR *dir = opendir("/sys/class/power_supply");
    if (!dir)
        return xstrdup("none");

    struct dirent *ent;
    char *result = NULL;

    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;
        if (strncmp(ent->d_name, "BAT", 3) != 0)
            continue;

        char path_cap[512];
        char path_stat[512];
        snprintf(path_cap, sizeof path_cap,
                 "/sys/class/power_supply/%s/capacity", ent->d_name);
        snprintf(path_stat, sizeof path_stat,
                 "/sys/class/power_supply/%s/status", ent->d_name);

        char *cap_str  = read_first_line_trim(path_cap);
        char *stat_str = read_first_line_trim(path_stat);

        const char *ac_state = NULL;
        if (stat_str) {
            if (strcmp(stat_str, "Charging") == 0 ||
                strcmp(stat_str, "Full") == 0 ||
                strcmp(stat_str, "Not charging") == 0)
                ac_state = "on AC";
            else if (strcmp(stat_str, "Discharging") == 0)
                ac_state = "on battery";
        }

        if (cap_str && stat_str && ac_state) {
            char buf[160];
            snprintf(buf, sizeof buf, "%s%% (%s, %s)", cap_str, stat_str, ac_state);
            result = xstrdup(buf);
        } else if (cap_str && stat_str) {
            char buf[160];
            snprintf(buf, sizeof buf, "%s%% (%s)", cap_str, stat_str);
            result = xstrdup(buf);
        } else if (cap_str) {
            char buf[64];
            snprintf(buf, sizeof buf, "%s%%", cap_str);
            result = xstrdup(buf);
        }

        free(cap_str);
        free(stat_str);

        if (result)
            break;
    }

    closedir(dir);

    if (!result)
        result = xstrdup("none");

    return result;
}

/* ---------- packages ---------- */

static long count_dpkg_status(void) {
    FILE *f = fopen("/var/lib/dpkg/status", "r");
    if (!f)
        return -1;

    char line[512];
    long count = 0;

    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "Package:", 8) == 0)
            count++;
    }

    fclose(f);
    if (count <= 0)
        return -1;
    return count;
}

static long count_dir_entries(const char *path) {
    DIR *dir = opendir(path);
    if (!dir)
        return -1;

    struct dirent *ent;
    long count = 0;

    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;
        count++;
    }

    closedir(dir);
    if (count <= 0)
        return -1;
    return count;
}

static long count_pacman_local(void) {
    return count_dir_entries("/var/lib/pacman/local");
}

static long count_flatpak_apps(void) {
    long total = 0;
    bool any = false;

    long system = count_dir_entries("/var/lib/flatpak/app");
    if (system > 0) {
        total += system;
        any = true;
    }

    const char *home = getenv("HOME");
    if (home && *home) {
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/.local/share/flatpak/app", home);
        long user = count_dir_entries(path);
        if (user > 0) {
            total += user;
            any = true;
        }
    }

    if (!any)
        return -1;
    return total;
}

static char *get_package_summary(void) {
    long base_count = -1;
    const char *base_label = NULL;

    long dpkg_count   = count_dpkg_status();
    long pacman_count = count_pacman_local();

    if (dpkg_count > 0) {
        base_count  = dpkg_count;
        base_label  = "dpkg";
    } else if (pacman_count > 0) {
        base_count  = pacman_count;
        base_label  = "pacman";
    }

    long flatpak_count = count_flatpak_apps();

    char buf[256];
    buf[0] = '\0';
    int first = 1;

    if (base_count > 0 && base_label) {
        char part[64];
        snprintf(part, sizeof part, "%ld (%s)", base_count, base_label);
        strncat(buf, part, sizeof(buf) - strlen(buf) - 1);
        first = 0;
    }

    if (flatpak_count > 0) {
        char part[64];
        snprintf(part, sizeof part, "%ld (flatpak)", flatpak_count);

        if (!first && strlen(buf) < sizeof(buf) - 2)
            strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);

        strncat(buf, part, sizeof(buf) - strlen(buf) - 1);
        first = 0;
    }

    if (buf[0] == '\0')
        return xstrdup("unknown");
    return xstrdup(buf);
}

/* ---------- gcc ---------- */

static char *get_gcc_version(void) {
    FILE *pipe = popen("gcc --version 2>/dev/null", "r");
    if (!pipe)
        return xstrdup("not found");

    char buf[256];
    if (!fgets(buf, sizeof buf, pipe)) {
        pclose(pipe);
        return xstrdup("not found");
    }

    pclose(pipe);
    rstrip(buf);
    if (buf[0] == '\0')
        return xstrdup("not found");
    return xstrdup(buf);
}

/* ---------- load avg ---------- */

static char *get_loadavg(double *one_min) {
    FILE *f = fopen("/proc/loadavg", "r");
    if (!f) {
        if (one_min) *one_min = -1.0;
        return xstrdup("unknown");
    }
    double a, b, c;
    if (fscanf(f, "%lf %lf %lf", &a, &b, &c) != 3) {
        fclose(f);
        if (one_min) *one_min = -1.0;
        return xstrdup("unknown");
    }
    fclose(f);
    if (one_min)
        *one_min = a;
    char buf[64];
    snprintf(buf, sizeof buf, "%.2f %.2f %.2f", a, b, c);
    return xstrdup(buf);
}

/* ---------- GPU ---------- */

static const char *map_pci_vendor(const char *id) {
    if (!id) return "Unknown";
    if (strcmp(id, "0x8086") == 0) return "Intel";
    if (strcmp(id, "0x10de") == 0) return "NVIDIA";
    if (strcmp(id, "0x1002") == 0 || strcmp(id, "0x1022") == 0) return "AMD";
    return "Unknown";
}

static char *get_gpu_info(void) {
    DIR *dir = opendir("/sys/class/drm");
    if (!dir)
        return xstrdup("unknown");

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
        return xstrdup("unknown");

    char vendor_path[PATH_MAX], dev_path[PATH_MAX], driver_link_path[PATH_MAX];
    snprintf(vendor_path, sizeof vendor_path, "%s/vendor", device_path);
    snprintf(dev_path, sizeof dev_path, "%s/device", device_path);
    snprintf(driver_link_path, sizeof driver_link_path, "%s/driver", device_path);

    char *vendor_id = read_first_line_trim(vendor_path);
    char *device_id = read_first_line_trim(dev_path);

    char driver_target[PATH_MAX];
    driver_target[0] = '\0';
    ssize_t len = readlink(driver_link_path, driver_target, sizeof(driver_target) - 1);
    if (len > 0) {
        driver_target[len] = '\0';
    }

    const char *driver_name = NULL;
    if (driver_target[0]) {
        char *slash = strrchr(driver_target, '/');
        driver_name = slash ? slash + 1 : driver_target;
    }

    const char *vendor_name = map_pci_vendor(vendor_id);

    char buf[256];
    if (vendor_id && device_id && driver_name) {
        snprintf(buf, sizeof buf, "%s GPU (%s:%s, driver %s)",
                 vendor_name, vendor_id, device_id, driver_name);
    } else if (vendor_id && device_id) {
        snprintf(buf, sizeof buf, "%s GPU (%s:%s)",
                 vendor_name, vendor_id, device_id);
    } else if (vendor_id) {
        snprintf(buf, sizeof buf, "%s GPU", vendor_name);
    } else {
        snprintf(buf, sizeof buf, "unknown");
    }

    free(vendor_id);
    free(device_id);
    return xstrdup(buf);
}

/* ---------- display via DRM ---------- */

static char *get_display_info(void) {
    DIR *dir = opendir("/sys/class/drm");
    if (!dir)
        return xstrdup("unknown");

    struct dirent *ent;
    char desc[128];
    desc[0] = '\0';

    while ((ent = readdir(dir)) != NULL) {
        if (!strchr(ent->d_name, '-'))
            continue; /* skip card0 itself */

            char base[PATH_MAX];
        snprintf(base, sizeof base, "/sys/class/drm/%s", ent->d_name);

        char status_path[PATH_MAX];
        snprintf(status_path, sizeof status_path, "%s/status", base);
        char *status = read_first_line_trim(status_path);
        if (!status)
            continue;
        if (strcmp(status, "connected") != 0) {
            free(status);
            continue;
        }
        free(status);

        char modes_path[PATH_MAX];
        snprintf(modes_path, sizeof modes_path, "%s/modes", base);
        FILE *f = fopen(modes_path, "r");
        if (!f)
            continue;

        char mode[64];
        if (fgets(mode, sizeof mode, f)) {
            rstrip(mode);
            const char *kind = "display";
            if (strstr(ent->d_name, "eDP") || strstr(ent->d_name, "LVDS"))
                kind = "internal";
            else if (strstr(ent->d_name, "HDMI") || strstr(ent->d_name, "DP") || strstr(ent->d_name, "DVI"))
                kind = "external";
            snprintf(desc, sizeof desc, "%s (%s)", mode, kind);
            fclose(f);
            break;
        }
        fclose(f);
    }

    closedir(dir);

    if (!desc[0])
        return xstrdup("unknown");
    return xstrdup(desc);
}

/* ---------- network ---------- */

static char *get_network_info(void) {
    struct ifaddrs *ifaddr = NULL;
    if (getifaddrs(&ifaddr) == -1)
        return xstrdup("offline");

    struct ifaddrs *ifa;
    char result[128];
    result[0] = '\0';

    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr)
            continue;
        if (ifa->ifa_addr->sa_family != AF_INET)
            continue;

        const char *name = ifa->ifa_name;
        if (!name || strcmp(name, "lo") == 0)
            continue;

        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
        struct sockaddr_in *nm = (struct sockaddr_in *)ifa->ifa_netmask;

        char addr[INET_ADDRSTRLEN];
        if (!inet_ntop(AF_INET, &sa->sin_addr, addr, sizeof addr))
            continue;

        uint32_t m = ntohl(nm->sin_addr.s_addr);
        int prefix = 0;
        for (int i = 0; i < 32; ++i) {
            if (m & (1u << (31 - i)))
                prefix++;
            else
                break;
        }

        snprintf(result, sizeof result, "%s %s/%d", name, addr, prefix);
        break;
    }

    freeifaddrs(ifaddr);

    if (!result[0])
        return xstrdup("offline");
    return xstrdup(result);
}

/* ---------- locale + keyboard ---------- */

static char *get_locale_str(void) {
    const char *lang = getenv("LC_ALL");
    if (!lang || !*lang)
        lang = getenv("LC_CTYPE");
    if (!lang || !*lang)
        lang = getenv("LANG");
    if (!lang || !*lang)
        return xstrdup("unknown");
    return xstrdup(lang);
}

static char *get_keyboard_layout(void) {
    const char *env = getenv("XKB_DEFAULT_LAYOUT");
    if (env && *env)
        return xstrdup(env);

    env = getenv("XKB_LAYOUT");
    if (env && *env)
        return xstrdup(env);

    FILE *f = fopen("/etc/default/keyboard", "r");
    if (!f)
        return xstrdup("unknown");

    char *line = NULL;
    size_t cap = 0;
    char *layout = NULL;

    while (getline(&line, &cap, f) != -1) {
        rstrip(line);
        if (!line[0])
            continue;
        char *q = line;
        while (*q == ' ' || *q == '\t')
            q++;
        if (*q == '#')
            continue;

        const char *keys[] = { "XKBLAYOUT=", "XKB_LAYOUT=" };
        for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
            size_t klen = strlen(keys[i]);
            if (strncmp(q, keys[i], klen) == 0) {
                char *val = q + klen;
                while (*val == ' ' || *val == '\t')
                    val++;
                if (*val == '"' || *val == '\'') {
                    char quote = *val++;
                    char *end = strchr(val, quote);
                    if (end)
                        *end = '\0';
                }
                if (*val)
                    layout = xstrdup(val);
                break;
            }
        }
        if (layout)
            break;
    }

    free(line);
    fclose(f);

    if (!layout)
        return xstrdup("unknown");
    return layout;
}

/* ---------- processes + users ---------- */

static char *get_process_count(void) {
    DIR *dir = opendir("/proc");
    if (!dir)
        return xstrdup("unknown");

    struct dirent *ent;
    long count = 0;

    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;

        if (name[0] < '0' || name[0] > '9')
            continue;

        bool all_digits = true;
        for (const char *p = name; *p; ++p) {
            if (*p < '0' || *p > '9') {
                all_digits = false;
                break;
            }
        }
        if (all_digits)
            count++;
    }

    closedir(dir);

    if (count <= 0)
        return xstrdup("unknown");

    char buf[32];
    snprintf(buf, sizeof buf, "%ld", count);
    return xstrdup(buf);
}

static char *get_user_summary(void) {
    setutxent();
    struct utmpx *ut;
    char names[256];
    names[0] = '\0';
    int count = 0;

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

    if (count <= 0)
        return xstrdup("0");

    char buf[320];
    if (names[0])
        snprintf(buf, sizeof buf, "%d (%s)", count, names);
    else
        snprintf(buf, sizeof buf, "%d", count);
    return xstrdup(buf);
}

/* ---------- session type + WM + terminal app ---------- */

static char *read_comm_for_pid(pid_t pid) {
    char path[64];
    snprintf(path, sizeof path, "/proc/%ld/comm", (long)pid);
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    char buf[64];
    if (!fgets(buf, sizeof buf, f)) {
        fclose(f);
        return NULL;
    }
    fclose(f);
    rstrip(buf);
    return xstrdup(buf);
}

static pid_t get_ppid_of(pid_t pid) {
    char path[64];
    snprintf(path, sizeof path, "/proc/%ld/stat", (long)pid);
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    char buf[512];
    if (!fgets(buf, sizeof buf, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);

    char *lpar = strchr(buf, '(');
    char *rpar = strrchr(buf, ')');
    if (!lpar || !rpar || rpar <= lpar)
        return -1;

    char *p = rpar + 2;
    char state;
    int ppid = -1;
    if (sscanf(p, "%c %d", &state, &ppid) != 2)
        return -1;

    return (pid_t) ppid;
}

static bool looks_like_terminal_name(const char *comm) {
    const char *terms[] = {
        "konsole", "kitty", "alacritty", "gnome-terminal",
        "xterm", "xfce4-terminal", "tilix", "st", "urxvt",
        "wezterm", "foot", "rio", "termite", "hyper",
        "yakuake", "guake", "qterminal", "lxterminal",
        "kgx", "io.elementary.terminal", "deepin-terminal"
    };
    size_t n = sizeof(terms) / sizeof(terms[0]);
    for (size_t i = 0; i < n; ++i) {
        size_t len = strlen(terms[i]);
        if (strncmp(comm, terms[i], len) == 0)
            return true;
    }
    return false;
}

static char *guess_terminal_app(void) {
    const char *term_prog = getenv("TERM_PROGRAM");
    if (term_prog && *term_prog)
        return xstrdup(term_prog);

    pid_t pid = getppid();
    for (int depth = 0; depth < 3 && pid > 1; ++depth) {
        char *comm = read_comm_for_pid(pid);
        if (!comm)
            break;
        if (looks_like_terminal_name(comm))
            return comm;
        free(comm);

        pid = get_ppid_of(pid);
        if (pid <= 1)
            break;
    }

    const char *term = getenv("TERM");
    if (term && *term)
        return xstrdup(term);

    return xstrdup("unknown");
}

static char *get_session_type(void) {
    const char *xdg  = getenv("XDG_SESSION_TYPE");
    const char *way  = getenv("WAYLAND_DISPLAY");
    const char *disp = getenv("DISPLAY");

    if (xdg && *xdg)
        return xstrdup(xdg);
    if (way && *way)
        return xstrdup("wayland");
    if (disp && *disp)
        return xstrdup("x11");
    return xstrdup("tty");
}

static bool looks_like_wm_name(const char *comm) {
    const char *wms[] = {
        "kwin_x11", "kwin_wayland", "kwin",
        "mutter", "gnome-shell",
        "sway", "i3", "i3wm", "bspwm",
        "openbox", "fluxbox", "xmonad", "awesome",
        "herbstluftwm", "qtile", "marco", "metacity",
        "icewm", "enlightenment", "pekwm", "blackbox"
    };
    size_t n = sizeof(wms) / sizeof(wms[0]);
    for (size_t i = 0; i < n; ++i) {
        if (strcmp(comm, wms[i]) == 0)
            return true;
    }
    return false;
}

static char *normalize_wm_name(const char *comm) {
    if (strncmp(comm, "kwin", 4) == 0)
        return xstrdup("kwin");
    if (strcmp(comm, "gnome-shell") == 0 || strcmp(comm, "mutter") == 0)
        return xstrdup("mutter");
    return xstrdup(comm);
}

static char *get_window_manager(const char *session_type) {
    char type_buf[16];
    const char *type = (session_type && *session_type) ? session_type : "unknown";

    size_t i;
    for (i = 0; i < sizeof(type_buf) - 1 && type[i]; ++i)
        type_buf[i] = (char)tolower((unsigned char)type[i]);
    type_buf[i] = '\0';

    if (strcmp(type_buf, "tty") == 0)
        return xstrdup("none (tty)");

    DIR *dir = opendir("/proc");
    if (!dir) {
        char buf[64];
        snprintf(buf, sizeof buf, "unknown (%s)", type_buf);
        return xstrdup(buf);
    }

    struct dirent *ent;
    char *found = NULL;

    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;

        if (name[0] < '0' || name[0] > '9')
            continue;

        bool all_digits = true;
        for (const char *p = name; *p; ++p) {
            if (*p < '0' || *p > '9') {
                all_digits = false;
                break;
            }
        }
        if (!all_digits)
            continue;

        pid_t pid = (pid_t)strtol(name, NULL, 10);
        if (pid <= 1)
            continue;

        char *comm = read_comm_for_pid(pid);
        if (!comm)
            continue;

        if (looks_like_wm_name(comm)) {
            found = normalize_wm_name(comm);
            free(comm);
            break;
        }

        free(comm);
    }

    closedir(dir);

    char buf[128];

    if (found) {
        snprintf(buf, sizeof buf, "%s (%s)", found, type_buf);
        free(found);
        return xstrdup(buf);
    } else {
        snprintf(buf, sizeof buf, "unknown (%s)", type_buf);
        return xstrdup(buf);
    }
}

static char *get_terminal_display(const char *term_env, const char *tty_path) {
    char *app = guess_terminal_app();

    const char *term = (term_env && *term_env) ? term_env : "unknown";
    const char *tty  = (tty_path && *tty_path && strcmp(tty_path, "not a tty") != 0)
    ? tty_path
    : NULL;

    char buf[256];

    if (app && *app && strcmp(app, term) != 0 && strcmp(app, "unknown") != 0) {
        if (tty)
            snprintf(buf, sizeof buf, "%s (TERM=%s, %s)", app, term, tty);
        else
            snprintf(buf, sizeof buf, "%s (TERM=%s)", app, term);
    } else {
        if (tty && strncmp(tty, "/dev/tty", 8) == 0) {
            snprintf(buf, sizeof buf, "Linux TTY (%s, TERM=%s)", tty, term);
        } else if (tty) {
            snprintf(buf, sizeof buf, "%s (%s)", term, tty);
        } else {
            snprintf(buf, sizeof buf, "%s", term);
        }
    }

    free(app);
    return xstrdup(buf);
}

/* ---------- fonts ---------- */

static char *parse_font_spec(const char *spec) {
    if (!spec || !*spec)
        return xstrdup("unknown");

    char *tmp = xstrdup(spec);
    char *save = NULL;
    char *family = strtok_r(tmp, ",", &save);
    char *size_str = strtok_r(NULL, ",", &save);

    if (!family) {
        free(tmp);
        return xstrdup("unknown");
    }

    while (*family == ' ' || *family == '\t')
        family++;
    char *end = family + strlen(family);
    while (end > family && (end[-1] == ' ' || end[-1] == '\t'))
        *--end = '\0';

    char result[256];

    if (size_str) {
        while (*size_str == ' ' || *size_str == '\t')
            size_str++;
        char *p = size_str;
        while (*p && isdigit((unsigned char)*p))
            p++;
        *p = '\0';
        if (*size_str)
            snprintf(result, sizeof result, "%s (%spt)", family, size_str);
        else
            snprintf(result, sizeof result, "%s", family);
    } else {
        snprintf(result, sizeof result, "%s", family);
    }

    free(tmp);
    return xstrdup(result);
}

static char *get_fc_match_font(const char *pattern) {
    char cmd[128];
    if (pattern && *pattern)
        snprintf(cmd, sizeof cmd, "fc-match %s 2>/dev/null", pattern);
    else
        snprintf(cmd, sizeof cmd, "fc-match 2>/dev/null");

    FILE *pipe = popen(cmd, "r");
    if (!pipe)
        return NULL;

    char buf[256];
    if (!fgets(buf, sizeof buf, pipe)) {
        pclose(pipe);
        return NULL;
    }
    pclose(pipe);
    rstrip(buf);
    if (!buf[0])
        return NULL;

    char *colon = strchr(buf, ':');
    char *p = buf;
    if (colon) {
        p = colon + 1;
        while (*p == ' ' || *p == '\t')
            p++;
    }

    char *style = strstr(p, ":style=");
    if (style)
        *style = '\0';

    rstrip(p);
    if (!*p)
        return NULL;

    if (*p == '"') {
        char *p2 = p + 1;
        char *end1 = strchr(p2, '"');
        if (end1) {
            *end1 = '\0';
            const char *family = p2;

            char *after = end1 + 1;
            while (*after == ' ' || *after == '\t')
                after++;

            const char *style_name = NULL;
            if (*after == '"') {
                after++;
                char *end2 = strchr(after, '"');
                if (end2) {
                    *end2 = '\0';
                    style_name = after;
                }
            }

            char out[256];
            if (style_name && *style_name)
                snprintf(out, sizeof out, "%s (%s)", family, style_name);
            else
                snprintf(out, sizeof out, "%s", family);
            return xstrdup(out);
        }
    }

    return xstrdup(p);
}

static char *get_system_font(void) {
    const char *home = getenv("HOME");
    if (!home || !*home) {
        char *fc = get_fc_match_font(NULL);
        return fc ? fc : xstrdup("unknown");
    }

    const char *rel_paths[] = {
        "/.config/kdeglobals",
        "/.config/kdeglobals6",
        "/.config/kdeglobals5"
    };

    char path[PATH_MAX];
    char *font_spec = NULL;

    for (size_t i = 0; i < sizeof(rel_paths) / sizeof(rel_paths[0]); ++i) {
        snprintf(path, sizeof path, "%s%s", home, rel_paths[i]);
        FILE *f = fopen(path, "r");
        if (!f)
            continue;

        char *line = NULL;
        size_t cap = 0;
        bool in_general = false;

        while (getline(&line, &cap, f) != -1) {
            rstrip(line);
            if (line[0] == '\0')
                continue;

            char *q = line;
            while (*q == ' ' || *q == '\t')
                q++;
            if (*q == '#' || *q == ';')
                continue;

            if (*q == '[') {
                in_general = (strcmp(q, "[General]") == 0 ||
                strcmp(q, "[KDE]") == 0);
                continue;
            }
            if (!in_general)
                continue;

            char *eq = strchr(q, '=');
            if (!eq)
                continue;

            *eq = '\0';
            char *key = q;
            char *val = eq + 1;
            while (*val == ' ' || *val == '\t')
                val++;

            if (!*val)
                continue;

            char keybuf[64];
            size_t klen = strlen(key);
            if (klen >= sizeof keybuf)
                klen = sizeof keybuf - 1;
            for (size_t j = 0; j < klen; ++j)
                keybuf[j] = (char)tolower((unsigned char)key[j]);
            keybuf[klen] = '\0';

            if (strcmp(keybuf, "font") == 0 ||
                strcmp(keybuf, "generalfont") == 0 ||
                strcmp(keybuf, "menufont") == 0 ||
                strcmp(keybuf, "activefont") == 0) {
                font_spec = xstrdup(val);
            break;
                }
        }

        free(line);
        fclose(f);

        if (font_spec)
            break;
    }

    if (font_spec) {
        char *res = parse_font_spec(font_spec);
        free(font_spec);
        return res;
    }

    char *fc = get_fc_match_font(NULL);
    if (fc)
        return fc;

    return xstrdup("unknown");
}

static char *read_konsole_font_from_profile(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;

    char *line = NULL;
    size_t cap = 0;
    char *font_spec = NULL;

    while (getline(&line, &cap, f) != -1) {
        rstrip(line);
        if (line[0] == '\0')
            continue;
        char *q = line;
        while (*q == ' ' || *q == '\t')
            q++;
        const char *key = "Font=";
        size_t klen = strlen(key);
        if (strncmp(q, key, klen) == 0) {
            const char *val = q + klen;
            while (*val == ' ' || *val == '\t')
                val++;
            if (*val)
                font_spec = xstrdup(val);
            break;
        }
    }

    free(line);
    fclose(f);

    if (!font_spec)
        return NULL;

    char *res = parse_font_spec(font_spec);
    free(font_spec);
    return res;
}

static char *get_terminal_font(void) {
    const char *home = getenv("HOME");
    if (!home || !*home) {
        goto fc_fallback;
    }

    char *app = guess_terminal_app();
    bool is_konsole = (app && strncmp(app, "konsole", 7) == 0);
    free(app);

    if (!is_konsole)
        goto fc_fallback;

    char profile_name[PATH_MAX];
    profile_name[0] = '\0';

    const char *pe = getenv("KONSOLE_PROFILE_NAME");
    if (!pe || !*pe)
        pe = getenv("KONSOLE_PROFILE");
    if (pe && *pe) {
        snprintf(profile_name, sizeof profile_name, "%s", pe);
    }

    if (profile_name[0] == '\0') {
        char rcpath[PATH_MAX];
        snprintf(rcpath, sizeof rcpath, "%s/.config/konsolerc", home);
        FILE *f = fopen(rcpath, "r");
        if (f) {
            char *line = NULL;
            size_t cap = 0;
            while (getline(&line, &cap, f) != -1) {
                rstrip(line);
                if (line[0] == '\0')
                    continue;
                char *q = line;
                while (*q == ' ' || *q == '\t')
                    q++;
                const char *key = "DefaultProfile=";
                size_t klen = strlen(key);
                if (strncmp(q, key, klen) == 0) {
                    const char *val = q + klen;
                    while (*val == ' ' || *val == '\t')
                        val++;
                    if (*val) {
                        snprintf(profile_name, sizeof profile_name, "%s", val);
                    }
                    break;
                }
            }
            free(line);
            fclose(f);
        }
    }

    if (profile_name[0] != '\0') {
        char p1[PATH_MAX], p2[PATH_MAX];
        snprintf(p1, sizeof p1, "%s/.local/share/konsole/%s", home, profile_name);
        snprintf(p2, sizeof p2, "%s/.local/share/konsole/%s.profile", home, profile_name);

        char *f1 = read_konsole_font_from_profile(p1);
        if (!f1)
            f1 = read_konsole_font_from_profile(p2);
        if (f1)
            return f1;
    }

    {
        char dirpath[PATH_MAX];
        snprintf(dirpath, sizeof dirpath, "%s/.local/share/konsole", home);
        DIR *d = opendir(dirpath);
        if (d) {
            struct dirent *ent;
            char first_profile[PATH_MAX];
            first_profile[0] = '\0';

            while ((ent = readdir(d)) != NULL) {
                if (ent->d_name[0] == '.')
                    continue;
                const char *dot = strrchr(ent->d_name, '.');
                if (dot && strcmp(dot, ".profile") == 0) {
                    snprintf(first_profile, sizeof first_profile,
                             "%s/%s", dirpath, ent->d_name);
                    break;
                }
            }
            closedir(d);

            if (first_profile[0] != '\0') {
                char *res = read_konsole_font_from_profile(first_profile);
                if (res)
                    return res;
            }
        }
    }

    fc_fallback:
    {
        char *fc = get_fc_match_font("monospace");
        if (fc)
            return fc;
    }
    return xstrdup("unknown");
}

/* ---------- bars ---------- */

static char *make_bar(double percent, int width) {
    if (width < 1) width = 10;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    int filled = (int)((percent / 100.0) * width + 0.5);

    char *buf = malloc((size_t)width + 3);
    if (!buf) die("malloc");

    buf[0] = '[';
    for (int i = 0; i < width; ++i)
        buf[1 + i] = (i < filled) ? '=' : ' ';
    buf[1 + width] = ']';
    buf[2 + width] = '\0';

    return buf;
}

/* ---------- UI helpers ---------- */

static void print_header(const char *user, const char *host) {
    char buf[256];
    snprintf(buf, sizeof buf, "%s@%s", user, host);

    char ver[128];
    snprintf(ver, sizeof ver, "YAFP v%s — yet another fetch program", YAFP_VERSION);

    if (g_use_color) {
        fputs("\033[1;38;5;45m", stdout);
        fputs(buf, stdout);
        fputs("\033[0m\n", stdout);

        fputs("\033[38;5;240m", stdout);
        fputs(ver, stdout);
        fputs("\033[0m\n", stdout);
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

static void print_section(const char *name) {
    if (g_minimal)
        return;

    putchar('\n');
    if (g_use_color) {
        fputs("\033[38;5;117m", stdout);
        printf("[ %s ]\n", name);
        fputs("\033[0m", stdout);
    } else {
        printf("[ %s ]\n", name);
    }
}

static void print_kv(const char *label, const char *value) {
    if (!value)
        value = "";

    if (g_use_color) {
        fputs("  ", stdout);
        fputs("\033[38;5;213m", stdout);
        fprintf(stdout, "%-*s", LABEL_WIDTH, label);
        fputs("\033[0m : ", stdout);
        fputs("\033[38;5;81m", stdout);
        fputs(value, stdout);
        fputs("\033[0m\n", stdout);
    } else {
        printf("  %-*s : %s\n", LABEL_WIDTH, label, value);
    }
}

/* ---------- help ---------- */

static void print_help(const char *prog) {
    printf("yafp %s - Yet Another Fetch Program\n\n", YAFP_VERSION);
    printf("Usage: %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  -h, --help       Show this help\n");
    printf("  -v, --version    Show version\n");
    printf("      --no-color   Disable ANSI colors\n");
    printf("      --plain      Plain mode (no colors, full info)\n");
    printf("      --minimal    Only core fields (OS, kernel, uptime, memory)\n");
}

/* ---------- main ---------- */

int main(int argc, char **argv) {
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
            g_minimal   = false;
        } else if (strcmp(arg, "--minimal") == 0) {
            g_minimal = true;
        } else {
            fprintf(stderr, "%s: unknown option '%s'\n", progname, arg);
            fprintf(stderr, "Try '%s --help'\n", progname);
            return 1;
        }
    }

    /* collect all info */

    char *user         = get_username();
    char *host         = get_hostname_simple();
    char *os_name      = get_os_pretty_name();
    char *host_model   = get_host_model();
    char *kernel       = get_kernel_release();
    char *arch         = get_architecture();

    long  uptime_secs  = 0;
    char *uptime       = format_uptime(&uptime_secs);
    time_t now         = 0;
    char *run_time     = get_run_datetime(&now);
    char *boot_time    = format_boot_time(now, uptime_secs);

    double load1 = -1.0;
    char *loadavg      = get_loadavg(&load1);

    char *shell        = get_shell_name();
    char *term_env     = get_term_env();
    char *tty_size     = get_tty_size();
    char *tty_path     = get_tty_path();
    char *de_session   = get_de_session();

    long cpu_threads   = 0;
    char *cpu_info     = get_cpu_info(&cpu_threads);
    char *cpu_gov      = get_cpu_governor();
    char *cpu_temp     = get_cpu_temp();

    double mem_pct  = -1.0, swap_pct = -1.0, disk_pct = -1.0;
    char *mem_usage    = get_memory_usage(&mem_pct);
    char *swap_usage   = get_swap_usage(&swap_pct);
    char *disk_usage   = get_disk_root_usage(&disk_pct);
    char *fs_type      = get_root_fs_type();
    char *disk_type    = get_root_disk_type();

    char *proc_count   = get_process_count();
    char *pkg_summary  = get_package_summary();
    char *battery      = get_battery_status();
    char *gcc_version  = get_gcc_version();
    char *session_type = get_session_type();
    char *wm_display   = get_window_manager(session_type);
    char *term_disp    = get_terminal_display(term_env, tty_path);
    char *sys_font     = get_system_font();
    char *term_font    = get_terminal_font();
    char *gpu_info     = get_gpu_info();
    char *display_info = get_display_info();
    char *net_info     = get_network_info();
    char *locale_str   = get_locale_str();
    char *user_summary = get_user_summary();
    char *kbd_layout   = get_keyboard_layout();

    if (strcmp(user_summary, "0") == 0) {
        free(user_summary);
        char buf[128];
        snprintf(buf, sizeof buf, "1 (%s)", user);
        user_summary = xstrdup(buf);
    }

    double cpu_pct = -1.0;
    if (cpu_threads < 1) cpu_threads = 1;
    if (load1 >= 0.0) {
        cpu_pct = (load1 / (double)cpu_threads) * 100.0;
        if (cpu_pct < 0.0) cpu_pct = 0.0;
        if (cpu_pct > 100.0) cpu_pct = 100.0;
    }

    char *cpu_bar  = (cpu_pct  >= 0.0) ? make_bar(cpu_pct, 20)  : xstrdup("");
    char *mem_bar  = (mem_pct  >= 0.0) ? make_bar(mem_pct, 20)  : xstrdup("");
    char *swap_bar = (swap_pct >= 0.0) ? make_bar(swap_pct, 20) : xstrdup("");
    char *disk_bar = (disk_pct >= 0.0) ? make_bar(disk_pct, 20) : xstrdup("");

    print_header(user, host);

    if (g_minimal) {
        print_kv("OS",      os_name);
        print_kv("Kernel",  kernel);
        print_kv("Uptime",  uptime);
        print_kv("Memory",  mem_usage);
    } else {
        print_section("System");
        print_kv("OS",        os_name);
        print_kv("Host",      host_model);
        print_kv("Kernel",    kernel);
        print_kv("Arch",      arch);

        char up_line[160];
        snprintf(up_line, sizeof up_line, "%s (boot %s)", uptime, boot_time);
        print_kv("Uptime",    up_line);
        print_kv("Run time",  run_time);
        print_kv("Load avg",  loadavg);

        if (cpu_bar[0]) {
            char cpu_load_line[160];
            if (cpu_pct >= 0.0)
                snprintf(cpu_load_line, sizeof cpu_load_line, "~%.0f%% %s", cpu_pct, cpu_bar);
            else
                snprintf(cpu_load_line, sizeof cpu_load_line, "%s", cpu_bar);
            print_kv("CPU load", cpu_load_line);
        }

        print_kv("Packages",  pkg_summary);
        print_kv("Processes", proc_count);
        print_kv("Users",     user_summary);
        print_kv("Locale",    locale_str);
        print_kv("GCC",       gcc_version);

        print_section("Hardware");
        print_kv("CPU",        cpu_info);
        print_kv("CPU gov",    cpu_gov);
        print_kv("CPU temp",   cpu_temp);
        print_kv("GPU",        gpu_info);
        print_kv("Display",    display_info);

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

        char fs_line[160];
        snprintf(fs_line, sizeof fs_line, "%s, %s", fs_type, disk_type);
        print_kv("FS / Disk", fs_line);

        print_kv("Network",    net_info);
        print_kv("Battery",    battery);

        print_section("Session");
        print_kv("Shell",      shell);
        print_kv("Terminal",   term_disp);
        print_kv("WM",         wm_display);
        print_kv("DE / Sess",  de_session);
        print_kv("Font (sys)", sys_font);
        print_kv("Font (term)",term_font);
        print_kv("Keyboard",   kbd_layout);

        char tty_line[160];
        snprintf(tty_line, sizeof tty_line, "%s (%s)", tty_path, tty_size);
        print_kv("TTY",        tty_line);
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
    free(cpu_info);
    free(cpu_gov);
    free(cpu_temp);
    free(mem_usage);
    free(swap_usage);
    free(disk_usage);
    free(fs_type);
    free(disk_type);
    free(proc_count);
    free(pkg_summary);
    free(battery);
    free(gcc_version);
    free(session_type);
    free(wm_display);
    free(term_disp);
    free(sys_font);
    free(term_font);
    free(gpu_info);
    free(display_info);
    free(net_info);
    free(locale_str);
    free(user_summary);
    free(kbd_layout);
    free(cpu_bar);
    free(mem_bar);
    free(swap_bar);
    free(disk_bar);

    return 0;
}
