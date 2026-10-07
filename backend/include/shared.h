#ifndef SHARED_H
#define SHARED_H

#include <string>
#include <vector>

using namespace std;

// funciones utilitarias comunes
string to_upper(const string& s);
string to_lower(const string& s);
bool compare_ci(const string& a, const string& b);
string trim(const string& s);

// conversion numerica estricta: rechaza "10abc", retorna false si falla
bool strict_stoi(const string& s, int& out);

// verificar extension .mia
bool ends_with_mia(const string& path);

// leer campo char de tamano fijo sin depender de terminador nulo
string read_fixed(const char* buf, int maxlen);

// escribir string en campo char de tamano fijo, rellenando con ceros
void write_fixed(char* buf, int maxlen, const string& src);

#endif
