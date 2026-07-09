#ifndef DGE_RULES_H
#define DGE_RULES_H

/*  Phase 5 — Win/Lose/Goal system.

    A project's rules.def defines the scripts that decide when the game
    is won or lost. These are standalone Lua files (not entity-bound
    behaviors) that return true when their condition is met.

    rules.def format (same key=value as project.dge):
        win_script=scripts/win.lua
        lose_script=scripts/lose.lua
        win_message=You gathered enough resources!
        lose_message=Your colony has perished.

    Both scripts are optional — a project with no rules.def (or with
    empty/missing entries) simply never triggers a win or lose, which
    is the correct behavior for a sandbox with no objectives.

    Scripts are evaluated once per simulation tick during Play mode.
    They receive the full dge.* API (resources, entities, properties)
    and return true to signal the condition is met. The engine calls
    the win script first; if it returns true, the game is won regardless
    of the lose script's state. If the win script doesn't fire but the
    lose script returns true, the game is lost.                          */

#include <stdbool.h>

#define RULES_PATH_MAX  256
#define RULES_MSG_MAX   128

typedef struct {
    char win_script [RULES_PATH_MAX];   /* path to win condition .lua   */
    char lose_script[RULES_PATH_MAX];   /* path to lose condition .lua  */
    char win_message [RULES_MSG_MAX];   /* overlay text on win          */
    char lose_message[RULES_MSG_MAX];   /* overlay text on lose         */
    bool loaded;                        /* true if rules.def was found  */
} GameRules;

/*  Load rules from <cwd>/rules.def into *r.
    If the file doesn't exist, *r is zeroed and r->loaded = false.
    Returns true if the file was found and parsed.                      */
bool rules_load(GameRules *r);

/*  Save current rules to <cwd>/rules.def.                              */
bool rules_save(const GameRules *r);

/*  Reset *r to empty (no scripts, no messages).                        */
void rules_clear(GameRules *r);

#endif /* DGE_RULES_H */
