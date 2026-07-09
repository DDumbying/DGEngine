#include "rules.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "log.h"

#define RULES_FILE "rules.def"

void rules_clear(GameRules *r) {
    memset(r, 0, sizeof(*r));
}

bool rules_load(GameRules *r) {
    rules_clear(r);

    FILE *f = fopen(RULES_FILE, "r");
    if (!f) return false;

    char line[512];
    while (fgets(line, sizeof line, f)) {
        /* strip trailing whitespace/newline */
        char *end = line + strlen(line) - 1;
        while (end >= line && isspace((unsigned char)*end)) *end-- = '\0';

        /* skip blanks and comments */
        if (line[0] == '\0' || line[0] == '#') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        /* trim leading whitespace on value */
        while (*val && isspace((unsigned char)*val)) val++;

        if (strcmp(key, "win_script") == 0)
            snprintf(r->win_script, sizeof r->win_script, "%s", val);
        else if (strcmp(key, "lose_script") == 0)
            snprintf(r->lose_script, sizeof r->lose_script, "%s", val);
        else if (strcmp(key, "win_message") == 0)
            snprintf(r->win_message, sizeof r->win_message, "%s", val);
        else if (strcmp(key, "lose_message") == 0)
            snprintf(r->lose_message, sizeof r->lose_message, "%s", val);
    }

    fclose(f);
    r->loaded = true;
    LOG_INFO("Rules loaded: win='%s' lose='%s'", r->win_script, r->lose_script);
    return true;
}

bool rules_save(const GameRules *r) {
    FILE *f = fopen(RULES_FILE, "w");
    if (!f) {
        LOG_ERROR("rules_save: cannot write '%s'", RULES_FILE);
        return false;
    }
    fprintf(f, "# DGEngine rules definition\n");
    fprintf(f, "win_script=%s\n", r->win_script);
    fprintf(f, "lose_script=%s\n", r->lose_script);
    fprintf(f, "win_message=%s\n", r->win_message);
    fprintf(f, "lose_message=%s\n", r->lose_message);
    fclose(f);
    return true;
}
