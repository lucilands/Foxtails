#include <config.h>
#include <clog.h>

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>



config_t config_parse(const char *restrict path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0)  {
        clog(CLOG_WARNING, "Failed to open config file %s", path);
        return (config_t) {};
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        clog(CLOG_WARNING, "Failed to get file size of file %s", path);
        close(fd);
        return (config_t) {};
    }

    if (sb.st_size == 0) {
        clog(CLOG_WARNING, "Config file %s is empty", path);
        close(fd);
        return (config_t) {};
    }

    config_t ret = {0};
    ret.config_file.size = sb.st_size;
    ret.config_file.path = strdup(path);

    ret.config_file.map = mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (ret.config_file.map == MAP_FAILED) {
        clog(CLOG_WARNING, "Failed to map file");
        close(fd);
        return (config_t) {};
    }

    ret.config_file.fd = fd;

    return ret;
}

int numdigits(int num) {
    int count = 0;
    do {
        count++;
        num /= 10;
    } while (num != 0);
    
    return count;
}


char* config_get(config_t conf, char *section, char *key) {
    char *line_start = (char*)conf.config_file.map;
    char *line_end = line_start;
    char *line = NULL;
    size_t line_no = 0;
    bool found_section = false;
    while ((line_end = memchr(line_start, '\n', conf.config_file.size - (line_start - conf.config_file.map)))) {
        line_no++;
        int line_len = line_end - line_start;
        
        if (line_len == 0 || line_start[0] == '#'){
            line_start = line_end+1;
            continue;
        }

        line = strndup(line_start, line_len);
        line_start = line_end+1;

        if (found_section) {
            if (line[0] == '[') {
                clog(CLOG_WARNING, "Could not find config key %s/%s.%s", conf.config_file.path, section, key);
                free(line);
                return NULL;
            }

            char *key_end = strpbrk(line, " =");
            if (!key_end) {
                clog(CLOG_WARNING, "Invalid syntax on line %i, not a valid key-value pair", line_no);
                free(line);
                return NULL;
            }

            if (strncmp(line, key, key_end - line) == 0) {
                while (isspace(*key_end) || *key_end == ' ') key_end++;
                char *value_end = strchr(key_end + 1, '\0');
                if (!value_end) {
                    clog(CLOG_WARNING, "Invalid syntax on line %i, not a valid key-value pair", line_no);
                    free(line);
                    return NULL;
                }

                char *value = strndup(key_end+1, value_end - (key_end+1));
                free(line);
                //clog(CLOG_DEBUG, "Found config value %s/%s.%s = %s", conf.config_file.path, section, key, value);
                return value;
            }
            free(line);
            continue;
        }

        if (line[0] == '[') {
            char *section_name_end = strchr(line, ']');
            if (!section_name_end) {
                clog(CLOG_WARNING, "Invalid syntax in line %i, a section name was opened but never closed.\n%s:%i\t%s\n%*s^ Consider adding a ']'",
                        line_no, conf.config_file.path, line_no, line, strlen(line) + strlen(conf.config_file.path) + 1 + numdigits(line_no), "");
                free(line);
                return NULL;
            }


            if (strncmp(line+1, section, section_name_end - line - 1) == 0) {
                found_section = true;
            }
        }

        free(line);
    }

    clog(CLOG_WARNING, "Could not find config key %s/%s.%s", conf.config_file.path, section, key);
    return NULL;
}

void config_delete(config_t conf) {
    free((void*)conf.config_file.path);
    munmap((void*)conf.config_file.map, conf.config_file.size);
    close(conf.config_file.fd);
}

int config_get_int(config_t conf, char *section, char *key) {
    char *val = config_get(conf, section, key);
    if (!val) return 0;
    int value = strtol(val, NULL, 10);

    free(val);
    return value;
}
