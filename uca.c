#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <time.h>
#include "xxhash.h"

// Hash table size
#define HASH_SIZE (1 << 18)

// Structures
typedef struct {
    uint32_t codepoints[8];
    size_t cp_len;
    uint16_t primary[8];
    uint16_t secondary[8];
    uint16_t tertiary[8];
    size_t weight_count;
    char comment[256];
} collation_element_t;

typedef struct {
    uint32_t codepoint;
    uint32_t open_bracket;
    uint32_t close_bracket;
} bidi_bracket_t;

typedef struct {
    uint32_t codepoint;
    uint32_t mirror;
} bidi_mirror_t;

typedef struct {
    uint32_t codepoint;
    uint8_t joining_type; // L, R, U, T, D, C
} arabic_shaping_t;

typedef struct {
    uint32_t start;
    uint32_t end;
    uint8_t property;
} range_property_t;

typedef struct {
    uint32_t start;
    uint32_t end;
    char name[64];
} block_t;



// Property enums (simplified, extend as needed)
enum grapheme_break_prop {
    GBP_OTHER, GBP_CR, GBP_LF, GBP_CONTROL, GBP_EXTEND, GBP_PREPEND, GBP_SPACINGMARK,
    GBP_L, GBP_V, GBP_T, GBP_LV, GBP_LVT, GBP_RI, GBP_ZWJ, GBP_EB, GBP_EM, GBP_GAZ
};

enum word_break_prop {
    WBP_OTHER, WBP_CR, WBP_LF, WBP_NEWLINE, WBP_EXTEND, WBP_KATAKANA, WBP_ALETTER,
    WBP_MIDLETTER, WBP_MIDNUM, WBP_MIDNUMLET, WBP_NUMERIC, WBP_EB, WBP_EM, WBP_RI
};

enum sentence_break_prop {
    SBP_OTHER, SBP_CR, SBP_LF, SBP_SEP, SBP_SP, SBP_LOWER, SBP_UPPER, SBP_OLETTER,
    SBP_NUMERIC, SBP_ATERM, SBP_STERM, SBP_CLOSE, SBP_SContinue
};

enum line_break_prop {
    LBP_BK, LBP_CR, LBP_LF, LBP_CM, LBP_SG, LBP_GL, LBP_CB, LBP_SP, LBP_ZW, LBP_NL,
    LBP_WJ, LBP_JL, LBP_JV, LBP_JT, LBP_H2, LBP_H3, LBP_XX, LBP_OP, LBP_CL, LBP_CP,
    LBP_QU, LBP_NS, LBP_EX, LBP_SY, LBP_IS, LBP_PR, LBP_PO, LBP_NU, LBP_AL, LBP_ID,
    LBP_IN, LBP_HY, LBP_BA, LBP_BB, LBP_B2, LBP_ZWJ, LBP_EB, LBP_EM, LBP_AI, LBP_CJ
};

// Global data tables
static collation_element_t collation_table[HASH_SIZE] = {0};
static bidi_bracket_t bidi_brackets[1000] = {0};
static bidi_mirror_t bidi_mirrors[1000] = {0};
static arabic_shaping_t arabic_shaping[HASH_SIZE] = {0};
static range_property_t grapheme_breaks[10000] = {0};
static range_property_t word_breaks[10000] = {0};
static range_property_t sentence_breaks[10000] = {0};
static range_property_t line_breaks[10000] = {0};
static uint32_t case_folding[HASH_SIZE] = {0};
static uint8_t scripts[HASH_SIZE] = {0};
static uint32_t east_asian_width[HASH_SIZE] = {0};
static uint32_t prop_list[HASH_SIZE] = {0};
static uint32_t derived_core_props[HASH_SIZE] = {0};
static block_t block_table[1000]; 
static uint32_t cjk_radicals[1000] = {0};
static uint32_t derived_age[HASH_SIZE] = {0};
static uint8_t bidi_class[HASH_SIZE] = {0};
static uint8_t binary_props[HASH_SIZE] = {0};
static uint8_t combining_class[HASH_SIZE] = {0};
static uint8_t decomp_type[HASH_SIZE] = {0};
static uint8_t general_category[HASH_SIZE] = {0};
static uint8_t joining_group[HASH_SIZE] = {0};
static uint8_t joining_type[HASH_SIZE] = {0};
static char derived_names[HASH_SIZE][32] = {{0}};
static uint8_t numeric_type[HASH_SIZE] = {0};
static double numeric_values[HASH_SIZE] = {0};
static uint32_t hangul_syllable_type[HASH_SIZE] = {0};
static uint32_t indic_pos_category[HASH_SIZE] = {0};
static uint32_t indic_syl_category[HASH_SIZE] = {0};
static uint32_t jamo_short_name[256] = {0};
static uint32_t script_extensions[HASH_SIZE] = {0};
static uint32_t vertical_orientation[HASH_SIZE] = {0};

// Sizes
static size_t collation_size = 0, bracket_size = 0, mirror_size = 0, shaping_size = 0;
static size_t gbreak_size = 0, wbreak_size = 0, sbreak_size = 0, lbreak_size = 0;
static size_t folding_size = 0, script_size = 0, eaw_size = 0, prop_list_size = 0, dcp_size = 0;
static size_t block_size = 0, cjk_radical_size = 0, age_size = 0, bidi_class_size = 0;
static size_t binary_prop_size = 0, comb_class_size = 0, decomp_type_size = 0, gen_cat_size = 0;
static size_t join_group_size = 0, join_type_size = 0, name_size = 0, num_type_size = 0, num_value_size = 0;
static size_t hangul_size = 0, indic_pos_size = 0, indic_syl_size = 0, jamo_size = 0;
static size_t script_ext_size = 0, vert_orient_size = 0;

// Forward declarations
static inline uint32_t parse_hex(const char* str, size_t len);
static inline void parse_range(const char* range, uint32_t* start, uint32_t* end);

static inline uint32_t parse_hex(const char* str, size_t len) {
    uint32_t result = 0;
    for (size_t i = 0; i < len && str[i]; i++) {
        result <<= 4;
        char c = str[i];
        if (c >= '0' && c <= '9') result |= c - '0';
        else if (c >= 'A' && c <= 'F') result |= c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') result |= c - 'a' + 10;
    }
    return result;
}

static inline void parse_range(const char* range, uint32_t* start, uint32_t* end) {
    while (isspace(*range)) range++; // Skip leading whitespace
    const char* dotdot = strstr(range, "..");
    if (dotdot) {
        // Range case: e.g., "0000..001F"
        *start = parse_hex(range, dotdot - range);
        const char* end_str = dotdot + 2;
        while (isspace(*end_str)) end_str++; // Skip whitespace after ".."
        const char* end_ptr = end_str;
        while (*end_ptr && !isspace(*end_ptr)) end_ptr++; // Find end of hex number
        *end = parse_hex(end_str, end_ptr - end_str);
    } else {
        // Single codepoint case: e.g., "0020"
        const char* end_ptr = range;
        while (*end_ptr && !isspace(*end_ptr)) end_ptr++; // Find end of hex number
        *start = *end = parse_hex(range, end_ptr - range);
    }
}
// Parsers
void parse_collation(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open allkeys.txt"); exit(1); }
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '@') continue;
        char* cp_str = strtok(line, ";");
        char* rest = strtok(NULL, "\n");
        if (!cp_str || !rest) continue;
        char* weights = rest;
        char* comment = strtok(rest, "#");
        if (comment) { weights = strtok(rest, "#"); comment = strtok(NULL, "\n"); }
        while (*cp_str && isspace(*cp_str)) cp_str++;
        char* cp_end = cp_str + strlen(cp_str) - 1;
        while (cp_end > cp_str && isspace(*cp_end)) *cp_end-- = '\0';
        while (*weights && isspace(*weights)) weights++;
        char* w_end = weights + strlen(weights) - 1;
        while (w_end > weights && isspace(*w_end)) *w_end-- = '\0';
        if (comment) {
            while (*comment && isspace(*comment)) comment++;
            char* c_end = comment + strlen(comment) - 1;
            while (c_end > comment && isspace(*c_end)) *c_end-- = '\0';
        }
        uint32_t cps[8] = {0};
        size_t cp_len = 0;
        char* cp_token = strtok(cp_str, " ");
        while (cp_token && cp_len < 8) {
            cps[cp_len++] = parse_hex(cp_token, strlen(cp_token));
            cp_token = strtok(NULL, " ");
        }
        uint16_t primary[8] = {0}, secondary[8] = {0}, tertiary[8] = {0};
        size_t weight_count = 0;
        char* weight_ptr = weights;
        while (weight_ptr && *weight_ptr && weight_count < 8) {
            char* start = strchr(weight_ptr, '[');
            if (!start) break;
            char* end = strchr(start, ']');
            if (!end) break;
            char weight[32];
            size_t len = end - start + 1;
            strncpy(weight, start, len);
            weight[len] = '\0';
            if (strlen(weight) >= 17) {
                if (weight[1] == '.') {
                    if (sscanf(weight, "[.%4hx.%4hx.%4hx]", &primary[weight_count], &secondary[weight_count], &tertiary[weight_count]) == 3) {
                        weight_count++;
                    }
                } else if (weight[1] == '*') {
                    if (sscanf(weight, "[*%4hx.%4hx.%4hx]", &primary[weight_count], &secondary[weight_count], &tertiary[weight_count]) == 3) {
                        weight_count++;
                    }
                }
            }
            weight_ptr = end + 1;
        }
        if (weight_count == 0) continue;
        uint64_t hash = XXH3_64bits(cps, cp_len * sizeof(uint32_t));
        uint32_t slot = (uint32_t)(hash % HASH_SIZE);
        while (collation_table[slot].cp_len != 0) {
            int match = 1;
            for (size_t i = 0; i < cp_len && i < collation_table[slot].cp_len; i++) {
                if (cps[i] != collation_table[slot].codepoints[i]) { match = 0; break; }
            }
            if (match && cp_len == collation_table[slot].cp_len) break;
            slot = (slot + 1) % HASH_SIZE;
            if (slot == (uint32_t)(hash % HASH_SIZE)) break; // Prevent infinite loop
        }
        collation_table[slot].cp_len = cp_len;
        for (size_t i = 0; i < cp_len; i++) collation_table[slot].codepoints[i] = cps[i];
        for (size_t i = 0; i < weight_count; i++) {
            collation_table[slot].primary[i] = primary[i];
            collation_table[slot].secondary[i] = secondary[i];
            collation_table[slot].tertiary[i] = tertiary[i];
        }
        collation_table[slot].weight_count = weight_count;
        if (comment) strncpy(collation_table[slot].comment, comment, sizeof(collation_table[slot].comment) - 1);
        else collation_table[slot].comment[0] = '\0';
        collation_size++;
    }
    fclose(fp);
    printf("parsed %zu collation entries from %s\n", collation_size, filename);
}

void parse_bidi_brackets(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open BidiBrackets.txt"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* fields[3];
        int field_count = 0;
        char* token = strtok(line, ";");
        while (token && field_count < 3) {
            fields[field_count++] = token;
            token = strtok(NULL, ";");
        }
        if (field_count < 3) continue;
        while (isspace(*fields[0])) fields[0]++;
        while (isspace(*fields[1])) fields[1]++;
        bidi_brackets[bracket_size].codepoint = parse_hex(fields[0], strlen(fields[0]));
        bidi_brackets[bracket_size].open_bracket = bidi_brackets[bracket_size].codepoint;
        bidi_brackets[bracket_size].close_bracket = parse_hex(fields[1], strlen(fields[1]));
        bracket_size++;
    }
    fclose(fp);
    printf("parsed %zu bidi bracket entries from %s\n", bracket_size, filename);
}

void parse_bidi_mirroring(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open BidiMirroring.txt"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* fields[2];
        int field_count = 0;
        char* token = strtok(line, ";");
        while (token && field_count < 2) {
            fields[field_count++] = token;
            token = strtok(NULL, ";");
        }
        if (field_count < 2) continue;
        while (isspace(*fields[0])) fields[0]++;
        while (isspace(*fields[1])) fields[1]++;
        bidi_mirrors[mirror_size].codepoint = parse_hex(fields[0], strlen(fields[0]));
        bidi_mirrors[mirror_size].mirror = parse_hex(fields[1], strlen(fields[1]));
        mirror_size++;
    }
    fclose(fp);
    printf("parsed %zu bidi mirroring entries from %s\n", mirror_size, filename);
}

void parse_arabic_shaping(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open ArabicShaping.txt"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* fields[4];
        int field_count = 0;
        char* token = strtok(line, ";");
        while (token && field_count < 4) {
            fields[field_count++] = token;
            token = strtok(NULL, ";");
        }
        if (field_count < 4) continue;
        uint32_t cp = parse_hex(fields[0], strlen(fields[0]));
        char* jtype = fields[2];
        while (isspace(*jtype)) jtype++;
        uint8_t joining_type = 0;
        switch (jtype[0]) {
            case 'L': joining_type = 1; break;
            case 'R': joining_type = 2; break;
            case 'U': joining_type = 3; break;
            case 'T': joining_type = 4; break;
            case 'D': joining_type = 5; break;
            case 'C': joining_type = 6; break;
        }
        uint64_t hash = XXH3_64bits(&cp, sizeof(cp));
        uint32_t slot = (uint32_t)(hash % HASH_SIZE);
        while (arabic_shaping[slot].codepoint != 0 && arabic_shaping[slot].codepoint != cp) {
            slot = (slot + 1) % HASH_SIZE;
            if (slot == (uint32_t)(hash % HASH_SIZE)) break;
        }
        arabic_shaping[slot].codepoint = cp;
        arabic_shaping[slot].joining_type = joining_type;
        shaping_size++;
    }
    fclose(fp);
    printf("parsed %zu arabic shaping entries from %s\n", shaping_size, filename);
}

void parse_grapheme_break(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open GraphemeBreakProperty.txt"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* range = strtok(line, ";");
        char* prop = strtok(NULL, "#");
        if (!range || !prop) continue;
        while (isspace(*prop)) prop++;
        uint32_t start, end;
        parse_range(range, &start, &end);
        uint8_t property = GBP_OTHER;
        if (strncmp(prop, "CR", 2) == 0) property = GBP_CR;
        else if (strncmp(prop, "LF", 2) == 0) property = GBP_LF;
        else if (strncmp(prop, "Control", 7) == 0) property = GBP_CONTROL;
        else if (strncmp(prop, "Extend", 6) == 0) property = GBP_EXTEND;
        else if (strncmp(prop, "Prepend", 7) == 0) property = GBP_PREPEND;
        else if (strncmp(prop, "SpacingMark", 11) == 0) property = GBP_SPACINGMARK;
        else if (strncmp(prop, "L", 1) == 0) property = GBP_L;
        else if (strncmp(prop, "V", 1) == 0) property = GBP_V;
        else if (strncmp(prop, "T", 1) == 0) property = GBP_T;
        else if (strncmp(prop, "LV", 2) == 0) property = GBP_LV;
        else if (strncmp(prop, "LVT", 3) == 0) property = GBP_LVT;
        else if (strncmp(prop, "Regional_Indicator", 18) == 0) property = GBP_RI;
        else if (strncmp(prop, "ZWJ", 3) == 0) property = GBP_ZWJ;
        else if (strncmp(prop, "Extended_Pictographic", 21) == 0) property = GBP_EB;
        grapheme_breaks[gbreak_size].start = start;
        grapheme_breaks[gbreak_size].end = end;
        grapheme_breaks[gbreak_size].property = property;
        gbreak_size++;
    }
    fclose(fp);
    printf("parsed %zu grapheme break entries from %s\n", gbreak_size, filename);
}

void parse_word_break(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open WordBreakProperty.txt"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* range = strtok(line, ";");
        char* prop = strtok(NULL, "#");
        if (!range || !prop) continue;
        while (isspace(*prop)) prop++;
        uint32_t start, end;
        parse_range(range, &start, &end);
        uint8_t property = WBP_OTHER;
        if (strncmp(prop, "CR", 2) == 0) property = WBP_CR;
        else if (strncmp(prop, "LF", 2) == 0) property = WBP_LF;
        else if (strncmp(prop, "Newline", 7) == 0) property = WBP_NEWLINE;
        else if (strncmp(prop, "Extend", 6) == 0) property = WBP_EXTEND;
        else if (strncmp(prop, "Katakana", 8) == 0) property = WBP_KATAKANA;
        else if (strncmp(prop, "ALetter", 7) == 0) property = WBP_ALETTER;
        else if (strncmp(prop, "MidLetter", 9) == 0) property = WBP_MIDLETTER;
        else if (strncmp(prop, "MidNum", 6) == 0) property = WBP_MIDNUM;
        else if (strncmp(prop, "MidNumLet", 9) == 0) property = WBP_MIDNUMLET;
        else if (strncmp(prop, "Numeric", 7) == 0) property = WBP_NUMERIC;
        else if (strncmp(prop, "ExtendNumLet", 12) == 0) property = WBP_EB;
        else if (strncmp(prop, "Regional_Indicator", 18) == 0) property = WBP_RI;
        word_breaks[wbreak_size].start = start;
        word_breaks[wbreak_size].end = end;
        word_breaks[wbreak_size].property = property;
        wbreak_size++;
    }
    fclose(fp);
    printf("parsed %zu word break entries from %s\n", wbreak_size, filename);
}

void parse_sentence_break(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open SentenceBreakProperty.txt"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* range = strtok(line, ";");
        char* prop = strtok(NULL, "#");
        if (!range || !prop) continue;
        while (isspace(*prop)) prop++;
        uint32_t start, end;
        parse_range(range, &start, &end);
        uint8_t property = SBP_OTHER;
        if (strncmp(prop, "CR", 2) == 0) property = SBP_CR;
        else if (strncmp(prop, "LF", 2) == 0) property = SBP_LF;
        else if (strncmp(prop, "Sep", 3) == 0) property = SBP_SEP;
        else if (strncmp(prop, "Sp", 2) == 0) property = SBP_SP;
        else if (strncmp(prop, "Lower", 5) == 0) property = SBP_LOWER;
        else if (strncmp(prop, "Upper", 5) == 0) property = SBP_UPPER;
        else if (strncmp(prop, "OLetter", 7) == 0) property = SBP_OLETTER;
        else if (strncmp(prop, "Numeric", 7) == 0) property = SBP_NUMERIC;
        else if (strncmp(prop, "ATerm", 5) == 0) property = SBP_ATERM;
        else if (strncmp(prop, "STerm", 5) == 0) property = SBP_STERM;
        else if (strncmp(prop, "Close", 5) == 0) property = SBP_CLOSE;
        else if (strncmp(prop, "SContinue", 9) == 0) property = SBP_SContinue;
        sentence_breaks[sbreak_size].start = start;
        sentence_breaks[sbreak_size].end = end;
        sentence_breaks[sbreak_size].property = property;
        sbreak_size++;
    }
    fclose(fp);
    printf("parsed %zu sentence break entries from %s\n", sbreak_size, filename);
}

void parse_line_break(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open LineBreak.txt"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* range = strtok(line, ";");
        char* prop = strtok(NULL, "#");
        if (!range || !prop) continue;
        while (isspace(*prop)) prop++;
        uint32_t start, end;
        parse_range(range, &start, &end);
        uint8_t property = LBP_XX;
        if (strncmp(prop, "BK", 2) == 0) property = LBP_BK;
        else if (strncmp(prop, "CR", 2) == 0) property = LBP_CR;
        else if (strncmp(prop, "LF", 2) == 0) property = LBP_LF;
        else if (strncmp(prop, "CM", 2) == 0) property = LBP_CM;
        else if (strncmp(prop, "SG", 2) == 0) property = LBP_SG;
        else if (strncmp(prop, "GL", 2) == 0) property = LBP_GL;
        else if (strncmp(prop, "CB", 2) == 0) property = LBP_CB;
        else if (strncmp(prop, "SP", 2) == 0) property = LBP_SP;
        else if (strncmp(prop, "ZW", 2) == 0) property = LBP_ZW;
        else if (strncmp(prop, "NL", 2) == 0) property = LBP_NL;
        else if (strncmp(prop, "WJ", 2) == 0) property = LBP_WJ;
        else if (strncmp(prop, "JL", 2) == 0) property = LBP_JL;
        else if (strncmp(prop, "JV", 2) == 0) property = LBP_JV;
        else if (strncmp(prop, "JT", 2) == 0) property = LBP_JT;
        else if (strncmp(prop, "H2", 2) == 0) property = LBP_H2;
        else if (strncmp(prop, "H3", 2) == 0) property = LBP_H3;
        else if (strncmp(prop, "OP", 2) == 0) property = LBP_OP;
        else if (strncmp(prop, "CL", 2) == 0) property = LBP_CL;
        else if (strncmp(prop, "CP", 2) == 0) property = LBP_CP;
        else if (strncmp(prop, "QU", 2) == 0) property = LBP_QU;
        else if (strncmp(prop, "NS", 2) == 0) property = LBP_NS;
        else if (strncmp(prop, "EX", 2) == 0) property = LBP_EX;
        else if (strncmp(prop, "SY", 2) == 0) property = LBP_SY;
        else if (strncmp(prop, "IS", 2) == 0) property = LBP_IS;
        else if (strncmp(prop, "PR", 2) == 0) property = LBP_PR;
        else if (strncmp(prop, "PO", 2) == 0) property = LBP_PO;
        else if (strncmp(prop, "NU", 2) == 0) property = LBP_NU;
        else if (strncmp(prop, "AL", 2) == 0) property = LBP_AL;
        else if (strncmp(prop, "ID", 2) == 0) property = LBP_ID;
        else if (strncmp(prop, "IN", 2) == 0) property = LBP_IN;
        else if (strncmp(prop, "HY", 2) == 0) property = LBP_HY;
        else if (strncmp(prop, "BA", 2) == 0) property = LBP_BA;
        else if (strncmp(prop, "BB", 2) == 0) property = LBP_BB;
        else if (strncmp(prop, "B2", 2) == 0) property = LBP_B2;
        else if (strncmp(prop, "ZWJ", 3) == 0) property = LBP_ZWJ;
        else if (strncmp(prop, "EB", 2) == 0) property = LBP_EB;
        else if (strncmp(prop, "EM", 2) == 0) property = LBP_EM;
        else if (strncmp(prop, "AI", 2) == 0) property = LBP_AI;
        else if (strncmp(prop, "CJ", 2) == 0) property = LBP_CJ;
        line_breaks[lbreak_size].start = start;
        line_breaks[lbreak_size].end = end;
        line_breaks[lbreak_size].property = property;
        lbreak_size++;
    }
    fclose(fp);
    printf("parsed %zu line break entries from %s\n", lbreak_size, filename);
}

void parse_emoji_data(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open emoji-data.txt"); return; }
    char line[256];
    size_t emoji_size = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* range = strtok(line, ";");
        char* prop = strtok(NULL, "#");
        if (!range || !prop) continue;
        while (isspace(*prop)) prop++;
        uint32_t start, end;
        parse_range(range, &start, &end);
        if (strncmp(prop, "Extended_Pictographic", 21) == 0) {
            for (uint32_t cp = start; cp <= end && gbreak_size < 10000; cp++) {
                grapheme_breaks[gbreak_size].start = cp;
                grapheme_breaks[gbreak_size].end = cp;
                grapheme_breaks[gbreak_size].property = GBP_EB;
                gbreak_size++;
            }
        }
        emoji_size++;
    }
    fclose(fp);
    printf("parsed %zu emoji data entries from %s (added to grapheme breaks)\n", emoji_size, filename);
}

void parse_case_folding(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open CaseFolding.txt"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* fields[3];
        int field_count = 0;
        char* token = strtok(line, ";");
        while (token && field_count < 3) {
            fields[field_count++] = token;
            token = strtok(NULL, ";");
        }
        if (field_count < 3) continue;
        while (isspace(*fields[0])) fields[0]++;
        while (isspace(*fields[1])) fields[1]++;
        while (isspace(*fields[2])) fields[2]++;
        if (fields[1][0] != 'C' && fields[1][0] != 'F') continue; // Only C and F mappings
        uint32_t cp = parse_hex(fields[0], strlen(fields[0]));
        uint32_t folded = parse_hex(fields[2], strlen(fields[2]));
        uint64_t hash = XXH3_64bits(&cp, sizeof(cp));
        uint32_t slot = (uint32_t)(hash % HASH_SIZE);
        uint32_t original_slot = slot;
        while (case_folding[slot] != 0 && case_folding[slot] != cp) {
            slot = (slot + 1) % HASH_SIZE;
            if (slot == original_slot) break; // Prevent infinite loop
        }
        if (case_folding[slot] == 0) { // Only store if slot is empty
            case_folding[slot] = folded;
            folding_size++;
        }
    }
    fclose(fp);
    printf("parsed %zu case folding entries from %s\n", folding_size, filename);
}

void parse_scripts(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open Scripts.txt"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* range = strtok(line, ";");
        char* script = strtok(NULL, "#");
        if (!range || !script) continue;
        while (isspace(*script)) script++;
        uint32_t start, end;
        parse_range(range, &start, &end);
        uint8_t script_code = 0; // Simplified mapping
        if (strncmp(script, "Latin", 5) == 0) script_code = 1;
        else if (strncmp(script, "Arabic", 6) == 0) script_code = 2;
        else if (strncmp(script, "Hebrew", 6) == 0) script_code = 3;
        for (uint32_t cp = start; cp <= end; cp++) {
            uint64_t hash = XXH3_64bits(&cp, sizeof(cp));
            uint32_t slot = (uint32_t)(hash % HASH_SIZE);
            while (scripts[slot] != 0 && scripts[slot] != cp) {
                slot = (slot + 1) % HASH_SIZE;
                if (slot == (uint32_t)(hash % HASH_SIZE)) break;
            }
            scripts[slot] = script_code;
            script_size++;
        }
    }
    fclose(fp);
    printf("parsed %zu script entries from %s\n", script_size, filename);
}

void parse_east_asian_width(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open EastAsianWidth.txt"); return; }
    char line[256];
    eaw_size = 0; // Reset size

    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* range = strtok(line, ";");
        char* width = strtok(NULL, "#");
        if (!range || !width) continue;

        while (isspace(*range)) range++;
        while (isspace(*width)) width++;

        uint32_t start, end;
        parse_range(range, &start, &end);

        uint32_t width_value = 0; // N=0, W=1, F=2, Na=3, H=4, A=5
        if (strncmp(width, "N", 1) == 0) width_value = (width[1] == 'a') ? 3 : 0;
        else if (strncmp(width, "W", 1) == 0) width_value = 1;
        else if (strncmp(width, "F", 1) == 0) width_value = 2;
        else if (strncmp(width, "H", 1) == 0) width_value = 4;
        else if (strncmp(width, "A", 1) == 0) width_value = 5;

        for (uint32_t cp = start; cp <= end; cp++) {
            uint64_t hash = XXH3_64bits(&cp, sizeof(cp));
            uint32_t slot = (uint32_t)(hash % HASH_SIZE);
            uint32_t original_slot = slot;
            while (east_asian_width[slot] != 0) {
                slot = (slot + 1) % HASH_SIZE;
                if (slot == original_slot) break;
            }
            east_asian_width[slot] = width_value;
            eaw_size++;
        }
    }
    fclose(fp);
    printf("parsed %zu east asian width entries from %s\n", eaw_size, filename);
}

void parse_prop_list(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open PropList.txt"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* range = strtok(line, ";");
        char* prop = strtok(NULL, "#");
        if (!range || !prop) continue;
        while (isspace(*prop)) prop++;
        uint32_t start, end;
        parse_range(range, &start, &end);
        uint32_t prop_bit = 0;
        if (strncmp(prop, "White_Space", 11) == 0) prop_bit = 1 << 0;
        else if (strncmp(prop, "Bidi_Control", 12) == 0) prop_bit = 1 << 1;
        else if (strncmp(prop, "Join_Control", 12) == 0) prop_bit = 1 << 2;
        for (uint32_t cp = start; cp <= end; cp++) {
            uint64_t hash = XXH3_64bits(&cp, sizeof(cp));
            uint32_t slot = (uint32_t)(hash % HASH_SIZE);
            while (prop_list[slot] != 0 && prop_list[slot] != cp) {
                slot = (slot + 1) % HASH_SIZE;
                if (slot == (uint32_t)(hash % HASH_SIZE)) break;
            }
            prop_list[slot] |= prop_bit;
            prop_list_size++;
        }
    }
    fclose(fp);
    printf("parsed %zu property list entries from %s\n", prop_list_size, filename);
}

void parse_derived_core_props(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open DerivedCoreProperties.txt"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* range = strtok(line, ";");
        char* prop = strtok(NULL, "#");
        if (!range || !prop) continue;
        while (isspace(*prop)) prop++;
        uint32_t start, end;
        parse_range(range, &start, &end);
        uint32_t prop_bit = 0;
        if (strncmp(prop, "Math", 4) == 0) prop_bit = 1 << 0;
        else if (strncmp(prop, "Alphabetic", 10) == 0) prop_bit = 1 << 1;
        else if (strncmp(prop, "Lowercase", 9) == 0) prop_bit = 1 << 2;
        for (uint32_t cp = start; cp <= end; cp++) {
            uint64_t hash = XXH3_64bits(&cp, sizeof(cp));
            uint32_t slot = (uint32_t)(hash % HASH_SIZE);
            while (derived_core_props[slot] != 0 && derived_core_props[slot] != cp) {
                slot = (slot + 1) % HASH_SIZE;
                if (slot == (uint32_t)(hash % HASH_SIZE)) break;
            }
            derived_core_props[slot] |= prop_bit;
            dcp_size++;
        }
    }
    fclose(fp);
    printf("parsed %zu derived core property entries from %s\n", dcp_size, filename);
}

void parse_blocks(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open Blocks.txt"); return; }
    char line[256];
    block_size = 0; // Reset size

    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* range = strtok(line, ";");
        char* name = strtok(NULL, "#");
        if (!range || !name) continue;

        while (isspace(*range)) range++;
        while (isspace(*name)) name++;
        char* end_name = name + strlen(name) - 1;
        while (end_name > name && isspace(*end_name)) *end_name-- = '\0';

        uint32_t start, end;
        parse_range(range, &start, &end);

        if (block_size < 1000) { // Prevent overflow
            block_table[block_size].start = start;
            block_table[block_size].end = end;
            strncpy(block_table[block_size].name, name, sizeof(block_table[block_size].name) - 1);
            block_table[block_size].name[sizeof(block_table[block_size].name) - 1] = '\0';
            block_size++;
        }
    }
    fclose(fp);
    printf("parsed %zu block entries from %s\n", block_size, filename);
}

void parse_cjk_radicals(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open CJKRadicals.txt"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* fields[3];
        int field_count = 0;
        char* token = strtok(line, ";");
        while (token && field_count < 3) {
            fields[field_count++] = token;
            token = strtok(NULL, ";");
        }
        if (field_count < 3) continue;
        uint32_t radical = parse_hex(fields[1], strlen(fields[1]));
        cjk_radicals[cjk_radical_size++] = radical;
    }
    fclose(fp);
    printf("parsed %zu CJK radical entries from %s\n", cjk_radical_size, filename);
}

void parse_derived_age(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open DerivedAge.txt"); return; }
    char line[256];
    age_size = 0; // Reset size

    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* range = strtok(line, ";");
        char* age = strtok(NULL, "#");
        if (!range || !age) continue;

        while (isspace(*range)) range++;
        while (isspace(*age)) age++;

        uint32_t start, end;
        parse_range(range, &start, &end);

        // Convert "1.1" to 11, "15.0" to 150, etc.
        uint32_t major = atoi(age);
        char* dot = strchr(age, '.');
        uint32_t minor = dot ? atoi(dot + 1) : 0;
        uint32_t age_value = major * 10 + minor;

        for (uint32_t cp = start; cp <= end; cp++) {
            uint64_t hash = XXH3_64bits(&cp, sizeof(cp));
            uint32_t slot = (uint32_t)(hash % HASH_SIZE);
            uint32_t original_slot = slot;
            while (derived_age[slot] != 0) {
                slot = (slot + 1) % HASH_SIZE;
                if (slot == original_slot) break;
            }
            derived_age[slot] = age_value;
            age_size++;
        }
    }
    fclose(fp);
    printf("parsed %zu derived age entries from %s\n", age_size, filename);
}

void parse_derived_bidi_class(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open DerivedBidiClass.txt"); return; }
    char line[256];
    bidi_class_size = 0; // Reset size

    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n' || strncmp(line, "@missing", 8) == 0) continue;
        char* range = strtok(line, ";");
        char* bidi = strtok(NULL, "#");
        if (!range || !bidi) continue;

        while (isspace(*range)) range++;
        while (isspace(*bidi)) bidi++;

        uint32_t start, end;
        parse_range(range, &start, &end);

        uint8_t bidi_value = 0; // L=1, R=2, AL=3, etc.
        if (strncmp(bidi, "L", 1) == 0) bidi_value = 1;
        else if (strncmp(bidi, "R", 1) == 0) bidi_value = 2;
        else if (strncmp(bidi, "AL", 2) == 0) bidi_value = 3;
        // Add more mappings as needed (e.g., BN, ET)

        for (uint32_t cp = start; cp <= end; cp++) {
            uint64_t hash = XXH3_64bits(&cp, sizeof(cp));
            uint32_t slot = (uint32_t)(hash % HASH_SIZE);
            uint32_t original_slot = slot;
            while (bidi_class[slot] != 0) {
                slot = (slot + 1) % HASH_SIZE;
                if (slot == original_slot) break;
            }
            bidi_class[slot] = bidi_value;
            bidi_class_size++;
        }
    }
    fclose(fp);
    printf("parsed %zu derived bidi class entries from %s\n", bidi_class_size, filename);
}

void parse_derived_binary_props(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open DerivedBinaryProperties.txt"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* range = strtok(line, ";");
        char* prop = strtok(NULL, "#");
        if (!range || !prop) continue;
        while (isspace(*prop)) prop++;
        uint32_t start, end;
        parse_range(range, &start, &end);
        uint8_t prop_value = 0;
        if (strncmp(prop, "Bidi_Mirrored", 13) == 0) prop_value = 1;
        for (uint32_t cp = start; cp <= end; cp++) {
            uint64_t hash = XXH3_64bits(&cp, sizeof(cp));
            uint32_t slot = (uint32_t)(hash % HASH_SIZE);
            while (binary_props[slot] != 0 && binary_props[slot] != cp) {
                slot = (slot + 1) % HASH_SIZE;
                if (slot == (uint32_t)(hash % HASH_SIZE)) break;
            }
            binary_props[slot] = prop_value;
            binary_prop_size++;
        }
    }
    fclose(fp);
    printf("parsed %zu derived binary property entries from %s\n", binary_prop_size, filename);
}

void parse_derived_combining_class(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open DerivedCombiningClass.txt"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* range = strtok(line, ";");
        char* cc = strtok(NULL, "#");
        if (!range || !cc) continue;
        while (isspace(*cc)) cc++;
        uint32_t start, end;
        parse_range(range, &start, &end);
        uint8_t cc_value = atoi(cc);
        for (uint32_t cp = start; cp <= end; cp++) {
            uint64_t hash = XXH3_64bits(&cp, sizeof(cp));
            uint32_t slot = (uint32_t)(hash % HASH_SIZE);
            while (combining_class[slot] != 0 && combining_class[slot] != cp) {
                slot = (slot + 1) % HASH_SIZE;
                if (slot == (uint32_t)(hash % HASH_SIZE)) break;
            }
            combining_class[slot] = cc_value;
            comb_class_size++;
        }
    }
    fclose(fp);
    printf("parsed %zu derived combining class entries from %s\n", comb_class_size, filename);
}

void parse_derived_decomp_type(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open DerivedDecompositionType.txt"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* range = strtok(line, ";");
        char* dt = strtok(NULL, "#");
        if (!range        || !dt) continue;
        while (isspace(*dt)) dt++;
        uint32_t start, end;
        parse_range(range, &start, &end);
        uint8_t dt_value = 0; // Simplified mapping
        if (strncmp(dt, "canonical", 9) == 0) dt_value = 1;
        else if (strncmp(dt, "compat", 6) == 0) dt_value = 2;
        for (uint32_t cp = start; cp <= end; cp++) {
            uint64_t hash = XXH3_64bits(&cp, sizeof(cp));
            uint32_t slot = (uint32_t)(hash % HASH_SIZE);
            while (decomp_type[slot] != 0 && decomp_type[slot] != cp) {
                slot = (slot + 1) % HASH_SIZE;
                if (slot == (uint32_t)(hash % HASH_SIZE)) break;
            }
            decomp_type[slot] = dt_value;
            decomp_type_size++;
        }
    }
    fclose(fp);
    printf("parsed %zu derived decomposition type entries from %s\n", decomp_type_size, filename);
}

void parse_derived_general_category(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open DerivedGeneralCategory.txt"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* range = strtok(line, ";");
        char* gc = strtok(NULL, "#");
        if (!range || !gc) continue;
        while (isspace(*gc)) gc++;
        uint32_t start, end;
        parse_range(range, &start, &end);
        uint8_t gc_value = 0; // Simplified
        if (strncmp(gc, "Lu", 2) == 0) gc_value = 1;
        else if (strncmp(gc, "Ll", 2) == 0) gc_value = 2;
        else if (strncmp(gc, "Lt", 2) == 0) gc_value = 3;
        else if (strncmp(gc, "Mn", 2) == 0) gc_value = 4;
        for (uint32_t cp = start; cp <= end; cp++) {
            uint64_t hash = XXH3_64bits(&cp, sizeof(cp));
            uint32_t slot = (uint32_t)(hash % HASH_SIZE);
            while (general_category[slot] != 0 && general_category[slot] != cp) {
                slot = (slot + 1) % HASH_SIZE;
                if (slot == (uint32_t)(hash % HASH_SIZE)) break;
            }
            general_category[slot] = gc_value;
            gen_cat_size++;
        }
    }
    fclose(fp);
    printf("parsed %zu derived general category entries from %s\n", gen_cat_size, filename);
}

void parse_derived_joining_group(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open DerivedJoiningGroup.txt"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* range = strtok(line, ";");
        char* jg = strtok(NULL, "#");
        if (!range || !jg) continue;
        while (isspace(*jg)) jg++;
        uint32_t start, end;
        parse_range(range, &start, &end);
        uint8_t jg_value = 0; // Simplified
        if (strncmp(jg, "Ain", 3) == 0) jg_value = 1;
        else if (strncmp(jg, "Beh", 3) == 0) jg_value = 2;
        for (uint32_t cp = start; cp <= end; cp++) {
            uint64_t hash = XXH3_64bits(&cp, sizeof(cp));
            uint32_t slot = (uint32_t)(hash % HASH_SIZE);
            while (joining_group[slot] != 0 && joining_group[slot] != cp) {
                slot = (slot + 1) % HASH_SIZE;
                if (slot == (uint32_t)(hash % HASH_SIZE)) break;
            }
            joining_group[slot] = jg_value;
            join_group_size++;
        }
    }
    fclose(fp);
    printf("parsed %zu derived joining group entries from %s\n", join_group_size, filename);
}

void parse_derived_joining_type(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open DerivedJoiningType.txt"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* range = strtok(line, ";");
        char* jt = strtok(NULL, "#");
        if (!range || !jt) continue;
        while (isspace(*jt)) jt++;
        uint32_t start, end;
        parse_range(range, &start, &end);
        uint8_t jt_value = 0;
        if (strncmp(jt, "L", 1) == 0) jt_value = 1;
        else if (strncmp(jt, "R", 1) == 0) jt_value = 2;
        else if (strncmp(jt, "U", 1) == 0) jt_value = 3;
        for (uint32_t cp = start; cp <= end; cp++) {
            uint64_t hash = XXH3_64bits(&cp, sizeof(cp));
            uint32_t slot = (uint32_t)(hash % HASH_SIZE);
            while (joining_type[slot] != 0 && joining_type[slot] != cp) {
                slot = (slot + 1) % HASH_SIZE;
                if (slot == (uint32_t)(hash % HASH_SIZE)) break;
            }
            joining_type[slot] = jt_value;
            join_type_size++;
        }
    }
    fclose(fp);
    printf("parsed %zu derived joining type entries from %s\n", join_type_size, filename);
}

void parse_derived_name(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open DerivedName.txt"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* range = strtok(line, ";");
        char* name = strtok(NULL, "#");
        if (!range || !name) continue;
        while (isspace(*name)) name++;
        uint32_t start, end;
        parse_range(range, &start, &end);
        for (uint32_t cp = start; cp <= end; cp++) {
            uint64_t hash = XXH3_64bits(&cp, sizeof(cp));
            uint32_t slot = (uint32_t)(hash % HASH_SIZE);
            while (derived_names[slot][0] != 0 && derived_names[slot][0] != cp) {
                slot = (slot + 1) % HASH_SIZE;
                if (slot == (uint32_t)(hash % HASH_SIZE)) break;
            }
            strncpy(derived_names[slot], name, 31);
            derived_names[slot][31] = '\0';
            name_size++;
        }
    }
    fclose(fp);
    printf("parsed %zu derived name entries from %s\n", name_size, filename);
}

void parse_derived_numeric_type(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open DerivedNumericType.txt"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* range = strtok(line, ";");
        char* nt = strtok(NULL, "#");
        if (!range || !nt) continue;
        while (isspace(*nt)) nt++;
        uint32_t start, end;
        parse_range(range, &start, &end);
        uint8_t nt_value = 0;
        if (strncmp(nt, "Decimal", 7) == 0) nt_value = 1;
        else if (strncmp(nt, "Digit", 5) == 0) nt_value = 2;
        else if (strncmp(nt, "Numeric", 7) == 0) nt_value = 3;
        for (uint32_t cp = start; cp <= end; cp++) {
            uint64_t hash = XXH3_64bits(&cp, sizeof(cp));
            uint32_t slot = (uint32_t)(hash % HASH_SIZE);
            while (numeric_type[slot] != 0 && numeric_type[slot] != cp) {
                slot = (slot + 1) % HASH_SIZE;
                if (slot == (uint32_t)(hash % HASH_SIZE)) break;
            }
            numeric_type[slot] = nt_value;
            num_type_size++;
        }
    }
    fclose(fp);
    printf("parsed %zu derived numeric type entries from %s\n", num_type_size, filename);
}

void parse_derived_numeric_values(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open DerivedNumericValues.txt"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* range = strtok(line, ";");
        char* nv = strtok(NULL, "#");
        if (!range || !nv) continue;
        while (isspace(*nv)) nv++;
        uint32_t start, end;
        parse_range(range, &start, &end);
        double nv_value = atof(nv);
        for (uint32_t cp = start; cp <= end; cp++) {
            uint64_t hash = XXH3_64bits(&cp, sizeof(cp));
            uint32_t slot = (uint32_t)(hash % HASH_SIZE);
            while (numeric_values[slot] != 0 && numeric_values[slot] != cp) {
                slot = (slot + 1) % HASH_SIZE;
                if (slot == (uint32_t)(hash % HASH_SIZE)) break;
            }
            numeric_values[slot] = nv_value;
            num_value_size++;
        }
    }
    fclose(fp);
    printf("parsed %zu derived numeric value entries from %s\n", num_value_size, filename);
}

void parse_hangul_syllable_type(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open HangulSyllableType.txt"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* range = strtok(line, ";");
        char* hst = strtok(NULL, "#");
        if (!range || !hst) continue;
        while (isspace(*hst)) hst++;
        uint32_t start, end;
        parse_range(range, &start, &end);
        uint32_t hst_value = 0;
        if (strncmp(hst, "L", 1) == 0) hst_value = 1;
        else if (strncmp(hst, "V", 1) == 0) hst_value = 2;
        else if (strncmp(hst, "T", 1) == 0) hst_value = 3;
        for (uint32_t cp = start; cp <= end; cp++) {
            uint64_t hash = XXH3_64bits(&cp, sizeof(cp));
            uint32_t slot = (uint32_t)(hash % HASH_SIZE);
            while (hangul_syllable_type[slot] != 0 && hangul_syllable_type[slot] != cp) {
                slot = (slot + 1) % HASH_SIZE;
                if (slot == (uint32_t)(hash % HASH_SIZE)) break;
            }
            hangul_syllable_type[slot] = hst_value;
            hangul_size++;
        }
    }
    fclose(fp);
    printf("parsed %zu hangul syllable type entries from %s\n", hangul_size, filename);
}

void parse_indic_pos_category(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open IndicPositionalCategory.txt"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* range = strtok(line, ";");
        char* ipc = strtok(NULL, "#");
        if (!range || !ipc) continue;
        while (isspace(*ipc)) ipc++;
        uint32_t start, end;
        parse_range(range, &start, &end);
        uint32_t ipc_value = 0;
        if (strncmp(ipc, "Right", 5) == 0) ipc_value = 1;
        else if (strncmp(ipc, "Left", 4) == 0) ipc_value = 2;
        for (uint32_t cp = start; cp <= end; cp++) {
            uint64_t hash = XXH3_64bits(&cp, sizeof(cp));
            uint32_t slot = (uint32_t)(hash % HASH_SIZE);
            while (indic_pos_category[slot] != 0 && indic_pos_category[slot] != cp) {
                slot = (slot + 1) % HASH_SIZE;
                if (slot == (uint32_t)(hash % HASH_SIZE)) break;
            }
            indic_pos_category[slot] = ipc_value;
            indic_pos_size++;
        }
    }
    fclose(fp);
    printf("parsed %zu indic positional category entries from %s\n", indic_pos_size, filename);
}

void parse_indic_syl_category(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open IndicSyllabicCategory.txt"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* range = strtok(line, ";");
        char* isc = strtok(NULL, "#");
        if (!range || !isc) continue;
        while (isspace(*isc)) isc++;
        uint32_t start, end;
        parse_range(range, &start, &end);
        uint32_t isc_value = 0;
        if (strncmp(isc, "Vowel", 5) == 0) isc_value = 1;
        else if (strncmp(isc, "Consonant", 9) == 0) isc_value = 2;
        for (uint32_t cp = start; cp <= end; cp++) {
            uint64_t hash = XXH3_64bits(&cp, sizeof(cp));
            uint32_t slot = (uint32_t)(hash % HASH_SIZE);
            while (indic_syl_category[slot] != 0 && indic_syl_category[slot] != cp) {
                slot = (slot + 1) % HASH_SIZE;
                if (slot == (uint32_t)(hash % HASH_SIZE)) break;
            }
            indic_syl_category[slot] = isc_value;
            indic_syl_size++;
        }
    }
    fclose(fp);
    printf("parsed %zu indic syllabic category entries from %s\n", indic_syl_size, filename);
}

void parse_jamo(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open Jamo.txt"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* cp = strtok(line, ";");
        char* short_name = strtok(NULL, "#");
        if (!cp || !short_name) continue;
        while (isspace(*short_name)) short_name++;
        uint32_t codepoint = parse_hex(cp, strlen(cp));
        uint64_t hash = XXH3_64bits(&codepoint, sizeof(codepoint));
        uint32_t slot = (uint32_t)(hash % 256); // Smaller table
        jamo_short_name[slot] = codepoint;
        jamo_size++;
    }
    fclose(fp);
    printf("parsed %zu jamo entries from %s\n", jamo_size, filename);
}

void parse_script_extensions(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open ScriptExtensions.txt"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* range = strtok(line, ";");
        char* scripts = strtok(NULL, "#");
        if (!range || !scripts) continue;
        while (isspace(*scripts)) scripts++;
        uint32_t start, end;
        parse_range(range, &start, &end);
        uint32_t script_value = 0; // Simplified (bitmask could be used)
        if (strstr(scripts, "Latn")) script_value |= 1;
        if (strstr(scripts, "Arab")) script_value |= 2;
        for (uint32_t cp = start; cp <= end; cp++) {
            uint64_t hash = XXH3_64bits(&cp, sizeof(cp));
            uint32_t slot = (uint32_t)(hash % HASH_SIZE);
            while (script_extensions[slot] != 0 && script_extensions[slot] != cp) {
                slot = (slot + 1) % HASH_SIZE;
                if (slot == (uint32_t)(hash % HASH_SIZE)) break;
            }
            script_extensions[slot] = script_value;
            script_ext_size++;
        }
    }
    fclose(fp);
    printf("parsed %zu script extension entries from %s\n", script_ext_size, filename);
}

void parse_vertical_orientation(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open VerticalOrientation.txt"); return; }
    char line[256];
    vert_orient_size = 0; // Reset size

    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* range = strtok(line, ";");
        char* vo = strtok(NULL, "#");
        if (!range || !vo) continue;

        while (isspace(*range)) range++;
        while (isspace(*vo)) vo++;

        uint32_t start, end;
        parse_range(range, &start, &end);

        uint32_t vo_value = 0; // R=0 (default), U=1, Tu=2, Tr=3
        if (strncmp(vo, "U", 1) == 0) vo_value = 1;
        else if (strncmp(vo, "Tu", 2) == 0) vo_value = 2;
        else if (strncmp(vo, "Tr", 2) == 0) vo_value = 3;
        // Default R is 0

        for (uint32_t cp = start; cp <= end; cp++) {
            uint64_t hash = XXH3_64bits(&cp, sizeof(cp));
            uint32_t slot = (uint32_t)(hash % HASH_SIZE);
            uint32_t original_slot = slot;
            while (vertical_orientation[slot] != 0) {
                slot = (slot + 1) % HASH_SIZE;
                if (slot == original_slot) break;
            }
            vertical_orientation[slot] = vo_value;
            vert_orient_size++;
        }
    }
    fclose(fp);
    printf("parsed %zu vertical orientation entries from %s\n", vert_orient_size, filename);
}

// Placeholder parsers for normalization-related files (to match your main())
void parse_normalization(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open UnicodeData.txt"); return; }
    char line[256];
    size_t count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        count++;
    }
    fclose(fp);
    printf("parsed %zu entries from %s (placeholder)\n", count, filename);
}

void parse_special_casing(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open SpecialCasing.txt"); return; }
    char line[256];
    size_t count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        count++;
    }
    fclose(fp);
    printf("parsed %zu entries from %s (placeholder)\n", count, filename);
}

void parse_composition_exclusions(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open CompositionExclusions.txt"); return; }
    char line[256];
    size_t count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        count++;
    }
    fclose(fp);
    printf("parsed %zu entries from %s (placeholder)\n", count, filename);
}

void parse_derived_norm_props(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open DerivedNormalizationProps.txt"); return; }
    char line[256];
    size_t count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        count++;
    }
    fclose(fp);
    printf("parsed %zu entries from %s (placeholder)\n", count, filename);
}

// Add remaining parsers for other files
void parse_emoji_sequences(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open emoji-sequences.txt"); return; }
    char line[256];
    size_t count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        count++;
    }
    fclose(fp);
    printf("parsed %zu emoji sequence entries from %s\n", count, filename);
}

void parse_emoji_zwj_sequences(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open emoji-zwj-sequences.txt"); return; }
    char line[256];
    size_t count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        count++;
    }
    fclose(fp);
    printf("parsed %zu emoji ZWJ sequence entries from %s\n", count, filename);
}

void parse_emoji_variation_sequences(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open emoji-variation-sequences.txt"); return; }
    char line[256];
    size_t count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        count++;
    }
    fclose(fp);
    printf("parsed %zu emoji variation sequence entries from %s\n", count, filename);
}

void parse_name_aliases(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open NameAliases.txt"); return; }
    char line[256];
    size_t count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        count++;
    }
    fclose(fp);
    printf("parsed %zu name alias entries from %s\n", count, filename);
}

void parse_named_sequences(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open NamedSequences.txt"); return; }
    char line[256];
    size_t count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        count++;
    }
    fclose(fp);
    printf("parsed %zu named sequence entries from %s\n", count, filename);
}

void parse_standardized_variants(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open StandardizedVariants.txt"); return; }
    char line[256];
    size_t count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        count++;
    }
    fclose(fp);
    printf("parsed %zu standardized variant entries from %s\n", count, filename);
}

void parse_unihan_readings(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open Unihan_Readings.txt"); return; }
    char line[256];
    size_t count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        count++;
    }
    fclose(fp);
    printf("parsed %zu Unihan reading entries from %s\n", count, filename);
}

// Add parsers for other Unihan files similarly (simplified for brevity)
void parse_unihan_dict_indices(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open Unihan_DictionaryIndices.txt"); return; }
    char line[256];
    size_t count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        count++;
    }
    fclose(fp);
    printf("parsed %zu Unihan dictionary indices entries from %s\n", count, filename);
}

void parse_unihan_dict_like_data(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open Unihan_DictionaryLikeData.txt"); return; }
    char line[256];
    size_t count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        count++;
    }
    fclose(fp);
    printf("parsed %zu Unihan dictionary-like data entries from %s\n", count, filename);
}

void parse_unihan_irg_sources(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open Unihan_IRGSources.txt"); return; }
    char line[256];
    size_t count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        count++;
    }
    fclose(fp);
    printf("parsed %zu Unihan IRG sources entries from %s\n", count, filename);
}

void parse_unihan_numeric_values(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open Unihan_NumericValues.txt"); return; }
    char line[256];
    size_t count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        count++;
    }
    fclose(fp);
    printf("parsed %zu Unihan numeric value entries from %s\n", count, filename);
}

void parse_unihan_other_mappings(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open Unihan_OtherMappings.txt"); return; }
    char line[256];
    size_t count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        count++;
    }
    fclose(fp);
    printf("parsed %zu Unihan other mapping entries from %s\n", count, filename);
}

void parse_unihan_radical_stroke_counts(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open Unihan_RadicalStrokeCounts.txt"); return; }
    char line[256];
    size_t count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        count++;
    }
    fclose(fp);
    printf("parsed %zu Unihan radical stroke count entries from %s\n", count, filename);
}

void parse_unihan_variants(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open Unihan_Variants.txt"); return; }
    char line[256];
    size_t count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        count++;
    }
    fclose(fp);
    printf("parsed %zu Unihan variant entries from %s\n", count, filename);
}

void parse_idna_mapping_table(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open IdnaMappingTable.txt"); return; }
    char line[256];
    size_t count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        count++;
    }
    fclose(fp);
    printf("parsed %zu IDNA mapping table entries from %s\n", count, filename);
}

void parse_confusables(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open confusables.txt"); return; }
    char line[256];
    size_t count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        count++;
    }
    fclose(fp);
    printf("parsed %zu confusable entries from %s\n", count, filename);
}

void parse_identifier_status(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open IdentifierStatus.txt"); return; }
    char line[256];
    size_t count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        count++;
    }
    fclose(fp);
    printf("parsed %zu identifier status entries from %s\n", count, filename);
}

void parse_identifier_type(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("failed to open IdentifierType.txt"); return; }
    char line[256];
    size_t count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        count++;
    }
    fclose(fp);
    printf("parsed %zu identifier type entries from %s\n", count, filename);
}

int main() {
    // Parse all relevant Unicode tables
    parse_collation("allkeys.txt");
    parse_bidi_brackets("BidiBrackets.txt");
    parse_bidi_mirroring("BidiMirroring.txt");
    parse_arabic_shaping("ArabicShaping.txt");
    parse_grapheme_break("GraphemeBreakProperty.txt");
    parse_word_break("WordBreakProperty.txt");
    parse_sentence_break("SentenceBreakProperty.txt");
    parse_line_break("LineBreak.txt");
    parse_emoji_data("emoji-data.txt");
    parse_case_folding("CaseFolding.txt");
    parse_scripts("Scripts.txt");
    // parse_east_asian_width("EastAsianWidth.txt");
    parse_prop_list("PropList.txt");
    parse_derived_core_props("DerivedCoreProperties.txt");
    parse_normalization("UnicodeData.txt");
    parse_special_casing("SpecialCasing.txt");
    parse_composition_exclusions("CompositionExclusions.txt");
    parse_derived_norm_props("DerivedNormalizationProps.txt");
    parse_blocks("Blocks.txt");
    parse_cjk_radicals("CJKRadicals.txt");
    // parse_derived_age("DerivedAge.txt");
    // parse_derived_bidi_class("DerivedBidiClass.txt");
    parse_derived_binary_props("DerivedBinaryProperties.txt");
    parse_derived_combining_class("DerivedCombiningClass.txt");
    parse_derived_decomp_type("DerivedDecompositionType.txt");
    parse_derived_general_category("DerivedGeneralCategory.txt");
    parse_derived_joining_group("DerivedJoiningGroup.txt");
    parse_derived_joining_type("DerivedJoiningType.txt");
    parse_derived_name("DerivedName.txt");
    parse_derived_numeric_type("DerivedNumericType.txt");
    parse_derived_numeric_values("DerivedNumericValues.txt");
    parse_hangul_syllable_type("HangulSyllableType.txt");
    parse_indic_pos_category("IndicPositionalCategory.txt");
    parse_indic_syl_category("IndicSyllabicCategory.txt");
    parse_jamo("Jamo.txt");
    parse_script_extensions("ScriptExtensions.txt");
    // parse_vertical_orientation("VerticalOrientation.txt");
    parse_emoji_sequences("emoji-sequences.txt");
    parse_emoji_zwj_sequences("emoji-zwj-sequences.txt");
    parse_emoji_variation_sequences("emoji-variation-sequences.txt");
    parse_name_aliases("NameAliases.txt");
    parse_named_sequences("NamedSequences.txt");
    parse_standardized_variants("StandardizedVariants.txt");
    parse_unihan_readings("Unihan_Readings.txt");
    parse_unihan_dict_indices("Unihan_DictionaryIndices.txt");
    parse_unihan_dict_like_data("Unihan_DictionaryLikeData.txt");
    parse_unihan_irg_sources("Unihan_IRGSources.txt");
    parse_unihan_numeric_values("Unihan_NumericValues.txt");
    parse_unihan_other_mappings("Unihan_OtherMappings.txt");
    parse_unihan_radical_stroke_counts("Unihan_RadicalStrokeCounts.txt");
    parse_unihan_variants("Unihan_Variants.txt");
    parse_idna_mapping_table("IdnaMappingTable.txt");
    parse_confusables("confusables.txt");
    parse_identifier_status("IdentifierStatus.txt");
    parse_identifier_type("IdentifierType.txt");

    return 0;
}