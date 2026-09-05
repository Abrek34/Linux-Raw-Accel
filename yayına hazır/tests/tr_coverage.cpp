// tr_coverage.cpp — Translation audit for the GUI.
//
// Static check that every user-visible English string threaded through the
// translation helpers (tr / trf / trlbl / trmlbl / trbtn / trchk / trtip /
// tr_combo_fill) has a Turkish entry in gui/tr.inl, and vice versa.
//
// Directories are given as command-line arguments; gui/tr.inl is always
// scanned.  Exit code:
//   0 = full coverage (no missing translations)
//   1 = at least one missing translation (UI would show English in Türkçe)
// Orphans (dict entries never referenced) are reported as warnings only.
//
// String-literal handling: C++ adjacent-literal concatenation ("a" "b") is
// joined with no separator; escapes are copied verbatim so dict keys and code
// literals compare equal without needing a full escape decoder.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

static std::string slurp(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    std::string s;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

// At pos (start of a '"'), parse a sequence of adjacent C string literals and
// return the concatenated raw interiors.  On success advances *pos past the
// last closing quote and returns true.
static bool parse_string_seq(const std::string& s, size_t* pos, std::string* out) {
    size_t i = *pos;
    while (true) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
            i++;
        if (i >= s.size() || s[i] != '"') break;
        i++; // opening quote
        while (i < s.size()) {
            char c = s[i];
            if (c == '\\') {
                // copy backslash + next char verbatim (handles \" \\ \n)
                out->push_back(c);
                if (i + 1 < s.size()) out->push_back(s[i + 1]);
                i += 2;
                continue;
            }
            if (c == '"') { i++; break; } // closing quote
            out->push_back(c);
            i++;
        }
    }
    if (out->empty()) return false; // failed parse: leave *pos untouched
    *pos = i;
    return true;
}

// Advance *i over the FIRST argument of trtip(widget, key) so we can read the
// second argument (the translatable key).  Handles balanced ( ) [ ] { } and
// skips quoted strings inside the widget expression.
static void skip_first_arg(const std::string& s, size_t* i) {
    int depth = 0;
    while (*i < s.size()) {
        char c = s[*i];
        if (c == '"') {
            (*i)++;
            while (*i < s.size() && s[*i] != '"') {
                if (s[*i] == '\\') (*i)++;
                (*i)++;
            }
            if (*i < s.size()) (*i)++;
            continue;
        }
        if (c == '(' || c == '[' || c == '{') depth++;
        else if (c == ')' || c == ']' || c == '}') depth--;
        else if (c == ',' && depth == 0) return;
        (*i)++;
    }
}

static bool is_c_ident(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           (c >= '0' && c <= '9');
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: tr_coverage gui_source...  (gui/tr.inl always scanned)\n";
        return 2;
    }

    std::string tr_path;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]).find("tr.inl") != std::string::npos) tr_path = argv[i];
    }
    if (tr_path.empty()) {
        // fall back to <dir of first source>/tr.inl
        std::string d = argv[1];
        size_t slash = d.find_last_of('/');
        tr_path = (slash == std::string::npos ? "" : d.substr(0, slash + 1)) + "tr.inl";
    }

    std::set<std::string> used;   // EN strings passed to translation helpers
    std::set<std::string> orphans;
    size_t dynamic_calls = 0;

    const std::set<std::string> call_names = {
        "tr", "trf", "trlbl", "trmlbl", "trbtn", "trchk", "trtip",
    };

    // Array names populated via tr_combo_fill(x, ARRAY).
    std::set<std::string> combo_arrays;
    std::map<std::string, std::string> file_of; // for later array scanning

    for (int i = 1; i < argc; i++) {
        std::string path = argv[i];
        std::string src = slurp(path);
        file_of[path] = src;

        size_t i2 = 0;
        while (i2 < src.size()) {
            // skip non-identifier
            char c = src[i2];
            if (is_c_ident(c)) {
                size_t start = i2;
                while (i2 < src.size() && is_c_ident(src[i2])) i2++;
                std::string ident = src.substr(start, i2 - start);
                size_t skip = 0; // # leading args before the translatable string
                if (ident == "trtip") skip = 1;        // trtip(widget, key)
                else if (ident == "grid_row" || ident == "grid_row2")
                    skip = 2;                          // grid_row(g, row, label, w)
                if (call_names.count(ident) || skip > 0) {
                    // expect '('
                    size_t j = i2;
                    while (j < src.size() && src[j] == ' ') j++;
                    if (j < src.size() && src[j] == '(') {
                        size_t arg = j + 1;
                        for (size_t k = 0; k < skip; k++) {
                            skip_first_arg(src, &arg);
                            if (arg < src.size() && src[arg] == ',') arg++;
                        }
                        std::string key;
                        if (parse_string_seq(src, &arg, &key)) {
                            if (std::getenv("TRC_DEBUG"))
                                std::fprintf(stderr, "DBG call %s @%zu in %s: %s\n", ident.c_str(), start, path.c_str(), key.c_str());
                            used.insert(key);
                        } else {
                            dynamic_calls++; // e.g. tr(err.c_str())
                        }
                    }
                } else if (ident == "tr_combo_fill") {
                    size_t j = i2;
                    while (j < src.size() && src[j] == ' ') j++;
                    if (j < src.size() && src[j] == '(') {
                        size_t arg = j + 1;
                        // first arg: widget expr; second arg: array name (or tr())
                        size_t comma = src.find(',', arg);
                        if (comma != std::string::npos) {
                            size_t k = comma + 1;
                            while (k < src.size() && (src[k] == ' ' || src[k] == '\n' || src[k] == '\t')) k++;
                            // If second arg is an identifier (array), grab it
                            char kc = src[k];
                            if (is_c_ident(kc)) {
                                size_t kstart = k;
                                while (k < src.size() && is_c_ident(src[k])) k++;
                                combo_arrays.insert(src.substr(kstart, k - kstart));
                            }
                        }
                    }
                }
            } else {
                i2++;
            }
        }
    }

    // Extract literals from tr_combo_fill arrays (e.g. MODE_KEYS).  Only match
// the array *definition* (`NAME[...] = { ... }`) so that unrelated '{' in a
// file (function bodies, dict entries) is never picked up.
const std::set<std::string> cpp_keywords = {
    "const", "static", "nullptr", "true", "false", "int", "char", "auto",
    "void", "bool", "double", "float", "return", "if", "else", "for",
    "while", "sizeof",
};
for (const auto& an : combo_arrays) {
    if (cpp_keywords.count(an)) continue;
    bool found_any = false;
    for (const auto& fp : file_of) {
        const std::string& src = fp.second;
        size_t p = 0;
        while ((p = src.find(an, p)) != std::string::npos) {
            size_t q = p + an.size();
            size_t end = std::min(src.size(), q + 64);
            size_t eq  = src.find('=', q);
            size_t br  = src.find('[', q);
            if (br != std::string::npos && br < end && eq != std::string::npos &&
                eq < end && br < eq) {
                size_t open = src.find('{', eq + 1);
                if (open != std::string::npos && open < end + 4096) {
                    found_any = true;
                    if (std::getenv("TRC_DEBUG"))
                        std::fprintf(stderr, "DBG combo %s @%zu in %s\n", an.c_str(), open, fp.first.c_str());
                    size_t cur = open + 1;
                    while (cur < src.size() && src[cur] != '}') {
                        std::string lit;
                        if (parse_string_seq(src, &cur, &lit)) {
                            if (std::getenv("TRC_DEBUG"))
                                std::fprintf(stderr, "DBG combo-item %s in %s: %s\n", an.c_str(), fp.first.c_str(), lit.c_str());
                            used.insert(lit);
                        }
                        else cur++;
                    }
                }
            }
            p += an.size();
        }
    }
    if (!found_any && std::getenv("TRC_DEBUG"))
        std::fprintf(stderr, "DBG combo array %s: no definition found\n", an.c_str());
}

    // ── Parse the dictionary from tr.inl ─────────────────────────────────────
    std::string trsrc = slurp(tr_path);
    std::set<std::string> dict_keys;
    {
        size_t block = trsrc.find("D = {");
        if (block == std::string::npos) {
            std::cerr << "tr.inl: 'D = {' block not found\n";
            return 2;
        }
        size_t cur = block + 5;
        size_t close = trsrc.find("};", cur);
        while (cur < close) {
            // skip to '{'
            while (cur < close && trsrc[cur] != '{') cur++;
            if (cur >= close) break;
            cur++; // '{'
            std::string key;
            if (!parse_string_seq(trsrc, &cur, &key)) {
                cur++; // non-literal first element — skip to next '{'
                continue;
            }
            dict_keys.insert(key);
            // skip the rest of this entry up to the matching '}'
            size_t rc = trsrc.find('}', cur);
            if (rc == std::string::npos || rc > close) break;
            cur = rc + 1;
        }
    }

    // ── Report ───────────────────────────────────────────────────────────────
    std::vector<std::string> missing, orphan_list;
    for (const auto& u : used)
        if (!dict_keys.count(u)) missing.push_back(u);
    for (const auto& d : dict_keys)
        if (!used.count(d)) orphan_list.push_back(d);

    std::sort(missing.begin(), missing.end());
    std::sort(orphan_list.begin(), orphan_list.end());

    std::cout << "tr.inl dictionary:      " << dict_keys.size() << " entries\n";
    std::cout << "translated call sites:  " << used.size() << " unique strings\n";
    std::cout << "dynamic (skipped) calls:" << dynamic_calls << "\n\n";

    bool fatal = !missing.empty();
    if (!missing.empty()) {
        std::cout << "MISSING TRANSLATIONS (" << missing.size() << ") — shown in English:\n";
        for (auto& m : missing) std::cout << "  \"" << m << "\"\n";
        std::cout << "\n";
    } else {
        std::cout << "MISSING: none — every UI string has a Turkish entry\n";
    }

    if (!orphan_list.empty()) {
        std::cout << "\nORPHANS (unreferenced entries, " << orphan_list.size() << "):\n";
        for (auto& o : orphan_list) std::cout << "  \"" << o << "\"\n";
    }

    std::cout << (fatal ? "\nResult: FAIL (missing translations)\n"
                        : "\nResult: PASS\n");
    return fatal ? 1 : 0;
}