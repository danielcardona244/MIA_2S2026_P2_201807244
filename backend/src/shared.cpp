#include "shared.h"
#include <algorithm>
#include <cstring>

string to_upper(const string& s) {
    string result = s;
    for (int i = 0; i < (int)result.size(); i++) {
        result[i] = toupper(result[i]);
    }
    return result;
}

string to_lower(const string& s) {
    string result = s;
    for (int i = 0; i < (int)result.size(); i++) {
        result[i] = tolower(result[i]);
    }
    return result;
}

// comparacion sin importar mayusculas o minusculas
bool compare_ci(const string& a, const string& b) {
    return to_lower(a) == to_lower(b);
}

string trim(const string& s) {
    int start = 0;
    int end = (int)s.size() - 1;
    while (start <= end && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r')) {
        start++;
    }
    while (end >= start && (s[end] == ' ' || s[end] == '\t' || s[end] == '\r')) {
        end--;
    }
    if (start > end) return "";
    return s.substr(start, end - start + 1);
}

// conversion numerica estricta: "10abc" es invalido, "10" es valido
bool strict_stoi(const string& s, int& out) {
    if (s.empty()) return false;
    size_t pos = 0;
    try {
        out = stoi(s, &pos);
    } catch (...) {
        return false;
    }
    // se consumio toda la cadena?
    return pos == s.size();
}

// verificar extension .mia (case insensitive)
bool ends_with_mia(const string& path) {
    if (path.size() < 5) return false; // al menos "x.mia"
    string ext = to_lower(path.substr(path.size() - 4));
    return ext == ".mia";
}

// leer campo char de tamano fijo sin depender de nulo
string read_fixed(const char* buf, int maxlen) {
    int len = 0;
    while (len < maxlen && buf[len] != '\0') len++;
    return string(buf, len);
}

// escribir string en campo char fijo, rellenando con ceros
void write_fixed(char* buf, int maxlen, const string& src) {
    memset(buf, 0, maxlen);
    int to_copy = (int)src.size();
    if (to_copy > maxlen) to_copy = maxlen;
    memcpy(buf, src.c_str(), to_copy);
}
